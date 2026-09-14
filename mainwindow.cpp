#include "mainwindow.h"
#include "renderarea.h"
#include "usercoordinatemanager.h"
#include "rotationmatrixdialog.h"
#include "pathplanner.h"
#include "pathplanningdialog.h"
#include "connectiondialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProcess>
#include <QToolBar>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QTemporaryFile>
#include <QSplitter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDialog>
#include <QStatusBar>
#include <QInputDialog>
#include <QSettings>
#include <QCloseEvent>

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
}

MainWindow::~MainWindow() {
    if (m_coordManager) {
        delete m_coordManager;
    }
}

void MainWindow::setupUi()
{
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);                                  // 水平分割器
    setCentralWidget(splitter);

    // 1. 先创建左侧：渲染区域 (RenderArea)
    renderArea = new RenderArea(this);
    renderArea->setMinimumSize(800, 400);
    renderArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(renderArea);

    // 显示/隐藏用户坐标系
    QVBoxLayout* overlayLayout = new QVBoxLayout(renderArea);
    overlayLayout->setContentsMargins(15, 15, 15, 15);
    overlayLayout->addStretch();
    QHBoxLayout* bottomBtnLayout = new QHBoxLayout();

    m_toggleCoordBtn = new QPushButton("显示用户坐标系", renderArea);
    m_toggleCoordBtn->setCheckable(true);
    m_toggleCoordBtn->setChecked(true);                                                         // 默认显示
    m_toggleCoordBtn->setVisible(false);                                                        // 初始不可见
    m_toggleCoordBtn->setMinimumWidth(120);                                                     // 按钮最小宽度，避免文字挤压
    m_toggleCoordBtn->setCursor(Qt::ArrowCursor);
    QString btnStyle = R"(
        QPushButton { padding: 6px 12px; background-color: rgba(255, 255, 255, 0.95); border: 1px solid #ccc; border-radius: 4px;}
        QPushButton:checked { background-color: #2196F3; color: white; border-color: #2196F3;}
        QPushButton:hover {border-color: #2196F3; }
    )";
    m_toggleCoordBtn->setStyleSheet(btnStyle);

    bottomBtnLayout->addWidget(m_toggleCoordBtn);

    m_startBtn = new QPushButton("预约", renderArea);
    m_startBtn->setStyleSheet("QPushButton { background-color: rgba(76, 175, 80, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background-color: #45a049; }");
    m_startBtn->setCursor(Qt::ArrowCursor);
    m_startBtn->setVisible(false);

    m_pauseBtn = new QPushButton("暂停", renderArea);
    m_pauseBtn->setCheckable(true); // 可切换暂停/继续
    m_pauseBtn->setStyleSheet("QPushButton { background-color: rgba(255, 152, 0, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:checked { background-color: #e65100; text-decoration: underline; }");
    m_pauseBtn->setCursor(Qt::ArrowCursor);
    m_pauseBtn->setVisible(false);

    m_resetBtn = new QPushButton("复位", renderArea);
    m_resetBtn->setStyleSheet("QPushButton { background-color: rgba(244, 67, 54, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background-color: #d32f2f; }");
    m_resetBtn->setCursor(Qt::ArrowCursor);
    m_resetBtn->setVisible(false);

    bottomBtnLayout->addWidget(m_startBtn);
    bottomBtnLayout->addWidget(m_pauseBtn);
    bottomBtnLayout->addWidget(m_resetBtn);
    bottomBtnLayout->addStretch();
    overlayLayout->addLayout(bottomBtnLayout);

    // 2. 后创建右侧：数据表格 (TableWidget)
    dataTable = new QTableWidget(this);
    dataTable->setMinimumSize(200,400);
    dataTable->setColumnCount(4);
    dataTable->setHorizontalHeaderLabels({"ID", "半径", "二维坐标", "三维坐标"});
    dataTable->verticalHeader()->setVisible(false);                                             // 关闭表格行头
    // 优化表格列宽显示
    dataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    dataTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    splitter->addWidget(dataTable);                                                             // 添加到分割器右侧

    // 3. 设置初始比例：左侧占 3份，右侧占 1份
    splitter->setCollapsible(0, false);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    // 4. 创建菜单栏
    fileMenu = menuBar()->addMenu("文件");
    loadAction = new QAction("导入DXF", this);
    loadAction->setIcon(QIcon(":/img/images/dxf.jpg"));
    fileMenu->addAction(loadAction);

    m_operationMenu = menuBar()->addMenu("操作");
    rotateAction = new QAction("应用旋转矩阵", this);
    m_operationMenu->addAction(rotateAction);
    m_setupCoordAction = new QAction("建立用户坐标系", this);
    m_setupCoordAction->setIcon(QIcon(":/img/images/icons1.png"));
    m_operationMenu->addAction(m_setupCoordAction);
    m_pathPlanningAction = new QAction("自动焊接路径规划", this);
    m_pathPlanningAction->setIcon(QIcon(":/img/images/icons3.png"));
    m_operationMenu->addAction(m_pathPlanningAction);
    m_manageProcessAction = new QAction("焊接工艺管理", this);
    m_manageProcessAction->setIcon(QIcon(":/img/images/icons4.png"));
    m_operationMenu->addAction(m_manageProcessAction);
    m_posMethodAction = new QAction("选择定位方式", this);
    m_posMethodAction->setIcon(QIcon(":/img/images/icons5.png"));
    m_operationMenu->addAction(m_posMethodAction);

    m_connectMenu = menuBar()->addMenu("连接");
    m_connectAction = new QAction("建立连接", this);
    m_connectAction->setIcon(QIcon(":/img/images/icons6.png"));
    m_connectMenu->addAction(m_connectAction);

    // 5. 创建工具栏
    toolBar = addToolBar("工具栏");
    toolBar->setIconSize(QSize(32, 32));                                                        // 设置工具栏图标统一大小
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);                                   // 设置工具栏按钮样式为文字在图标下方
    toolBar->addAction(m_setupCoordAction);
    toolBar->addAction(m_pathPlanningAction);
    toolBar->addAction(m_manageProcessAction);
    toolBar->addSeparator();                                                                    // 加分隔符，和原有工具栏内容区分

    // 6. 状态标签
    m_statusLabel = new QLabel("就绪", this);
    statusBar()->addWidget(m_statusLabel);

    // 状态栏标签（管板半径/焊接孔数）
    QWidget* labelContainer = new QWidget(this);
    QVBoxLayout* labelLayout = new QVBoxLayout(labelContainer);                                 // 创建垂直布局，设置到容器上
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(2);                                                                 // 两个标签之间的垂直间距
    labelLayout->setAlignment(Qt::AlignCenter);                                                 // 标签在容器中居中对齐
    mainPlateRadiusLabel = new QLabel("管板半径：--", this);
    weldHoleCountLabel = new QLabel("焊接管孔数：0", this);
    QFont statusFont = mainPlateRadiusLabel->font();                                            // 设置字体
    statusFont.setPointSize(11);
    mainPlateRadiusLabel->setFont(statusFont);
    weldHoleCountLabel->setFont(statusFont);
    mainPlateRadiusLabel->setMinimumWidth(120);                                                 // 设置最小宽度，防止字体显示不全
    weldHoleCountLabel->setMinimumWidth(120);
    labelLayout->addWidget(mainPlateRadiusLabel);
    labelLayout->addWidget(weldHoleCountLabel);
    toolBar->addWidget(labelContainer);
    toolBar->addSeparator();

    // 工具栏上的状态指示器容器 (网络 + 伺服)
    QWidget* statusContainer = new QWidget(this);
    QHBoxLayout* statusLayout = new QHBoxLayout(statusContainer);
    statusLayout->setContentsMargins(10, 0, 10, 0);
    statusLayout->setSpacing(6);
    // 获取基础字体
    QFont statusFontObj = font();
    statusFontObj.setPointSize(11);
    // 网络连接状态
    m_statusIconLabel = new QLabel(this);
    m_statusIconLabel->setFixedSize(16, 16);
    m_statusIconLabel->setStyleSheet("background-color: #F44336; border-radius: 8px;");
    m_statusTextLabel = new QLabel("未连接", this);
    m_statusTextLabel->setFont(statusFontObj);
    m_statusTextLabel->setStyleSheet("color: #333333;");
    // 分隔符 1
    QLabel* separator1 = new QLabel(" | ", this);
    separator1->setStyleSheet("color: #999; font-weight: bold;");
    // 伺服使能状态
    m_servoIconLabel = new QLabel(this);
    m_servoIconLabel->setFixedSize(16, 16);
    m_servoIconLabel->setStyleSheet("background-color: #9E9E9E; border-radius: 8px;");
    m_servoTextLabel = new QLabel("伺服断开", this);
    m_servoTextLabel->setFont(statusFontObj);
    m_servoTextLabel->setStyleSheet("color: #333333;");
    // 分隔符 2
    QLabel* separator2 = new QLabel(" | ", this);
    separator2->setStyleSheet("color: #999; font-weight: bold;");
    // 自动/手动模式
    m_autoTextLabel = new QLabel("手动模式", this);
    QFont autoFont = statusFontObj;
    autoFont.setBold(true);
    m_autoTextLabel->setFont(autoFont);
    m_autoTextLabel->setStyleSheet("color: #FF9800;");
    // 按顺序添加到布局
    statusLayout->addWidget(m_statusIconLabel);
    statusLayout->addWidget(m_statusTextLabel);
    statusLayout->addSpacing(10);
    statusLayout->addWidget(separator1);
    statusLayout->addSpacing(10);
    statusLayout->addWidget(m_servoIconLabel);
    statusLayout->addWidget(m_servoTextLabel);
    statusLayout->addSpacing(10);
    statusLayout->addWidget(separator2);
    statusLayout->addSpacing(10);
    statusLayout->addWidget(m_autoTextLabel);
    toolBar->addWidget(statusContainer);

    // 7. 初始化坐标管理器
    m_coordManager = new usercoordinatemanager(this);
    m_coordManager->initialize(renderArea, dataTable, weldHoles, mainPlateHole, m_statusLabel);

    // 8. 信号槽连接
    connect(loadAction, &QAction::triggered, this, &MainWindow::importDxf);                 // 导入DXF → 触发importDxf函数
    connect(dataTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::handleTableSelectionChanged);                                // 表格选中行变化 → 处理选中逻辑
    connect(dataTable, &QTableWidget::cellChanged,
            this, &MainWindow::handleTableCellChanged);                                     // 表格单元格修改 → 同步更新管孔数据
    connect(rotateAction, &QAction::triggered, this, &MainWindow::applyRotationMatrix);     // 应用旋转矩阵 → 触发applyRotationMatrix函数
    // 连接 Manager的更新信号到表格刷新
    connect(m_coordManager, &usercoordinatemanager::update3DCoordinates,
            this, &MainWindow::updateTableFromData);
    // 建立用户坐标系的流程逻辑
    connect(m_setupCoordAction, &QAction::triggered, this, &MainWindow::setupCoordinateWizard);
    // 显示/隐藏坐标系按钮连接
    connect(m_toggleCoordBtn, &QPushButton::clicked, this, [this](bool checked) {
        m_coordManager->toggleCoordinateDisplay(checked);
        m_toggleCoordBtn->setText(checked ? "隐藏用户坐标系" : "显示用户坐标系");
    });
    // 路径规划
    connect(m_pathPlanningAction, &QAction::triggered, this, &MainWindow::onPathPlanningTriggered);
    // 焊接工艺
    connect(m_manageProcessAction, &QAction::triggered, this, &MainWindow::onManageWeldingProcess);
    // 通信
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnectTriggered);
    connect(m_posMethodAction, &QAction::triggered, this, &MainWindow::onSelectPositioningMethod);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(m_resetBtn, &QPushButton::clicked, this, &MainWindow::onResetClicked);

    resize(1200, 700);
}

