#include "modbusmanager.h"
#include <QDebug>
#include <QtEndian>

ModbusManager::ModbusManager(QObject *parent) : QObject(parent)
{
    m_modbus = new QModbusTcpClient(this);
    m_modbus->setTimeout(1000);
    m_modbus->setNumberOfRetries(2);

    connect(m_modbus, &QModbusClient::stateChanged, this, &ModbusManager::onStateChanged);
    connect(m_modbus, &QModbusClient::errorOccurred, this, [this](QModbusDevice::Error){
        emit errorOccurred(m_modbus->errorString());
    });

    // 循环读取定时器 (200ms)
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, this, &ModbusManager::monitorRobotStatus);
}

ModbusManager::~ModbusManager()
{
    disconnectFromRobot();
}

void ModbusManager::connectToRobot(const QString &ip, int port)
{
    if (m_modbus->state() != QModbusDevice::UnconnectedState)
        m_modbus->disconnectDevice();

    m_modbus->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    m_modbus->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);

    if (m_modbus->connectDevice()) {
        emit logMessage(QString("正在连接 %1:%2 ...").arg(ip).arg(port));
    } else {
        emit errorOccurred("连接失败: " + m_modbus->errorString());
    }
}

void ModbusManager::disconnectFromRobot()
{
    if (m_modbus && m_modbus->state() != QModbusDevice::UnconnectedState) {
        m_modbus->disconnectDevice();
    }
}

void ModbusManager::onStateChanged(QModbusDevice::State state)
{
    if (state == QModbusDevice::ConnectedState) {
        emit connectionStateChanged(2);
        emit logMessage("已连接到机器人");
        m_monitorTimer->start(200);
    } else if (state == QModbusDevice::UnconnectedState) {
        emit connectionStateChanged(0);
        m_monitorTimer->stop();
        m_startupState = StartIdle;
        m_jobState = JobIdle;

        // 断开连接时，复位伺服状态变量
        if (m_lastServoState) {
            m_lastServoState = false;
            emit servoStateChanged(false);
        }

        // 断开连接时，复位自动状态变量
        if (m_isAutoMode) {
            m_isAutoMode = false;
            emit autoStateChanged(false);
        }

        emit startButtonTextChanged("预约");
    } else if (state == QModbusDevice::ConnectingState) {
        emit connectionStateChanged(1);
    }
}

// ==========================================================
// 循环读取机器人状态
// ==========================================================
void ModbusManager::monitorRobotStatus()
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) return;
    readRegisters(0, 20);
}

