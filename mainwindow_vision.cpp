#include "mainwindow.h"
#include "pointcloudprocessor.h"
#include "pointcloudviewer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QTextStream>
#include <cmath>
#include <pcl/io/pcd_io.h>

// ==========================================
// 菜单栏：设置图片保存路径
// ==========================================
void MainWindow::onSetSaveDirTriggered()
{
    QString currentDir = m_camera->getSaveDirectory();

    QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择图片及点云的保存目录",
        currentDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (!dir.isEmpty()) {
        // 1. 更新相机的底层路径
        m_camera->setSaveDirectory(dir);

        // 将路径持久化保存到 config.ini 文件中
        QString iniPath = QCoreApplication::applicationDirPath() + "/config.ini";
        QSettings settings(iniPath, QSettings::IniFormat);
        settings.setValue("General/ImageSavePath", dir);

        QMessageBox::information(this, "设置成功", "保存路径已更新并保存至配置文件:\n" + dir);
    }
}

// ==========================================
// 菜单栏：查看本地 3D 点云 (独立 3D 窗口渲染)
// ==========================================
void MainWindow::onViewPointCloudTriggered()
{
    QString currentDir = m_camera->getSaveDirectory();
    QString filePath = QFileDialog::getOpenFileName(this, "选择 3D 点云文件进行分析", currentDir, "PCD Files (*.pcd)");
    if(filePath.isEmpty()) return;

    m_statusLabel->setText("正在加载 3D 点云数据...");
    QApplication::processEvents();

    static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGBA>);
    cloud->points.clear();
    if (pcl::io::loadPCDFile<pcl::PointXYZRGBA>(filePath.toStdString(), *cloud) == -1) {
        QMessageBox::critical(this, "读取失败", "无法读取 PCD 文件！");
        m_statusLabel->setText("就绪");
        return;
    }

    m_statusLabel->setText("点云加载完毕，正在运行 3D 智能分析算法...");
    QApplication::processEvents();

    PointCloudProcessor processor;
    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr baseSurface(new pcl::PointCloud<pcl::PointXYZRGBA>);
    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr features(new pcl::PointCloud<pcl::PointXYZRGBA>);
    std::vector<HoleFeature> detectedHoles;

    VisionParams params;
    params.ransacDistanceThresh = m_ransacThreshSpin->value();
    params.clusterMinSize = m_clusterMinSpin->value();
    params.circleDistanceThresh = m_circleThreshSpin->value();

    bool ok = processor.extractTubeSheetSurface(cloud, baseSurface, features, detectedHoles, params);

    QStringList pcdFilesToView;

    if (ok) {
        try {
            // 给母材和特征点云上色并保存临时文件...
            // (你现有的这部分代码完全保持不变)
            for (auto& p : baseSurface->points) { p.r = 30; p.g = 144; p.b = 255; p.a = 255; }
            for (auto& p : features->points)    { p.r = 255; p.g = 50; p.b = 50; p.a = 255; }

            baseSurface->width = baseSurface->points.size(); baseSurface->height = 1; baseSurface->is_dense = false;
            features->width = features->points.size(); features->height = 1; features->is_dense = false;

            QString tempBase = QCoreApplication::applicationDirPath() + "/temp_base.pcd";
            QString tempFeat = QCoreApplication::applicationDirPath() + "/temp_features.pcd";
            pcl::io::savePCDFileASCII(tempBase.toLocal8Bit().constData(), *baseSurface);
            pcl::io::savePCDFileASCII(tempFeat.toLocal8Bit().constData(), *features);
            pcdFilesToView << tempBase << tempFeat;
            m_statusLabel->setText(QString("分析完成！检出管孔: %1 个").arg(detectedHoles.size()));

            // ==========================================
            // 关键动作：将 3D 视觉识别出的物理坐标填入 Qt 表格！
            // ==========================================
            if (!detectedHoles.empty()) {
                QString txtPath = m_camera->getSaveDirectory() + "/DetectedHoles3D_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".txt";
                QFile txtFile(txtPath);
                if (txtFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&txtFile);
                    out << "ID\tX(mm)\t\tY(mm)\t\tZ(mm)\t\tRadius(mm)\n";
                    int id = 1;
                    for (const auto& h : detectedHoles) {
                        out << id++ << "\t"
                            << QString::number(h.x, 'f', 3) << "\t\t"
                            << QString::number(h.y, 'f', 3) << "\t\t"
                            << QString::number(h.z, 'f', 3) << "\t\t"
                            << QString::number(h.radius, 'f', 3) << "\n";
                    }
                    txtFile.close();
                    qDebug() << ">> 3D孔位物理数据已安全导出至 TXT 文件：" << txtPath;
                }

                pcl::PointCloud<pcl::PointXYZRGBA>::Ptr centersCloud(new pcl::PointCloud<pcl::PointXYZRGBA>);

                for (const auto& dh : detectedHoles) {
                    // 1. 在圆心位置画一个“亮绿色”的 3D 十字星 (臂长 5mm)
                    for (float d = -5.0f; d <= 5.0f; d += 0.2f) {
                        pcl::PointXYZRGBA pX, pY, pZ;
                        pX.x = dh.x + d; pX.y = dh.y;     pX.z = dh.z + 0.5f; // Z轴稍微抬高0.5mm，防止被蓝色母材遮挡埋在地里
                        pY.x = dh.x;     pY.y = dh.y + d; pY.z = dh.z + 0.5f;
                        pZ.x = dh.x;     pZ.y = dh.y;     pZ.z = dh.z + d + 0.5f;

                        // 设为纯绿色 (0, 255, 0)
                        pX.r = pY.r = pZ.r = 0; pX.g = pY.g = pZ.g = 255; pX.b = pY.b = pZ.b = 0; pX.a = pY.a = pZ.a = 255;
                        centersCloud->points.push_back(pX); centersCloud->points.push_back(pY); centersCloud->points.push_back(pZ);
                    }

                    // 2. 将数学拟合出的完美圆环画出来 (设为亮黄色)
                    for (float angle = 0; angle < 2 * M_PI; angle += 0.05f) {
                        pcl::PointXYZRGBA pRing;
                        pRing.x = dh.x + dh.radius * std::cos(angle);
                        pRing.y = dh.y + dh.radius * std::sin(angle);
                        pRing.z = dh.z + 0.5f;

                        // 设为亮黄色 (255, 255, 0)
                        pRing.r = 255; pRing.g = 255; pRing.b = 0; pRing.a = 255;
                        centersCloud->points.push_back(pRing);
                    }
                }
                centersCloud->width = centersCloud->points.size();
                centersCloud->height = 1;
                centersCloud->is_dense = false;
                QString tempCenters = QCoreApplication::applicationDirPath() + "/temp_centers.pcd";
                pcl::io::savePCDFileASCII(tempCenters.toLocal8Bit().constData(), *centersCloud);
                pcdFilesToView << tempCenters;

                dataTable->blockSignals(true); // 屏蔽表格信号，防止触发无限循环

                int matchCount = 0;
                // 遍历 3D 算法算出的每一个真实孔心
                for (const auto& dh : detectedHoles) {
                    int bestIdx = -1;
                    double minDist = 99999.0;

                    // 采用“欧氏距离最近邻算法”，把它和 DXF 导入的孔位进行动态对应
                    for (int i = 0; i < weldHoles.size(); ++i) {
                        double dx = weldHoles[i].center.x() - dh.x;
                        double dy = weldHoles[i].center.y() - dh.y;
                        double dist = std::sqrt(dx*dx + dy*dy);
                        if (dist < minDist) {
                            minDist = dist;
                            bestIdx = i;
                        }
                    }

                    // 如果找到了距离相近的模板孔（假设相机装配导致的位置偏差不超过 20mm）
                    if (bestIdx != -1 && minDist < 20.0) {
                        // 1. 将真实的物理 XYZ 坐标覆盖到底层容器中
                        weldHoles[bestIdx].center3D = QVector3D(dh.x, dh.y, dh.z);

                        // 2. 将数据刷到表格界面的第 3 列 (三维坐标)
                        QString center3DStr = QString("(%1, %2, %3)")
                                                  .arg(dh.x, 0, 'f', 2)
                                                  .arg(dh.y, 0, 'f', 2)
                                                  .arg(dh.z, 0, 'f', 2);

                        if (dataTable->item(bestIdx, 3)) {
                            dataTable->item(bestIdx, 3)->setText(center3DStr);
                            // 标浅绿色表示视觉匹配成功！悬停还能看真实测出的物理半径
                            dataTable->item(bestIdx, 3)->setBackground(QBrush(QColor(144, 238, 144)));
                            dataTable->item(bestIdx, 3)->setToolTip(QString("3D 视觉实测半径: %1 mm").arg(dh.radius, 0, 'f', 2));
                            matchCount++;
                        }
                    }
                }
                dataTable->blockSignals(false); // 恢复信号
                QMessageBox::information(this, "识别成功", QString("视觉算法共检出 %1 个孔\n成功匹配并更新了表格中 %2 个孔的 3D 坐标！").arg(detectedHoles.size()).arg(matchCount));
            }

        } catch (const std::exception &e) {
            qDebug() << "[UI 阶段] 保存或上色时发生异常：" << e.what();
            QMessageBox::critical(this, "严重错误", "保存分析结果时发生崩溃！");
            return;
        }
    } else {
        pcdFilesToView << filePath; // 算法失败时，仅显示原图
        m_statusLabel->setText("算法分析失败，仅显示原始点云。");
    }

    launchPclViewer(pcdFilesToView, "3");
}

void MainWindow::launchPclViewer(const QString& pcdPath, const QString& pointSize) {
    launchPclViewer(QStringList() << pcdPath, pointSize);
}

void MainWindow::launchPclViewer(const QStringList& pcdPaths, const QString& pointSize) {
    QStringList args{PointCloudViewer::ModeArgument, pointSize};
    for (const QString& path : pcdPaths) {
        if (!QFile::exists(path)) {
            QMessageBox::warning(this, "文件不存在", "未找到对应的过程文件：\n" + path + "\n请先执行一次 3D 分析！");
            return;
        }
        args << path;
    }

    QProcess viewerProcess;
    viewerProcess.setProgram(QCoreApplication::applicationFilePath());
    viewerProcess.setArguments(args);
    viewerProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());

    QString logPath = QCoreApplication::applicationDirPath() + "/pointcloud_viewer.log";
    viewerProcess.setStandardOutputFile(logPath, QIODevice::Append);
    viewerProcess.setStandardErrorFile(logPath, QIODevice::Append);

    if (!viewerProcess.startDetached()) {
        QMessageBox::warning(this, "点云查看器启动失败", viewerProcess.errorString());
    }
}
