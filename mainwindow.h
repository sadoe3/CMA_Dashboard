#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSql>
#include <QMessageBox>
#include <QSqlTableModel>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QChart>
#include <QScreen>
#include <QGuiApplication>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>

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

    QChart *dashboardChart;
    QPieSeries *dashboardSeries;
    QComboBox *cb_DashboardMonthStart;
    QComboBox *cb_DashboardMonthEnd;

    void initDatabase();
    void initCategoryTable();
    void initEventHandlers();
    void initCurrencyComponent();
    void initTableModel();
    void loadCategories();

    void initDashboard();
    void updateDashboardMonthList();
    void updateDashboard();

    void initMenuBar();
    void openCategoryManager();
};

#endif // MAINWINDOW_H