#include "vizumcamera.h"
#include "VZNL_Graphics.h"
#include "VZNL_DetectLaser.h"
#include "VZNL_DetectConfig.h"
#include "VZNL_EyeConfig.h"
#include "VZNL_RGBConfig.h"
#include "VZNL_SwingMotor.h"
#include "VZNL_Utils.h"
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>

VizumCamera::VizumCamera(QObject *parent) : QObject(parent)
{
    VizumCamera::initActions();
    m_saveDirectory = QCoreApplication::applicationDirPath() + "/CaptureImages";
}

VizumCamera::~VizumCamera()
{
    onCloseDeviceTriggered();
}

void VizumCamera::setSaveDirectory(const QString& dir)
{
    if (!dir.isEmpty()) {
        m_saveDirectory = dir;
    }
}

QString VizumCamera::getSaveDirectory() const
{
    return m_saveDirectory;
}

void VizumCamera::initActions()
{
    m_openDeviceAction=new QAction("打开设备",this);
    m_captureAction=new QAction("开启采图",this);
    m_closeDeviceAction=new QAction("关闭设备",this);

    m_captureAction->setEnabled(false);
    m_closeDeviceAction->setEnabled(false);

    connect(m_openDeviceAction,&QAction::triggered,this,&VizumCamera::onOpenDeviceTriggered);
    connect(m_captureAction,&QAction::triggered,this,&VizumCamera::onCaptureTriggered);
    connect(m_closeDeviceAction,&QAction::triggered,this,&VizumCamera::onCloseDeviceTriggered);
}

void VizumCamera::addActionsToToolBar(QToolBar *toolbar)
{
    toolbar->addAction(m_openDeviceAction);
    toolbar->addAction(m_captureAction);
    toolbar->addAction(m_closeDeviceAction);
}