// ----------------------------------------------------
// 功能：DXF导入与 Python脚本调用
// 核心：处理用户选择和 Python 调用的函数
// ----------------------------------------------------
void MainWindow::importDxf()
{
    // 1. 弹出文件对话框，让用户选择 DXF文件
    QString dxfPath = QFileDialog::getOpenFileName(this,"选择DXF文件","","DXF Files (*.dxf)");//提示，能选型的文件类型

    if (dxfPath.isEmpty()) return; // 用户取消选择

    // 2. 准备 Python脚本路径和临时 JSON 输出路径
    // QCoreApplication::applicationDirPath()可以找到 exe所在的目录
    QString pythonScript = QCoreApplication::applicationDirPath() + "/import_py_test.py";

    // 使用 QTemporaryFile 确保生成的 JSON文件路径唯一且安全
    // QTemporaryFile tempFile;
    // if (!tempFile.open()) {
    //     QMessageBox::critical(this, "Error", "Cannot create temporary file path.");
    //     return;
    // }

    //这是保存 JSON文件
    QString jsonOutputPath = QFileDialog::getSaveFileName(this, "保存JSON文件", "", "JSON Files (*.json)");
    if (jsonOutputPath.isEmpty()) return;
    // tempFile.close();                                   // 关闭临时文件，让 Python可以写入

    // 3. 配置Python进程参数
    QProcess process;
    QStringList params;
    params << pythonScript;                             // 参数1：脚本路径
    params << dxfPath;                                  // 参数2：用户选择的 DXF 路径 (动态传入！)
    params << jsonOutputPath;                           // 参数3：JSON 输出路径

    // 4. 运行 Python进程 (注意：需要确保系统 PATH 中有 'python' 命令)
    QString pythonExePath = "C:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/python.exe";
    process.start(pythonExePath, params);

    // 5. 处理进程执行结果（超时、错误）
    if (!process.waitForFinished(10000)) { // 等待 10 秒
        QMessageBox::warning(this, "Error", "Python script timeout or failed to start.");
        qDebug() << "Error:" << process.errorString();
        return;
    }
    if (process.exitCode() != 0) {
        // 读取 Python 脚本的标准错误输出 (stderr)
        QString errorOutput = QString::fromUtf8(process.readAllStandardError());
        QMessageBox::critical(this, "Script Error", "Python script failed:\n" + errorOutput);
        return;
    }

    // 6. 检查 Python 脚本是否运行成功
    if (process.exitCode() != 0) {
        QString error = process.readAllStandardError();
        QMessageBox::critical(this, "Script Error", "Python script failed:\n" + error);
        return;
    }

    // 7. 加载解析后的 JSON数据
    loadDrawingData(jsonOutputPath);
}

