#ifndef USERCOORDINATEMANAGER_H
#define USERCOORDINATEMANAGER_H

#include "mainwindow.h"
#include <QObject>
#include <QMatrix4x4>
#include <QVector>
#include <QPointF>
#include <QTimer>
#include <QTableWidget>
#include <QLabel>

class RenderArea;

class usercoordinatemanager : public QObject
{
    Q_OBJECT
public:
    enum Step {
        StepNone,
        StepOrigin,      // 原点确定步骤
        StepXAxis,       // X轴确定步骤
        StepPlane        // 平面确定步骤
    };
    explicit usercoordinatemanager(QObject *parent = nullptr);
    // 初始化数据
    void initialize(RenderArea* renderArea, QTableWidget* table,
                    QVector<Hole>& weldHoles, const Hole& mainPlateHole,
                    QLabel* statusLabel);

    // 开始建立用户坐标系流程
    void startSetup();

    // 取消当前操作
    void cancelCurrentStep();

    // 确认当前步骤
    void confirmCurrentStep();

    // 显示/隐藏用户坐标系
    void toggleCoordinateDisplay(bool show);

    // 获取用户坐标系矩阵
    QMatrix4x4 getCoordinateMatrix() const { return m_coordinateMatrix; }

    // 是否完成了坐标系建立
    bool isSetupComplete() const { return m_isSetupComplete; }

signals:
    // 通知UI更新坐标系显示
    void updateCoordinateDisplay(bool show);

    // 通知更新三维坐标数据
    void update3DCoordinates();

private slots:
    void onBlinkTimerTimeout();

private:
    // 寻找最近的管孔
    int findNearestHole(const QPointF& targetPoint, int excludeIndex = -1);

    // 计算步骤目标点
    QPointF calculateOriginTarget();
    QPointF calculateXAxisTarget();
    QPointF calculatePlaneTarget();

    // 更新状态提示
    void updateStatusMessage();

    // 应用坐标系变换到所有点
    void applyCoordinateTransformation();

    RenderArea* m_renderArea = nullptr;                                     // RenderArea类
    QTableWidget* m_dataTable = nullptr;                                    // QTableWidget类
    QVector<Hole>* m_weldHoles = nullptr;                                   // 管孔
    Hole m_mainPlateHole;                                                   // 管板
    QLabel* m_statusLabel = nullptr;                                        // 状态标签

    Step m_currentStep = StepNone;
    bool m_isSetupComplete = false;                                         // 用户坐标系完成标志位
    int m_blinkingHoleIndex = -1;                                           // 闪烁管孔索引
    bool m_isBlinkingVisible = true;                                        // 管孔亮暗
    QTimer* m_blinkTimer = nullptr;                                         // 创建定时器类（实现闪烁）

    // 坐标系参数
    QPointF m_origin;                                                       // 原点
    QPointF m_xAxisPoint;                                                   // 确定 x轴方向的点
    QPointF m_planePoint;                                                   // 确定 y轴方向的点
    QMatrix4x4 m_coordinateMatrix;
};

#endif // USERCOORDINATEMANAGER_H
