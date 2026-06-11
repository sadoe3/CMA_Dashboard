#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QRegularExpressionValidator>
#include <QLocale>
#include <QComboBox>
#include <QtCharts/QPieSlice>
#include <QScreen>
#include <QGuiApplication>
#include <QSqlRelationalTableModel>
#include <QSqlRelationalDelegate>
#include <QSqlRelation>

// ==========================================================================
// CustomSqlTableModel 클래스
// --------------------------------------------------------------------------
// QSqlRelationalTableModel을 상속받아 외래키(Foreign Key) 자동 매핑을 지원하며,
// 미저장 데이터의 시각적 하이라이팅 및 금액 데이터의 동적 통화 포맷팅을 처리합니다.
// ==========================================================================
class CustomSqlTableModel : public QSqlRelationalTableModel {
public:
    CustomSqlTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase())
        : QSqlRelationalTableModel(parent, db) {}

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override {
        // 미저장 변경 데이터 하이라이팅
        if (isDirty(idx)) {
            if (role == Qt::BackgroundRole) return QBrush(QColor(255, 250, 205));
            if (role == Qt::ForegroundRole) return QBrush(Qt::black);
        }

        // 통화 데이터 우측 정렬 및 포맷팅 처리
        if (idx.column() == 3) {
            if (role == Qt::TextAlignmentRole) {
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            }
            if (role == Qt::DisplayRole) {
                QVariant originalValue = QSqlRelationalTableModel::data(idx, role);
                bool ok;
                qlonglong number = originalValue.toLongLong(&ok);
                if (ok) return QLocale().toString(number);
            }
        }

        return QSqlRelationalTableModel::data(idx, role);
    }
};

// ==========================================================================
// MainWindow 생성자
// --------------------------------------------------------------------------
// 앱의 해상도를 고정하고 각 컴포넌트(DB, UI, 이벤트)를 순차적으로 초기화합니다.
// ==========================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 창 크기를 강제 할당 및 고정하여 레이아웃이 깨지지 않도록 방어
    this->resize(1200, 990);
    this->setMinimumSize(1200, 990);

    // QScreen을 통해 모니터 크기를 읽어와 창을 가로 중앙, 세로 상단(0.5% 여백)에 띄움
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = 3;
    this->move(x, y);

    initDatabase();
    initCategoryTable();
    initMenuBar();
    initEventHandlers();
    initCurrencyComponent();
    loadCategories();
    initTableModel();
    initDashboard();

    // 모든 날짜 입력 위젯을 오늘 날짜로 세팅하고 캘린더 팝업을 활성화
    for (auto* dateEdit : {ui->de_Add, ui->de_EditStart, ui->de_EditEnd}) {
        dateEdit->setDate(QDate::currentDate());
        dateEdit->setCalendarPopup(true);
    }

    // 초기화 시점부터 Edit 페이지의 종료일(To)이 시작일(From)보다 과거일 수 없도록 제한 적용
    ui->de_EditEnd->setMinimumDate(ui->de_EditStart->date());
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ==========================================================================
// DB 및 테이블 구성 로직
// --------------------------------------------------------------------------
// 정규화 원칙에 따라 CMA_Records 테이블은 카테고리의 고유 ID(category_id)를
// 외래키로 저장하도록 구성합니다.
// ==========================================================================
void MainWindow::initDatabase()
{
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("money_data.db");

    if (!db.open()) {
        QMessageBox::critical(this, "DB Error", db.lastError().text());
        return;
    }

    QSqlQuery query;
    QString createTable = "CREATE TABLE IF NOT EXISTS CMA_Records ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                          "date TEXT, "
                          "category_id INTEGER, "
                          "amount INTEGER, "
                          "memo TEXT, "
                          "FOREIGN KEY(category_id) REFERENCES Category(id))";

    if (!query.exec(createTable)) {
        qDebug() << "Table create error:" << query.lastError();
    }
}

