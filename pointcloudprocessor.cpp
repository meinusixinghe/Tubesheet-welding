#include "pointcloudprocessor.h"
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/search/kdtree.h>
#include <pcl/io/pcd_io.h>
#include <QCoreApplication>
#include <QDebug>
#include <cmath>
#include <pcl/features/normal_3d.h>
#include <pcl/features/boundary.h>

PointCloudProcessor::PointCloudProcessor() {}
PointCloudProcessor::~PointCloudProcessor() {}

bool PointCloudProcessor::extractTubeSheetSurface(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr inputCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &baseSurfaceCloud,
                                                  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr &featureCloud,
                                                  std::vector<HoleFeature> &detectedHoles,
                                                  const VisionParams& params)
{
    qDebug() << "===========================================";
    qDebug() << ">> 启动 3D 边界特征分析引擎 (Planar Boundary Mode)...";
    if (!inputCloud || inputCloud->empty()) return false;
    detectedHoles.clear();

    QString exePath = QCoreApplication::applicationDirPath();

    try {
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_clean(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZRGBA>);
        static pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
        static pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients());

        cloud_clean->points.clear(); cloud_filtered->points.clear();
        inliers->indices.clear(); coefficients->values.clear();
        baseSurfaceCloud->points.clear(); featureCloud->points.clear();

        // 1. 洗数据
        cloud_clean->points.reserve(inputCloud->points.size());
        for (const auto& pt : inputCloud->points) {
            if (std::isfinite(pt.x) && std::isfinite(pt.y) && std::isfinite(pt.z)) cloud_clean->points.push_back(pt);
        }
        cloud_clean->width = cloud_clean->points.size(); cloud_clean->height = 1; cloud_clean->is_dense = true;
        if (cloud_clean->points.size() < 100) return false;
        pcl::io::savePCDFileASCII((exePath + "/temp_clean.pcd").toLocal8Bit().constData(), *cloud_clean);

        // 2. 统计滤波降噪
        pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA> *sor = new pcl::StatisticalOutlierRemoval<pcl::PointXYZRGBA>();
        sor->setInputCloud(cloud_clean);
        sor->setMeanK(50);
        sor->setStddevMulThresh(1.0);
        sor->filter(*cloud_filtered);
        delete sor;
        if (cloud_filtered->points.size() < 10) return false;
        pcl::io::savePCDFileASCII((exePath + "/temp_filtered.pcd").toLocal8Bit().constData(), *cloud_filtered);

        // 3. RANSAC 提取蓝色母材大平原
        qDebug() << ">> 正在提取母材基准面...";
        pcl::SACSegmentation<pcl::PointXYZRGBA> *seg = new pcl::SACSegmentation<pcl::PointXYZRGBA>();
        seg->setOptimizeCoefficients(true);
        seg->setModelType(pcl::SACMODEL_PLANE);
        seg->setMethodType(pcl::SAC_RANSAC);
        seg->setMaxIterations(1000);
        seg->setDistanceThreshold(params.ransacDistanceThresh);
        seg->setInputCloud(cloud_filtered);
        seg->segment(*inliers, *coefficients);
        delete seg;
        if (inliers->indices.empty()) return false;

        // 将平坦母材提取到 baseSurfaceCloud
        baseSurfaceCloud->points.reserve(inliers->indices.size());
        for (int idx : inliers->indices) {
            baseSurfaceCloud->points.push_back(cloud_filtered->points[idx]);
        }
        baseSurfaceCloud->width = baseSurfaceCloud->points.size(); baseSurfaceCloud->height = 1; baseSurfaceCloud->is_dense = true;

        // ==========================================
        // 4. 核心工艺替换：在蓝色母材上计算法向量与孔洞边界！
        // ==========================================
        qDebug() << ">> 正在计算母材表面法向量与物理边界...";
        pcl::search::KdTree<pcl::PointXYZRGBA>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGBA>());

        // 4.1 计算法向量
        pcl::NormalEstimation<pcl::PointXYZRGBA, pcl::Normal> *ne = new pcl::NormalEstimation<pcl::PointXYZRGBA, pcl::Normal>();
        pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
        ne->setInputCloud(baseSurfaceCloud);
        ne->setSearchMethod(tree);
        ne->setRadiusSearch(3.0); // 检索 3mm 内的邻居算表面法向
        ne->compute(*normals);
        delete ne;

        // 4.2 提取边界线 (缺口)
        pcl::BoundaryEstimation<pcl::PointXYZRGBA, pcl::Normal, pcl::Boundary> *est = new pcl::BoundaryEstimation<pcl::PointXYZRGBA, pcl::Normal, pcl::Boundary>();
        pcl::PointCloud<pcl::Boundary>::Ptr boundaries(new pcl::PointCloud<pcl::Boundary>);
        est->setInputCloud(baseSurfaceCloud);
        est->setInputNormals(normals);
        est->setSearchMethod(tree);
        est->setRadiusSearch(4.0);         // 检索 4mm 判断是否为边缘点
        est->setAngleThreshold(M_PI * 0.5); // 如果周围 90 度没点，就是边缘！
        est->compute(*boundaries);
        delete est;

        // 4.3 将纯粹的边缘轮廓点，塞入红色的 featureCloud 中！
        for (size_t i = 0; i < baseSurfaceCloud->points.size(); ++i) {
            if (boundaries->points[i].boundary_point > 0) {
                featureCloud->points.push_back(baseSurfaceCloud->points[i]);
            }
        }
        featureCloud->width = featureCloud->points.size(); featureCloud->height = 1; featureCloud->is_dense = true;
        qDebug() << "   提取成功！共发现完美的边界点：" << featureCloud->points.size();

        // ==========================================
        // 5. 欧几里得聚类 (切分边界线)
        // ==========================================
        if (featureCloud->points.size() > 20) {
            pcl::search::KdTree<pcl::PointXYZRGBA>::Ptr tree_cluster(new pcl::search::KdTree<pcl::PointXYZRGBA>);
            tree_cluster->setInputCloud(featureCloud);
            std::vector<pcl::PointIndices> cluster_indices;
            pcl::EuclideanClusterExtraction<pcl::PointXYZRGBA> *ec = new pcl::EuclideanClusterExtraction<pcl::PointXYZRGBA>();
            ec->setClusterTolerance(3.0);
            ec->setMinClusterSize(30);
            ec->setMaxClusterSize(5000);
            ec->setSearchMethod(tree_cluster);
            ec->setInputCloud(featureCloud);
            ec->extract(cluster_indices);
            delete ec;

            qDebug() << "   独立孔洞轮廓切分完成，共计：" << cluster_indices.size() << " 个簇。";

            // ==========================================
            // 6. 3D 空间圆精准拟合
            // ==========================================
            for (const auto& indices : cluster_indices) {
                pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_cluster(new pcl::PointCloud<pcl::PointXYZRGBA>);
                for (const auto& idx : indices.indices) cloud_cluster->points.push_back(featureCloud->points[idx]);

                pcl::SACSegmentation<pcl::PointXYZRGBA> *circle_seg = new pcl::SACSegmentation<pcl::PointXYZRGBA>();
                pcl::PointIndices::Ptr circle_inliers(new pcl::PointIndices);
                pcl::ModelCoefficients::Ptr circle_coeff(new pcl::ModelCoefficients);

                circle_seg->setOptimizeCoefficients(true);
                circle_seg->setModelType(pcl::SACMODEL_CIRCLE3D);
                circle_seg->setMethodType(pcl::SAC_RANSAC);
                circle_seg->setMaxIterations(2000);
                circle_seg->setDistanceThreshold(params.circleDistanceThresh);
                // 限制物理半径：你的大孔 25(r=12.5)，小孔 15(r=7.5)。放宽一点允许范围 6.0 ~ 14.0
                circle_seg->setRadiusLimits(6.0, 14.0);
                circle_seg->setInputCloud(cloud_cluster);
                circle_seg->segment(*circle_inliers, *circle_coeff);
                delete circle_seg;

                if (!circle_inliers->indices.empty() && circle_coeff->values.size() >= 4) {
                    HoleFeature h;
                    h.x = circle_coeff->values[0];
                    h.y = circle_coeff->values[1];
                    h.z = circle_coeff->values[2];
                    h.radius = circle_coeff->values[3];

                    // 核心算法升级：象限覆盖率检验 (过滤外边缘倒角伪影)
                    int quadrants[4] = {0, 0, 0, 0};
                    for (const auto& idx : circle_inliers->indices) {
                        const auto& pt = cloud_cluster->points[idx];
                        float dx = pt.x - h.x;
                        float dy = pt.y - h.y;
                        if (dx >= 0 && dy >= 0) quadrants[0]++;
                        else if (dx < 0 && dy >= 0) quadrants[1]++;
                        else if (dx < 0 && dy < 0) quadrants[2]++;
                        else if (dx >= 0 && dy < 0) quadrants[3]++;
                    }
                    // 统计有多少个象限包含超过 5 个点
                    int filledQuadrants = (quadrants[0]>5) + (quadrants[1]>5) + (quadrants[2]>5) + (quadrants[3]>5);

                    // 终极裁决：必须是闭合的圆（占满至少 3个象限），且半径合法
                    if (filledQuadrants >= 3 && h.radius >= 6.0 && h.radius <= 14.0) {
                        detectedHoles.push_back(h);
                    } else {
                        qDebug() << "   -> 剔除无效伪圆特征：算得半径" << h.radius << "mm, 闭合象限数仅为" << filledQuadrants;
                    }
                }
            }
        }
        qDebug() << ">> 边界拟合算法流水线安全执行完毕！";
        qDebug() << "===========================================";
        return true;

    } catch (const std::exception &e) {
        qDebug() << "算法异常：" << e.what();
        return false;
    } catch (...) { return false; }
}
