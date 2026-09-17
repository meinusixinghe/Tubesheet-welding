#ifndef VIZUMCAMERA_H
#define VIZUMCAMERA_H

#include <QWidget>
#include <QAction>
#include <QToolBar>
#include <QObject>
#include "VZNL_Common.h"

class VizumCamera : public QObject
{
    Q_OBJECT
public:
    explicit VizumCamera(QObject *parent=nullptr);
    ~VizumCamera();

    void addActionsToToolBar(QToolBar *toolbar);                // 添加动作到工具栏

signals:
    void deviceOpened(QString ip);
    void deviceClosed();
    void imageSaved(QString filePath);
    void errorOccurred(QString msg);

private slots:
    void onCaptureTriggered();
    void onOpenDeviceTriggered();
    void onCloseDeviceTriggered();
private:
    VZNLHANDLE m_mainCameraHandle = nullptr;
    bool m_isCapturing = false;
    QAction *m_openDeviceAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_closeDeviceAction = nullptr;

    static void _AutoOutputLaserLineExCB(EVzResultDataType eDataType, SVzLaserLineData* pLaserLinePoint, void* pParam);

    void initActions(); // 内部初始化动作、绑定信号槽
};

#endif // VIZUMCAMERA_H
