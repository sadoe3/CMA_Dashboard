#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QRegularExpressionValidator>
#include <QLocale>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initDatabase();
    initEventHandlers();
    initCategoryTable();
    initCurrencyComponent();

    loadCategories();

    ui->de_Add->setDate(QDate::currentDate());
    ui->de_EditStart->setDate(QDate::currentDate());
    ui->de_EditEnd->setDate(QDate::currentDate());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initDatabase()
{
    // 1. SQLite 연결
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("money_data.db"); // 실행 파일 경로에 저장됨

    if (!db.open()) {
        QMessageBox::critical(this, "DB Error", db.lastError().text());
        return;
    }

    // 2. 테이블 생성 (일시, 카테고리, 금액, 메모)
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

    // 1. Category 테이블 생성
    QString createTable = "CREATE TABLE IF NOT EXISTS Category ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                          "name TEXT UNIQUE)";
    if (!query.exec(createTable)) {
        qDebug() << "Category table create error:" << query.lastError();
        return;
    }

    // 2. 데이터가 하나도 없는지 확인
    // todo : 추후 기본 카테고리는 변경 가능
    query.exec("SELECT COUNT(*) FROM Category");
    if (query.next() && query.value(0).toInt() == 0) {
        // 기본 데이터 Insert
        QStringList defaultCategories = {"식비", "교통비", "문화생활", "생필품", "기타"};

        query.prepare("INSERT INTO Category (name) VALUES (:name)");
        for (const QString& cat : defaultCategories) {
            query.bindValue(":name", cat);
            query.exec();
        }
        qDebug() << "기본 카테고리가 세팅되었습니다.";
    }
}

void MainWindow::initEventHandlers()
{
    connect(ui->bt_BackToPage1, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(0);
    });

    connect(ui->bt_BackToPage1_, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(0);
    });

    connect(ui->bt_BackToPage2, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(1);
    });

    connect(ui->bt_BackToPage2_, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(1);
    });

    connect(ui->bt_ToPage2, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(1);
    });

    connect(ui->bt_ToPage3, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(2);
    });

    connect(ui->bt_ToPage4, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(3);
    });

    connect(ui->bt_ToPage5, &QPushButton::clicked, this, [this]() {
        ui->stackedWidget->setCurrentIndex(4);
    });
}

void MainWindow::initCurrencyComponent()
{
    QRegularExpression rx("^[0-9,]*$");
    ui->le_AddAmount->setValidator(new QRegularExpressionValidator(rx, this));
    ui->le_AddAmount->setAlignment(Qt::AlignRight);

    ui->le_AddAmount->setProperty("lastValidText", "");
    ui->le_AddAmount->setProperty("lastValidCursor", 0);

    connect(ui->le_AddAmount, &QLineEdit::textEdited, this, [=](const QString &text) {
        // 1. 방금 전까지 안전했던 이전 상태 불러오기
        QString lastValidText = ui->le_AddAmount->property("lastValidText").toString();
        int lastValidCursor = ui->le_AddAmount->property("lastValidCursor").toInt();

        // 2. 현재 커서 위치 및 텍스트
        int originalCursorPos = ui->le_AddAmount->cursorPosition();
        QString cleanText = text;
        cleanText.remove(",");

        // 3. 다 지워졌을 때의 처리
        if (cleanText.isEmpty()) {
            ui->le_AddAmount->clear();
            ui->le_AddAmount->setProperty("lastValidText", "");
            ui->le_AddAmount->setProperty("lastValidCursor", 0);
            return;
        }

        // 4. 오버플로우 방지 (14자리, 99조 원까지만 허용)
        bool ok;
        qulonglong number = cleanText.toULongLong(&ok);

        // 변환에 실패했거나(너무 크거나), 우리가 정한 자릿수를 초과하면?
        if (!ok || cleanText.length() > 14) {
            // 입력을 무시하고 이전 정상 상태로 강제 롤백!
            ui->le_AddAmount->setText(lastValidText);
            ui->le_AddAmount->setCursorPosition(lastValidCursor);
            return;
        }

        // 5. 커서 앞쪽의 순수 숫자 개수 카운트 (커서 추적 로직)
        int digitsBeforeCursor = 0;
        for (int i = 0; i < originalCursorPos; ++i) {
            if (text[i].isDigit()) {
                digitsBeforeCursor++;
            }
        }

        // 6. 포맷팅 (천 단위 콤마 삽입)
        QString formattedText = QLocale().toString(number);
        ui->le_AddAmount->setText(formattedText);

        // 7. 잃어버린 커서 위치 다시 찾기
        int newCursorPos = 0;
        int digitCount = 0;
        for (int i = 0; i < formattedText.length(); ++i) {
            if (digitCount == digitsBeforeCursor) {
                break;
            }
            if (formattedText[i].isDigit()) {
                digitCount++;
            }
            newCursorPos++;
        }

        // 8. 커서 원상복구
        ui->le_AddAmount->setCursorPosition(newCursorPos);

        // 9. 현재의 성공적인 상태를 다음번 검사를 위해 위젯에 몰래 저장
        ui->le_AddAmount->setProperty("lastValidText", formattedText);
        ui->le_AddAmount->setProperty("lastValidCursor", newCursorPos);
    });
}


void MainWindow::loadCategories()
{
    ui->cb_Add->clear();
    ui->cb_Edit->clear();


    QSqlQuery query("SELECT id, name FROM Category ORDER BY id ASC");

    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();

        ui->cb_Add->addItem(name, id);
        ui->cb_Edit->addItem(name, id);
    }
}
