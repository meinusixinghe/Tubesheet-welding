#include "vizumcamera.h"
#include "VZNL_Graphics.h"
#include "VZNL_DetectLaser.h"
#include "VZNL_DetectConfig.h"
#include "VZNL_EyeConfig.h"
#include "VZNL_RGBConfig.h"
#include "VZNL_SwingMotor.h"
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>

VizumCamera::VizumCamera(QObject *parent) : QObject(parent)
{
    VizumCamera::initActions();
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
        emit errorOccurred("警告请先打开设备！");
        return;
    }

    if (!m_isCapturing) {
        // 1. 启用 RGB Sensor 与摆动电机 (严格参考官方)
        VzNL_EnableRGB(m_mainCameraHandle, VzTrue);
        VzNL_EnableSwingMotor(m_mainCameraHandle, VzTrue);

        // 2. 创建激光线检测工具
        int nErr = VzNL_BeginDetectLaser(m_mainCameraHandle);
        if (nErr != 0) {
            emit errorOccurred( QString("创建检测激光线工具失败，错误码：%1").arg(nErr));
            return;
        }

        // 3. 设置主模式
        VzNL_SetTriggerMode(m_mainCameraHandle, keEyeTriggerMode_Master);

        // 4. 开始流模式检测 (使用 3D 专属 API)
        nErr = VzNL_StartAutoDetectEx(m_mainCameraHandle, keResultDataType_PointXYZRGBA, keFlipType_None, _AutoOutputLaserLineExCB, this);

        if (nErr == 0) {
            m_isCapturing = true;
            m_captureAction->setText("停止扫描并保存图像");
            qDebug() << "激光扫描已启动，请等待扫描完成后点击停止...";
        } else {
            emit errorOccurred(QString("开流失败，错误码：%1").arg(nErr));
            VzNL_EndDetectLaser(m_mainCameraHandle);
        }

    } else {
        // ==========================================
        // 停止扫描并提取 2D 表面图
        // ==========================================
        VzNL_StopAutoDetect(m_mainCameraHandle);
        m_isCapturing = false;
        m_captureAction->setText("开启采图");

        // 1. 提取自动合成的表面图像
        SVzNLImageData* psCenterImage = nullptr;
        VzNL_GetAutoDetectResultSurface(m_mainCameraHandle, &psCenterImage);

        if (psCenterImage != nullptr) {
            // 2. 构建保存路径
            QString saveDir = QCoreApplication::applicationDirPath() + "/CaptureImages";
            QDir().mkpath(saveDir);
            QString fileName = saveDir + QString("/Surface_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

            // 3. 保存图像
            if (VzNL_SaveImage(fileName.toUtf8().data(), psCenterImage) == 0) {
                qDebug() << "表面图像已成功保存至：" << fileName;
                emit imageSaved(fileName);
            } else {
                qDebug() << "图像保存失败！";
            }

            // 4. 释放内存 (必须执行，否则内存泄漏)
            VzNL_ReleaseImage(&psCenterImage);
        } else {
            emit errorOccurred("未能提取到有效的表面图像，请检查扫描过程是否完整。");
        }

        // 5. 结束激光检测工具
        VzNL_EndDetectLaser(m_mainCameraHandle);
        qDebug() << "激光扫描已安全结束。";
    }
}

void VizumCamera::onOpenDeviceTriggered()
{
    if (m_mainCameraHandle != nullptr) {
        emit errorOccurred("设备已经处于打开状态。");
        return;
    }

    // 1. 初始化 SDK (官方推荐带超时参数)
    SVzNLConfigParam configParam;
    memset(&configParam, 0, sizeof(SVzNLConfigParam));
    configParam.nDeviceTimeOut = 0;
    if (VzNL_Init(&configParam) != 0) {
        emit errorOccurred("SDK初始化失败，请查看是否有其他程序在使用SDK?");
        return;
    }

    // 2. 官方的 do-while 强制绑定与重搜机制
    bool bCanResearch;
    std::vector<SVzNLEyeCBInfo> vetDevice;
    int nErrorCode = 0;

    do {
        bCanResearch = false;
        VzNL_ResearchDevice(keSearchDeviceFlag_EthLaserRobotEye); // 搜索 3D 激光相机

        int nDevCount = 0;
        VzNL_GetEyeCBDeviceInfo(nullptr, &nDevCount);
        if (nDevCount <= 0) break;

        vetDevice.resize(nDevCount);
        VzNL_GetEyeCBDeviceInfo(vetDevice.data(), &nDevCount);

        for (auto& devInfo : vetDevice) {
            if (devInfo.bValidDevice == VzFalse) {
                // 尝试绑定未识别的相机
                if (VzNL_BindEthernetEye(&devInfo) == 0) {
                    bCanResearch = true; // 绑定成功，必须重新搜索
                    break;
                }
            }
        }
    } while (bCanResearch);

    // 3. 寻找有效设备并打开
    for (auto& devInfo : vetDevice) {
        if (devInfo.bValidDevice == VzTrue) {
            SVzNLOpenDeviceParam sOpenDevParam;
            memset(&sOpenDevParam, 0, sizeof(SVzNLOpenDeviceParam));

            m_mainCameraHandle = VzNL_OpenDevice(&devInfo, &sOpenDevParam, &nErrorCode);
            if (m_mainCameraHandle != nullptr) {

                m_captureAction->setEnabled(true);
                m_closeDeviceAction->setEnabled(true);
                m_openDeviceAction->setEnabled(false);

                // 官方建议：开启 RGB 与摆动电机 (如果硬件支持)
                VzNL_EnableRGB(m_mainCameraHandle, VzTrue);
                if (VzNL_IsSupportSwingMotor(m_mainCameraHandle, nullptr)) {
                    VzNL_EnableSwingMotor(m_mainCameraHandle, VzTrue);
                }
                return; // 成功连接一台即可返回
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
            VzNL_StopAutoDetect(m_mainCameraHandle);
            VzNL_EndDetectLaser(m_mainCameraHandle);
            m_isCapturing = false;
        }

        VzNL_CloseDevice(m_mainCameraHandle);
        m_mainCameraHandle = nullptr;

        m_captureAction->setText("▶ 开启采图");
        m_captureAction->setEnabled(false);
        m_closeDeviceAction->setEnabled(false);
        m_openDeviceAction->setEnabled(true);

        qDebug() << "相机已安全关闭";
        emit deviceClosed();
    }
}

void VizumCamera::_AutoOutputLaserLineExCB(EVzResultDataType eDataType, SVzLaserLineData* pLaserLinePoint, void* pParam)
{
    // 这里用于接收底层的高频激光线数据 (用于拼接点云和深度图)
    // 根据官方警告，不要在此处做耗时操作。
    // 由于我们在 UI 线程的 Stop 操作里抓取了最终合成图，这里暂可不写业务逻辑。

    // 使用 Q_UNUSED 消除编译器“形参未引用”的警告
    Q_UNUSED(eDataType);
    Q_UNUSED(pLaserLinePoint);
    VizumCamera* pThis = static_cast<VizumCamera*>(pParam);
}
