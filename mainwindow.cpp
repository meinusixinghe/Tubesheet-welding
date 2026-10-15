#include "mainwindow.h"
#include "renderarea.h"
#include "usercoordinatemanager.h"
#include "connectiondialog.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    // 1. 初始化 UI
    setupUi();

    // 2. 初始化 Modbus 管理器
    m_modbusManager = new ModbusManager(this);

    // 3. 基础状态与日志信号绑定
    connect(m_modbusManager, &ModbusManager::connectionStateChanged, this, &MainWindow::onModbusStateChanged);
    connect(m_modbusManager, &ModbusManager::logMessage, this, [this](QString msg){
        m_statusLabel->setText(msg);
    });
    connect(m_modbusManager, &ModbusManager::servoStateChanged, this, &MainWindow::onServoStateChanged);
    connect(m_modbusManager, &ModbusManager::autoStateChanged, this, &MainWindow::onAutoStateChanged);

    // 4. 业务逻辑信号绑定

    // 4.1 错误拦截：断网或急停时，除了弹窗，还必须中断连续焊接状态
    connect(m_modbusManager, &ModbusManager::errorOccurred, this, [this](QString msg){
        m_statusLabel->setText("错误: " + msg);
        QMessageBox::warning(this, "通讯错误", msg);

        // 如果正在连续焊接，强制打断并恢复按钮
        if (m_isWeldingProcessRunning) {
            m_isWeldingProcessRunning = false;
            m_startBtn->setText("预约");
            m_startBtn->setEnabled(true);
            m_pauseBtn->setVisible(false);
        }
    });

    // 4.2 动态按钮文字：不仅改字，还要监控底层状态回退
    connect(m_modbusManager, &ModbusManager::startButtonTextChanged, this, [this](QString text) {
        m_startBtn->setText(text);

        // 如果底层因为某种原因退回到了"预约"状态，解除连续焊接禁用
        if (text == "预约"|| text == "启动") {
            m_isWeldingProcessRunning = false;
            m_startBtn->setEnabled(true);
            m_pauseBtn->setVisible(false);
        }
    });

    // 4.3 收到总命令握手完成信号，立刻下发第一个孔的数据
    connect(m_modbusManager, &ModbusManager::cmdHandshakeCompleted, this, [this]() {
        if (m_isWeldingProcessRunning) {
            sendNextWeldHole();
        }
    });

    // 4.4 单孔闭环完成，自动触发下一个！
    connect(m_modbusManager, &ModbusManager::jobSentSuccess, this, [this]() {
        if (m_isWeldingProcessRunning) {
            dataTable->blockSignals(true);
            for (int col = 0; col < dataTable->columnCount(); ++col) {
                if (QTableWidgetItem* item = dataTable->item(m_currentWeldIndex, col)) {
                    item->setBackground(QBrush(QColor(144, 238, 144)));
                    item->setForeground(QBrush(Qt::black));
                }
            }
            dataTable->blockSignals(false);
            renderArea->setHoleCompleted(m_currentWeldIndex);
            dataTable->viewport()->update();
            QApplication::processEvents();
            m_currentWeldIndex++; // 索引加 1
            sendNextWeldHole();   // 自动调取下一行数据发送给机器人
        }
    });

    // 5. 初始化数据与自动连接
    loadWeldingProcesses();  // 启动时自动加载焊接工艺数据

    // 读取本地保存的 IP 和 端口配置，并尝试自动连接
    QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    m_lastIp = settings.value("RobotIP", "").toString();
    m_lastPort = settings.value("RobotPort", 502).toInt();
    if (!m_lastIp.isEmpty()) {
        // 如果之前存过 IP，软件打开直接自动连接
        m_modbusManager->connectToRobot(m_lastIp, m_lastPort);
    } else {
        // 如果是第一次运行，给个默认值，等用户手动去点“建立连接”
        m_lastIp = "192.168.1.10";
        m_lastPort = 502;
    }

    QString savedPath = settings.value("General/ImageSavePath", "").toString();
    if (!savedPath.isEmpty() && m_camera != nullptr) {
        m_camera->setSaveDirectory(savedPath); // 将读取到的路径下发给相机实例
        qDebug() << "已成功加载 3D 相机历史存储路径：" << savedPath;
    }
}

MainWindow::~MainWindow() {
    if (m_coordManager) {
        delete m_coordManager;
    }
}

// ----------------------------------------------------
// 功能：打开焊接工艺管理对话框
// ----------------------------------------------------
void MainWindow::onManageWeldingProcess()
{
    // 创建对话框，并传入当前工艺列表的指针
    WeldingProcessDialog dialog(&m_weldingProcesses, this);
    dialog.exec(); // 模态显示
}