void MainWindow::initCategoryTable()
{
    QSqlQuery query;
    QString createTable = "CREATE TABLE IF NOT EXISTS Category ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                          "name TEXT UNIQUE)";

    if (!query.exec(createTable)) {
        qDebug() << "Category table create error:" << query.lastError();
        return;
    }

    // 초기 실행 시점에만 기본 카테고리 데이터를 구성합니다.
    query.exec("SELECT COUNT(*) FROM Category");
    if (query.next() && query.value(0).toInt() == 0) {
        QStringList defaultCategories = {"식비", "교통비", "문화생활", "생필품", "기타"};
        query.prepare("INSERT INTO Category (name) VALUES (:name)");
        for (const QString& cat : defaultCategories) {
            query.bindValue(":name", cat);
            query.exec();
        }
    }
}

// ==========================================================================
// 메뉴바 동적 초기화
// --------------------------------------------------------------------------
// UI 디자이너에 생성된 기존 menubar 객체에 접근하여 '설정' 메뉴와
// 카테고리 관리 액션을 코드로 직접 마운트합니다.
// ==========================================================================
void MainWindow::initMenuBar()
{
    QMenu *settingMenu = ui->menubar->addMenu("설정");
    QAction *editCategoryAction = new QAction("카테고리 관리", this);

    settingMenu->addAction(editCategoryAction);

    connect(editCategoryAction, &QAction::triggered, this, &MainWindow::openCategoryManager);
}

// ==========================================================================
// 동적 카테고리 관리 다이얼로그
// --------------------------------------------------------------------------
// 카테고리 테이블 전용 모델을 붙여 즉각적인 CRUD를 지원하며,
// 다이얼로그 종료 시 메인 화면의 외래키 연동 데이터들을 일괄 갱신합니다.
// ==========================================================================
void MainWindow::openCategoryManager()
{
    QDialog dialog(this);
    dialog.setWindowTitle("카테고리 관리");
    dialog.resize(350, 400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QSqlTableModel *catModel = new QSqlTableModel(&dialog, db);
    catModel->setTable("Category");
    catModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    catModel->setHeaderData(0, Qt::Horizontal, "ID");
    catModel->setHeaderData(1, Qt::Horizontal, "카테고리명");
    catModel->select();

    QTableView *tv = new QTableView(&dialog);
    tv->setModel(catModel);
    tv->hideColumn(0);
    tv->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tv->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(tv);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnAdd = new QPushButton("행 추가", &dialog);
    QPushButton *btnDel = new QPushButton("선택 삭제", &dialog);
    QPushButton *btnSave = new QPushButton("저장 후 닫기", &dialog);

    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnDel);
    btnLayout->addStretch();
    btnLayout->addWidget(btnSave);
    layout->addLayout(btnLayout);

    connect(btnAdd, &QPushButton::clicked, [&]() {
        int row = catModel->rowCount();
        catModel->insertRow(row);
        QModelIndex index = catModel->index(row, 1);
        tv->setCurrentIndex(index);
        tv->edit(index);
    });

    connect(btnDel, &QPushButton::clicked, [&]() {
        QModelIndexList selected = tv->selectionModel()->selectedRows();
        if (selected.isEmpty()) return;

        // 1. 방어 로직: 선택된 카테고리가 가계부 내역(CMA_Records)에서 사용 중인지 검증
        for (const QModelIndex &idx : selected) {
            int catId = catModel->data(catModel->index(idx.row(), 0)).toInt();
            QString catName = catModel->data(catModel->index(idx.row(), 1)).toString();

            QSqlQuery checkQuery;
            checkQuery.prepare("SELECT COUNT(*) FROM CMA_Records WHERE category_id = :id");
            checkQuery.bindValue(":id", catId);
            checkQuery.exec();

            if (checkQuery.next() && checkQuery.value(0).toInt() > 0) {
                QMessageBox::warning(&dialog, "삭제 불가",
                                     QString("'%1' 카테고리를 사용하는 내역이 존재하여 삭제할 수 없습니다.\n"
                                             "해당 내역을 먼저 다른 카테고리로 변경하거나 지워주세요.").arg(catName));
                return; // 사용 중인 항목이 하나라도 발견되면 즉시 전체 삭제 프로세스 중단
            }
        }

        // 2. 검증을 모두 통과했다면 정상적으로 삭제 수행 (역순 삭제)
        for (int i = selected.count() - 1; i >= 0; --i) {
            catModel->removeRow(selected.at(i).row());
        }
    });

    connect(btnSave, &QPushButton::clicked, [&]() {
        if (catModel->submitAll()) {
            dialog.accept();
        } else {
            QMessageBox::warning(&dialog, "저장 실패", "카테고리명이 중복되거나 잘못되었습니다.");
            catModel->revertAll();
        }
    });

    dialog.exec();

    // 카테고리 변경 사항을 메인 뷰의 모든 컴포넌트(콤보박스, 그리드, 대시보드)에 즉시 동기화
    loadCategories();
    if (tableModel) {
        tableModel->select();
    }
    updateDashboard();
}