void ModbusManager::onReadReady()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply) return;

    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();

        if (unit.startAddress() == 0 && unit.valueCount() >= 15) {
            quint16 status1    = unit.value(Addr::ROBOT_STATUS);       // 40001
            quint16 progLoaded = unit.value(Addr::ROBOT_PROG_LOAD);    // 40012
            quint16 cmdResp    = unit.value(Addr::ROBOT_CMD_RESP);     // 40014
            quint16 weldDone   = unit.value(Addr::ROBOT_WELD_DONE);    // 40015

            // 解析 40001 状态位
            bool isAuto      = (status1 & (1 << 1));
            bool servoStatus = (status1 & (1 << 3));
            bool alarm       = (status1 & (1 << 4));
            bool estop       = (status1 & (1 << 5));
            bool progRunning = (status1 & (1 << 6));
            // bool progLoadDone  = (status1 & (1 << 11));
            m_isReserved = (status1 & (1 << 13));

            // 全局安全拦截机制 (急停/报警)
            if (alarm || estop) {
                if (!m_isAlarmActive) {
                    m_isAlarmActive = true;

                    // 1. 强制清空所有的任务状态机，彻底打断握手
                    m_startupState = StartIdle;
                    m_jobState = JobIdle;

                    emit startButtonTextChanged("预约");
                    // 2. 触发 UI 弹窗和底栏日志
                    QString msg = estop ? "触发物理急停！" : "机器人系统报警！";
                    emit logMessage("=== 严重安全事件: " + msg + " 所有任务已强制中止 ===");
                    emit errorOccurred("系统已强制停止。\n原因: " + msg);
                }

                // 3. 拦截执行：只要处于报警状态，直接 return，禁止往下走任何业务逻辑！
                return;
            } else {
                // 如果物理急停被旋开，或者报警被复位清除了
                if (m_isAlarmActive) {
                    m_isAlarmActive = false;
                    emit logMessage("报警/急停已解除，系统恢复安全待机状态。");
                }
            }

            // 发送伺服状态变化信号给主界面
            if (servoStatus != m_lastServoState) {
                m_lastServoState = servoStatus;
                emit servoStateChanged(servoStatus);
            }

            // 发送自动模式变化信号给主界面
            if (isAuto != m_isAutoMode) {
                m_isAutoMode = isAuto;
                emit autoStateChanged(isAuto);
            }

            // 手动模式拦截机制
            if (!m_isAutoMode) {
                // 如果在运行过程中突然被切回了手动模式，立刻打断所有任务
                if (m_startupState != StartIdle || m_jobState != JobIdle) {
                    m_startupState = StartIdle;
                    m_jobState = JobIdle;
                    emit startButtonTextChanged("预约");
                    emit logMessage("=== 机器人已切换至手动模式，当前任务已强制中止 ===");
                }
                // 只要是手动模式，状态机绝对不往下走
                return;
            }

            // 步骤 1 & 2: 启动状态机 (系统就绪与主程序加载)
            // 发伺服使能脉冲(40129.Bit12)
            switch (m_startupState) {
            case CheckSafety:
                if (!alarm && !estop) {
                    emit logMessage("步骤1: 无报警急停，发伺服使能脉冲(40129.Bit12)");
                    pulseControlWord129(ControlBits::SERVO_ENABLE, 300);
                    m_startupState = WaitServoReady;
                } else {
                    emit errorOccurred("步骤1失败：存在报警或急停！");
                    m_startupState = StartIdle;
                }
                break;

            // 发送开启程序预约启停脉冲(40129.Bit11)
            case WaitServoReady:
                if (servoStatus) {
                    if (!progRunning) {
                        if (!m_isReserved) {
                            emit logMessage("步骤2: 伺服就绪，发送开启预约脉冲(40129.Bit11)");
                            m_startupState = WaitProgramLoaded;

                            m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_ENABLE);
                            writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                            // 延时 200ms 后拉高
                            QTimer::singleShot(200, this, [this](){
                                if (m_startupState != WaitProgramLoaded) return;
                                m_ctrlWord129 |= (1 << ControlBits::RESERVE_ENABLE);
                                writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                                m_startupState = WaitReserveReady;
                                // 延时 200ms 后拉低
                                // QTimer::singleShot(200, this, [this](){
                                //     if (m_startupState != WaitProgramLoaded) return;
                                //     m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_ENABLE);
                                //     writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                                //     m_timeoutCounter = 0;
                                //     m_startupState = WaitReserveReady;
                                // });
                            });
                        } else {
                            emit logMessage("步骤2: 探测到机器人已处于预约状态。");
                            m_startupState = WaitReserveReady;
                        }
                    } else {
                        emit logMessage("程序已经在运行中。");
                        m_startupState = StartIdle;
                    }
                }
                break;

            // 写入程序号于确认预约程序脉冲(40129.Bit9)
            case WaitReserveReady:
                if (m_isReserved) {
                    emit logMessage(QString("步骤2: 预约就绪，写入程序号 %1 并等待系统加载").arg(m_targetProgram));
                    writeRegister(Addr::PC_RESERVE_PROG, m_targetProgram);
                    m_startupState = WaitProgramLoaded;
                    m_retryCounter = 0;
                    m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_CONFIRM);
                    writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                    // 确认预约程序 40129.9拉高
                    QTimer::singleShot(200, this, [this](){
                        if (m_startupState != WaitProgramLoaded) return;
                        m_ctrlWord129 |= (1 << ControlBits::RESERVE_CONFIRM);
                        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                        m_timeoutCounter = 0;
                        m_startupState = WaitProgramRunning;
                        // 200ms后，确认预约程序 40129.9拉低
                        // QTimer::singleShot(200, this, [this](){
                        //     if (m_startupState != WaitProgramLoaded) return;
                        //     m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_CONFIRM);
                        //     writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
                        //     m_timeoutCounter = 0;
                        //     m_startupState = WaitProgramRunning;
                        // });
                    });
                }else {
                    m_timeoutCounter++;
                    if (m_timeoutCounter > 15) {
                        m_timeoutCounter = 0;
                        m_retryCounter++;
                        if (m_retryCounter >= 3) {
                            emit logMessage(">> 严重超时：程序加载连续 3 次(共9秒)失败！回到 CheckSafety 重新开始...");
                            m_startupState = CheckSafety;
                            m_retryCounter = 0;
                        } else {
                            emit logMessage(QString(">> 警告：等待程序加载超时(3秒)，进行第 %1 次重新发送确认脉冲...").arg(m_retryCounter));
                            m_startupState = WaitServoReady;
                        }
                    }
                }
                break;

            case WaitProgramRunning:
                if(progLoaded == 1 && progRunning){
                    emit logMessage("步骤2完成：机器人主程序空跑就绪。");
                    emit startButtonTextChanged("启动"); // UI 变化
                    m_startupState = StartIdle; // 启动流程结束
                    m_retryCounter = 0;
                }else {
                    m_timeoutCounter++;
                    if (m_timeoutCounter > 15) {
                        m_timeoutCounter = 0;
                        m_retryCounter++;
                        if (m_retryCounter >= 3) {
                            emit logMessage(">> 严重超时：程序加载连续 3 次(共9秒)失败！回到 CheckSafety 重新开始...");
                            m_startupState = CheckSafety;
                            m_retryCounter = 0;
                        } else {
                            emit logMessage(QString(">> 警告：等待程序加载超时(3秒)，进行第 %1 次重新发送确认脉冲...").arg(m_retryCounter));
                            m_startupState = WaitReserveReady;
                        }
                    }
                }
                break;

            case WaitProgramLoaded:
                break;

            default: break;
            }

            // 步骤 3 & 4: 业务握手状态机
            switch (m_jobState) {
            case WaitingCmdAck:
                if (cmdResp == 1) {
                    emit logMessage("步骤3: 机器人收到启动命令(40014=1)，上位机清零命令(40142=0)...");
                    writeRegister(Addr::PC_CMD, 0);
                    m_jobState = WaitingCmdClear;
                }
                break;

            case WaitingCmdClear:
                if (cmdResp == 0) {
                    emit logMessage("步骤3结束: 总启动握手完成(40014=0)，准备下发数据...");
                    emit cmdHandshakeCompleted();
                    m_jobState = JobIdle;
                }
                break;

            case WaitingJobDone:
                // 收到管子焊接完成 40015=1，将 40143 置 1
                if (weldDone == 1) {
                    emit logMessage("步骤4: 当前管孔焊接完成(40015=1)，上位机响应置位(40143=1)...");
                    emit robotWeldCompleted();
                    writeRegister(Addr::PC_WELD_ACK, 1);
                    m_jobState = WaitingRobotClearDone;
                }
                break;

            case WaitingRobotClearDone:
                // 等待机器人看到 40143=1 后将 40015 清零
                if (weldDone == 0) {
                    emit logMessage("步骤4结束: 机器人已清零(40015=0)，单孔握手闭环！");
                    emit jobSentSuccess(); // 通知 UI 去发下一个孔的数据
                    m_jobState = JobIdle;
                }
                break;

            default: break;
            }
        }
    }
    reply->deleteLater();
}

