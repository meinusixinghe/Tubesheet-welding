#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QString>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>

class ConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ~ConnectionDialog();

    QString getIp() const;
    int getPort() const;

    void setIp(const QString& ip);
    void setPort(int port);

    int getAction() const;                                      // 获取用户的操作类型 (1: 连接, 2: 断开连接)

private slots:
    void onConnectRobotClicked();

private:
    QLineEdit *ipLineEdit;
    QLineEdit *portLineEdit;
    QPushButton *connectBtn;
    QPushButton *cancelBtn;
};

#endif // CONNECTIONDIALOG_H
