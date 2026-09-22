#ifndef POINTCLOUDPROCESSOR_H
#define POINTCLOUDPROCESSOR_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <vector>

struct HoleFeature {
    float x;
    float y;
    float z;
    float radius;
};

struct VisionParams {
    double ransacDistanceThresh = 1.0;  // 基准面拟合容差
    int clusterMinSize = 150;           // 聚类最小点数
    double circleDistanceThresh = 0.5;  // 圆拟合紧密度
};

class PointCloudProcessor
{
public:
    PointCloudProcessor();
    ~PointCloudProcessor();

    bool extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud,
                                 std::vector<HoleFeature> &detectedHoles,
                                 const VisionParams& params);
};

#endif // POINTCLOUDPROCESSOR_H