// ==========================================================
// 外部调用：触发步骤1和2
// ==========================================================
void ModbusManager::prepareAndStart()
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) return;

    // 检查是否为自动模式
    if (!m_isAutoMode) {
        emit errorOccurred("机器人处于手动模式，请在控制柜上切换至自动模式后再启动！");
        return;
    }

    // 报警状态下禁止启动
    if (m_isAlarmActive) {
        emit errorOccurred("系统处于报警或急停状态，请先解除报警！");
        return;
    }

    // 拦截重复点击
    if (m_startupState != StartIdle) return;
    emit logMessage("开始系统就绪与启动流程");
    m_timeoutCounter = 0;
    m_retryCounter = 0;
    m_startupState = CheckSafety;

    m_ctrlWord129 = 0;
    writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
    QTimer::singleShot(200, this, [this](){
        m_startupState = CheckSafety;
    });
}

// ==========================================================
// 外部调用：步骤 3 - 下发总启动命令 (进行 40014 握手)
// ==========================================================
void ModbusManager::startWeldingProcess(RobotCmd cmd)
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) return;
    if (m_isAlarmActive || !m_isAutoMode) return;
    if (m_jobState != JobIdle) return;

    emit logMessage(QString("开始下发总启动命令 (CMD=%1)").arg(cmd));

    writeRegister(Addr::PC_CMD, (quint16)cmd);
    m_jobState = WaitingCmdAck;
}

