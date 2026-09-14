#ifndef MODBUSMANAGER_H
#define MODBUSMANAGER_H

#include <QObject>
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QTimer>
#include <QVector>
#include <QVariant>

// 焊接数据结构
struct WeldingData {
    float x;
    float y;
    float z;
    float r;
    int processId;
};

class ModbusManager : public QObject
{
    Q_OBJECT
public:
    enum RobotCmd {
        Cmd_Standby             = 0,   // 待机 / 复位
        Cmd_Start_NoLoc         = 10,  // 无定位方式启动
        Cmd_Start_MechLoc       = 20,  // 机械定位方式启动
        Cmd_Start_PointLaser    = 30,  // 点激光定位方式启动
        Cmd_Start_LineLaser     = 40,  // 线激光定位方式启动
        Cmd_Start_3DScan        = 50,  // 3D相机定位方式扫描启动
        Cmd_Start_3DWeld        = 51   // 3D相机定位方式管焊接启动
    };
    Q_ENUM(RobotCmd)

    struct Addr {
        // --- PC 写入 ---
        static const int PC_CONTROL_WORD = 128; // 40129 (位控制: 伺服/运行/加载)
        static const int PC_RESERVE_PROG = 138; // 40139 (设置预约程序号）

        static const int PC_PROCESS_ID   = 140; // 40141 (工艺号)
        static const int PC_CMD          = 141; // 40142 (动作命令 CMD)
        static const int PC_WELD_ACK     = 142; // 40143 (管焊接完成响应)

        static const int PC_DATA_X       = 148; // 40149 (X坐标 Real)
        static const int PC_DATA_Y       = 150; // 40151 (Y坐标 Real)
        static const int PC_DATA_Z       = 152; // 40153 (Z坐标 Real)
        static const int PC_DATA_R       = 154; // 40155 (半径 Real)

        // --- 机器人 反馈 ---
        static const int ROBOT_STATUS    = 0;   // 40001 (状态位)
        static const int ROBOT_PROG_LOAD = 11;  // 40012 (已加载的程序号)
        static const int ROBOT_CMD_RESP  = 13;  // 40014 (命令响应)
        static const int ROBOT_WELD_DONE = 14;  // 40015 (焊接完成信号)
    };

    // 40129 控制位定义
    struct ControlBits {
        static const int RUN_PULSE       = 1;  // Bit 1: 运行脉冲
        static const int PAUSE_PULSE     = 2;  // Bit 2: 暂停脉冲
        static const int ALARM_RESET     = 3;  // Bit 3: 报警复位
        static const int EXT_ALARM       = 6;  // Bit 6: 外部报警 (用于强制下电)
        static const int RESERVE_CONFIRM = 9;  // Bit 9: 确定程序预约
        static const int RESERVE_CANCEL = 10; // Bit 10: 取消程序预约
        static const int RESERVE_ENABLE  = 11; // Bit 11: 程序启停
        static const int SERVO_ENABLE    = 12; // Bit 12: 伺服使能脉冲
    };

    explicit ModbusManager(QObject *parent = nullptr);
    ~ModbusManager();

    void connectToRobot(const QString &ip, int port = 502);                 // 连接机器人
    void disconnectFromRobot();                                             // 显式断开连接的接口（用于安全退出）

    // 核心功能接口
    void prepareAndStart();                                                 // 执行步骤1和步骤2 (系统就绪与启动)
    void resetAlarm();                                                      // 解除报警
    void startWeldingProcess(RobotCmd cmd);                                 // 步骤3：仅下发总启动命令
    void sendWeldHoleData(const WeldingData &data);                         // 步骤4：下发管孔数据并置位 40143=0
    void sendWeldingFinished();                                             // 结束：所有管子完成后下发全 0
    // 暂停程序
    void setPause(bool paused);

    // 安全关闭机器人 (下伺服、关预约、暂停)
    void safeShutdown();

    // 查询是否处于连接状态
    bool isConnected() const { return m_modbus && m_modbus->state() == QModbusDevice::ConnectedState; }

signals:
    void connectionStateChanged(int state);                                 // 连接状态变化
    void errorOccurred(QString msg);                                        // 错误信息
    void logMessage(QString msg);                                           // 运行日志
    void jobSentSuccess();                                                  // 任务发送并握手成功
    void robotWeldCompleted();                                              // 机器人焊接完成信号
    void servoStateChanged(bool enabled);                                   // 伺服使能状态变化信号
    void autoStateChanged(bool isAuto);                                     // 自动/手动模式变化信号
    void startButtonTextChanged(QString text);                              // 动态更改主界面启动按钮的文字
    void shutdownFinished();                                                // 关机序列完成信号
    void cmdHandshakeCompleted();                                           // 总启动命令 40014 握手完成信号

private slots:
    void onStateChanged(QModbusDevice::State state);
    void onReadReady();                                                     // 读取回调
    void monitorRobotStatus();                                              // 定时轮询机器人状态
    void processShutdownStep();                                             // 处理关机步进动作

private:
    QModbusTcpClient *m_modbus;
    QTimer *m_monitorTimer;                                                 // 监控定时器

    // 步骤1和2：启动状态机
    enum StartupState {
        StartIdle,
        CheckSafety,                                                        // 检查报警/急停并发送伺服脉冲
        WaitServoReady,                                                     // 等待伺服就绪并发送加载脉冲
        WaitReserveReady,
        WaitProgramLoaded,                                                  // 等待程序加载并发送运行脉冲
        WaitProgramRunning,                                                 // 等待程序运行
    };
    StartupState m_startupState = StartIdle;
    int m_targetProgram = 1;

    // 步骤3和4：业务状态机
    enum JobState {
        JobIdle,
        WaitingCmdAck,                                                      // 等待机器人响应 40035=1
        WaitingCmdClear,                                                    // 等待机器人释放 40035=0
        WaitingJobDone,                                                     // 等待动作结束 40036=1
        WaitingRobotClear,                                                  // 等待机器人清零 40036=0
        AckJobDone_CleanLow,                                                // 将响应清零 40143=0
        WaitingRobotClearDone                                               // 等待机器人清零 40015=0
    };
    JobState m_jobState = JobIdle;

    quint16 m_ctrlWord129 = 0;                                              // 缓存 40129 的值
    bool m_isAlarmActive = false;                                           // 记录当前是否处于报警/急停状态
    bool m_lastServoState = false;                                          // 记录上一次的伺服状态
    bool m_isAutoMode = false;                                              // 记录当前是否处于自动模式
    bool m_isReserved = false;                                              // 记录机器人的预约状态
    int m_timeoutCounter = 0;
    int m_retryCounter = 0;
    QTimer* m_shutdownTimer = nullptr;                                      // 关机专用定时器
    int m_shutdownStep = 0;                                                 // 关机当前步数

    // 辅助函数
    void writeRegister(int address, quint16 value);
    void writeRegisters(int address, const QVector<quint16> &values);
    void readRegisters(int address, int count);
    void pulseControlWord129(int bitIndex, int durationMs);                 // 发送脉冲指令
    QVector<quint16> floatToRegisters(float val);
};

#endif // MODBUSMANAGER_H
