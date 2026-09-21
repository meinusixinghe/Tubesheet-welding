#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
// 🌟 删除了对 extract_indices 的依赖
#include <pcl/filters/filter.h>
#include <QDebug>
#include <vector>

PointCloudProcessor::PointCloudProcessor() {
    m_planeCoefficients.reset(new pcl::ModelCoefficients());
}

PointCloudProcessor::~PointCloudProcessor() {}

bool PointCloudProcessor::extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud)
{
    qDebug() << "===========================================";
    qDebug() << ">> 进入 3D 智能分析算法底层...";

    if (!inputCloud || inputCloud->empty()) {
        qDebug() << "❌ 错误：输入点云为空！";
        return false;
    }

    try {
        // ==========================================
        // 1. 洗数据：清除 NaN 点
        // ==========================================
        qDebug() << "1. 正在清洗点云 (去除 NaN/无效物理坐标)...";
        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_clean(new pcl::PointCloud<pcl::PointXYZRGBA>);
        std::vector<int> mapping;
        pcl::removeNaNFromPointCloud(*inputCloud, *cloud_clean, mapping);

        if (cloud_clean->points.size() < 100) return false;

        // ==========================================
        // 2. 统计滤波 (Sor)
        // ==========================================
        qDebug() << "2. 正在进行统计滤波 (Sor) 降噪...";
        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);
        pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> sor;
        sor.setInputCloud(cloud_clean);
        sor.setMeanK(50);
        sor.setStddevMulThresh(1.0);
        sor.filter(*cloud_filtered);

        if (cloud_filtered->points.size() < 10) return false;

        // ==========================================
        // 3. RANSAC 拟合管板基准平面
        // ==========================================
        qDebug() << "3. 正在启动 RANSAC 拟合空间基准平面...";
        pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
        pcl::SACSegmentation<pcl::PointXYZRGBA> seg;
        seg.setOptimizeCoefficients(true);
        seg.setModelType(pcl::SACMODEL_PLANE);
        seg.setMethodType(pcl::SAC_RANSAC);
        seg.setMaxIterations(1000);
        seg.setDistanceThreshold(1.0); // 容差 1.0mm
        seg.setInputCloud(cloud_filtered);
        seg.segment(*inliers, *m_planeCoefficients);

        if (inliers->indices.empty()) {
            qDebug() << "❌ 错误：无法拟合出有效的平面！";
            return false;
        }

        // ==========================================
        // 🌟 4. 手动剥离特征 (完美绕过 ABI 内存释放崩溃)
        // ==========================================
        qDebug() << "4. 正在安全剥离母材与特征点云 (Manual Extraction)...";

        // 提前分配内存，防止动态扩容
        baseSurfaceCloud->points.reserve(inliers->indices.size());
        featureCloud->points.reserve(cloud_filtered->points.size() - inliers->indices.size());

        // 使用布尔数组标记哪些点属于平坦母材
        std::vector<bool> is_inlier(cloud_filtered->points.size(), false);

        for (int idx : inliers->indices) {
            if (idx >= 0 && idx < cloud_filtered->points.size()) {
                is_inlier[idx] = true;
                // 将母材点装入 baseSurfaceCloud
                baseSurfaceCloud->points.push_back(cloud_filtered->points[idx]);
            }
        }

        // 将剩下的点（高于或低于平面的焊缝特征）装入 featureCloud
        for (size_t i = 0; i < cloud_filtered->points.size(); ++i) {
            if (!is_inlier[i]) {
                featureCloud->points.push_back(cloud_filtered->points[i]);
            }
        }

        // 重新同步尺寸属性
        baseSurfaceCloud->width = baseSurfaceCloud->points.size();
        baseSurfaceCloud->height = 1;
        baseSurfaceCloud->is_dense = true;

        featureCloud->width = featureCloud->points.size();
        featureCloud->height = 1;
        featureCloud->is_dense = true;

        qDebug() << ">> 算法流水线全部安全执行完毕！";
        qDebug() << "===========================================";
        return true;

    } catch (...) {
        qDebug() << "❌ 发生未知内存崩溃！";
        return false;
    }
}