// ==========================================================
// 外部调用：步骤4 - 下发管孔数据 (进行 40143/40015 握手)
// ==========================================================
void ModbusManager::sendWeldHoleData(const WeldingData &data)
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) return;

    // 1. 先单独发送工艺号
    writeRegister(Addr::PC_PROCESS_ID, (quint16)data.processId);

    // 2. 将 R, Y, X, Z 连续发
    QVector<quint16> payload;
    payload.append(floatToRegisters(data.x));
    payload.append(floatToRegisters(data.y));
    payload.append(floatToRegisters(data.z));
    payload.append(floatToRegisters(data.r));

    // 3. 仅调用 1 次底层网络发送
    writeRegisters(Addr::PC_DATA_X, payload);

    // 数据发完后，按照要求将 40143 置 0，并等待 40015 等于 1
    QTimer::singleShot(100, this, [this](){
        writeRegister(Addr::PC_WELD_ACK, 0);
        m_jobState = WaitingJobDone;
    });
}

// ==========================================================
// 外部调用：结束连续焊接
// ==========================================================
void ModbusManager::sendWeldingFinished()
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) return;

    emit logMessage("所有管孔焊接完成");
    writeRegister(Addr::PC_PROCESS_ID, 0);

    QVector<quint16> zeroPayload;
    zeroPayload.append(floatToRegisters(0.0f));
    zeroPayload.append(floatToRegisters(0.0f));
    zeroPayload.append(floatToRegisters(0.0f));
    zeroPayload.append(floatToRegisters(0.0f));
    writeRegisters(Addr::PC_DATA_X, zeroPayload);

    QTimer::singleShot(100, this, [this](){
        writeRegister(Addr::PC_WELD_ACK, 0);
        writeRegister(Addr::PC_CMD, 0);
        m_jobState = JobIdle;
    });
}

// ==========================================================
// 报警复位 (发送 40129.Bit3 脉冲)
// ==========================================================
void ModbusManager::resetAlarm()
{
    if (m_modbus->state() == QModbusDevice::ConnectedState) {
        emit logMessage("下发系统报警复位脉冲 (40129.Bit3)");
        pulseControlWord129(ControlBits::ALARM_RESET, 300);
    }
}

// ==========================================================
// 底层辅助函数
// ==========================================================

// 发送脉冲指令 (置 1 -> 延时 -> 置 0)
void ModbusManager::pulseControlWord129(int bitIndex, int durationMs)
{
    m_ctrlWord129 |= (1 << bitIndex);
    writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);

    QTimer::singleShot(durationMs, this, [this, bitIndex](){
        m_ctrlWord129 &= ~(1 << bitIndex);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
    });
}

void ModbusManager::writeRegister(int address, quint16 value)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, 1);
    unit.setValue(0, value);
    m_modbus->sendWriteRequest(unit, 1);
}

void ModbusManager::writeRegisters(int address, const QVector<quint16> &values)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, values);
    m_modbus->sendWriteRequest(unit, 1);
}

void ModbusManager::readRegisters(int address, int count)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, count);
    if (auto *reply = m_modbus->sendReadRequest(unit, 1)) {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &ModbusManager::onReadReady);
        else
            delete reply;
    }
}

QVector<quint16> ModbusManager::floatToRegisters(float val)
{
    QVector<quint16> regs;
    union { float f; uint32_t i; } u;
    u.f = val;
    regs.append((quint16)(u.i & 0xFFFF)); // 低位
    regs.append((quint16)(u.i >> 16));    // 高位
    return regs;
}