// ----------------------------------------------------
// 功能：JSON解析与 UI更新
// 核心：解析 Python成的 JSON数据，将数据存入结构体，并更新表格和绘图区
// ----------------------------------------------------
void MainWindow::loadDrawingData(const QString &filePath)
{
    // 1. 打开 JSON文件
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", "Failed to open data file.");
        return;
    }

    // 2. 解析JSON根对象
    QByteArray data = file.readAll();                               // 读取文件
    QJsonDocument doc = QJsonDocument::fromJson(data);              // 相当于把 JSON字符串转换成 Qt能理解的 JSON文档结构
    QJsonObject rootObj = doc.object();                             // 从解析好的 QJsonDocument中，提取出根层级的 JSON对象，存到 QJsonObject

    // 3. 解析圆数据
    allHoles.clear();
    QJsonArray holeArray = rootObj["holes"].toArray();              // 通过键名 holes从根对象中获取对应的值，将获取到的值强制转换为 QJsonArray（Qt的 JSON数组类型）
    for (const auto& holeObj : std::as_const(holeArray)) {          // 从所有管孔遍历 holeObj
        QJsonObject hole = holeObj.toObject();                      // 将当前遍历到的 JSON值（holeObj）转换为 QJsonObject（Qt的 JSON对象类型）
        Hole h;                                                     // 存储当前解析出的单个管孔数据
        h.id = hole.contains("id") ? hole["id"].toInt() : (allHoles.size() + 1);        // 管孔唯一编号
        QJsonArray center = hole["center"].toArray();               // 圆心（数组）
        h.center = QPointF(center[0].toDouble(), center[1].toDouble());
        h.radius = hole["radius"].toDouble();                       // 半径
        allHoles.append(h);                                         // 将解析完成的 Hole对象 h添加到 allHoles容器的末尾，完成单个管孔的解析和存储
    }

    // 2. 解析轮廓
    contours.clear();
    QJsonArray contourArray = rootObj["contours"].toArray();
    for (const auto& cObj : std::as_const(contourArray)) {
        Contour c;
        QJsonArray pts = cObj.toArray();
        for (const auto& pVal : std::as_const(pts)) {
            QJsonArray xy = pVal.toArray();
            c.points.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
        }
        contours.append(c);
    }
    weldHoles.clear();
    mainPlateContour.clear();
    mainPlateHole = Hole();                                         // 初始化主体圆
    if (!contours.isEmpty()) {
        isRectangularPlate = true;
        // 假设第一个轮廓就是外边框 (通常 DXF 里最大的框是外边框)
        const auto& points = contours[0].points;
        for(const QPointF& p : points) {
            mainPlateContour << p;
        }
        weldHoles = allHoles;                                       // 这种模式下，所有的圆都是焊接孔
        // 需要计算一个虚拟的主板圆心和半径给后续的坐标系功能使用
        QRectF rect = mainPlateContour.boundingRect();
        mainPlateHole.center = rect.center();
        // 近似半径：取宽高中较小的一半，或者对角线的一半，用于坐标系向导的参考距离
        mainPlateHole.radius = qMin(rect.width(), rect.height()) / 2.0;

    } else {
        // 没有轮廓，找最大圆
        isRectangularPlate = false;
        if (!allHoles.isEmpty()) {
            mainPlateHole = allHoles[0];
            // 找最大半径
            for (const auto& hole : std::as_const(allHoles)) {
                if (hole.radius > mainPlateHole.radius + 1e-3)
                    mainPlateHole = hole;
            }
            // 分离管孔
            int weldHoles_count = 0;
            for (const auto& hole : std::as_const(allHoles)) {
                if (hole.radius < mainPlateHole.radius - 1e-3) {
                    weldHoles_count++;
                    Hole newHole = hole;
                    newHole.id = weldHoles_count;
                    weldHoles.append(newHole);
                }
            }
        }
    }
    if (isRectangularPlate) {
        mainPlateRadiusLabel->setText("管板类型：方形/多边形");
    } else {
        mainPlateRadiusLabel->setText(QString("管板半径：%1").arg(mainPlateHole.radius, 0, 'f', 2));
    }
    weldHoleCountLabel->setText(QString("焊接管孔数：%1").arg(weldHoles.size()));
    // 4. 解析轮廓数据（暂未启用，保留扩展）
    // contours.clear();
    // QJsonArray contourArray = rootObj["contours"].toArray();
    // for (const auto& contourObj : std::as_const(contourArray)) {
    //     Contour c;
    //     QJsonArray pointsArray = contourObj.toArray();
    //     for (const auto& pointObj : std::as_const(pointsArray)) {
    //         QJsonArray point = pointObj.toArray();
    //         c.points.append(QPointF(point[0].toDouble(), point[1].toDouble()));
    //     }
    //     contours.append(c);
    // }

    // 5. 更新右侧表格(只更新管孔的)
    // 加载表格前断开信号连接
    /*
     QTableWidget::cellChanged是 “单元格内容被修改” 时触发的信号
    */
    disconnect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
    dataTable->setRowCount(0);                                                                          // 清空表格数据
    for (int i = 0; i < weldHoles.size(); ++i) {
        dataTable->insertRow(i);                                                                        // 插入行
        /*
         * 可替换dataTable->setItem(i, 0, new QTableWidgetItem(QString::number(weldHoles[i].id)));使第 0列数据不能修改数据
         * QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(weldHoles[i].id));           // 创建对象
         * idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);                                     // 移除可编辑标志，保留可选择、启用
         * dataTable->setItem(i, 0, idItem);
         */
        dataTable->setItem(i, 0, new QTableWidgetItem(QString::number(weldHoles[i].id)));               // 第 i行第 0列：ID
        dataTable->setItem(i, 1, new QTableWidgetItem(QString::number(weldHoles[i].radius, 'f', 2)));   // 半径
        QString centerStr = QString("(%1, %2)")                                                         // 二维圆心坐标
                                .arg(weldHoles[i].center.x(), 0, 'f', 2)
                                .arg(weldHoles[i].center.y(), 0, 'f', 2);
        dataTable->setItem(i, 2, new QTableWidgetItem(centerStr));
        QString center3DStr = QString("(%1, %2, %3)")                                                   // 三维坐标
                                  .arg(weldHoles[i].center3D.x(), 0, 'f', 2)
                                  .arg(weldHoles[i].center3D.y(), 0, 'f', 2)
                                  .arg(weldHoles[i].center3D.z(), 0, 'f', 2);
        QTableWidgetItem* item3D = new QTableWidgetItem(center3DStr);
        item3D->setFlags(item3D->flags() & ~Qt::ItemIsEditable);                                        // 初始加载时，禁止编辑三维坐标
        item3D->setBackground(QBrush(QColor(245, 245, 245)));                                           // 设置浅灰色背景提示用户不可编辑
        dataTable->setItem(i, 3, item3D);
    }
    // 更新表格后重新连接信号
    connect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
    // updateTableFromData();

    // 6. 更新左侧绘图区
    renderArea->setData(weldHoles, mainPlateHole, mainPlateContour, isRectangularPlate);
    renderArea->setShowUserCoordinate(false);

    // 7. 重置选中状态
    m_currentSelectedIndex = -1;                                                                        // 当前选中的孔洞索引
    renderArea->setHighlightedIndex(m_currentSelectedIndex);
    if (m_coordManager) {                                                                               // 判断指针是否有效
        delete m_coordManager;
    }
    m_coordManager = new usercoordinatemanager(this);
    m_coordManager->initialize(renderArea, dataTable, weldHoles, mainPlateHole, m_statusLabel);

    if (m_toggleCoordBtn) {
        m_toggleCoordBtn->setVisible(false);
        m_toggleCoordBtn->setChecked(true);
        m_toggleCoordBtn->setText("显示用户坐标系");
    }
    m_isPathPlanned = false;
}

