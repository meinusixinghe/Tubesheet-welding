#include "usercoordinatemanager.h"
#include "renderarea.h"
#include <cmath>
#include <QtMath>
#include <QVector3D>

// ----------------------------------------------------
// 功能：用户坐标系类构造函数
// ----------------------------------------------------
usercoordinatemanager::usercoordinatemanager(QObject *parent) : QObject(parent)
{
    m_blinkTimer = new QTimer(this);                                                                // 在堆上创建
    m_blinkTimer->setInterval(500);                                                                // 1秒间隔
    connect(m_blinkTimer, &QTimer::timeout, this, &usercoordinatemanager::onBlinkTimerTimeout);
}

// ----------------------------------------------------
// 功能：初始化函数（数据初始化）
// ----------------------------------------------------
void usercoordinatemanager::initialize(RenderArea* renderArea, QTableWidget* table,
                                       QVector<Hole>& weldHoles, const Hole& mainPlateHole,
                                       QLabel* statusLabel)
{
    m_renderArea = renderArea;
    m_dataTable = table;
    m_weldHoles = &weldHoles;
    m_mainPlateHole = mainPlateHole;
    m_statusLabel = statusLabel;
    m_isSetupComplete = false;
}

// ----------------------------------------------------
// 功能：开启坐标系创建
// ----------------------------------------------------
void usercoordinatemanager::startSetup()
{
    if (!m_weldHoles || m_weldHoles->isEmpty()) return;

    m_isSetupComplete = false;
    m_currentStep = StepOrigin;                                             // 将当前步骤改为设置原点
    m_blinkingHoleIndex = findNearestHole(calculateOriginTarget(), -1);     // 找用户坐标系原点高亮点
    m_blinkTimer->start();                                                  // 开启定数器
    updateStatusMessage();
}

// ----------------------------------------------------
// 功能：取消坐标系创建
// ----------------------------------------------------
void usercoordinatemanager::cancelCurrentStep()
{
    m_blinkTimer->stop();                                                   // 停止定时器
    m_blinkingHoleIndex = -1;
    m_currentStep = StepNone;
    if (m_renderArea) {
        m_renderArea->setHighlightedIndex(-1);
    }
    if (m_statusLabel) {
        m_statusLabel->setText("操作已取消");
    }
}

// ----------------------------------------------------
// 功能：确认用户坐标系设置当前步骤
// ----------------------------------------------------
void usercoordinatemanager::confirmCurrentStep()
{
    if (m_currentStep == StepNone || m_blinkingHoleIndex < 0 || m_blinkingHoleIndex >= m_weldHoles->size())
        return;

    const Hole& selectedHole = (*m_weldHoles)[m_blinkingHoleIndex];         // 获取当前选中的管孔和索引（原点）
    int currentHoleIndex = m_blinkingHoleIndex;

    // 记录当前步骤的点
    if (m_currentStep == StepOrigin) {
        m_origin = selectedHole.center;
        m_currentStep = StepXAxis;
        m_blinkingHoleIndex = findNearestHole(calculateXAxisTarget(), currentHoleIndex);
    }
    else if (m_currentStep == StepXAxis) {
        m_xAxisPoint = selectedHole.center;
        m_currentStep = StepPlane;
        m_blinkingHoleIndex = findNearestHole(calculatePlaneTarget(), -1);
    }
    else if (m_currentStep == StepPlane) {
        m_planePoint = selectedHole.center;
        m_currentStep = StepNone;
        m_blinkTimer->stop();
        m_blinkingHoleIndex = -1;
        if (m_renderArea) m_renderArea->setHighlightedIndex(-1);

        applyCoordinateTransformation();
        m_isSetupComplete = true;                                       // 标记用户坐标系设置完成
        emit update3DCoordinates();                                     // 发送信号：通知其他模块更新3D坐标
    }

    updateStatusMessage();

    // 如果还有步骤，继续闪烁
    if (m_currentStep != StepNone) {
        if (!m_blinkTimer->isActive()) m_blinkTimer->start();
    }
}

// ----------------------------------------------------
// 功能：坐标系显示/隐藏
// ----------------------------------------------------
void usercoordinatemanager::toggleCoordinateDisplay(bool show)
{
    if (m_renderArea) {
        m_renderArea->setShowUserCoordinate(show);
    }
}

// ----------------------------------------------------
// 功能: 找最接近目标点的焊孔
// 参数: 1.目标点 2.排除索引（如避免选中当前已选中的管孔）
// 返回值: 返回距离最近的管孔索引
// ----------------------------------------------------
int usercoordinatemanager::findNearestHole(const QPointF& targetPoint, int excludeIndex)
{
    if (!m_weldHoles || m_weldHoles->isEmpty()) return -1;          // 若列表为空 / 指针无效，返回 -1

    int nearestIndex = -1;                                          // 最近管孔索引，初始为 -1（未找到）
    double minDistance = std::numeric_limits<double>::max();        // 最小距离

    for (int i = 0; i < m_weldHoles->size(); ++i) {
        if (i == excludeIndex) continue;                            // 跳过排除索引的管孔
        const Hole& h = (*m_weldHoles)[i];                          // 取当前管孔（引用方式，避免拷贝，提升效率）
        double dx = h.center.x() - targetPoint.x();
        double dy = h.center.y() - targetPoint.y();
        double dist = std::hypot(dx, dy);                           // 计算两点间欧式距离（等价于 sqrt(dx*dx + dy*dy)，但更安全）
        if (dist < minDistance) {
            minDistance = dist;
            nearestIndex = i;
        }
    }

    if (nearestIndex == -1 && !m_weldHoles->isEmpty()) return 0;    // 返回 0（兜底）
    return nearestIndex;
}