void VizumCamera::onCaptureTriggered()
{
    if (m_mainCameraHandle == nullptr) {
        emit errorOccurred("警告：请先打开设备！");
        return;
    }

    if (!m_isCapturing) {
        // ==========================================
        // 阶段 1：获取相机的底层原始二维灰度图
        // (必须在启动激光 3D 扫描前进行)
        // ==========================================

        // 保存相机的旧曝光和 ROI 状态以便后续恢复
        unsigned int nOldExpose = 0;
        EVzNLExposeMode eExposeMode = keVzNLExposeMode_Fix;
        VzNL_GetConfigEyeExpose(m_mainCameraHandle, &eExposeMode, &nOldExpose);

        SVzNLROIRect sOldLeftROI, sOldRightROI;
        VzNL_GetConfigDetectROI(m_mainCameraHandle, &sOldLeftROI, &sOldRightROI);

        // 🌟 2. 核心修复：获取相机真实的最大物理分辨率[cite: 15]
        SVzNLEyeDeviceInfoEx sDeviceInfoEx;
        sDeviceInfoEx.sEyeCBInfo.nSize = sizeof(SVzNLEyeDeviceInfoEx);
        VzNL_GetDeviceInfo(m_mainCameraHandle, &sDeviceInfoEx.sEyeCBInfo);

        int maxWidth = sDeviceInfoEx.sVideoRes.nFrameWidth;
        int maxHeight = sDeviceInfoEx.sVideoRes.nFrameHeight;

        // 🌟 3. 构造全视野的 ROI 区域，防止画面被裁剪
        SVzNLROIRect fullLeftROI, fullRightROI;
        fullLeftROI.left = 0; fullLeftROI.top = 0;
        fullLeftROI.right = maxWidth; fullLeftROI.bottom = maxHeight;

        fullRightROI.left = 0; fullRightROI.top = 0;
        fullRightROI.right = maxWidth; fullRightROI.bottom = maxHeight;

        // 切换至定焦标定模式
        VzNL_EnableCalibROI(m_mainCameraHandle, VzTrue);

        // 🌟 4. 强制应用全视野 ROI，并提高曝光拍出明亮的全景图[cite: 15]
        VzNL_ConfigDetectROI(m_mainCameraHandle, &fullLeftROI, &fullRightROI);
        VzNL_ConfigEyeExpose(m_mainCameraHandle, keVzNLExposeMode_Fix, 50000);

        // 抓取全景底图 (超时时间 5000ms)
        SVzNLImageData* pLeftImage = nullptr;
        SVzNLImageData* pRightImage = nullptr;
        int nImgErr = VzNL_GetEyeImage(m_mainCameraHandle, &pLeftImage, &pRightImage, 5000);

        QString saveDir = m_saveDirectory;
        QDir().mkpath(saveDir);
        QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

        if (nImgErr == 0 && pLeftImage != nullptr) {
            QString grayFileName = saveDir + QString("/GrayLeft_%1.png").arg(timeStr);
            if (VzNL_SaveImage(grayFileName.toUtf8().data(), pLeftImage) == 0) {
                qDebug() << "1️⃣ 原始全景灰度图像已成功保存至：" << grayFileName;
                emit imageSaved(grayFileName);
            }
            VzNL_ReleaseImage(&pLeftImage);
            VzNL_ReleaseImage(&pRightImage);
        } else {
            qDebug() << "获取底层灰度图像失败，错误码:" << nImgErr;
        }

        // 拍完恢复旧的 ROI 和旧曝光，以免影响接下来的高频激光扫描[cite: 15]
        VzNL_EnableCalibROI(m_mainCameraHandle, VzFalse);
        VzNL_ConfigEyeExpose(m_mainCameraHandle, keVzNLExposeMode_Fix, nOldExpose);
        VzNL_ConfigDetectROI(m_mainCameraHandle, &sOldLeftROI, &sOldRightROI);

        // ==========================================
        // 阶段 2：正式开启 3D 激光扫描流
        // ==========================================
        VzNL_EnableRGB(m_mainCameraHandle, VzTrue);

        int nErr = VzNL_BeginDetectLaser(m_mainCameraHandle);
        if (nErr != 0) {
            emit errorOccurred(QString("创建检测激光线工具失败，错误码：%1").arg(nErr));
            return;
        }

        // 获取分辨率并为 3D 点云开辟内存空间
        VzNL_GetRGBResolution(m_mainCameraHandle, &m_sRGBVideoRes);
        int nFrameSize = m_sRGBVideoRes.nFrameWidth * m_sRGBVideoRes.nFrameHeight;

        if (m_p2DToPointMap) { delete[] m_p2DToPointMap; m_p2DToPointMap = nullptr; }
        if (m_pb2DInvalidPt) { delete[] m_pb2DInvalidPt; m_pb2DInvalidPt = nullptr; }

        m_p2DToPointMap = new SVzNLPointXYZRGBA[nFrameSize];
        m_pb2DInvalidPt = new bool[nFrameSize];
        memset(m_p2DToPointMap, 0, sizeof(SVzNLPointXYZRGBA) * nFrameSize);
        memset(m_pb2DInvalidPt, 0, sizeof(bool) * nFrameSize);

        VzNL_SetTriggerMode(m_mainCameraHandle, keEyeTriggerMode_Master);
        unsigned int nMin, nMax;
        VzNL_QueryParamRange(m_mainCameraHandle, keDeviceParamType_FrameRate, &nMin, &nMax);
        VzNL_SetFrameRate(m_mainCameraHandle, nMax);

        VzNL_EnableSwingMotor(m_mainCameraHandle, VzTrue);

        nErr = VzNL_StartAutoDetectEx(m_mainCameraHandle, keResultDataType_PointXYZRGBA, keFlipType_None, _AutoOutputLaserLineExCB, this);

        if (nErr == 0) {
            m_isCapturing = true;
            m_captureAction->setText("停止扫描并保存图像");
            qDebug() << "▶ 激光扫描已启动，请等待扫描完成后点击停止...";
        } else {
            emit errorOccurred(QString("开流失败，错误码：%1").arg(nErr));
            VzNL_EndDetectLaser(m_mainCameraHandle);
        }

    } else {
        VzNL_StopAutoDetect(m_mainCameraHandle);
        m_isCapturing = false;
        m_captureAction->setText("开启采图");

        QString saveDir = m_saveDirectory;
        QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

        if (m_p2DToPointMap != nullptr) {
            // 2️提取并保存真实的 3D 深度图 (.tif格式)
            QString depthFileName = saveDir + QString("/DepthData_%1.tif").arg(timeStr);
            if (VzNL_SaveDepthMapTiffImage(depthFileName.toUtf8().data(), m_sRGBVideoRes.nFrameWidth, m_sRGBVideoRes.nFrameHeight, keResultDataType_PointXYZRGBA, m_p2DToPointMap) == 0) {
                qDebug() << "2️⃣ 3D 原始深度图已成功保存至：" << depthFileName;
                emit imageSaved(depthFileName);
            }

            // 3️渲染 2D 可视化深度点云图 (.png格式)
            SVzNLImageData depthImageData;
            depthImageData.nWidth = m_sRGBVideoRes.nFrameWidth;
            depthImageData.nHeight = m_sRGBVideoRes.nFrameHeight;
            depthImageData.nChannels = 3;
            depthImageData.nBufferSize = depthImageData.nWidth * depthImageData.nHeight * 3;
            depthImageData.byBitDepth = 8;
            depthImageData.eImageType = keVzNLImageType_BGR888;
            depthImageData.ptOriPos.x = 0;
            depthImageData.ptOriPos.y = 0;

            depthImageData.pBuffer = new unsigned char[depthImageData.nBufferSize];
            memset(depthImageData.pBuffer, 0, depthImageData.nBufferSize);

            int nWritePos = 0;
            unsigned char* pRGB = depthImageData.pBuffer;
            for (int nHIdx = 0; nHIdx < depthImageData.nHeight; nHIdx++) {
                for (int nWIdx = 0; nWIdx < depthImageData.nWidth; nWIdx++) {
                    if (m_pb2DInvalidPt[nWritePos]) {
                        unsigned char* pCurRGB = (unsigned char*)&m_p2DToPointMap[nWritePos].nRGB;
                        pRGB[0] = pCurRGB[2];
                        pRGB[1] = pCurRGB[1];
                        pRGB[2] = pCurRGB[0];
                    }
                    nWritePos++;
                    pRGB += 3;
                }
            }

            QString depthPngName = saveDir + QString("/DepthMap_%1.png").arg(timeStr);
            if (VzNL_SaveImage(depthPngName.toUtf8().data(), &depthImageData) == 0) {
                qDebug() << "3️⃣ 2D 可视化深度图已保存至：" << depthPngName;
                emit imageSaved(depthPngName);
            }
            delete[] depthImageData.pBuffer;

            // ==========================================
            // 🌟 新增的 5️⃣：将数据转换为 PCL 点云并保存为 .pcd
            // ==========================================
            pcl::PointCloud<pcl::PointXYZRGBA>::Ptr pclCloud(new pcl::PointCloud<pcl::PointXYZRGBA>());
            int nPclReadPos = 0;

            // 遍历整个画面，只提取有效的高精度 3D 点
            for (int nHIdx = 0; nHIdx < m_sRGBVideoRes.nFrameHeight; nHIdx++) {
                for (int nWIdx = 0; nWIdx < m_sRGBVideoRes.nFrameWidth; nWIdx++) {
                    if (m_pb2DInvalidPt[nPclReadPos]) {
                        pcl::PointXYZRGBA pclPoint;
                        // 取出物理空间坐标 (单位 mm)
                        pclPoint.x = m_p2DToPointMap[nPclReadPos].x;
                        pclPoint.y = m_p2DToPointMap[nPclReadPos].y;
                        pclPoint.z = m_p2DToPointMap[nPclReadPos].z;

                        // 取出颜色信息 (RGBA 倒序处理)
                        unsigned char* pCurRGB = (unsigned char*)&m_p2DToPointMap[nPclReadPos].nRGB;
                        pclPoint.r = pCurRGB[2];
                        pclPoint.g = pCurRGB[1];
                        pclPoint.b = pCurRGB[0];
                        pclPoint.a = 255;

                        pclCloud->push_back(pclPoint); // 塞入 PCL 容器
                    }
                    nPclReadPos++;
                }
            }

            // 设置 PCL 点云属性
            pclCloud->width = pclCloud->size();
            pclCloud->height = 1;
            pclCloud->is_dense = false;

            // 调用 PCL 保存文件
            QString pcdFileName = saveDir + QString("/PclCloud_%1.pcd").arg(timeStr);
            if (pcl::io::savePCDFileBinary(pcdFileName.toStdString(), *pclCloud) == 0) {
                qDebug() << "5️⃣ PCL 点云图已成功保存至：" << pcdFileName << " (总点数:" << pclCloud->size() << ")";
                emit imageSaved(pcdFileName);
            }
        }

        // 4️⃣ 保存 2D 表面参考图像 (自动中心图)
        SVzNLImageData* psCenterImage = nullptr;
        VzNL_GetAutoDetectResultSurface(m_mainCameraHandle, &psCenterImage);

        if (psCenterImage != nullptr) {
            QString surfaceFileName = saveDir + QString("/Surface_%1.png").arg(timeStr);
            if (VzNL_SaveImage(surfaceFileName.toUtf8().data(), psCenterImage) == 0) {
                qDebug() << "4️⃣ 2D 表面图像已成功保存至：" << surfaceFileName;
                emit imageSaved(surfaceFileName);
            }
            VzNL_ReleaseImage(&psCenterImage);
        }

        // 🧹 清理堆区内存，防止内存泄漏
        if (m_p2DToPointMap) { delete[] m_p2DToPointMap; m_p2DToPointMap = nullptr; }
        if (m_pb2DInvalidPt) { delete[] m_pb2DInvalidPt; m_pb2DInvalidPt = nullptr; }

        VzNL_EndDetectLaser(m_mainCameraHandle);
        qDebug() << "⏹ 激光扫描已安全结束，本次共保存 5 种图像/点云数据。";
    }
}

