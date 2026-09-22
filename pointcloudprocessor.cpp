#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/search/kdtree.h>
#include <QDebug>
#include <cmath>

PointCloudProcessor::PointCloudProcessor() {}
PointCloudProcessor::~PointCloudProcessor() {}

bool PointCloudProcessor::extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud,
                                                  std::vector<HoleFeature> &detectedHoles)
{
    qDebug() << "===========================================";
    qDebug() << ">> 进入 3D 智能分析底层...";

    if (!inputCloud || inputCloud->empty()) return false;
    detectedHoles.clear();

    try {
        // [静态缓存池：避免内存释放崩溃]
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_clean(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
        static pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients());

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
        cloud_clean->width = cloud_clean->points.size(); cloud_clean->height = 1; cloud_clean->is_dense = true;
        if (cloud_clean->points.size() < 100) return false;

        // 2. 统计滤波 (Sor)
        pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> sor;
        sor.setInputCloud(cloud_clean);
        sor.setMeanK(50);
        sor.setStddevMulThresh(1.0);
        sor.filter(*cloud_filtered);
        if (cloud_filtered->points.size() < 10) return false;

        // 3. RANSAC 拟合基准面
        pcl::SACSegmentation<pcl::PointXYZRGBA> seg;
        seg.setOptimizeCoefficients(true);
        seg.setModelType(pcl::SACMODEL_PLANE);
        seg.setMethodType(pcl::SAC_RANSAC);
        seg.setMaxIterations(1000);
        seg.setDistanceThreshold(1.0);
        seg.setInputCloud(cloud_filtered);
        seg.segment(*inliers, *coefficients);
        if (inliers->indices.empty()) return false;

        // 4. 手工提取分离特征
        baseSurfaceCloud->points.clear(); featureCloud->points.clear();
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
        baseSurfaceCloud->width = baseSurfaceCloud->points.size(); baseSurfaceCloud->height = 1; baseSurfaceCloud->is_dense = true;
        featureCloud->width = featureCloud->points.size(); featureCloud->height = 1; featureCloud->is_dense = true;

        // ==========================================
        // 【第 3 步】欧几里得聚类 (切分独立的管孔)
        // ==========================================
        if (featureCloud->points.size() > 50) {
            qDebug() << "5. 正在对特征点云进行空间聚类切分...";
            pcl::search::KdTree<pcl::PointXYZRGBA>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGBA>);
            tree->setInputCloud(featureCloud);

            std::vector<pcl::PointIndices> cluster_indices;
            pcl::EuclideanClusterExtraction<pcl::PointXYZRGBA> ec;
            ec.setClusterTolerance(3.0);  // 容差：3mm以内的点被认为是同一个孔
            ec.setMinClusterSize(150);     // 最小点数：排除离散飞溅物噪点
            ec.setMaxClusterSize(10000);  // 最大点数：排除大块杂质
            ec.setSearchMethod(tree);
            ec.setInputCloud(featureCloud);
            ec.extract(cluster_indices);

            qDebug() << "   共切分出独立特征簇：" << cluster_indices.size() << " 个。";

            // ==========================================
            // 🌟 【第 4 步】3D 空间圆拟合 (求解绝对物理坐标)
            // ==========================================
            qDebug() << "6. 正在执行 3D 空间圆数学模型拟合...";
            for (const auto& indices : cluster_indices) {
                // 将单个聚类的点提取出来
                pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_cluster(new pcl::PointCloud<pcl::PointXYZRGBA>);
                for (const auto& idx : indices.indices) {
                    cloud_cluster->points.push_back(featureCloud->points[idx]);
                }

                pcl::SACSegmentation<pcl::PointXYZRGBA> circle_seg;
                pcl::PointIndices::Ptr circle_inliers(new pcl::PointIndices);
                pcl::ModelCoefficients::Ptr circle_coeff(new pcl::ModelCoefficients);

                circle_seg.setOptimizeCoefficients(true);
                circle_seg.setModelType(pcl::SACMODEL_CIRCLE3D); // 三维空间圆拟合
                circle_seg.setMethodType(pcl::SAC_RANSAC);
                circle_seg.setMaxIterations(1000);
                circle_seg.setDistanceThreshold(0.5); // 圆拟合的紧密度 0.5mm
                circle_seg.setInputCloud(cloud_cluster);
                circle_seg.segment(*circle_inliers, *circle_coeff);

                // SACMODEL_CIRCLE3D 输出 7 个参数：x, y, z (中心), r (半径), nx, ny, nz (法向量)
                if (!circle_inliers->indices.empty() &&
                    circle_inliers->indices.size() > 100 &&
                    circle_coeff->values.size() >= 4) {

                    HoleFeature h;
                    h.x = circle_coeff->values[0];
                    h.y = circle_coeff->values[1];
                    h.z = circle_coeff->values[2];
                    h.radius = circle_coeff->values[3];

                    if (h.radius >= 5.0 && h.radius <= 30.0) {
                        detectedHoles.push_back(h);
                    }
                }
            }
            qDebug() << "   成功解算出有效管孔中心坐标：" << detectedHoles.size() << " 个！";
        }

        qDebug() << ">> 算法流水线全部安全执行完毕！";
        qDebug() << "===========================================";
        return true;

    } catch (...) {
        return false;
    }
}
