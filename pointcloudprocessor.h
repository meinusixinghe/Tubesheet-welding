#ifndef POINTCLOUDPROCESSOR_H
#define POINTCLOUDPROCESSOR_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/ModelCoefficients.h>

class PointCloudProcessor
{
public:
    PointCloudProcessor();
    ~PointCloudProcessor();

    // 🌟 核心算法：提取管板基准面与焊缝/特征点
    bool extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                 pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud);

private:
    pcl::ModelCoefficients::Ptr m_planeCoefficients; // 存储拟合出的平面方程 (a,b,c,d)
};

#endif // POINTCLOUDPROCESSOR_H
