#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <QDebug>
#include <cmath> // 🌟 新增：用于判断 NaN 无效数字
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
        // 🌟 1. 纯手工清洗 NaN 点，彻底绕过 PCL 底层 std::vector 跨域释放漏洞！
        // ==========================================
        qDebug() << "1. 正在清洗点云 (手工模式，拒绝系统崩溃)...";
        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_clean(new pcl::PointCloud<pcl::PointXYZRGBA>);
        cloud_clean->points.reserve(inputCloud->points.size());

        for (const auto& pt : inputCloud->points) {
            // 只要不是无效数值(NaN)，就安全收录到我们自己的容器里
            if (std::isfinite(pt.x) && std::isfinite(pt.y) && std::isfinite(pt.z)) {
                cloud_clean->points.push_back(pt);
            }
        }
        cloud_clean->width = cloud_clean->points.size();
        cloud_clean->height = 1;
        cloud_clean->is_dense = true;

        qDebug() << "   清洗完成！有效健康点数：" << cloud_clean->points.size();
        if (cloud_clean->points.size() < 100) return false;

        // ==========================================
        // 🌟 2. 统计滤波 (使用 new 分配在堆区，绕过栈析构炸弹)
        // ==========================================
        qDebug() << "2. 正在进行统计滤波 (Sor) 降噪...";
        pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);

        pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> *sor = new pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA>();
        sor->setInputCloud(cloud_clean);
        sor->setMeanK(50);
        sor->setStddevMulThresh(1.0);
        sor->filter(*cloud_filtered);
        delete sor; // 用完立刻安全释放，绝不拖到函数结尾

        if (cloud_filtered->points.size() < 10) return false;

        // ==========================================
        // 🌟 3. RANSAC 拟合空间基准平面 (同样使用 new 分配)
        // ==========================================
        qDebug() << "3. 正在启动 RANSAC 拟合空间基准平面...";
        pcl::PointIndices::Ptr inliers(new pcl::PointIndices());

        pcl::SACSegmentation<pcl::PointXYZRGBA> *seg = new pcl::SACSegmentation<pcl::PointXYZRGBA>();
        seg->setOptimizeCoefficients(true);
        seg->setModelType(pcl::SACMODEL_PLANE);
        seg->setMethodType(pcl::SAC_RANSAC);
        seg->setMaxIterations(1000);
        seg->setDistanceThreshold(1.0); // 容差 1.0mm
        seg->setInputCloud(cloud_filtered);
        seg->segment(*inliers, *m_planeCoefficients);
        delete seg; // 用完立刻安全释放

        if (inliers->indices.empty()) {
            qDebug() << "❌ 错误：无法拟合出有效的平面！";
            return false;
        }

        // ==========================================
        // 🌟 4. 安全剥离母材与特征点云
        // ==========================================
        qDebug() << "4. 正在安全剥离母材与特征点云 (Manual Extraction)...";
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

        qDebug() << ">> 算法流水线全部安全执行完毕！";
        qDebug() << "===========================================";
        return true;

    } catch (const std::exception &e) {
        qDebug() << "❌ C++ 异常：" << e.what();
        return false;
    } catch (...) {
        qDebug() << "❌ 发生未知异常！";
        return false;
    }
}
