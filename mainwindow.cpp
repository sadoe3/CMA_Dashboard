#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QRegularExpressionValidator>
#include <QLocale>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QtCharts/QPieSlice>
#include <QScreen>
#include <QGuiApplication>

// ==========================================================================
// CategoryDelegate 클래스
// --------------------------------------------------------------------------
// 데이터 그리드(QTableView)에서 카테고리 셀을 편집할 때 일반 텍스트 입력창 대신
// DB에 등록된 카테고리 목록을 콤보박스로 띄워줍니다.
// 사용자의 오타나 규격 외 데이터 입력을 차단하여 무결성을 유지하는 역할을 합니다.
// ==========================================================================
class CategoryDelegate : public QStyledItemDelegate {
public:
    CategoryDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &/*option*/, const QModelIndex &/*index*/) const override {
        QComboBox *editor = new QComboBox(parent);
        QSqlQuery query("SELECT name FROM Category ORDER BY id ASC");
        while (query.next()) {
            editor->addItem(query.value(0).toString());
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        QComboBox *cb = static_cast<QComboBox*>(editor);
        int cbIndex = cb->findText(value);
        if (cbIndex >= 0) {
            cb->setCurrentIndex(cbIndex);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        QComboBox *cb = static_cast<QComboBox*>(editor);
        model->setData(index, cb->currentText(), Qt::EditRole);
    }
};

// ==========================================================================
// CustomSqlTableModel 클래스
// --------------------------------------------------------------------------
// 기본 QSqlTableModel을 상속받아 화면 렌더링 시점에만 데이터의 표현 방식을 바꿉니다.
// 수정된 데이터에 연노랑색 배경을 칠해주고, 금액 데이터(3번 컬럼)에 천 단위 콤마를 찍어
// 사용자의 가독성과 편집 상태 인지를 돕는 시각화 전용 모델입니다.
// ==========================================================================
class CustomSqlTableModel : public QSqlTableModel {
public:
    CustomSqlTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase())
        : QSqlTableModel(parent, db) {}

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
                QVariant originalValue = QSqlTableModel::data(idx, role);
                bool ok;
                qlonglong number = originalValue.toLongLong(&ok);
                if (ok) return QLocale().toString(number);
            }
        }

        return QSqlTableModel::data(idx, role);
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

    // 창 크기를 1200x950으로 강제 할당 및 고정하여 레이아웃이 깨지지 않도록 방어
    this->resize(1200, 950);
    this->setMinimumSize(1200, 950);

    // QScreen을 통해 모니터 크기를 읽어와 창을 가로 중앙, 세로 상단(0.5% 여백)에 띄움
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = screenGeometry.height() * 0.005;
    this->move(x, y);

    initDatabase();
    initCategoryTable();
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
// SQLite를 연결하고 CMA_Records(가계부 본체)와 Category(카테고리 목록) 테이블이
// 존재하지 않으면 신규 생성합니다. 카테고리가 비어있으면 5개의 기본값을 삽입합니다.
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
                          "category TEXT, "
                          "amount INTEGER, "
                          "memo TEXT)";

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
// 주요 이벤트 핸들러 초기화
// --------------------------------------------------------------------------
// 사용자의 클릭 동작이나 입력 변화에 대응하는 비즈니스 로직(CRUD)을 매핑합니다.
// ==========================================================================
void MainWindow::initEventHandlers()
{
    // 1. 화면 전환 라우팅 연결
    connect(ui->bt_BackToPage1, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage1_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_BackToPage2_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage3, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(2); });
    connect(ui->bt_ToPage4, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(3); });

    // 대시보드 페이지 진입 시, 백엔드 데이터 최신화
    connect(ui->bt_ToPage5, &QPushButton::clicked, this, [this]() {
        updateDashboardMonthList();
        updateDashboard();
        ui->stackedWidget->setCurrentIndex(4);
    });

    // 2. [입력/수정 탭] 기간(From-To) 설정 방어 로직 (핵심 요구사항)
    // 시작일(From)이 변경될 때마다, 종료일(To)이 선택할 수 있는 가장 빠른 날짜를 제한합니다.
    connect(ui->de_EditStart, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        ui->de_EditEnd->setMinimumDate(date);
    });

    // 3. 신규 가계부 내역 저장(INSERT) 로직
    connect(ui->bt_Add, &QPushButton::clicked, this, [this]() {
        QString date = ui->de_Add->date().toString("yyyy-MM-dd");
        QString category = ui->cb_Add->currentText();
        QString memo = ui->le_AddMemo->text();
        int amount = ui->le_AddAmount->text().remove(",").toInt();

        if (amount <= 0) {
            QMessageBox::warning(this, "입력 오류", "금액을 올바르게 입력해주세요.");
            return;
        }

        QSqlQuery query;
        query.prepare("INSERT INTO CMA_Records (date, category, amount, memo) VALUES (:date, :category, :amount, :memo)");
        query.bindValue(":date", date);
        query.bindValue(":category", category);
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

    // 4. 데이터 조회(SELECT) 및 필터링 로직
    connect(ui->bt_SearchEdit, &QPushButton::clicked, this, [this]() {
        QString startDate = ui->de_EditStart->date().toString("yyyy-MM-dd");
        QString endDate = ui->de_EditEnd->date().toString("yyyy-MM-dd");
        QString category = ui->cb_Edit->currentText();

        QString filter = QString("date >= '%1' AND date <= '%2'").arg(startDate, endDate);
        if (category != "전체") {
            filter += QString(" AND category = '%1'").arg(category);
        }

        tableModel->setFilter(filter);
        tableModel->select();
    });

    // 5. 다중 선택 행 삭제(DELETE) 로직
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

    // 6. 인라인 에디팅 내역 일괄 커밋(UPDATE) 로직
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
// 데이터 그리드 뷰(QTableView)와 SQL 모델을 연결하고,
// 각 컬럼의 너비를 조정하여 화면 가독성을 최적화합니다.
// ==========================================================================
void MainWindow::initTableModel()
{
    tableModel = new CustomSqlTableModel(this, db);
    tableModel->setTable("CMA_Records");
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
    ui->tv_Edit->setColumnWidth(2, 90);
    ui->tv_Edit->setColumnWidth(3, 120);
    ui->tv_Edit->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch); // 메모 열 가변 확장

    ui->tv_Edit->setItemDelegateForColumn(2, new CategoryDelegate(this));
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

    // 대시보드 상단 레이아웃을 찾아 위젯들을 끼워 넣음
    if (QHBoxLayout *hLayout = ui->page_5->findChild<QHBoxLayout*>("horizontalLayout_9")) {
        hLayout->insertWidget(1, cb_DashboardMonthStart);
        hLayout->insertWidget(2, tildeLabel);
        hLayout->insertWidget(3, cb_DashboardMonthEnd);
    }

    // [시작 월] 콤보박스 변경 이벤트 처리 (핵심 요구사항)
    connect(cb_DashboardMonthStart, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;

        bool isAll = (cb_DashboardMonthStart->currentData().toString() == "ALL");
        cb_DashboardMonthEnd->setEnabled(!isAll);

        if (!isAll) {
            QString startMonth = cb_DashboardMonthStart->currentData().toString();
            QString currentEndMonth = cb_DashboardMonthEnd->currentData().toString();

            // 사용자가 선택한 Start 기준보다 과거나 유효하지 않은 데이터를 End 콤보박스에서 제거하여 원천 차단
            cb_DashboardMonthEnd->blockSignals(true);
            cb_DashboardMonthEnd->clear();

            for (int i = 1; i < cb_DashboardMonthStart->count(); ++i) {
                QString ym = cb_DashboardMonthStart->itemData(i).toString();
                if (ym >= startMonth) { // Start 월과 같거나 이후인 데이터만 필터링
                    cb_DashboardMonthEnd->addItem(cb_DashboardMonthStart->itemText(i), ym);
                }
            }

            // 기존 End 선택값이 여전히 유효하다면 복구, 불가능해졌다면 Start와 강제로 동일하게 맞춤
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

    // [종료 월] 변경 이벤트 처리
    connect(cb_DashboardMonthEnd, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) updateDashboard();
    });

    // 파이 차트 초기화 세팅
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
// 선택된 기간 필터(WHERE 절)를 반영하여 카테고리별 통계를 내고 표와 차트에 렌더링합니다.
// ==========================================================================
void MainWindow::updateDashboard()
{
    dashboardSeries->clear();
    ui->tw_DashboardSummary->setRowCount(0);

    QString startMonth = cb_DashboardMonthStart->currentData().toString();
    QString endMonth = cb_DashboardMonthEnd->currentData().toString();
    QString whereClause = "";

    if (startMonth != "ALL" && !startMonth.isEmpty() && !endMonth.isEmpty()) {
        // UI에서 방어 코드가 적용되어 있지만, 데이터 무결성을 위해 백엔드 레벨에서 한 번 더 Swap 검증(BETWEEN)
        QString minMonth = qMin(startMonth, endMonth);
        QString maxMonth = qMax(startMonth, endMonth);
        whereClause = QString("WHERE substr(date, 1, 7) BETWEEN '%1' AND '%2'").arg(minMonth, maxMonth);
    }

    qulonglong totalAmount = 0;
    int row = 0;

    QSqlQuery catQuery(QString("SELECT category, SUM(amount) FROM CMA_Records %1 GROUP BY category ORDER BY SUM(amount) DESC").arg(whereClause));

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

    // 비율이 작은 항목(3% 미만)의 라벨 숨김 처리를 통해 텍스트 겹침 오류를 우회하는 스마트 라벨링 로직
    for (QPieSlice *slice : dashboardSeries->slices()) {
        QString labelText = QString("%1 (%2%)")
        .arg(slice->label())
            .arg(QString::number(slice->percentage() * 100, 'f', 1));
        slice->setLabel(labelText); // 범례(Legend) 출력을 위해 텍스트는 할당

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
    // 리스트를 초기화하는 동안 이벤트가 무한 반복되어 꼬이는 것을 방지
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
    }

    // 기본 '전체 기간'에 맞추어 종료 월 콤보박스는 비활성화 초기화
    cb_DashboardMonthEnd->setEnabled(false);

    cb_DashboardMonthStart->blockSignals(false);
    cb_DashboardMonthEnd->blockSignals(false);
}