// ==========================================================================
// 주요 이벤트 핸들러 초기화
// --------------------------------------------------------------------------
// 사용자의 클릭 동작이나 입력 변화에 대응하는 비즈니스 로직(CRUD)을 매핑합니다.
// ==========================================================================
void MainWindow::initEventHandlers()
{
    // 화면 네비게이션 라우팅
    connect(ui->bt_BackToPage1, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage1_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_BackToPage2_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage3, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(2); });
    connect(ui->bt_ToPage4, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(3); });

    // 대시보드 진입 시 최신 상태 동기화 처리
    connect(ui->bt_ToPage5, &QPushButton::clicked, this, [this]() {
        updateDashboardMonthList();
        updateDashboard();
        ui->stackedWidget->setCurrentIndex(4);
    });

    // 기간(From-To) 설정 방어 로직: 시작일보다 과거를 종료일로 설정할 수 없게 차단
    connect(ui->de_EditStart, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        ui->de_EditEnd->setMinimumDate(date);
    });

    // 신규 내역 추가 (카테고리 ID 기반 외래키 삽입)
    connect(ui->bt_Add, &QPushButton::clicked, this, [this]() {
        QString date = ui->de_Add->date().toString("yyyy-MM-dd");
        int categoryId = ui->cb_Add->currentData().toInt();
        QString memo = ui->le_AddMemo->text();
        int amount = ui->le_AddAmount->text().remove(",").toInt();

        if (amount <= 0) {
            QMessageBox::warning(this, "입력 오류", "금액을 올바르게 입력해주세요.");
            return;
        }

        QSqlQuery query;
        query.prepare("INSERT INTO CMA_Records (date, category_id, amount, memo) VALUES (:date, :category_id, :amount, :memo)");
        query.bindValue(":date", date);
        query.bindValue(":category_id", categoryId);
        query.bindValue(":amount", amount);
        query.bindValue(":memo", memo);

        if (query.exec()) {
            QMessageBox::information(this, "성공", "데이터가 성공적으로 저장되었습니다.");
            ui->le_AddAmount->clear();
            ui->le_AddMemo->clear();
            ui->le_AddAmount->setProperty("lastValidText", "");
            ui->le_AddAmount->setProperty("lastValidCursor", 0);
            if (tableModel) tableModel->select();
        } else {
            QMessageBox::critical(this, "실패", "저장 중 오류가 발생했습니다.\n" + query.lastError().text());
        }
    });

    // 데이터 조회 및 필터링 (카테고리 ID 매칭)
    connect(ui->bt_SearchEdit, &QPushButton::clicked, this, [this]() {
        QString startDate = ui->de_EditStart->date().toString("yyyy-MM-dd");
        QString endDate = ui->de_EditEnd->date().toString("yyyy-MM-dd");
        int categoryId = ui->cb_Edit->currentData().toInt();

        QString filter = QString("date >= '%1' AND date <= '%2'").arg(startDate, endDate);
        if (categoryId != 0) {
            filter += QString(" AND category_id = %1").arg(categoryId);
        }

        tableModel->setFilter(filter);
        tableModel->select();
    });

    // 다중 선택 행 삭제 로직
    connect(ui->bt_Delete, &QPushButton::clicked, this, [this]() {
        QModelIndexList selectedRows = ui->tv_Edit->selectionModel()->selectedRows();
        if (selectedRows.isEmpty()) {
            QMessageBox::warning(this, "알림", "삭제할 항목을 먼저 선택해주세요.");
            return;
        }

        auto reply = QMessageBox::question(this, "삭제 확인",
                                           QString("%1개의 기록을 정말 삭제하시겠습니까?").arg(selectedRows.count()),
                                           QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            // 인덱스 시프트로 인해 엉뚱한 행이 지워지는 것을 막기 위해 뒤에서부터 순회하며 제거
            for (int i = selectedRows.count() - 1; i >= 0; --i) {
                tableModel->removeRow(selectedRows.at(i).row());
            }
            tableModel->submitAll();
            tableModel->select();
        }
    });

    // 인라인 에디팅 내역 일괄 커밋
    connect(ui->bt_EditConfirm, &QPushButton::clicked, this, [this]() {
        if (tableModel->submitAll()) {
            QMessageBox::information(this, "성공", "수정된 내용이 안전하게 저장되었습니다.");
        } else {
            QMessageBox::critical(this, "오류", "저장 중 오류가 발생했습니다.\n" + tableModel->lastError().text());
            tableModel->revertAll();
        }
    });
}

