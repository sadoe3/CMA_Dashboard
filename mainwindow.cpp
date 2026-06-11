#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QRegularExpressionValidator>
#include <QLocale>
#include <QStyledItemDelegate>
#include <QComboBox>

// ==========================================
// CategoryDelegate
// 그리드 내 카테고리 열을 더블클릭할 때 일반 텍스트 에디터 대신
// DB와 연동된 콤보박스를 제공하여 데이터 무결성을 유지합니다.
// ==========================================
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

// ==========================================
// CustomSqlTableModel
// QSqlTableModel을 상속받아 그리드에 데이터를 렌더링할 때의 시각적 요소를 제어합니다.
// 변경된 데이터의 하이라이팅 처리 및 금액 컬럼의 통화 포맷팅을 담당합니다.
// ==========================================
class CustomSqlTableModel : public QSqlTableModel {
public:
    CustomSqlTableModel(QObject *parent = nullptr, QSqlDatabase db = QSqlDatabase())
        : QSqlTableModel(parent, db) {}

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override {
        // 미저장 변경 사항이 있는 셀의 시각적 강조 (노란 배경, 검은 글씨)
        if (isDirty(idx)) {
            if (role == Qt::BackgroundRole) return QBrush(QColor(255, 250, 205));
            if (role == Qt::ForegroundRole) return QBrush(Qt::black);
        }

        // 금액 컬럼(인덱스 3)의 우측 정렬 및 천 단위 콤마 포맷팅
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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initDatabase();
    initCategoryTable();
    initEventHandlers();
    initCurrencyComponent();
    loadCategories();
    initTableModel();

    // 입력 및 조회 폼의 날짜 위젯을 캘린더 팝업 형태로 초기화
    ui->de_Add->setDate(QDate::currentDate());
    ui->de_Add->setCalendarPopup(true);
    ui->de_EditStart->setDate(QDate::currentDate());
    ui->de_EditStart->setCalendarPopup(true);
    ui->de_EditEnd->setDate(QDate::currentDate());
    ui->de_EditEnd->setCalendarPopup(true);
}

MainWindow::~MainWindow()
{
    delete ui;
}

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

    // 초기 실행 시 기본 카테고리 세팅
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

void MainWindow::initEventHandlers()
{
    // 네비게이션 이벤트 연결
    connect(ui->bt_BackToPage1, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage1_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->bt_BackToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_BackToPage2_, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage2, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->bt_ToPage3, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(2); });
    connect(ui->bt_ToPage4, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(3); });
    connect(ui->bt_ToPage5, &QPushButton::clicked, this, [this]() { ui->stackedWidget->setCurrentIndex(4); });

    // 신규 데이터 저장 처리
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

    // 조건 필터 검색 처리
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

    // 선택된 행 일괄 삭제 처리
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
            // 인덱스 꼬임을 방지하기 위해 역순으로 순회하며 삭제
            for (int i = selectedRows.count() - 1; i >= 0; --i) {
                tableModel->removeRow(selectedRows.at(i).row());
            }
            // 삭제 동작을 DB에 즉시 반영
            tableModel->submitAll();
            tableModel->select();
        }
    });

    // 수정된 셀 데이터 일괄 저장 처리
    connect(ui->bt_EditConfirm, &QPushButton::clicked, this, [this]() {
        if (tableModel->submitAll()) {
            QMessageBox::information(this, "성공", "수정된 내용이 안전하게 저장되었습니다.");
        } else {
            QMessageBox::critical(this, "오류", "저장 중 오류가 발생했습니다.\n" + tableModel->lastError().text());
            tableModel->revertAll();
        }
    });
}

void MainWindow::initCurrencyComponent()
{
    // 금액 입력란에 실시간 통화 포맷팅을 적용합니다.
    // 사용자의 텍스트 커서 위치를 추적하여 콤마가 삽입되더라도 입력 흐름이 끊기지 않도록 처리합니다.
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

        // 오버플로우 또는 허용 자릿수 초과 시 이전 정상 상태로 복구
        if (!ok || cleanText.length() > 14) {
            ui->le_AddAmount->setText(lastValidText);
            ui->le_AddAmount->setCursorPosition(lastValidCursor);
            return;
        }

        // 현재 커서 앞의 숫자 개수를 기반으로 포맷팅 후의 커서 위치 재계산
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

void MainWindow::initTableModel()
{
    // 그리드 뷰와 DB를 연결하는 메인 테이블 모델 구성
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

    // 컬럼 너비 분배: 특정 열은 고정하고 메모 열에 가변 공간 할당
    ui->tv_Edit->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tv_Edit->setColumnWidth(1, 100);
    ui->tv_Edit->setColumnWidth(2, 90);
    ui->tv_Edit->setColumnWidth(3, 120);
    ui->tv_Edit->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    ui->tv_Edit->setItemDelegateForColumn(2, new CategoryDelegate(this));
    ui->tv_Edit->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tv_Edit->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 초기 데이터 표출
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