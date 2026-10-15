#ifndef POINTCLOUDVIEWER_H
#define POINTCLOUDVIEWER_H

#include <QString>
#include <QStringList>
#include <memory>

namespace pcl { namespace visualization { class PCLVisualizer; } }

namespace PointCloudViewer {
inline constexpr char ModeArgument[] = "--pointcloud-viewer";

std::shared_ptr<pcl::visualization::PCLVisualizer> createViewer(
    const QStringList& pcdPaths, double pointSize, QString& error);
int run(const QStringList& arguments);
}

#endif // POINTCLOUDVIEWER_H