// ==========================================================================
// 금액 입력 컴포넌트 편의성 강화 로직
// --------------------------------------------------------------------------
// 사용자가 금액을 입력할 때 실시간으로 천 단위 콤마를 찍어주며,
// 글자 사이에서 편집을 하더라도 텍스트 커서 위치가 튕기지 않도록 고정하는 기능입니다.
// ==========================================================================
void MainWindow::initCurrencyComponent()
{
    QRegularExpression rx("^[0-9,]*$");
    ui->le_AddAmount->setValidator(new QRegularExpressionValidator(rx, this));
    ui->le_AddAmount->setAlignment(Qt::AlignRight);
    ui->le_AddAmount->setProperty("lastValidText", "");
    ui->le_AddAmount->setProperty("lastValidCursor", 0);

    connect(ui->le_AddAmount, &QLineEdit::textEdited, this, [=](const QString &text) {
        QString lastValidText = ui->le_AddAmount->property("lastValidText").toString();
        int lastValidCursor = ui->le_AddAmount->property("lastValidCursor").toInt();

        int originalCursorPos = ui->le_AddAmount->cursorPosition();
        QString cleanText = text;
        cleanText.remove(",");

        if (cleanText.isEmpty()) {
            ui->le_AddAmount->clear();
            ui->le_AddAmount->setProperty("lastValidText", "");
            ui->le_AddAmount->setProperty("lastValidCursor", 0);
            return;
        }

        bool ok;
        qulonglong number = cleanText.toULongLong(&ok);

        if (!ok || cleanText.length() > 14) {
            ui->le_AddAmount->setText(lastValidText);
            ui->le_AddAmount->setCursorPosition(lastValidCursor);
            return;
        }

        int digitsBeforeCursor = 0;
        for (int i = 0; i < originalCursorPos; ++i) {
            if (text[i].isDigit()) digitsBeforeCursor++;
        }

        QString formattedText = QLocale().toString(number);
        ui->le_AddAmount->setText(formattedText);

        int newCursorPos = 0;
        int digitCount = 0;
        for (int i = 0; i < formattedText.length(); ++i) {
            if (digitCount == digitsBeforeCursor) break;
            if (formattedText[i].isDigit()) digitCount++;
            newCursorPos++;
        }

        ui->le_AddAmount->setCursorPosition(newCursorPos);
        ui->le_AddAmount->setProperty("lastValidText", formattedText);
        ui->le_AddAmount->setProperty("lastValidCursor", newCursorPos);
    });
}

// ==========================================================================
// 메인 테이블 모델 세팅
// --------------------------------------------------------------------------
// 데이터 그리드 뷰와 관계형 SQL 모델을 연결하고, Category 테이블과의
// 외래키 릴레이션을 매핑하여 화면에는 자동으로 이름을 보여주도록 구성합니다.
// ==========================================================================
void MainWindow::initTableModel()
{
    tableModel = new CustomSqlTableModel(this, db);
    tableModel->setTable("CMA_Records");

    // 2번 컬럼(category_id)을 Category 테이블의 id와 name으로 매핑 연결
    static_cast<CustomSqlTableModel*>(tableModel)->setRelation(2, QSqlRelation("Category", "id", "name"));
    tableModel->setEditStrategy(QSqlTableModel::OnManualSubmit);

    tableModel->setHeaderData(0, Qt::Horizontal, "ID");
    tableModel->setHeaderData(1, Qt::Horizontal, "일시");
    tableModel->setHeaderData(2, Qt::Horizontal, "카테고리");
    tableModel->setHeaderData(3, Qt::Horizontal, "금액");
    tableModel->setHeaderData(4, Qt::Horizontal, "메모");

    ui->tv_Edit->setModel(tableModel);
    ui->tv_Edit->hideColumn(0);

    ui->tv_Edit->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tv_Edit->setColumnWidth(1, 100);
    ui->tv_Edit->setColumnWidth(2, 300);
    ui->tv_Edit->setColumnWidth(3, 120);
    ui->tv_Edit->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    // QSqlRelationalDelegate를 통해 외래키 컬럼의 콤보박스를 자동 지원하도록 설정
    ui->tv_Edit->setItemDelegate(new QSqlRelationalDelegate(ui->tv_Edit));
    ui->tv_Edit->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tv_Edit->setSelectionMode(QAbstractItemView::ExtendedSelection);

    emit ui->bt_SearchEdit->clicked();
}

