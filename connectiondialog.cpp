#include "connectiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QComboBox>
#include <QDebug>

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("工业控制设备通信连接");
    setMinimumSize(480, 380);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // =========================================================
    // 1. 机器人连接区域 (保留原有功能)
    // =========================================================
    QGroupBox *robotGroup = new QGroupBox("一、 机器人底层控制 (EFORT SDK)", this);
    QGridLayout *robotLayout = new QGridLayout(robotGroup);

    robotLayout->addWidget(new QLabel("机器人 IP:", this), 0, 0);
    ipLineEdit = new QLineEdit("192.168.1.12", this);
    robotLayout->addWidget(ipLineEdit, 0, 1);

    robotLayout->addWidget(new QLabel("控制端口:", this), 1, 0);
    portLineEdit = new QLineEdit("8000", this);
    robotLayout->addWidget(portLineEdit, 1, 1);

    mainLayout->addWidget(robotGroup);

    // =========================================================
    // 3. 底部确认区
    // =========================================================
    QHBoxLayout *btnLayout = new QHBoxLayout();
    connectBtn = new QPushButton("确认参数并返回", this);
    cancelBtn = new QPushButton("取消", this);
    btnLayout->addStretch();
    btnLayout->addWidget(connectBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    // =========================================================
    // 4. 事件绑定
    // =========================================================
    connect(connectBtn, &QPushButton::clicked, this, &ConnectionDialog::onConnectRobotClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

}

ConnectionDialog::~ConnectionDialog()
{

}

QString ConnectionDialog::getIp() const { return ipLineEdit->text(); }
int ConnectionDialog::getPort() const { return portLineEdit->text().toInt(); }
void ConnectionDialog::setIp(const QString& ip) { ipLineEdit->setText(ip); }
void ConnectionDialog::setPort(int port) { portLineEdit->setText(QString::number(port)); }

void ConnectionDialog::onConnectRobotClicked()
{
    accept();
}