// ==========================================================
// 暂停/继续 控制逻辑
// ==========================================================
void ModbusManager::setPause(bool paused)
{
    if (m_modbus->state() == QModbusDevice::ConnectedState) {
        if (paused) {
            emit logMessage("下发暂停脉冲 (40129.Bit2)");
            pulseControlWord129(ControlBits::PAUSE_PULSE, 300);
        } else {
            emit logMessage("下发恢复运行脉冲 (触发确认预约 40129.Bit9)");
            pulseControlWord129(ControlBits::RESERVE_CONFIRM, 300);
        }
    }
}

// ==========================================================
// 触发安全关闭
// ==========================================================
void ModbusManager::safeShutdown()
{
    if (m_modbus->state() != QModbusDevice::ConnectedState) {
        emit shutdownFinished(); // 没连上直接允许退出
        return;
    }
    emit logMessage("执行安全退出：启动步进序列...");

    // 停止日常轮询，独占网络
    if (m_monitorTimer->isActive()) {
        m_monitorTimer->stop();
    }

    // 初始化关机步进器
    m_shutdownStep = 0;
    if (!m_shutdownTimer) {
        m_shutdownTimer = new QTimer(this);
        m_shutdownTimer->setSingleShot(true); // 设为单次触发
        // 定时器时间一到，就自动执行下一步
        connect(m_shutdownTimer, &QTimer::timeout, this, &ModbusManager::processShutdownStep);
    }

    // 立刻执行第 0 步
    processShutdownStep();
}

// ==========================================================
// 扁平化的关机步进状态机
// ==========================================================
void ModbusManager::processShutdownStep()
{
    switch (m_shutdownStep) {
    case 0:
        // 第 1 步：发暂停 (Bit 2)机械臂
        m_ctrlWord129 |= (1 << ControlBits::PAUSE_PULSE);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(100);
        break;

    case 1:
        // 第 2 步：发复位 (Bit 3)打断程序
        m_ctrlWord129 &= ~(1 << ControlBits::PAUSE_PULSE);
        m_ctrlWord129 |= (1 << ControlBits::ALARM_RESET);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(100);
        break;

    case 2:
        // 第 3 步：松开复位，洗白 40129 基础控制字
        m_ctrlWord129 &= ~(1 << ControlBits::ALARM_RESET);
        m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_CONFIRM);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(100);
        break;

    case 3:
        // 第 4 步：清零参数 (40139程序号, 40141工艺号, 40142CMD, 40143响应)
        writeRegister(Addr::PC_RESERVE_PROG, 0);
        {
            QVector<quint16> cmds(4, 0);
            writeRegisters(Addr::PC_PROCESS_ID, cmds);
        }
        m_shutdownStep++;
        m_shutdownTimer->start(100);
        break;

    case 4:
        // 第 5 步：清零 XYZR 浮点数坐标 (40149 ~ 40156)
        {
            QVector<quint16> zeroPayload;
            zeroPayload.append(floatToRegisters(0.0f));
            zeroPayload.append(floatToRegisters(0.0f));
            zeroPayload.append(floatToRegisters(0.0f));
            zeroPayload.append(floatToRegisters(0.0f));
            writeRegisters(Addr::PC_DATA_X, zeroPayload);
        }
        m_shutdownStep++;
        m_shutdownTimer->start(100);
        break;

    case 5:
        // 第 6 步：检查预约状态分流
        if (m_isReserved) {
            m_shutdownStep++;
            processShutdownStep();
        } else {
            m_shutdownStep=9;
            processShutdownStep();
        }
        break;

    case 6:
        // 第 7 步：关预约 -> 先拉低
        m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_ENABLE);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(200);
        break;

    case 7:
        // 第 8 步：关预约 -> 再拉高
        m_ctrlWord129 |= (1 << ControlBits::RESERVE_ENABLE);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(200);
        break;

    case 8:
        // 第 9 步：关预约 -> 再拉低
        m_ctrlWord129 &= ~(1 << ControlBits::RESERVE_ENABLE);
        writeRegister(Addr::PC_CONTROL_WORD, m_ctrlWord129);
        m_shutdownStep++;
        m_shutdownTimer->start(200);
        break;

    case 9:
        emit shutdownFinished();
        break;

    default: break;
    }
}