// void MainWindow::loadJSONData()
// {
//     // 保持这个备用函数不变，如果你想单独加载 JSON
//     QString filePath = QFileDialog::getOpenFileName(this, "Open JSON File", "", "JSON Files (*.json)");
//     if (!filePath.isEmpty()) {
//         loadDrawingData(filePath);
//         update();
//     }
// }

// ----------------------------------------------------
// 功能：表格与绘图区的联动（交互逻辑）
// 核心：表格选中某行 → 绘图区高亮对应圆
// ----------------------------------------------------
void MainWindow::handleTableSelectionChanged()
{
    int selectedRow = dataTable->currentRow();                          // 获取当前选中行（-1表示未选中）
    m_currentSelectedIndex = selectedRow;                               // 更新主窗口的选中索引
    renderArea->setHighlightedIndex(m_currentSelectedIndex);            // 传递给绘图区，实现高亮
}

// ----------------------------------------------------
// 功能：响应单元格内容的修改，验证输入的有效性，更新底层数据模型，并同步刷新绘图区
// ----------------------------------------------------
void MainWindow::handleTableCellChanged(int row, int column)
{
    if (row < 0 || row >= weldHoles.size())                             // 索引有效性检查：防止行索引超出weldHoles的范围，避免数组越界
        return;

    // 获取当前单元格数据
    QTableWidgetItem *item = dataTable->item(row, column);              // 获取当前单元格的 item对象，判空防止空指针访问（一个单元格对象）
    if (!item) return;

    // 校验输入的数据有效性
    QString value = item->text();                                       // 获取 item对象的内容
    bool ok;
    double num =0;
    if (column == 1) {                                                  // 第一列半径
        // 半径列：用原有的toDouble验证
        // bool ok;
        num=value.toDouble(&ok);                                        // 转换成功，ok=true
        if (!ok) {                                                      // 转换失败
            QMessageBox::warning(this, "输入错误", "请输入有效的数字");
            updateTableItem(row, column);                               // 将输入错误的数据还原为修改前的数据
            return;
        }
    } else if (column == 2) {                                           // 第二列二维圆心坐标
        QStringList parts = value.split(',');                           // 按逗号，把字符串拆分成字符串列表
        parts[0].replace('(', ' ').trimmed().toDouble(&ok);             // 先将(替换为空格，再去掉首尾的所有空格，最后转换为浮点数
        if (!ok || parts.size() != 2) {
            QMessageBox::warning(this, "输入错误", "请输入有效的数字");
            updateTableItem(row, column);
            return;
        }
        parts[1].replace(')', ' ').trimmed().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(this, "输入错误", "请输入有效的数字");
            updateTableItem(row, column);
            return;
        }
    }

    // 修改对应单元格数据
    Hole &hole = weldHoles[row];                                        // 根据列索引更新 weldHoles中对应的数据
    switch (column) {
    case 1:{                                                            // 半径
        hole.radius = num;
        break;}
    case 2:{                                                            // 圆心坐标，需要特殊处理
        QStringList parts = value.split(',');                           // 解析 "X, Y"格式
        if (parts.size() == 2) {
            double x = parts[0].replace('(', ' ').trimmed().toDouble(&ok);          // 去除括号和空格，转换为 X坐标
            if (ok) {
                double y = parts[1].replace(')', ' ').trimmed().toDouble(&ok);      // 去除括号和空格，转换为 Y坐标
                if (ok) {
                    hole.center.setX(x);
                    hole.center.setY(y);
                }
            }
        }
        item->setText(QString("(%1, %2)")
                          .arg(hole.center.x(), 0, 'f', 2)
                          .arg(hole.center.y(), 0, 'f', 2));         // 重新格式化显示：保证坐标始终是"(X, Y)"且保留 2位小数，统一格式
        break;
    }
    case 3:{
        QString str = value;
        str.remove('(').remove(')');
        QStringList parts = str.split(',');
        if (parts.size() == 3) {
            double x = parts[0].trimmed().toDouble(&ok);
            if (ok) {
                double y = parts[1].trimmed().toDouble(&ok);
                if (ok) {
                    double z = parts[2].trimmed().toDouble(&ok);
                    if (ok) {
                        hole.center3D = QVector3D(x, y, z);
                        // 如果修改了三维坐标，同步更新二维坐标（投影）
                        hole.center = QPointF(x, y);
                        // 更新二维坐标单元格
                        dataTable->item(row, 2)->setText(QString("(%1, %2)")
                                                             .arg(x, 0, 'f', 2)
                                                             .arg(y, 0, 'f', 2));
                    }
                }
            }
        }
        item->setText(QString("(%1, %2, %3)")
                          .arg(hole.center3D.x(), 0, 'f', 2)
                          .arg(hole.center3D.y(), 0, 'f', 2)
                          .arg(hole.center3D.z(), 0, 'f', 2));
        break;}
    }

    // 更新绘图区
    renderArea->setData(weldHoles, mainPlateHole, mainPlateContour, isRectangularPlate, false);
}