void MainWindow::loadCategories()
{
    ui->cb_Add->clear();
    ui->cb_Edit->clear();
    ui->cb_Edit->addItem("전체", 0);

    QSqlQuery query("SELECT id, name FROM Category ORDER BY id ASC");
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();

        ui->cb_Add->addItem(name, id);
        ui->cb_Edit->addItem(name, id);
    }
}

// ==========================================================================
// 대시보드 UI 컴포넌트 초기화
// --------------------------------------------------------------------------
// 기간 선택용 콤보박스 쌍(From, To)과 파이 차트 위젯을 동적으로 배치합니다.
// ==========================================================================
void MainWindow::initDashboard()
{
    cb_DashboardMonthStart = new QComboBox(this);
    cb_DashboardMonthEnd = new QComboBox(this);
    QLabel *tildeLabel = new QLabel("~", this);

    cb_DashboardMonthStart->setMinimumSize(120, 30);
    cb_DashboardMonthEnd->setMinimumSize(120, 30);

    if (QHBoxLayout *hLayout = ui->page_5->findChild<QHBoxLayout*>("horizontalLayout_9")) {
        hLayout->insertWidget(1, cb_DashboardMonthStart);
        hLayout->insertWidget(2, tildeLabel);
        hLayout->insertWidget(3, cb_DashboardMonthEnd);
    }

    // [시작 월] 콤보박스 변경 이벤트 처리 (종료 월의 범위를 제한하여 역순 방어)
    connect(cb_DashboardMonthStart, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;

        bool isAll = (cb_DashboardMonthStart->currentData().toString() == "ALL");
        cb_DashboardMonthEnd->setEnabled(!isAll);

        if (!isAll) {
            QString startMonth = cb_DashboardMonthStart->currentData().toString();
            QString currentEndMonth = cb_DashboardMonthEnd->currentData().toString();

            cb_DashboardMonthEnd->blockSignals(true);
            cb_DashboardMonthEnd->clear();

            for (int i = 1; i < cb_DashboardMonthStart->count(); ++i) {
                QString ym = cb_DashboardMonthStart->itemData(i).toString();
                if (ym >= startMonth) {
                    cb_DashboardMonthEnd->addItem(cb_DashboardMonthStart->itemText(i), ym);
                }
            }

            int endIdx = cb_DashboardMonthEnd->findData(currentEndMonth);
            if (endIdx >= 0) {
                cb_DashboardMonthEnd->setCurrentIndex(endIdx);
            } else {
                cb_DashboardMonthEnd->setCurrentIndex(cb_DashboardMonthEnd->findData(startMonth));
            }
            cb_DashboardMonthEnd->blockSignals(false);
        }
        updateDashboard();
    });

    connect(cb_DashboardMonthEnd, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) updateDashboard();
    });

    dashboardSeries = new QPieSeries(this);
    dashboardSeries->setPieSize(0.85);

    dashboardChart = new QChart();
    dashboardChart->addSeries(dashboardSeries);
    dashboardChart->setTitle("카테고리별 현황");
    dashboardChart->setAnimationOptions(QChart::SeriesAnimations);
    dashboardChart->legend()->setAlignment(Qt::AlignRight);
    dashboardChart->setMargins(QMargins(10, 10, 10, 10));
    dashboardChart->setTheme(QChart::ChartThemeLight);

    QChartView *chartView = new QChartView(dashboardChart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(750);
    chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    if (QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->tab_Chart->layout())) {
        layout->addWidget(chartView);
    }

    ui->tw_DashboardSummary->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tw_DashboardSummary->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tw_DashboardSummary->verticalHeader()->setVisible(false);
}

