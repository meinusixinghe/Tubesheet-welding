#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <QDebug>
#include <cmath>
#include <vector>

PointCloudProcessor::PointCloudProcessor() {}
PointCloudProcessor::~PointCloudProcessor() {}

bool PointCloudProcessor::extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud)
{
    qDebug() << "===========================================";
    qDebug() << ">> 进入 3D 智能分析底层 (开启静态缓存池模式)...";

    if (!inputCloud || inputCloud->empty()) return false;

    try {
        // ==========================================
        // 🌟 核心破局点：使用 static 声明，将内存驻留，
        // 彻底切断函数结尾时 EXE 强制释放 DLL 内存的跨域崩溃！
        // ==========================================
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_clean(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
        static pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients());

        // 每次进来前先清空旧数据，但不释放底层容量 (Capacity)
        cloud_clean->points.clear();
        cloud_filtered->points.clear();
        inliers->indices.clear();
        coefficients->values.clear();

        // 1. 手工清洗 NaN
        cloud_clean->points.reserve(inputCloud->points.size());
        for (const auto& pt : inputCloud->points) {
            if (std::isfinite(pt.x) && std::isfinite(pt.y) && std::isfinite(pt.z)) {
                cloud_clean->points.push_back(pt);
            }
        }
        cloud_clean->width = cloud_clean->points.size();
        cloud_clean->height = 1;
        cloud_clean->is_dense = true;
        if (cloud_clean->points.size() < 100) return false;

        // 2. 统计滤波 (直接利用 static 缓存接收 DLL 数据)
        pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> sor;
        sor.setInputCloud(cloud_clean);
        sor.setMeanK(50);
        sor.setStddevMulThresh(1.0);
        sor.filter(*cloud_filtered);
        if (cloud_filtered->points.size() < 10) return false;

        // 3. RANSAC 拟合基准面 (利用 static inliers 接收 DLL 分割索引)
        pcl::SACSegmentation<pcl::PointXYZRGBA> seg;
        seg.setOptimizeCoefficients(true);
        seg.setModelType(pcl::SACMODEL_PLANE);
        seg.setMethodType(pcl::SAC_RANSAC);
        seg.setMaxIterations(1000);
        seg.setDistanceThreshold(1.0);
        seg.setInputCloud(cloud_filtered);
        seg.segment(*inliers, *coefficients);

        if (inliers->indices.empty()) {
            qDebug() << "❌ 错误：无法拟合出有效的平面！";
            return false;
        }

        // 4. 手工提取分离特征
        baseSurfaceCloud->points.clear();
        featureCloud->points.clear();
        baseSurfaceCloud->points.reserve(inliers->indices.size());
        featureCloud->points.reserve(cloud_filtered->points.size() - inliers->indices.size());

        std::vector<bool> is_inlier(cloud_filtered->points.size(), false);
        for (int idx : inliers->indices) {
            if (idx >= 0 && idx < cloud_filtered->points.size()) {
                is_inlier[idx] = true;
                baseSurfaceCloud->points.push_back(cloud_filtered->points[idx]);
            }
        }
        for (size_t i = 0; i < cloud_filtered->points.size(); ++i) {
            if (!is_inlier[i]) {
                featureCloud->points.push_back(cloud_filtered->points[i]);
            }
        }

        baseSurfaceCloud->width = baseSurfaceCloud->points.size();
        baseSurfaceCloud->height = 1;
        baseSurfaceCloud->is_dense = true;

        featureCloud->width = featureCloud->points.size();
        featureCloud->height = 1;
        featureCloud->is_dense = true;

        qDebug() << ">> 算法流水线全部安全执行完毕！即将平稳返回 UI 层！";
        qDebug() << "===========================================";
        return true;

    } catch (...) {
        return false;
    }
}
