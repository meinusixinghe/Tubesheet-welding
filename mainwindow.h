#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QVector>
#include <QAction>
#include <QLabel>
#include <QVector3D>
#include <QMatrix4x4>
#include <QTimer>
#include <QPushButton>
#include "weldingprocessdialog.h"
#include "modbusmanager.h"
#include <QCloseEvent>
#include <QSettings>

class RenderArea;
class usercoordinatemanager;

struct Hole { int id; QPointF center; QVector3D center3D; double radius; };     // 管孔的唯一编号、管孔的二维坐标（二维点类，浮点型）、管孔的三维坐标（三维点类，浮点型）、管孔的半径
struct Contour { QVector<QPointF> points; };                                    // 多段线（目前未启用）

class MainWindow : public QMainWindow
{
    Q_OBJECT

protected:
    // 拦截窗口关闭事件，确保安全断开连接
    void closeEvent(QCloseEvent *event) override;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 导入DXF的触发
    void importDxf();
    // void loadJSONData(); // 备用，如果想直接加载 JSON

    // 处理右侧列表点击的槽函数
    void handleTableSelectionChanged();
    // 修改表格数据触发
    void handleTableCellChanged(int row, int column);

    void applyRotationMatrix();
    void updateTableItem(int row, int column);
    void updateTableFromData();

    // 建立用户坐标系向导
    void setupCoordinateWizard();

    // 路径规划
    void onPathPlanningTriggered();

    // 管理焊接工艺
    void onManageWeldingProcess();

    // Modbus
    void onConnectTriggered();
    void onModbusStateChanged(int state);
    void onStartClicked();
    void onPauseClicked();
    void onResetClicked();

    // 定位方式选择
    void onSelectPositioningMethod();

    // 响应伺服状态变化的槽函数
    void onServoStateChanged(bool enabled);

    // 响应自动模式变化的槽函数
    void onAutoStateChanged(bool isAuto);

    // 持续发送下一个管孔的函数
    void sendNextWeldHole();
private:
    void loadDrawingData(const QString &filePath);      // 核心数据加载函数
    void setupUi();                                     // UI初始化函数

    void loadWeldingProcesses();                        // 从 JSON文件加载焊接工艺

    QVector<Hole> allHoles;                             // 所有圆（含主体圆+焊接管孔）
    QVector<Hole> weldHoles;                            // 仅焊接管孔（不含主体圆）
    Hole mainPlateHole;                                 // 管板主体圆（最大半径)
    QVector<Contour> contours;
    QPolygonF mainPlateContour;                         // 方形/多边形管板数据
    bool isRectangularPlate = false;                    // 标记当前是否为方形管板

    RenderArea* renderArea;                             // 左侧绘图区
    QTableWidget* dataTable;                            // 右侧数据表格

    QAction* loadAction;
    QMenu* fileMenu;
    QToolBar* toolBar;
    QLabel* mainPlateRadiusLabel;                       // 显示主体圆半径
    QLabel* weldHoleCountLabel;                         // 显示焊接管孔数量

    int m_currentSelectedIndex = -1;                    // 存储当前选中的孔洞索引 (-1表示未选中)

    QAction* rotateAction;
    QMatrix4x4 rotationMatrix;                          // 存储旋转矩阵

    usercoordinatemanager* m_coordManager;              // 自定义坐标类
    QAction* m_setupCoordAction;
    QPushButton* m_toggleCoordBtn;
    QLabel* m_statusLabel;                              // 状态栏标签
    QMenu* m_operationMenu;

    QAction* m_pathPlanningAction;                      // 路径规划菜单项

    QVector<WeldingProcess> m_weldingProcesses;         // 存储所有的焊接工艺数据
    QAction* m_manageProcessAction;                     // 菜单动作

    ModbusManager* m_modbusManager;
    QMenu* m_connectMenu;
    QAction* m_connectAction;
    QAction* m_posMethodAction;                         // 选择定位方式

    QPushButton* m_startBtn;
    QPushButton* m_pauseBtn;
    QPushButton* m_resetBtn;

    QString m_lastIp = "192.168.1.10";                  // 记住上次IP
    int m_lastPort = 502;
    QLabel* m_statusIconLabel;                          // 连接状态指示灯
    QLabel* m_statusTextLabel;                          // 文字标签

    QLabel* m_servoIconLabel;                           // 伺服状态相关的 UI 控件
    QLabel* m_servoTextLabel;

    QLabel* m_autoTextLabel;                            // 自动/手动状态相关的 UI 控件

    bool m_isShuttingDown = false;                      // 用于标记是否正在执行安全退出

    int m_currentWeldIndex = 0;                         // 当前正在焊接的管孔索引
    bool m_isWeldingProcessRunning = false;             // 是否正在连续焊接中

    int m_positioningMethod = 0;                        // 保存用户选择的定位方式号，默认为 0
    bool m_isPathPlanned = false;
};

#endif // MAINWINDOW_H
