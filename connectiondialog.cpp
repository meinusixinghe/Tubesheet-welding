#include "connectiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include "VZNL_EyeConfig.h"

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent), m_hCamera(nullptr), m_action(1)
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
    // 2. 视觉相机连接区域 (VzNLSDK 植入)
    // =========================================================
    QGroupBox *camGroup = new QGroupBox("二、 3D 视觉系统 (VzNLSDK)", this);
    QVBoxLayout *camLayout = new QVBoxLayout(camGroup);

    QHBoxLayout *camTopLayout = new QHBoxLayout();
    m_searchCamBtn = new QPushButton("搜索局域网相机", this);
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    camTopLayout->addWidget(m_searchCamBtn);
    camTopLayout->addWidget(m_cameraCombo);
    camLayout->addLayout(camTopLayout);

    QHBoxLayout *camBottomLayout = new QHBoxLayout();
    m_connectCamBtn = new QPushButton("连接选中相机", this);
    m_camStatusLabel = new QLabel("相机状态: 等待搜索...", this);
    m_camStatusLabel->setStyleSheet("color: #757575; font-weight: bold;"); // 灰色
    camBottomLayout->addWidget(m_connectCamBtn);
    camBottomLayout->addWidget(m_camStatusLabel);
    camBottomLayout->addStretch();
    camLayout->addLayout(camBottomLayout);

    mainLayout->addWidget(camGroup);

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

    connect(m_searchCamBtn, &QPushButton::clicked, this, &ConnectionDialog::onSearchCameraClicked);
    connect(m_connectCamBtn, &QPushButton::clicked, this, &ConnectionDialog::onConnectCameraClicked);
}

ConnectionDialog::~ConnectionDialog()
{
    // 如果窗口销毁时相机还在连接，需断开避免句柄泄露
    if (m_hCamera != nullptr) {
        VzNL_CloseDevice(m_hCamera);
        m_hCamera = nullptr;
    }
}

QString ConnectionDialog::getIp() const { return ipLineEdit->text(); }
int ConnectionDialog::getPort() const { return portLineEdit->text().toInt(); }
int ConnectionDialog::getAction() const { return m_action; }
void ConnectionDialog::setIp(const QString& ip) { ipLineEdit->setText(ip); }
void ConnectionDialog::setPort(int port) { portLineEdit->setText(QString::number(port)); }

void ConnectionDialog::onConnectRobotClicked()
{
    m_action = 0;
    accept();
}

void ConnectionDialog::onSearchCameraClicked()
{
    m_camStatusLabel->setText("状态: 正在初始化与深度搜索...");
    m_cameraCombo->clear();
    m_cameraList.clear();
    QApplication::processEvents();

    // 1. 初始化
    SVzNLConfigParam configParam;
    memset(&configParam, 0, sizeof(SVzNLConfigParam));
    int ret = VzNL_Init(&configParam);
    if (ret != 0) {
        m_camStatusLabel->setText("状态: ❌ 初始化失败");
        return;
    }

    // 2. 照搬官方 Demo 的“搜索->绑定->重搜”循环机制
    bool bCanResearch;
    do {
        bCanResearch = false;

        // 官方 Demo 使用的是特定的以太网激光相机宏，我们保持一致
        VzNL_ResearchDevice(keSearchDeviceFlag_EthLaserRobotEye);

        int camCount = 0;
        VzNL_GetEyeCBDeviceInfo(nullptr, &camCount);
        if (camCount <= 0) break;

        std::vector<SVzNLEyeCBInfo> tempDevices(camCount);
        VzNL_GetEyeCBDeviceInfo(tempDevices.data(), &camCount);

        for (int i = 0; i < camCount; ++i) {
            // 如果发现相机未被识别 (官方判断标准)
            if (tempDevices[i].bValidDevice == VzFalse) {
                qDebug() << "发现未识别/未绑定设备，正在执行绑定...";

                int bindRet = VzNL_BindEthernetEye(&tempDevices[i]);
                if (bindRet == 0) {
                    qDebug() << "绑定成功，触发重新搜索机制！";
                    bCanResearch = true; // 核心：绑定成功后必须回到起点重新搜
                    break;               // 跳出 for 循环，重新执行 do-while
                } else {
                    qDebug() << "绑定失败，错误码：" << bindRet;
                }
            }
        }
    } while (bCanResearch);

    // 3. 循环结束后，提取真正合法的设备展示到 UI
    int finalCount = 0;
    VzNL_GetEyeCBDeviceInfo(nullptr, &finalCount);

    if (finalCount > 0) {
        std::vector<SVzNLEyeCBInfo> finalDevices(finalCount);
        VzNL_GetEyeCBDeviceInfo(finalDevices.data(), &finalCount);

        for (int i = 0; i < finalCount; ++i) {
            // 只有 Valid 的设备才允许进入我们的连接列表
            if (finalDevices[i].bValidDevice == VzTrue) {
                m_cameraList.push_back(finalDevices[i]);
                // 使用官方的 byServerIP 打印真实 IP
                QString camIp = QString::fromUtf8((char*)finalDevices[i].byServerIP);
                m_cameraCombo->addItem(QString("可用相机 [%1] (IP: %2)").arg(i + 1).arg(camIp));
            }
        }

        if (m_cameraList.empty()) {
            m_camStatusLabel->setText("状态: 发现设备，但权限被拒绝");
        } else {
            m_camStatusLabel->setText(QString("状态: 准备就绪，发现 %1 台合法相机").arg(m_cameraList.size()));
            m_camStatusLabel->setStyleSheet("color: #1976D2; font-weight: bold;");
        }
    } else {
        m_camStatusLabel->setText("状态: 未探测到相机在线");
    }
}

// =========================================================
// 核心：执行官方 SDK 3.1.6 连接流程
// =========================================================
void ConnectionDialog::onConnectCameraClicked()
{
    if (m_cameraList.empty() || m_cameraCombo->currentIndex() < 0) {
        QMessageBox::warning(this, "操作提示", "没有可用的合法相机，请重新搜索！");
        return;
    }

    if (m_hCamera != nullptr) {
        VzNL_CloseDevice(m_hCamera);
        m_hCamera = nullptr;
    }

    m_camStatusLabel->setText("状态: 正在建立握手...");
    QApplication::processEvents();

    int idx = m_cameraCombo->currentIndex();
    SVzNLEyeCBInfo* pTargetInfo = &m_cameraList[idx];

    // 官方直接传入干净的结构体地址
    SVzNLOpenDeviceParam sOpenDevParam;
    memset(&sOpenDevParam, 0, sizeof(SVzNLOpenDeviceParam));
    int errCode = 0;

    VZNLHANDLE handle = VzNL_OpenDevice(pTargetInfo, &sOpenDevParam, &errCode);

    if (handle != nullptr) {
        m_hCamera = handle;
        m_camStatusLabel->setText("状态: 相机句柄获取成功！");
        m_camStatusLabel->setStyleSheet("color: #388E3C; font-weight: bold;");
        m_connectCamBtn->setEnabled(false);
    } else {
        m_camStatusLabel->setText("状态: 连接拒绝或失败");
        m_camStatusLabel->setStyleSheet("color: #D32F2F; font-weight: bold;");
        QMessageBox::critical(this, "设备异常", QString("所选相机打开失败！\n错误码：%1").arg(errCode));
    }
}
