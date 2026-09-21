#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/filters/extract_indices.h>
#include <QDebug>

PointCloudProcessor::PointCloudProcessor() {
    m_planeCoefficients.reset(new pcl::ModelCoefficients());
}

PointCloudProcessor::~PointCloudProcessor() {}

bool PointCloudProcessor::extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud)
{
    if (!inputCloud || inputCloud->empty()) {
        qDebug() << "输入点云为空！";
        return false;
    }

    // ==========================================
    // 1. 统计滤波 (Sor) - 剔除空气中的粉尘与噪点
    // ==========================================
    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);
    pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> sor;
    sor.setInputCloud(inputCloud);
    sor.setMeanK(50);            // 考察每个点周围的 50 个邻居
    sor.setStddevMulThresh(1.0); // 过滤掉超过 1 倍标准差的孤立噪点
    sor.filter(*cloud_filtered);

    // ==========================================
    // 2. RANSAC 拟合基准平面 (死死咬住管板母材)
    // ==========================================
    pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
    pcl::SACSegmentation<pcl::PointXYZRGBA> seg;
    seg.setOptimizeCoefficients(true);
    seg.setModelType(pcl::SACMODEL_PLANE); // 拟合数学平面
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setMaxIterations(1000);

    // 🌟 核心参数：容差 1.0mm。距离基准面大于 1mm 的凸起焊缝将被视为 Outliers (特征)
    seg.setDistanceThreshold(1.0);
    seg.setInputCloud(cloud_filtered);
    seg.segment(*inliers, *m_planeCoefficients);

    if (inliers->indices.empty()) {
        qDebug() << "RANSAC 无法拟合出管板基准平面！";
        return false;
    }

    // ==========================================
    // 3. 提取分离数据
    // ==========================================
    pcl::ExtractIndices<pcl::PointXYZRGBA> extract;
    extract.setInputCloud(cloud_filtered);
    extract.setIndices(inliers);

    // 提取平坦的母材表面 (基准面)
    extract.setNegative(false);
    extract.filter(*baseSurfaceCloud);

    // 提取高于或低于基准面的特征 (凸起的焊缝、杂质等)
    extract.setNegative(true);
    extract.filter(*featureCloud);

    return true;
}
