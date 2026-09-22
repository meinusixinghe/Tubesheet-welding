#ifndef POINTCLOUDPROCESSOR_H
#define POINTCLOUDPROCESSOR_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

class PointCloudProcessor
{
public:
    PointCloudProcessor();
    ~PointCloudProcessor();

    bool extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud);
};

#endif // POINTCLOUDPROCESSOR_H
