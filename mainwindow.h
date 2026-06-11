#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSql>
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 카테고리 데이터를 담을 구조체 정의
struct CategoryData {
    int id;
    QString name;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;


    std::vector<CategoryData> cachedCategories;
private:
    Ui::MainWindow *ui;
    QSqlDatabase db;

    void initDatabase();
    void initCategoryTable();
    void initEventHandlers();
    void initCurrencyComponent();

    void loadCategories();
};
#endif // MAINWINDOW_H