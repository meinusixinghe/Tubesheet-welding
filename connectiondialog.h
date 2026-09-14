#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

class ConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget *parent = nullptr);

    QString getIp() const;
    int getPort() const;

    void setIp(const QString& ip);
    void setPort(int port);

    int getAction() const;                                      // 获取用户的操作类型 (1: 连接, 2: 断开连接)
private:
    QLineEdit* m_ipEdit;
    QSpinBox* m_portSpin;
    int m_action = 0;
};

#endif // CONNECTIONDIALOG_H
