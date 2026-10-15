#include "mainwindow.h"
#include "renderarea.h"
#include "usercoordinatemanager.h"
#include "rotationmatrixdialog.h"
#include "pathplanner.h"
#include "pathplanningdialog.h"
#include "runtimepaths.h"
#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QVBoxLayout>

// ----------------------------------------------------
// 功能：DXF导入与 Python脚本调用
// 核心：处理用户选择和 Python 调用的函数
// ----------------------------------------------------
void MainWindow::importDxf()
{
    const QString dxfPath = QFileDialog::getOpenFileName(this, "选择DXF文件", "", "DXF Files (*.dxf)");
    if (dxfPath.isEmpty()) return;

    const QString pythonExePath = RuntimePaths::pythonExecutable();
    if (pythonExePath.isEmpty()) {
        QMessageBox::warning(this, "未找到 Python",
                             "请安装 Python 3 并加入 PATH，或将 PYTHON_EXECUTABLE 环境变量设为解释器的完整路径。");
        return;
    }

    const QString pythonScript = QCoreApplication::applicationDirPath() + "/import_py_test.py";
    if (!QFileInfo(pythonScript).isFile()) {
        QMessageBox::warning(this, "缺少解析脚本", "未找到 DXF 解析脚本：\n" + pythonScript);
        return;
    }

    const QString jsonOutputPath = QFileDialog::getSaveFileName(this, "保存JSON文件", "", "JSON Files (*.json)");
    if (jsonOutputPath.isEmpty()) return;

    QProcess process;
    process.start(pythonExePath, {pythonScript, dxfPath, jsonOutputPath});
    if (!process.waitForStarted(3000)) {
        QMessageBox::warning(this, "Python 启动失败", pythonExePath + "\n" + process.errorString());
        return;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        QMessageBox::warning(this, "导入超时", "DXF 解析超过 10 秒，已终止解析进程。");
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString error = QString::fromUtf8(process.readAllStandardError());
        QMessageBox::critical(this, "DXF 解析失败",
                              "请确认该 Python 已安装 ezdxf。\n解释器：" + pythonExePath + "\n" + error);
        return;
    }

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
        mainPlateRadiusLabel->setVisible(true);
    }
    weldHoleCountLabel->setText(QString("焊接管孔数：%1").arg(weldHoles.size()));
    weldHoleCountLabel->setVisible(true);
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

