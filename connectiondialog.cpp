#include "connectiondialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QLabel>

ConnectionDialog::ConnectionDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("通讯连接设置");

    QFormLayout* layout = new QFormLayout(this);

    // IP 输入框
    m_ipEdit = new QLineEdit(this);
    QRegularExpression ipRegex("^((2[0-4]\\d|25[0-5]|[01]?\\d\\d?)\\.){3}(2[0-4]\\d|25[0-5]|[01]?\\d\\d?)$");
    m_ipEdit->setValidator(new QRegularExpressionValidator(ipRegex, this));
    m_ipEdit->setPlaceholderText("例如: 192.168.1.10");
    m_ipEdit->setMinimumWidth(200);
    layout->addRow("机器人 IP:", m_ipEdit);

    // 端口输入框
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(502);
    m_portSpin->setMinimumWidth(200);
    layout->addRow("端口号:", m_portSpin);

    // =======================================================
    // 自定义按钮：连接、断开连接
    // =======================================================
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnConnect = new QPushButton("连接", this);
    QPushButton* btnDisconnect = new QPushButton("断开连接", this);

    btnConnect->setStyleSheet("background-color: #2196F3; color: white; padding: 5px 15px; border-radius: 4px;");
    btnDisconnect->setStyleSheet("background-color: #F44336; color: white; padding: 5px 15px; border-radius: 4px;");

    btnLayout->addStretch();
    btnLayout->addWidget(btnConnect);
    btnLayout->addWidget(btnDisconnect);
    layout->addRow(btnLayout);

    // 按钮信号槽
    connect(btnConnect, &QPushButton::clicked, this, [this](){
        m_action = 1; // 1 表示点击了“连接”
        accept();     // 关闭窗口并返回 Accepted
    });

    connect(btnDisconnect, &QPushButton::clicked, this, [this](){
        m_action = 2; // 2 表示点击了“断开连接”
        accept();     // 关闭窗口并返回 Accepted
    });

    adjustSize();
    setFixedSize(size());
}

QString ConnectionDialog::getIp() const { return m_ipEdit->text(); }
int ConnectionDialog::getPort() const { return m_portSpin->value(); }
void ConnectionDialog::setIp(const QString& ip) { m_ipEdit->setText(ip); }
void ConnectionDialog::setPort(int port) { m_portSpin->setValue(port); }

// 获取操作类型
int ConnectionDialog::getAction() const { return m_action; }