// ----------------------------------------------------
// 功能：启动自动加载数据
// ----------------------------------------------------
void MainWindow::loadWeldingProcesses()
{
    // 路径：exe 同级目录下的 welding_processes.json
    QString path = QCoreApplication::applicationDirPath() + "/welding_processes.json";
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return;                                             // 文件不存在（第一次运行），直接返回，不做任何事
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;

    m_weldingProcesses.clear(); // 清空默认值
    const QJsonArray arr = doc.array();

    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        WeldingProcess p;
        p.id = obj["id"].toString();
        p.strategy = obj["strategy"].toString();
        p.remark = obj["remark"].toString();
        m_weldingProcesses.append(p);
    }
}

// =============================================================================
// 连接设置与逻辑
// =============================================================================
void MainWindow::onConnectTriggered()
{
    ConnectionDialog dlg(this);

    if (dlg.exec() == QDialog::Accepted) {

        int action = dlg.getAction();

        if (action == 1) {
            // 点击连接
            m_lastIp = dlg.getIp();
            m_lastPort = dlg.getPort();

            // 保存到 config.ini 配置文件
            QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
            settings.setValue("RobotIP", m_lastIp);
            settings.setValue("RobotPort", m_lastPort);

            // 发起连接
            m_modbusManager->connectToRobot(m_lastIp, m_lastPort);

        } else if (action == 2) {
            // 点击断开连接
            m_modbusManager->disconnectFromRobot();
        }
    }
}

void MainWindow::onModbusStateChanged(int state)
{
    if (state == 2) { // 已连接 (绿色)
        m_statusIconLabel->setStyleSheet("background-color: #4CAF50; border-radius: 8px;");
        m_statusTextLabel->setText("已连接");
        m_statusLabel->setText(QString("已连接到机器人: %1:%2").arg(m_lastIp).arg(m_lastPort));

        m_startBtn->setVisible(true);
        m_pauseBtn->setVisible(false);
        m_resetBtn->setVisible(true);
    }
    else if (state == 1) { // 正在连接 (黄色)
        m_statusIconLabel->setStyleSheet("background-color: #FFC107; border-radius: 8px;");
        m_statusTextLabel->setText("正在连接...");
        m_statusLabel->setText("尝试连接中...");

        m_startBtn->setVisible(false);
        m_pauseBtn->setVisible(false);
        m_resetBtn->setVisible(false);
    }
    else { // 未连接 / 断开 (红色)
        m_statusIconLabel->setStyleSheet("background-color: #F44336; border-radius: 8px;");
        m_statusTextLabel->setText("未连接");
        m_statusLabel->setText("未连接 / 连接断开");

        m_startBtn->setVisible(false);
        m_pauseBtn->setVisible(false);
        m_resetBtn->setVisible(false);
    }
}

// =============================================================================
// 机器人控制逻辑
// =============================================================================
void MainWindow::onStartClicked()
{
    if (m_startBtn->text() == "预约") {
        // 触发启动信号 (40129.9) - 跑步骤1和2
        m_modbusManager->prepareAndStart();
    }
    else if (m_startBtn->text() == "启动") {
        // 如果是点击启动，严格检查是否做过路径规划！
        if (!m_isPathPlanned) {
            QMessageBox::warning(this, "操作提示", "启动失败：必须先完成管孔排序！\n请点击上方工具栏或“操作”菜单中的“自动焊接路径规划”。");
            return;
        }

        if (m_positioningMethod == 0) {
            QMessageBox::warning(this, "操作提示", "启动失败：未选择定位方式！\n请先在顶部菜单栏点击“操作 -> 选择定位方式”进行设置。");
            return;
        }

        // 到达启动之后，不再跑步骤2，而是持续跑步骤3！
        if (weldHoles.isEmpty()) {
            QMessageBox::warning(this, "警告", "没有管孔数据可供焊接！");
            return;
        }

        m_currentWeldIndex = 0;
        m_isWeldingProcessRunning = true;
        m_startBtn->setText("连续焊接中...");
        m_startBtn->setEnabled(false); // 防止中途乱点

        m_pauseBtn->setVisible(true);
        m_pauseBtn->setChecked(false); // 确保按钮处于没被按下的状态
        m_pauseBtn->setText("暂停");

        renderArea->clearCompletedHoles();
        dataTable->blockSignals(true);
        for (int r = 0; r < dataTable->rowCount(); ++r) {
            for (int c = 0; c < dataTable->columnCount(); ++c) {
                if (QTableWidgetItem* item = dataTable->item(r, c)) {
                    item->setForeground(QBrush(Qt::black));
                    if (c == 3) {
                        item->setBackground(QBrush(QColor(245, 245, 245)));
                    } else {
                        item->setBackground(QBrush(Qt::white));
                    }
                }
            }
        }
        dataTable->blockSignals(false);
        ModbusManager::RobotCmd cmd = static_cast<ModbusManager::RobotCmd>(m_positioningMethod);
        m_modbusManager->startWeldingProcess(cmd);
    }
}