// ----------------------------------------------------
// 功能：用于输入验证失败时恢复数据，或初始化表格单元格内容
// 核心：从底层数据结构 weldHoles中读取原始数据，重新设置到表格单元格中
// ----------------------------------------------------
void MainWindow::updateTableItem(int row, int column)
{
    if (row < 0 || row >= weldHoles.size())
        return;

    const Hole &hole = weldHoles[row];                                                  // dxf导入的的焊接管孔数据
    switch (column) {
    case 0:
        dataTable->setItem(row, 0, new QTableWidgetItem(QString::number(hole.id)));
        break;
    case 1:
        dataTable->setItem(row, 1, new QTableWidgetItem(QString::number(hole.radius, 'f', 2)));
        break;
    case 2:
        dataTable->setItem(row, 2, new QTableWidgetItem(QString("(%1, %2)").arg(hole.center.x(), 0, 'f', 2).arg(hole.center.y(), 0, 'f', 2)));
        break;
    }
}

// ----------------------------------------------------
// 功能：（与新建立的坐标系连接）
// ----------------------------------------------------
void MainWindow::updateTableFromData()
{
    disconnect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
    dataTable->setRowCount(0);

    for (int i = 0; i < weldHoles.size(); ++i) {
        dataTable->insertRow(i);
        dataTable->setItem(i, 0, new QTableWidgetItem(QString::number(weldHoles[i].id)));
        dataTable->setItem(i, 1, new QTableWidgetItem(QString::number(weldHoles[i].radius, 'f', 2)));

        QString centerStr = QString("(%1, %2)")
                                .arg(weldHoles[i].center3D.x(), 0, 'f', 2)
                                .arg(weldHoles[i].center3D.y(), 0, 'f', 2);
        dataTable->setItem(i, 2, new QTableWidgetItem(centerStr));

        QString center3DStr = QString("(%1, %2, %3)")
                                  .arg(weldHoles[i].center3D.x(), 0, 'f', 2)
                                  .arg(weldHoles[i].center3D.y(), 0, 'f', 2)
                                  .arg(weldHoles[i].center3D.z(), 0, 'f', 2);
        dataTable->setItem(i, 3, new QTableWidgetItem(center3DStr));
    }

    connect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
}