// ==========================================================================
// 대시보드 데이터 동기화
// --------------------------------------------------------------------------
// 선택된 기간 필터를 반영하고 Category 테이블과 JOIN하여 통계를 렌더링합니다.
// ==========================================================================
void MainWindow::updateDashboard()
{
    dashboardSeries->clear();
    ui->tw_DashboardSummary->setRowCount(0);

    QString startMonth = cb_DashboardMonthStart->currentData().toString();
    QString endMonth = cb_DashboardMonthEnd->currentData().toString();
    QString whereClause = "";

    if (startMonth != "ALL" && !startMonth.isEmpty() && !endMonth.isEmpty()) {
        QString minMonth = qMin(startMonth, endMonth);
        QString maxMonth = qMax(startMonth, endMonth);
        // 조인 쿼리를 위해 기준 테이블의 알리아스(r) 명시
        whereClause = QString("WHERE substr(r.date, 1, 7) BETWEEN '%1' AND '%2'").arg(minMonth, maxMonth);
    }

    qulonglong totalAmount = 0;
    int row = 0;

    // 카테고리 ID가 아닌 JOIN을 통해 실제 Category 테이블의 이름을 추출하여 집계
    QSqlQuery catQuery(QString("SELECT c.name, SUM(r.amount) FROM CMA_Records r "
                               "JOIN Category c ON r.category_id = c.id "
                               "%1 GROUP BY r.category_id ORDER BY SUM(r.amount) DESC").arg(whereClause));

    while (catQuery.next()) {
        QString cat = catQuery.value(0).toString();
        qulonglong amt = catQuery.value(1).toULongLong();

        if (amt > 0) {
            totalAmount += amt;
            dashboardSeries->append(cat, amt);

            ui->tw_DashboardSummary->insertRow(row);

            QTableWidgetItem *catItem = new QTableWidgetItem(cat);
            catItem->setTextAlignment(Qt::AlignCenter);
            ui->tw_DashboardSummary->setItem(row, 0, catItem);

            QTableWidgetItem *amtItem = new QTableWidgetItem(QLocale().toString(amt) + " 원");
            amtItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            ui->tw_DashboardSummary->setItem(row, 1, amtItem);

            row++;
        }
    }

    ui->label->setText(QString("총 누적 금액: %1 원").arg(QLocale().toString(totalAmount)));

    for (QPieSlice *slice : dashboardSeries->slices()) {
        QString labelText = QString("%1 (%2%)")
        .arg(slice->label())
            .arg(QString::number(slice->percentage() * 100, 'f', 1));
        slice->setLabel(labelText);

        if (slice->percentage() >= 0.03) {
            slice->setLabelVisible(true);
            slice->setLabelColor(Qt::black);
            slice->setLabelPosition(QPieSlice::LabelOutside);
        } else {
            slice->setLabelVisible(false);
        }
    }
}

// ==========================================================================
// 대시보드 월별 필터 목록 업데이트
// --------------------------------------------------------------------------
// DB를 스캔하여 데이터가 존재하는 연-월만 추출해 콤보박스 선택지로 만듭니다.
// ==========================================================================
void MainWindow::updateDashboardMonthList()
{
    cb_DashboardMonthStart->blockSignals(true);
    cb_DashboardMonthEnd->blockSignals(true);

    cb_DashboardMonthStart->clear();
    cb_DashboardMonthEnd->clear();

    cb_DashboardMonthStart->addItem("전체 기간", "ALL");

    QSqlQuery query("SELECT DISTINCT substr(date, 1, 7) AS ym FROM CMA_Records ORDER BY ym DESC");

    while (query.next()) {
        QString ym = query.value(0).toString();
        QString displayText = QString("%1년 %2월").arg(ym.mid(0, 4)).arg(ym.mid(5, 2));

        cb_DashboardMonthStart->addItem(displayText, ym);
        cb_DashboardMonthEnd->addItem(displayText, ym);
    }

    cb_DashboardMonthEnd->setEnabled(false);

    cb_DashboardMonthStart->blockSignals(false);
    cb_DashboardMonthEnd->blockSignals(false);
}