// ----------------------------------------------------
// 功能: 确定原点坐标
// ----------------------------------------------------
QPointF usercoordinatemanager::calculateOriginTarget()
{
    return m_mainPlateHole.center;
}

// ----------------------------------------------------
// 功能: 确定 x轴
// ----------------------------------------------------
QPointF usercoordinatemanager::calculateXAxisTarget()
{
    // X轴目标：原点右侧，距离管板半径 3/4的位置
    double radius = m_mainPlateHole.radius * 0.75;
    return QPointF(m_origin.x() + radius, m_origin.y());
}

// ----------------------------------------------------
// 功能: 确定 y轴
// ----------------------------------------------------
QPointF usercoordinatemanager::calculatePlaneTarget()
{
    // 平面目标：原点上方，距离管板半径 3/4的位置
    double radius = m_mainPlateHole.radius * 0.75;
    return QPointF(m_origin.x(), m_origin.y() + radius);
}

// ----------------------------------------------------
// 功能: 更新提示创建用户坐标系信息（左下角的状态标签）
// ----------------------------------------------------
void usercoordinatemanager::updateStatusMessage()
{
    if (!m_statusLabel) return;

    switch (m_currentStep) {
    case StepOrigin:
        m_statusLabel->setText("步骤1/3: 请将机器人移动到闪烁的圆（靠近大管板中心最近的管孔），完成后点击确定");
        break;
    case StepXAxis:
        m_statusLabel->setText("步骤2/3: 请将机器人移动到闪烁的圆（原点右侧参考点），完成后点击确定");
        break;
    case StepPlane:
        m_statusLabel->setText("步骤3/3: 请将机器人移动到闪烁的圆（原点上方参考点），完成后点击确定");
        break;
    case StepNone:
        if (m_isSetupComplete)
            m_statusLabel->setText("用户坐标系建立完成");
        else
            m_statusLabel->setText("就绪");
        break;
    default:
        break;
    }
}

// ----------------------------------------------------
// 功能: 更新提示创建用户坐标系信息（左下角的状态标签）
// ----------------------------------------------------
void usercoordinatemanager::applyCoordinateTransformation()
{
    // 计算从世界坐标系到用户坐标系的变换矩阵
    QVector3D pOrigin(m_origin.x(), m_origin.y(), 0);
    QVector3D pX(m_xAxisPoint.x(), m_xAxisPoint.y(), 0);
    QVector3D pPlane(m_planePoint.x(), m_planePoint.y(), 0);

    // 计算 X轴方向向量
    QVector3D vx = (pX - pOrigin);
    double axisLength = vx.length();
    if (vx.lengthSquared() < 1e-6) return;                                  // 防止两点重合导致的除零错误
    vx.normalize();

    // 强制定义 Z轴垂直于屏幕向外 (0, 0, 1)
    QVector3D vz(0, 0, 1);

    // 计算 Y轴 (通过 Z叉乘 X)
    QVector3D vy = QVector3D::crossProduct(vz, vx).normalized();

    // 方向校正
    QVector3D vUserRef = pPlane - pOrigin;

    // 使用点积判断方向：如果计算出的 vy 与用户向量的点积为负，说明方向反了，需要翻转
    if (QVector3D::dotProduct(vy, vUserRef) < 0) {
        vy = -vy;                                                           // 翻转 Y 轴
        vz = -vz;
    }

    // 计算一个严格垂直的点：原点 + Y轴方向 * 长度，这样 RenderArea画出来的线就是严格 90度的
    QVector3D correctedYEndPoint = pOrigin + (vy * axisLength);
    QPointF visualPlanePoint(correctedYEndPoint.x(), correctedYEndPoint.y());

    // 更新 RenderArea显示
    if (m_renderArea) {
        m_renderArea->setUserCoordinatePoints(m_origin, m_xAxisPoint, visualPlanePoint);
        m_renderArea->setShowUserCoordinate(true);
    }

    // 构建变换矩阵
    QMatrix4x4 rotation;
    rotation.setColumn(0, QVector4D(vx, 0));
    rotation.setColumn(1, QVector4D(vy, 0));
    rotation.setColumn(2, QVector4D(vz, 0));
    rotation.setColumn(3, QVector4D(0, 0, 0, 1));

    QMatrix4x4 translation;
    translation.translate(pOrigin);

    m_coordinateMatrix = translation * rotation;
    bool invertible;
    QMatrix4x4 worldToUser = m_coordinateMatrix.inverted(&invertible);
    if (!invertible) return;

    // 应用变换到所有点
    for (auto& hole : *m_weldHoles) {
        QVector3D worldPos(hole.center.x(), hole.center.y(), 0);
        hole.center3D = worldToUser.map(worldPos);
    }
}

// ----------------------------------------------------
// 功能：定时超时函数
// ----------------------------------------------------
void usercoordinatemanager::onBlinkTimerTimeout()
{
    if (!m_renderArea) return;

    m_isBlinkingVisible = !m_isBlinkingVisible;                             // 明暗反转
    if (m_isBlinkingVisible) {                                              // 亮
        m_renderArea->setHighlightedIndex(m_blinkingHoleIndex);             // 调用 renderArea类中的设置高亮方法
    } else {                                                                // 暗
        m_renderArea->setHighlightedIndex(-1);                              // 取消高亮
    }
}