// ----------------------------------------------------
// 功能：旋转矩阵
// ----------------------------------------------------
void MainWindow::applyRotationMatrix()
{
    RotationMatrixDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        rotationMatrix = dialog.getRotationMatrix();

        // 应用旋转矩阵到所有孔洞
        for (auto& hole : weldHoles) {
            // 初始三维坐标 (x, y, 0)
            QVector3D original(hole.center.x(), hole.center.y(), 0.0);

            // 应用旋转
            QVector3D transformed = rotationMatrix.map(original);

            // 更新三维坐标
            hole.center3D = transformed;
        }

        disconnect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
        for (int i = 0; i < weldHoles.size(); ++i) {
            const Hole& hole = weldHoles[i];
            QString center3DStr = QString("(%1, %2, %3)")
                                      .arg(hole.center3D.x(), 0, 'f', 2)
                                      .arg(hole.center3D.y(), 0, 'f', 2)
                                      .arg(hole.center3D.z(), 0, 'f', 2);
            dataTable->setItem(i, 3, new QTableWidgetItem(center3DStr));
        }
        connect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);

        QMessageBox::information(this, "成功", "旋转矩阵已应用");
    }
}

// ----------------------------------------------------
// 功能：建立用户坐标系向导
// ----------------------------------------------------
void MainWindow::setupCoordinateWizard()
{
    // 如果没有管孔数据，弹窗提示并返回
    if (weldHoles.isEmpty()) {
        QMessageBox::warning(this, "操作提示", "当前没有管孔数据，无法建立坐标系。\n请先点击“文件 -> 导入DXF”加载图纸。");
        return;
    }

    if (m_coordManager && m_coordManager->isSetupComplete()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this,
                                      "重新建立",
                                      "检测到当前已建立用户坐标系，是否重新建立？\n（重新建立将覆盖原有坐标系数据）",
                                      QMessageBox::Yes | QMessageBox::Cancel,
                                      QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    QDialog* dialog = new QDialog(this);
    // 添加自动销毁属性，避免内存泄漏
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("用户坐标系建立向导");
    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setMinimumWidth(400);

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    // 标题标签
    QLabel* titleLabel = new QLabel("请按照提示操作机器人", dialog);
    QFont font = titleLabel->font();
    font.setBold(true);
    font.setPointSize(14);
    titleLabel->setFont(font);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    QLabel* infoLabel = new QLabel("初始化中...", dialog);
    infoLabel->setWordWrap(true);
    infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoLabel->setStyleSheet(R"(
        QLabel {
            color: blue;
            font-size: 14px;
            padding: 10px; /* 标签内部上下左右内边距，避免文本贴边 */
            line-height: 1.5; /* 行间距1.5倍，文本不拥挤 */
        }
    )");
    infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    infoLabel->setMinimumHeight(60);
    layout->addWidget(infoLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* confirmBtn = new QPushButton("确定 (当前步骤完成)", dialog);
    QPushButton* cancelBtn = new QPushButton("取消", dialog);
    btnLayout->addWidget(confirmBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    // 使用定时器同步 Manager 的状态文字到对话框
    QTimer* syncTimer = new QTimer(dialog);
    connect(syncTimer, &QTimer::timeout, this, [this, infoLabel]() {
        if (m_statusLabel) infoLabel->setText(m_statusLabel->text());
    });
    syncTimer->start(200);

    // 确认按钮逻辑
    connect(confirmBtn, &QPushButton::clicked, this, [this, dialog]() {
        m_coordManager->confirmCurrentStep();
        if (m_coordManager->isSetupComplete()) {
            dialog->accept();
            QMessageBox::information(this, "成功", "用户坐标系已建立！\n三维坐标已更新。");
            m_toggleCoordBtn->setVisible(true); // 按照要求，完成后显示控制按钮
            m_toggleCoordBtn->setChecked(true);
        }
    });

    // 取消按钮逻辑
    connect(cancelBtn, &QPushButton::clicked, this, [this, dialog]() {
        m_coordManager->cancelCurrentStep();
        dialog->reject();
    });

    // 右上角叉号，执行取消逻辑
    connect(dialog, &QDialog::finished, this, [this](int result) {

        if (result == QDialog::Rejected) {
            m_coordManager->cancelCurrentStep();
        }
    });

    // 启动向导
    m_coordManager->startSetup();

    dialog->adjustSize();
    dialog->move(this->geometry().center() - dialog->rect().center());

    dialog->exec();
}

// ----------------------------------------------------
// 功能：自动焊接路径规划
// ----------------------------------------------------
void MainWindow::onPathPlanningTriggered()
{
    // 1. 前置检查
    if (weldHoles.isEmpty()) {
        QMessageBox::warning(this, "警告", "没有管孔数据，请先导入DXF文件。");
        return;
    }

    // 检查是否建立了用户坐标系 (需求提到：在建立完坐标系之后)
    if (!m_coordManager || !m_coordManager->isSetupComplete()) {
        QMessageBox::warning(this, "警告", "请先建立用户坐标系后再进行路径规划。");
        return;
    }

    // 2. 弹出算法选择对话框
    PathPlanningDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    PathPlanner::AlgorithmType selectedAlgo = dialog.getSelectedAlgorithm();

    // 3. 执行路径规划 (传入当前 weldingHoles和主体圆)
    QVector<Hole> sortedHoles = PathPlanner::planPath(weldHoles, mainPlateHole, selectedAlgo);

    // 4. 更新数据
    weldHoles = sortedHoles;
    m_isPathPlanned = true;

    // 5. 刷新界面 (表格 + 绘图区)
    // 更新表格
    disconnect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);
    dataTable->setRowCount(0);
    for (int i = 0; i < weldHoles.size(); ++i) {
        dataTable->insertRow(i);
        dataTable->setItem(i, 0, new QTableWidgetItem(QString::number(weldHoles[i].id)));
        dataTable->setItem(i, 1, new QTableWidgetItem(QString::number(weldHoles[i].radius, 'f', 2)));
        QString centerStr = QString("(%1, %2)").arg(weldHoles[i].center3D.x(), 0, 'f', 2).arg(weldHoles[i].center3D.y(), 0, 'f', 2);
        dataTable->setItem(i, 2, new QTableWidgetItem(centerStr));
        QString center3DStr = QString("(%1, %2, %3)").arg(weldHoles[i].center3D.x(), 0, 'f', 2).arg(weldHoles[i].center3D.y(), 0, 'f', 2).arg(weldHoles[i].center3D.z(), 0, 'f', 2);
        dataTable->setItem(i, 3, new QTableWidgetItem(center3DStr));
    }
    connect(dataTable, &QTableWidget::cellChanged, this, &MainWindow::handleTableCellChanged);

    // 更新绘图区
    renderArea->setData(weldHoles, mainPlateHole, mainPlateContour, isRectangularPlate, false);

    QMessageBox::information(this, "成功", "路径规划已完成，管孔编号已更新。");

    // 6. 保存为 JSON
    QString savePath = QFileDialog::getSaveFileName(this, "保存规划结果", "", "JSON Files (*.json)");
    if (savePath.isEmpty()) return;

    QJsonObject rootObj;
    QJsonArray holeArray;
    for (const auto& h : std::as_const(weldHoles)) {
        QJsonObject hObj;
        hObj["id"] = h.id;
        hObj["radius"] = h.radius;

        // 保存规划后的顺序和ID
        //QJsonArray centerArr;
        //centerArr.append(h.center.x());
        //centerArr.append(h.center.y());
        //hObj["center"] = centerArr;

        // 也可以保存三维坐标
        QJsonArray center3DArr;
        center3DArr.append(h.center3D.x());
        center3DArr.append(h.center3D.y());
        center3DArr.append(h.center3D.z());
        hObj["center3D"] = center3DArr;

        holeArray.append(hObj);
    }
    rootObj["holes"] = holeArray;
    rootObj["algorithm"] = static_cast<int>(selectedAlgo); // 记录用的什么算法

    QJsonDocument doc(rootObj);
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        QMessageBox::information(this, "保存成功", "文件已保存至: " + savePath);
    } else {
        QMessageBox::critical(this, "错误", "无法写入文件");
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
    dlg.setIp(m_lastIp);
    dlg.setPort(m_lastPort);

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