void VizumCamera::onOpenDeviceTriggered()
{
    if (m_mainCameraHandle != nullptr) {
        emit errorOccurred("设备已经处于打开状态。");
        return;
    }

    SVzNLConfigParam configParam;
    memset(&configParam, 0, sizeof(SVzNLConfigParam));
    configParam.nDeviceTimeOut = 0;
    if (VzNL_Init(&configParam) != 0) {
        emit errorOccurred("SDK初始化失败，请查看是否有其他程序在使用SDK?");
        return;
    }

    bool bCanResearch;
    std::vector<SVzNLEyeCBInfo> vetDevice;
    int nErrorCode = 0;

    do {
        bCanResearch = false;
        VzNL_ResearchDevice(keSearchDeviceFlag_EthLaserRobotEye);

        int nDevCount = 0;
        VzNL_GetEyeCBDeviceInfo(nullptr, &nDevCount);
        if (nDevCount <= 0) break;

        vetDevice.resize(nDevCount);
        VzNL_GetEyeCBDeviceInfo(vetDevice.data(), &nDevCount);

        for (auto& devInfo : vetDevice) {
            if (devInfo.bValidDevice == VzFalse) {
                if (VzNL_BindEthernetEye(&devInfo) == 0) {
                    bCanResearch = true;
                    break;
                }
            }
        }
    } while (bCanResearch);

    for (auto& devInfo : vetDevice) {
        if (devInfo.bValidDevice == VzTrue) {
            SVzNLOpenDeviceParam sOpenDevParam;
            memset(&sOpenDevParam, 0, sizeof(SVzNLOpenDeviceParam));

            m_mainCameraHandle = VzNL_OpenDevice(&devInfo, &sOpenDevParam, &nErrorCode);
            if (m_mainCameraHandle != nullptr) {

                m_captureAction->setEnabled(true);
                m_closeDeviceAction->setEnabled(true);
                m_openDeviceAction->setEnabled(false);

                VzNL_EnableRGB(m_mainCameraHandle, VzTrue);
                if (VzNL_IsSupportSwingMotor(m_mainCameraHandle, nullptr)) {
                    VzNL_EnableSwingMotor(m_mainCameraHandle, VzTrue);
                }

                // 🌟 修复点：抛出设备成功连接的信号，携带 IP 地址
                emit deviceOpened(QString((char*)devInfo.byServerIP));
                return;
            }
        }
    }
    emit errorOccurred(QString("打开设备失败，错误码：%1").arg(nErrorCode));
}

