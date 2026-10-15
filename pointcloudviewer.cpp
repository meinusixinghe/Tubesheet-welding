#include "pointcloudviewer.h"

#include <QFile>
#include <QMessageBox>
#include <cmath>
#include <sstream>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <vtkAutoInit.h>

// VTK factories are process-local; register them in the executable hosting the viewer.
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);
VTK_MODULE_INIT(vtkRenderingContextOpenGL2);

namespace {
bool hasValidEmptyHeader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bool zeroPoints = false;
    QByteArray header;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().simplified();
        header += line + '\n';
        if (line.startsWith("POINTS ")) zeroPoints = line == "POINTS 0";
        if (line.startsWith("DATA ")) {
            if (!zeroPoints || (line != "DATA ascii" && line != "DATA binary" && line != "DATA binary_compressed")) return false;
            std::istringstream stream(header.toStdString());
            pcl::PCLPointCloud2 cloud;
            Eigen::Vector4f origin;
            Eigen::Quaternionf orientation;
            int version, dataType;
            unsigned int dataIndex = 0;
            pcl::PCDReader reader;
            // A zero-point error reaches DATA; earlier header errors leave its offset unset.
            return reader.readHeader(stream, cloud, origin, orientation, version, dataType, dataIndex) < 0
                   && dataIndex > 0;
        }
    }
    return false;
}
}

std::shared_ptr<pcl::visualization::PCLVisualizer> PointCloudViewer::createViewer(
    const QStringList& pcdPaths, double pointSize, QString& error)
{
    error.clear();
    if (pcdPaths.isEmpty() || !std::isfinite(pointSize) || pointSize <= 0) {
        error = "请选择点云文件，并使用大于 0 的点大小。";
        return {};
    }

    auto viewer = std::make_shared<pcl::visualization::PCLVisualizer>("MXEditer - Point Cloud");
    viewer->setBackgroundColor(0.15, 0.15, 0.15);
    int layer = 0;
    for (const QString& path : pcdPaths) {
        pcl::PCLPointCloud2::Ptr cloud(new pcl::PCLPointCloud2);
        Eigen::Vector4f origin;
        Eigen::Quaternionf orientation;
        const int readResult = pcl::io::loadPCDFile(path.toLocal8Bit().constData(), *cloud, origin, orientation);
        // PCL 1.12 also returns an error for a valid zero-point feature layer.
        if (cloud->width == 0 && cloud->height == 1 && cloud->data.empty() && !cloud->fields.empty()
            && hasValidEmptyHeader(path)) continue;
        if (readResult < 0) {
            error = "无法读取点云文件：\n" + path;
            return {};
        }
        // Analysis can legitimately produce an empty feature layer.
        if (cloud->width == 0 || cloud->height == 0) continue;

        auto geometry = std::make_shared<pcl::visualization::PointCloudGeometryHandlerXYZ<pcl::PCLPointCloud2>>(cloud);
        if (!geometry->isCapable()) {
            error = "点云缺少 X、Y、Z 坐标：\n" + path;
            return {};
        }
        pcl::visualization::PointCloudColorHandler<pcl::PCLPointCloud2>::Ptr color(
            new pcl::visualization::PointCloudColorHandlerRGBField<pcl::PCLPointCloud2>(cloud));
        if (!color->isCapable()) {
            color.reset(new pcl::visualization::PointCloudColorHandlerCustom<pcl::PCLPointCloud2>(cloud, 255, 255, 255));
        }

        const std::string id = "cloud_" + std::to_string(layer++);
        if (!viewer->addPointCloud(cloud, geometry, color, origin, orientation, id)) {
            error = "无法显示点云文件：\n" + path;
            return {};
        }
        viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, id);
    }
    if (layer == 0) {
        error = "所选文件中没有可显示的点云数据。";
        return {};
    }

    viewer->resetCamera();
    return viewer;
}

int PointCloudViewer::run(const QStringList& arguments)
{
    bool validSize = false;
    const double pointSize = arguments.value(0).toDouble(&validSize);
    QString error;
    try {
        auto viewer = createViewer(arguments.mid(1), validSize ? pointSize : 0, error);
        if (viewer) {
            viewer->spin();
            return 0;
        }
    } catch (const std::exception& e) {
        error = "点云查看失败：\n" + QString::fromLocal8Bit(e.what());
    }
    QMessageBox::warning(nullptr, "点云查看失败", error);
    return 1;
}
