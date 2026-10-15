#include "mainwindow.h"
#include "renderarea.h"
#include "usercoordinatemanager.h"
#include <QActionGroup>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

void MainWindow::setupUi()
{
    // ==========================================
    // 1. 全局架构：使用堆叠引擎包裹主界面
    // ==========================================
    m_mainStackedWidget = new QStackedWidget(this);
    setCentralWidget(m_mainStackedWidget);

    m_main3DWidget = new QWidget(this);
    QVBoxLayout* main3DLayout = new QVBoxLayout(m_main3DWidget);
    main3DLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, m_main3DWidget);
    main3DLayout->addWidget(splitter);
    m_mainStackedWidget->addWidget(m_main3DWidget); // 第 0 页：主视图

    // 为将来的全屏面板预留页面
    QWidget* processPage = new QWidget(this);
    QWidget* pathPlanPage = new QWidget(this);
    m_mainStackedWidget->addWidget(processPage);  // 第 1 页
    m_mainStackedWidget->addWidget(pathPlanPage); // 第 2 页

    // ==========================================
    // 2. 左侧：渲染区域 (RenderArea) 与 悬浮按键
    // ==========================================
    renderArea = new RenderArea(this);
    renderArea->setMinimumSize(800, 400);
    renderArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(renderArea);

    QVBoxLayout* overlayLayout = new QVBoxLayout(renderArea);
    overlayLayout->setContentsMargins(15, 15, 15, 15);
    overlayLayout->addStretch();
    QHBoxLayout* bottomBtnLayout = new QHBoxLayout();

    m_toggleCoordBtn = new QPushButton("显示用户坐标系", renderArea);
    m_toggleCoordBtn->setCheckable(true);
    m_toggleCoordBtn->setChecked(true);
    m_toggleCoordBtn->setVisible(false);
    m_toggleCoordBtn->setMinimumWidth(120);
    m_toggleCoordBtn->setCursor(Qt::ArrowCursor);
    QString btnStyleCoord = R"(
        QPushButton { padding: 6px 12px; background-color: rgba(255, 255, 255, 0.95); border: 1px solid #ccc; border-radius: 4px;}
        QPushButton:checked { background-color: #2196F3; color: white; border-color: #2196F3;}
        QPushButton:hover {border-color: #2196F3; }
    )";
    m_toggleCoordBtn->setStyleSheet(btnStyleCoord);

    m_startBtn = new QPushButton("预约", renderArea);
    m_startBtn->setStyleSheet("QPushButton { background-color: rgba(76, 175, 80, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background-color: #45a049; }");
    m_startBtn->setVisible(false);

    m_pauseBtn = new QPushButton("暂停", renderArea);
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setStyleSheet("QPushButton { background-color: rgba(255, 152, 0, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:checked { background-color: #e65100; text-decoration: underline; }");
    m_pauseBtn->setVisible(false);

    m_resetBtn = new QPushButton("复位", renderArea);
    m_resetBtn->setStyleSheet("QPushButton { background-color: rgba(244, 67, 54, 0.95); color: white; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background-color: #d32f2f; }");
    m_resetBtn->setVisible(false);

    bottomBtnLayout->addWidget(m_toggleCoordBtn);
    bottomBtnLayout->addWidget(m_startBtn);
    bottomBtnLayout->addWidget(m_pauseBtn);
    bottomBtnLayout->addWidget(m_resetBtn);
    bottomBtnLayout->addStretch();
    overlayLayout->addLayout(bottomBtnLayout);

    // ==========================================
    // 3. 右侧：导航切换按键 + 堆叠窗口
    // ==========================================
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* navLayout = new QHBoxLayout();
    m_btnShowTable = new QPushButton("管孔与焊缝数据", this);
    m_btnShowConfig = new QPushButton("3D视觉配置", this);
    m_btnShowTable->setMinimumHeight(30);
    m_btnShowConfig->setMinimumHeight(30);

    QString navBtnStyle = "QPushButton { font-weight: bold; background-color: #f0f0f0; border: 1px solid #ccc; border-radius: 4px; font-size: 12px; }"
                          "QPushButton:hover { background-color: #e0e0e0; }"
                          "QPushButton:checked { background-color: #d0e8f2; border: 1px solid #0078d7; color: #0078d7; }";
    m_btnShowTable->setStyleSheet(navBtnStyle);
    m_btnShowTable->setCheckable(true);
    m_btnShowTable->setChecked(true);
    m_btnShowConfig->setStyleSheet(navBtnStyle);
    m_btnShowConfig->setCheckable(true);

    navLayout->addWidget(m_btnShowTable);
    navLayout->addWidget(m_btnShowConfig);
    rightLayout->addLayout(navLayout);

    m_rightStacked = new QStackedWidget(this);
    m_rightStacked->setMinimumSize(250, 400);

    // [第 0 页：管孔表格]
    dataTable = new QTableWidget(this);
    dataTable->setColumnCount(4);
    dataTable->setHorizontalHeaderLabels({"ID", "半径", "二维坐标", "三维坐标"});
    dataTable->verticalHeader()->setVisible(false);
    dataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    dataTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_rightStacked->addWidget(dataTable);

    // [第 1 页：3D 设置]
    QWidget* visionSettingsWidget = new QWidget();
    QVBoxLayout* vLayout = new QVBoxLayout(visionSettingsWidget);
    QGroupBox* paramGroup = new QGroupBox("核心算法参数设置");
    QFormLayout* formLayout = new QFormLayout(paramGroup);
    m_ransacThreshSpin = new QDoubleSpinBox();
    m_ransacThreshSpin->setRange(0.1, 10.0); m_ransacThreshSpin->setSingleStep(0.1); m_ransacThreshSpin->setValue(1.0);
    formLayout->addRow("基准面拟合容差 (mm):", m_ransacThreshSpin);
    m_clusterMinSpin = new QSpinBox();
    m_clusterMinSpin->setRange(10, 1000); m_clusterMinSpin->setValue(30);
    formLayout->addRow("聚类最小点数 (个):", m_clusterMinSpin);
    m_circleThreshSpin = new QDoubleSpinBox();
    m_circleThreshSpin->setRange(0.1, 5.0); m_circleThreshSpin->setSingleStep(0.1); m_circleThreshSpin->setValue(0.5);
    formLayout->addRow("圆拟合紧密度 (mm):", m_circleThreshSpin);
    vLayout->addWidget(paramGroup);


    QGroupBox* viewGroup = new QGroupBox("算法中间过程检视");
    QVBoxLayout* VviewLayout = new QVBoxLayout(viewGroup);
    QHBoxLayout* HviewLayout1 = new QHBoxLayout();
    QHBoxLayout* HviewLayout2 = new QHBoxLayout();
    VviewLayout->addLayout(HviewLayout1);
    VviewLayout->addLayout(HviewLayout2);
    QPushButton* btnOrig = new QPushButton("1. 查看清洗点云");
    QPushButton* btnFilter = new QPushButton("2. 查看Sor滤波后点云");
    QPushButton* btnBase = new QPushButton("3. 仅查看基准面点云");
    QPushButton* btnCenter = new QPushButton("4. 查看拟合圆点云图");
    HviewLayout1->addWidget(btnOrig);
    HviewLayout1->addWidget(btnFilter);
    HviewLayout2->addWidget(btnBase);
    HviewLayout2->addWidget(btnCenter);
    vLayout->addWidget(viewGroup);
    vLayout->addStretch();
    m_rightStacked->addWidget(visionSettingsWidget);

    rightLayout->addWidget(m_rightStacked);
    splitter->addWidget(rightPanel);

    // 绑定右侧切换
    connect(m_btnShowTable, &QPushButton::clicked, this, [this](){
        m_rightStacked->setCurrentIndex(0); m_btnShowTable->setChecked(true); m_btnShowConfig->setChecked(false);
    });
    connect(m_btnShowConfig, &QPushButton::clicked, this, [this](){
        m_rightStacked->setCurrentIndex(1); m_btnShowConfig->setChecked(true); m_btnShowTable->setChecked(false);
    });

    // PCL Viewer 绑定
    connect(btnOrig, &QPushButton::clicked, this, [this](){ launchPclViewer(QCoreApplication::applicationDirPath() + "/temp_clean.pcd", "2"); });
    connect(btnFilter, &QPushButton::clicked, this, [this](){ launchPclViewer(QCoreApplication::applicationDirPath() + "/temp_filtered.pcd", "2"); });
    connect(btnBase, &QPushButton::clicked, this, [this](){ launchPclViewer(QCoreApplication::applicationDirPath() + "/temp_base.pcd", "3"); });
    connect(btnCenter, &QPushButton::clicked, this, [this](){
        QStringList finalFiles;
        finalFiles << QCoreApplication::applicationDirPath() + "/temp_base.pcd" << QCoreApplication::applicationDirPath() + "/temp_features.pcd" << QCoreApplication::applicationDirPath() + "/temp_centers.pcd";
        launchPclViewer(finalFiles, "3");
    });

    splitter->setCollapsible(0, false);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    // ==========================================
    // 4. Ribbon 双层选项卡与工具栏
    // ==========================================
    menuBar()->hide();

    // --- 动作初始化 ---
    loadAction = new QAction(QIcon(":/img/images/inport dxf.png"), "导入DXF图纸", this);
    m_setSaveDirAction = new QAction("设置图片保存路径", this);
    m_viewPointCloudAction = new QAction("查看点云图", this);
    rotateAction = new QAction("应用旋转矩阵", this);
    m_setupCoordAction = new QAction(QIcon(":/img/images/define a user coordinate system.png"), "建立用户坐标系", this);
    m_pathPlanningAction = new QAction(QIcon(":/img/images/icons3.png"), "自动焊接路径规划", this);
    m_manageProcessAction = new QAction(QIcon(":/img/images/icons4.png"), "焊接工艺管理", this);
    m_posMethodAction = new QAction(QIcon(":/img/images/icons5.png"), "选择定位方式", this);
    m_connectAction = new QAction(QIcon(":/img/images/establish links.png"), "建立连接", this);

    m_pathPlanningAction->setCheckable(true);
    m_manageProcessAction->setCheckable(true);

    // --- 选项卡层 (降低高度，减小字体) ---
    QToolBar* tabBar = addToolBar("选项卡");
    tabBar->setMovable(false);
    tabBar->setStyleSheet(
        "QToolBar { background-color: #FFFFFF; border-bottom: 1px solid #E4E4E4; padding: 0px; margin: 0px; min-height: 28px; }"
        "QToolButton { padding: 2px 16px; font-size: 12px; border: none; background: transparent; color: #555; }"
        "QToolButton:checked { border-bottom: 2px solid #2196F3; color: #2196F3; font-weight: bold; }"
        "QToolButton:hover:!checked { background-color: #F5F5F5; color: #333; }"
        );

    QActionGroup* tabGroup = new QActionGroup(this);
    tabGroup->setExclusive(true);
    QAction* tabFile = new QAction("文件", this); tabFile->setCheckable(true); tabGroup->addAction(tabFile);
    QAction* tabOperation = new QAction("操作", this); tabOperation->setCheckable(true); tabGroup->addAction(tabOperation);
    QAction* tabVision = new QAction("视觉", this); tabVision->setCheckable(true); tabGroup->addAction(tabVision);
    QAction* tabConnect = new QAction("连接", this); tabConnect->setCheckable(true); tabGroup->addAction(tabConnect);

    tabBar->addAction(tabFile);
    tabBar->addAction(tabOperation);
    tabBar->addAction(tabVision);
    tabBar->addAction(tabConnect);

    // 将管板参数推至选项卡最右侧，设为左右布局，并默认隐藏
    QWidget *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tabBar->addWidget(spacer);

    QWidget* dxfInfoContainer = new QWidget(this);
    QHBoxLayout* dxfInfoLayout = new QHBoxLayout(dxfInfoContainer); // 改为左右横向排列
    dxfInfoLayout->setContentsMargins(0, 0, 15, 0);
    dxfInfoLayout->setSpacing(15);

    mainPlateRadiusLabel = new QLabel("管板半径：--", this);
    weldHoleCountLabel = new QLabel("焊接管孔数：0", this);

    QFont infoFont = font();
    infoFont.setPointSize(9); // 字体调小
    mainPlateRadiusLabel->setFont(infoFont);
    weldHoleCountLabel->setFont(infoFont);
    mainPlateRadiusLabel->setStyleSheet("color: #666;");
    weldHoleCountLabel->setStyleSheet("color: #666;");

    dxfInfoLayout->addWidget(mainPlateRadiusLabel);
    dxfInfoLayout->addWidget(weldHoleCountLabel);

    // 默认隐藏，待导入DXF时再开启显示
    mainPlateRadiusLabel->setVisible(false);
    weldHoleCountLabel->setVisible(false);
    tabBar->addWidget(dxfInfoContainer);

    // --- 内容层工具栏 ---
    addToolBarBreak();
    toolBar = addToolBar("内容工具栏");
    toolBar->setMovable(false);
    toolBar->setIconSize(QSize(28, 28)); // 图标适度缩小
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->setStyleSheet(
        "QToolBar { background-color: #FAFAFB; border-bottom: 1px solid #E4E4E4; padding: 2px; }"
        "QToolButton { padding: 4px; font-size: 11px; border: 1px solid transparent; border-radius: 4px; }"
        "QToolButton:hover { background-color: #E3F2FD; border: 1px solid #90CAF9; }"
        "QToolButton:checked { background-color: #E3F2FD; border: 1px solid #2196F3; color: #1976D2; }"
        );

    // ==========================================
    // 5. 绑定动态切换与相机构造逻辑
    // ==========================================
    m_camera = new VizumCamera(this);
    connect(m_camera, &VizumCamera::deviceOpened, this, [this](QString ip){
        if (m_statusLabel) m_statusLabel->setText("相机已连接：" + ip);
    });
    connect(m_camera, &VizumCamera::errorOccurred, this, [this](QString msg){
        QMessageBox::warning(this, "相机错误", msg);
    });

    auto switchTab = [=](QAction* currentTab) {
        toolBar->clear();
        if (currentTab == tabFile) {
            toolBar->addAction(loadAction);
            toolBar->addAction(m_setSaveDirAction);
        } else if (currentTab == tabOperation) {
            toolBar->addAction(rotateAction);
            toolBar->addAction(m_setupCoordAction);
            toolBar->addAction(m_posMethodAction);
            toolBar->addAction(m_pathPlanningAction);
            toolBar->addAction(m_manageProcessAction);
        } else if (currentTab == tabVision) {
            toolBar->addAction(m_viewPointCloudAction);
            m_camera->addActionsToToolBar(toolBar); // 动态追加相机按钮
        } else if (currentTab == tabConnect) {
            toolBar->addAction(m_connectAction);
        }
    };
    connect(tabGroup, &QActionGroup::triggered, this, switchTab);

    // 页面跳转逻辑
    connect(toolBar, &QToolBar::actionTriggered, this, [=](QAction* action) {
        if (action == m_manageProcessAction) {
            m_mainStackedWidget->setCurrentIndex(1);
            m_pathPlanningAction->setChecked(false);
            if (m_statusLabel) m_statusLabel->setText("当前模式：工艺管理");
        } else if (action == m_pathPlanningAction) {
            m_mainStackedWidget->setCurrentIndex(2);
            m_manageProcessAction->setChecked(false);
            if (m_statusLabel) m_statusLabel->setText("当前模式：路径规划");
        } else {
            m_mainStackedWidget->setCurrentIndex(0);
            m_manageProcessAction->setChecked(false);
            m_pathPlanningAction->setChecked(false);
        }
    });

    tabFile->setChecked(true);
    switchTab(tabFile);

    // ==========================================
    // 6.底部状态栏与右下角指示器
    // ==========================================
    m_statusLabel = new QLabel("就绪", this);
    statusBar()->addWidget(m_statusLabel); // 左下角状态提示

    // 将网络连接等信息放置到右下角 Permanent 区域
    QWidget* statusContainer = new QWidget(this);
    QHBoxLayout* statusLayout = new QHBoxLayout(statusContainer);
    statusLayout->setContentsMargins(0, 0, 10, 0);
    statusLayout->setSpacing(6);
    QFont statusFontObj = font();
    statusFontObj.setPointSize(9); // 状态栏字体调小

    m_statusIconLabel = new QLabel(this);
    m_statusIconLabel->setFixedSize(12, 12);
    m_statusIconLabel->setStyleSheet("background-color: #F44336; border-radius: 6px;");
    m_statusTextLabel = new QLabel("未连接", this);
    m_statusTextLabel->setFont(statusFontObj);
    m_statusTextLabel->setStyleSheet("color: #555;");

    QLabel* separator1 = new QLabel(" | ", this);
    separator1->setStyleSheet("color: #999; font-weight: bold;");

    m_servoIconLabel = new QLabel(this);
    m_servoIconLabel->setFixedSize(12, 12);
    m_servoIconLabel->setStyleSheet("background-color: #9E9E9E; border-radius: 6px;");
    m_servoTextLabel = new QLabel("伺服断开", this);
    m_servoTextLabel->setFont(statusFontObj);
    m_servoTextLabel->setStyleSheet("color: #555;");

    QLabel* separator2 = new QLabel(" | ", this);
    separator2->setStyleSheet("color: #999; font-weight: bold;");

    m_autoTextLabel = new QLabel("手动模式", this);
    QFont autoFont = statusFontObj;
    autoFont.setBold(true);
    m_autoTextLabel->setFont(autoFont);
    m_autoTextLabel->setStyleSheet("color: #FF9800;");

    statusLayout->addWidget(m_statusIconLabel);
    statusLayout->addWidget(m_statusTextLabel);
    statusLayout->addWidget(separator1);
    statusLayout->addWidget(m_servoIconLabel);
    statusLayout->addWidget(m_servoTextLabel);
    statusLayout->addWidget(separator2);
    statusLayout->addWidget(m_autoTextLabel);

    // 核心代码：添加到右侧固定区
    statusBar()->addPermanentWidget(statusContainer);

    // ==========================================
    // 7. 信号槽绑定与最终初始化
    // ==========================================
    m_coordManager = new usercoordinatemanager(this);
    m_coordManager->initialize(renderArea, dataTable, weldHoles, mainPlateHole, m_statusLabel);

    connect(loadAction, &QAction::triggered, this, &MainWindow::importDxf);
    connect(dataTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::handleTableSelectionChanged);
    connect(dataTable, &QTableWidget::cellChanged,
            this, &MainWindow::handleTableCellChanged);
    connect(rotateAction, &QAction::triggered, this, &MainWindow::applyRotationMatrix);
    connect(m_coordManager, &usercoordinatemanager::update3DCoordinates,
            this, &MainWindow::updateTableFromData);

    connect(m_toggleCoordBtn, &QPushButton::clicked, this, [this](bool checked) {
        m_coordManager->toggleCoordinateDisplay(checked);
        m_toggleCoordBtn->setText(checked ? "隐藏用户坐标系" : "显示用户坐标系");
    });

    connect(m_setupCoordAction, &QAction::triggered, this, &MainWindow::setupCoordinateWizard);
    connect(m_pathPlanningAction, &QAction::triggered, this, &MainWindow::onPathPlanningTriggered);
    connect(m_manageProcessAction, &QAction::triggered, this, &MainWindow::onManageWeldingProcess);
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnectTriggered);
    connect(m_posMethodAction, &QAction::triggered, this, &MainWindow::onSelectPositioningMethod);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(m_resetBtn, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_setSaveDirAction, &QAction::triggered, this, &MainWindow::onSetSaveDirTriggered);
    connect(m_viewPointCloudAction, &QAction::triggered, this, &MainWindow::onViewPointCloudTriggered);

    resize(1200, 700);
}