// ----------------------------------------------------
// 暂停按钮：处理暂停和恢复
// ----------------------------------------------------
void MainWindow::onPauseClicked()
{
    bool isPaused = m_pauseBtn->isChecked(); // 按钮按下状态为 true (暂停中)

    if (isPaused) {
        m_pauseBtn->setText("继续");
        m_modbusManager->setPause(true); // 发送暂停信号
    } else {
        m_pauseBtn->setText("暂停");
        m_modbusManager->setPause(false); // 清除暂停并触发启动信号
    }
}

// ----------------------------------------------------
// 复位按钮
// ----------------------------------------------------
void MainWindow::onResetClicked()
{
    m_modbusManager->resetAlarm();
}

// =============================================================================
// 选择定位方式
// =============================================================================
void MainWindow::onSelectPositioningMethod()
{
    QStringList items;
    items << "10 - 无定位方式启动"
          << "20 - 机械定位方式启动"
          << "30 - 点激光定位方式启动"
          << "40 - 线激光定位方式启动"
          << "50 - 3D相机定位方式扫描启动"
          << "51 - 3D相机定位方式管焊接启动";

    bool ok;
    QString item = QInputDialog::getItem(this, "选择定位方式",
                                         "请选择机器人启动模式 (CMD):", items, 0, false, &ok);
    if (ok && !item.isEmpty()) {
        m_positioningMethod = item.split(" - ").first().toInt();
        m_statusLabel->setText(QString("已成功选择定位方式 CMD: %1").arg(m_positioningMethod));
    }
}

// ----------------------------------------------------
// 窗口关闭事件，保证安全退出下发指令并断开 TCP 连接
// ----------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_isShuttingDown || !m_modbusManager || !m_modbusManager->isConnected()) {
        if (m_modbusManager) {
            m_modbusManager->disconnectFromRobot();
        }
        event->accept();
        return;
    }

    event->ignore();
    m_isShuttingDown = true;
    m_statusLabel->setText("正在安全关闭机器人并清理状态...");

    // 监听底层的“关机完成”信号。收到信号后，立刻重新触发关闭
    connect(m_modbusManager, &ModbusManager::shutdownFinished, this, [this]() {
        this->close();
    });

    // 调用底层的安全关闭逻辑
    m_modbusManager->safeShutdown();
}

// =============================================================================
// 更新伺服使能 UI 状态
// =============================================================================
void MainWindow::onServoStateChanged(bool enabled)
{
    if (enabled) {
        // 绿色：伺服已上电
        m_servoIconLabel->setStyleSheet("background-color: #4CAF50; border-radius: 8px;");
        m_servoTextLabel->setText("伺服使能");
    } else {
        // 灰色：伺服未上电/断开
        m_servoIconLabel->setStyleSheet("background-color: #9E9E9E; border-radius: 8px;");
        m_servoTextLabel->setText("伺服断开");
    }
}

// =============================================================================
// 更新自动/手动 UI 状态
// =============================================================================
void MainWindow::onAutoStateChanged(bool isAuto)
{
    if (isAuto) {
        m_autoTextLabel->setStyleSheet("color: #4CAF50;"); // 绿色文字
        m_autoTextLabel->setText("自动模式");
    } else {
        m_autoTextLabel->setStyleSheet("color: #FF9800;"); // 橙色文字
        m_autoTextLabel->setText("手动模式");
    }
}

// ----------------------------------------------------
// 功能：持续下发步骤三的数据 (从表格里依次取点)
// ----------------------------------------------------
void MainWindow::sendNextWeldHole()
{
    if (!m_isWeldingProcessRunning) return;

    if (m_currentWeldIndex >= weldHoles.size()) {

        m_statusLabel->setText("所有管孔焊接完成！下发全 0 数据...");
        m_modbusManager->sendWeldingFinished();

        m_isWeldingProcessRunning = false;
        m_startBtn->setText("预约");
        m_startBtn->setEnabled(true);
        m_pauseBtn->setVisible(false);

        QTimer::singleShot(500, this, [this](){
            QMessageBox::information(this, "完成", "恭喜，所有管孔已连续焊接完毕！");
        });
        return;
    }

    const Hole& hole = weldHoles[m_currentWeldIndex];

    WeldingData data;
    data.x = hole.center3D.x();
    data.y = hole.center3D.y();
    data.z = hole.center3D.z();
    data.r = hole.radius;
    data.processId = 1;

    dataTable->selectRow(m_currentWeldIndex);
    m_statusLabel->setText(QString("正在下发并焊接第 %1 / %2 个管孔 (CMD: %3)...")
                               .arg(m_currentWeldIndex + 1)
                               .arg(weldHoles.size())
                               .arg(m_positioningMethod));

    // 调用专属的管孔数据下发函数
    m_modbusManager->sendWeldHoleData(data);
}


