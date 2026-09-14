#ifndef WELDINGPROCESSDIALOG_H
#define WELDINGPROCESSDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QVector>
#include <QString>

// 定义焊接工艺结构体
struct WeldingProcess {
    QString id;         // 工艺编号
    QString strategy;   // 焊接策略
    QString remark;     // 备注
};

class WeldingProcessDialog : public QDialog
{
    Q_OBJECT
public:
    // 构造函数接收一个指向数据列表的指针，直接修改外部数据
    explicit WeldingProcessDialog(QVector<WeldingProcess>* processList, QWidget *parent = nullptr);

private slots:
    void onAddProcess();
    void onEditProcess();
    void onDeleteProcess();
    void onSaveProcess();

private:
    void setupUi();
    void refreshTable();
    // 弹出编辑/添加子对话框
    bool openProcessEditor(WeldingProcess& process, const QString& title);

    QVector<WeldingProcess>* m_processList; // 指向主窗口的数据
    QTableWidget* m_table;
};

#endif // WELDINGPROCESSDIALOG_H
