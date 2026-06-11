#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSql>
#include <QMessageBox>
#include <QSqlTableModel>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    QSqlDatabase db;
    QSqlTableModel *tableModel;

    void initDatabase();
    void initCategoryTable();
    void initEventHandlers();
    void initCurrencyComponent();
    void initTableModel();
    void loadCategories();
};

#endif // MAINWINDOW_H