void VizumCamera::onCloseDeviceTriggered()
{
    if (m_mainCameraHandle != nullptr) {
        // 如果还在采图，先安全停止
        if (m_isCapturing) {
            onCaptureTriggered(); // 🌟 直接复用采图逻辑，保证内存和硬件被正确释放
        }

        VzNL_CloseDevice(m_mainCameraHandle);
        m_mainCameraHandle = nullptr;

        m_captureAction->setText("开启采图");
        m_captureAction->setEnabled(false);
        m_closeDeviceAction->setEnabled(false);
        m_openDeviceAction->setEnabled(true);

        // 释放整个 SDK 环境
        VzNL_Destroy();

        qDebug() << "相机已安全关闭";
        emit deviceClosed();
    }
}

void VizumCamera::_AutoOutputLaserLineExCB(EVzResultDataType eDataType, SVzLaserLineData* pLaserLinePoint, void* pParam)
{
    if (keResultDataType_PointXYZRGBA != eDataType || pParam == nullptr) return;

    VizumCamera* pThis = static_cast<VizumCamera*>(pParam);
    if (pThis->m_p2DToPointMap == nullptr || pThis->m_pb2DInvalidPt == nullptr) return;

    SVzNLPointXYZRGBA* p3DPoint = (SVzNLPointXYZRGBA*)pLaserLinePoint->p3DPoint;
    SVzNL2DLRPoint* p2DPoint = (SVzNL2DLRPoint*)pLaserLinePoint->p2DPoint;

    for (int nPtIdx = 0; nPtIdx < pLaserLinePoint->nPointCount; nPtIdx++)
    {
        int x = p2DPoint->sLeft.x;
        int y = p2DPoint->sLeft.y;

        if (x >= 0 && x < pThis->m_sRGBVideoRes.nFrameWidth &&
            y >= 0 && y < pThis->m_sRGBVideoRes.nFrameHeight)
        {
            int nPos = x + y * pThis->m_sRGBVideoRes.nFrameWidth;
            pThis->m_p2DToPointMap[nPos] = *p3DPoint;
            pThis->m_pb2DInvalidPt[nPos] = true;
        }
        p3DPoint++;
        p2DPoint++;
    }
}
