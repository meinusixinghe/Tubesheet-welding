#include "renderarea.h"
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QDebug>
#include <limits>
#include <algorithm>
#include <QPolygonF>
#include <QtMath>

using std::min;
using std::max;

// ----------------------------------------------------
// 功能：构造函数，初始化控件属性
// 讲解：RenderArea是 QWidget的子类，作为独立的绘图区域
// 核心：初始化变换参数的默认值，设置控件的视觉和交互属性，为后续绘图和交互做准备
// ----------------------------------------------------
RenderArea::RenderArea(QWidget *parent): QWidget(parent),
    m_scaleFactor(1.0),                                 // 缩放因子（首次加载时自动计算，滚轮事件中修改）
    m_panOffsetDXF(0.0, 0.0),                           // 用户拖拽产生的平移量（DXF坐标单位）
    m_initialContentOffset(0.0, 0.0)                    // 首次加载时，将内容几何中心移到原点的偏移量（实现居中的核心）
{
    setBackgroundRole(QPalette::Base);                  // 设置背景色为控件默认的基础色
    setAutoFillBackground(true);                        // 自动填充背景，避免透明
    setFocusPolicy(Qt::StrongFocus);                    // 获得焦点，支持键盘事件（此处未用，预留）
    setCursor(Qt::OpenHandCursor);                      // 设置鼠标样式，提醒用户可以拖拽
}

// ----------------------------------------------------
// 功能：数据管理，负责接收外部数据并触发重绘
// 讲解：setData，加载孔洞和轮廓数据
// 核心：接收外部数据后，重置所有变换状态（避免旧数据的缩放/平移影响新数据），并触发重绘
// ----------------------------------------------------
void RenderArea::setData(const QVector<Hole> &h,const Hole &mPH,const QPolygonF &platePoly, bool isRect, bool resetView)
{
    weldHoles = h;                                      // 管孔数据（不包含主管板圆）
    mainPlateHole = mPH;                                // 管板圆
    m_mainPlatePolygon = platePoly;                     // 存多边形
    m_isRectangular = isRect;                           // 存标志位

    if (resetView) {
        m_scaleFactor = 1.0;
        m_panOffsetDXF = QPointF(0.0, 0.0);
        m_initialContentOffset = QPointF(0.0, 0.0);

        m_completedHoles.clear(); // 只有这时才清空已完成的列表

        m_dxfMinBound = QPointF(0.0, 0.0);
        m_dxfMaxBound = QPointF(0.0, 0.0);
    }

    // 触发重绘
    update();
}

// ----------------------------------------------------
// 功能：数据管理，负责接收外部数据并触发重绘
// 讲解：setHighlightedIndex，设置高亮孔洞索引
// 核心：实现 “点击列表高亮绘图区对应孔洞 ”的核心逻辑，仅当索引变化时才重绘，优化性能
// ----------------------------------------------------
void RenderArea::setHighlightedIndex(int index)
{
    if (m_highlightIndex != index) {                    // 仅当索引变化时才重绘
        m_highlightIndex = index;
        update();                                       // 触发重绘以显示新的高亮状态
    }
}

// ----------------------------------------------------
// 功能：Qt绘图的入口，负责重绘
// 讲解：paintEvent是 QWidget的虚函数，当控件需要重绘时（如 update()调用）自动触发
// ----------------------------------------------------
void RenderArea::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);                                         // 2D绘图类
    painter.setRenderHint(QPainter::Antialiasing);                  // 开启抗锯齿，让图形更平滑
    painter.setRenderHint(QPainter::TextAntialiasing);              // 文字抗锯齿，更清晰

    if (weldHoles.isEmpty() && contours.isEmpty()) return;  // 无数据时直接返回

    // 首次加载：计算初始变换（适配窗口+居中）；后续：应用当前变换（缩放+平移）
    if (m_scaleFactor == 1.0 && m_initialContentOffset == QPointF(0.0, 0.0)) {
        calculateInitialTransform(painter);
    } else {
        applyCurrentTransform(painter);
    }

    // 绘制管板外轮廓
    QPen mainPlatePen(Qt::black, 0);
    mainPlatePen.setCosmetic(true);                         // 用于将画笔设置为装饰性画笔（让画笔的线宽不随缩放变换而改变）
    mainPlatePen.setWidth(4);
    painter.setPen(mainPlatePen);
    painter.setBrush(Qt::NoBrush);
    if (m_isRectangular) {
        if (!m_mainPlatePolygon.isEmpty())                  // 如果是方形模式，画多边形
            painter.drawPolygon(m_mainPlatePolygon);
    } else {
        painter.drawEllipse(mainPlateHole.center, mainPlateHole.radius, mainPlateHole.radius);  // 如果是圆形模式，画圆
    }
    for (int i = 0; i < weldHoles.size(); ++i) {
        const Hole &hole = weldHoles[i];
        bool isHighlighted = (i == m_highlightIndex);
        bool isCompleted = m_completedHoles.contains(i);

        // 4.1 绘制圆 (几何体：直接画)
        if (isHighlighted) {
            QPen highlightPen(Qt::red, 0);
            highlightPen.setCosmetic(true);
            highlightPen.setWidth(3);
            painter.setPen(highlightPen);
            painter.setBrush(QBrush(Qt::yellow, Qt::Dense6Pattern));
        }else if (isCompleted) {
            QPen completePen(Qt::darkGreen, 0);
            completePen.setCosmetic(true);
            completePen.setWidth(2);
            painter.setPen(completePen);
            painter.setBrush(QBrush(QColor(144, 238, 144))); // 浅绿色 (LightGreen)
        }else {
            QPen holePen(Qt::blue, 0);
            holePen.setCosmetic(true);
            holePen.setWidth(1);
            painter.setPen(holePen);
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawEllipse(hole.center, hole.radius, hole.radius);

        painter.save();
        painter.translate(hole.center);

        // 再次翻转 Y 轴 (负负得正，让文字正立)
        painter.scale(1.0, -1.0);

        // C. 设置字体和位置 (此时坐标系原点就是圆心)
        QFont numFont;
        int fontSize = qMax(1, static_cast<int>(hole.radius / 2.5)); // 调整字体大小
        numFont.setBold(true);
        numFont.setPointSizeF(fontSize);
        painter.setFont(numFont);

        // 计算文字矩形（以(0,0)为中心）
        double rectSize = hole.radius * 1.5;
        QRectF numRect(-rectSize/2, -rectSize/2, rectSize, rectSize);

        painter.setPen(isHighlighted ? Qt::red : Qt::black);
        painter.drawText(numRect, Qt::AlignCenter, QString::number(hole.id));

        painter.restore(); // 恢复到全局坐标系（Y向上），准备画下一个圆
    }

    // 绘制用户坐标系
    if (m_showUserCoordinate) {
        painter.save();

        // 1. 动态计算参考大小 (依据管板实际物理尺寸边界)
        double contentW = m_dxfMaxBound.x() - m_dxfMinBound.x();
        double contentH = m_dxfMaxBound.y() - m_dxfMinBound.y();
        double refSize = std::max(contentW, contentH);
        if (refSize < 1.0) refSize = 100.0; // 防止异常数据的安全后备

        // 2. 根据管板大小动态计算各元素尺寸
        double originRadius = refSize * 0.01;  // 原点半径取 1%
        double arrowSize = refSize * 0.03;      // 箭头大小取 2%
        double fontSize = refSize * 0.025;       // 字体大小取 1.5%
        double textOffset = refSize * 0.02;

        // 绘制原点
        QPen originPen(Qt::yellow, 0);
        originPen.setCosmetic(true);
        originPen.setWidth(5);
        painter.setPen(originPen);
        painter.setBrush(Qt::yellow);
        painter.drawEllipse(m_userOrigin, originRadius, originRadius);

        // 绘制 X轴
        QPen xAxisPen(Qt::red, 0);
        xAxisPen.setCosmetic(true);
        xAxisPen.setWidth(2);
        painter.setPen(xAxisPen);
        painter.drawLine(m_userOrigin, m_userXAxis);

        // 绘制 X轴箭头
        QPointF dir = m_userXAxis - m_userOrigin;
        double length = std::hypot(dir.x(), dir.y());
        if (length > 0) dir /= length;
        QPointF arrowBase = m_userXAxis - dir * arrowSize * 0.5;
        QPointF perp(-dir.y(), dir.x());
        QPointF arrowP2 = arrowBase + perp * (arrowSize * 0.3);
        QPointF arrowP3 = arrowBase - perp * (arrowSize * 0.3);
        QPolygonF arrowHead;
        arrowHead << m_userXAxis << arrowP2 << arrowP3;
        painter.setBrush(Qt::red);
        painter.drawPolygon(arrowHead);
        painter.setBrush(Qt::NoBrush);

        // x轴文字
        painter.save();
        painter.scale(1.0, -1.0);
        QFont xFont = painter.font();
        xFont.setPointSizeF(fontSize);
        painter.setFont(xFont);
        painter.drawText(m_userXAxis.x() + textOffset, -(m_userXAxis.y() - textOffset), "X轴");
        painter.restore();

        // 绘制平面参考线(Y轴)
        QPen yAxisPen(Qt::darkGreen, 0);
        yAxisPen.setCosmetic(true);
        yAxisPen.setWidth(2);
        painter.setPen(yAxisPen);
        painter.drawLine(m_userOrigin, m_userPlanePoint);

        QPointF dirY = m_userPlanePoint - m_userOrigin;
        double lengthY = std::hypot(dirY.x(), dirY.y());
        if (lengthY > 0) dirY /= lengthY;
        QPointF arrowBaseY = m_userPlanePoint - dirY * arrowSize * 0.5;
        QPointF perpY(-dirY.y(), dirY.x());
        QPointF arrowP2Y = arrowBaseY + perpY * (arrowSize * 0.3);
        QPointF arrowP3Y = arrowBaseY - perpY * (arrowSize * 0.3);
        QPolygonF arrowHeadY;
        arrowHeadY << m_userPlanePoint << arrowP2Y << arrowP3Y;
        painter.setBrush(Qt::darkGreen);
        painter.drawPolygon(arrowHeadY);
        painter.setBrush(Qt::NoBrush);

        // y轴文字
        painter.save();
        painter.scale(1.0, -1.0);
        QFont yFont = painter.font();
        yFont.setPointSizeF(fontSize);
        painter.setFont(yFont);
        painter.drawText(m_userPlanePoint.x() + textOffset, -(m_userPlanePoint.y() - textOffset), "Y轴");
        painter.restore();
    }
}

// ----------------------------------------------------
// 功能：鼠标滚轮缩放
// 核心：通过滚轮角度计算缩放因子，限制缩放范围，避免过度缩放
// ----------------------------------------------------
void RenderArea::wheelEvent(QWheelEvent *event)
{
    const double scaleStep = 1.15;              // 缩放步长（滚轮每滚一格，缩放15%）
    const double MIN_SCALE = 0.1;               // 最小缩放（防止缩太小看不见）
    const double MAX_SCALE = 2;                 // 最大缩放（防止缩太大）
    int degrees = event->angleDelta().y();      // 滚轮滚动的角度（正为向上，负为向下）

    // 计算新的缩放因子
    if (degrees > 0) {
        m_scaleFactor *= scaleStep;                           // 向上滚：放大
    }else if(degrees < 0) {m_scaleFactor /= scaleStep;}       // 向下滚：缩小

    // 限制缩放范围
    if (m_scaleFactor < MIN_SCALE) {
        m_scaleFactor = MIN_SCALE;
    }else if(m_scaleFactor > MAX_SCALE)m_scaleFactor = MAX_SCALE;

    update();
    event->accept();                            // 标记事件已处理
}

// ----------------------------------------------------
// 功能：左键按下时，记录鼠标初始位置，改变鼠标样式为 “闭手”
// ----------------------------------------------------
void RenderArea::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {        // 触发条件是鼠标左键
        m_lastMousePos = event->pos();              // 记录当前鼠标位置
        setCursor(Qt::ClosedHandCursor);            // 改变鼠标图标为 “闭手”
        event->accept();
    }
}

// ----------------------------------------------------
// 功能：左键按住时，计算鼠标移动的像素差，转换为 DXF坐标的平移量，应用平移限制后更新平移量
// ----------------------------------------------------
void RenderArea::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        QPoint delta = event->pos() - m_lastMousePos;                       // 像素移动量

        // 1.将像素移动量转换为 DXF坐标单位
        double dx = delta.x() / m_scaleFactor;
        double dy = -delta.y() / m_scaleFactor;

        // 2.计算潜在的新平移量
        QPointF newPanOffset = m_panOffsetDXF + QPointF(dx, dy);

        // 计算管板内容的实际物理尺寸 (DXF单位)
        double contentW = m_dxfMaxBound.x() - m_dxfMinBound.x();
        double contentH = m_dxfMaxBound.y() - m_dxfMinBound.y();
        if (contentW < 1.0) contentW = 100.0;                               // 防止除零或空数据的安全检查
        if (contentH < 1.0) contentH = 100.0;
        // 定义限制范围：管板边界的 1.5 倍
        double limitX = contentW * 0.75;
        double limitY = contentH * 0.75;

        // 3.平移限制
        if (newPanOffset.x() > limitX) {
            newPanOffset.setX(limitX);
        }else if (newPanOffset.x() < -limitX) {
            newPanOffset.setX(-limitX);
        }
        if (newPanOffset.y() > limitY) {
            newPanOffset.setY(limitY);
        }else if (newPanOffset.y() < -limitY) {
            newPanOffset.setY(-limitY);
        }

        // 4.应用新的平移量
        m_panOffsetDXF = newPanOffset;

        // 5.更新上次鼠标位置
        m_lastMousePos = event->pos();

        // 6.触发重绘
        update();
        event->accept();
    }
}

// ----------------------------------------------------
// 功能：左键松开时，恢复鼠标样式为 “开手 ”
// ----------------------------------------------------
void RenderArea::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);                      // 鼠标松开后恢复为 “开手”图标
        event->accept();
    }
}

// ----------------------------------------------------
// 坐标变换：合并初始居中、缩放和用户平移
// 解决了 DXF坐标与屏幕坐标的映射问题
// ----------------------------------------------------
void RenderArea::calculateInitialTransform(QPainter &painter)
{
    // 初始化 DXF边界
    double minX = std::numeric_limits<double>::max();           // 返回其能表示的最大正有限值
    double maxX = std::numeric_limits<double>::lowest();        // 返回其能表示的最小正有限值
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    m_dxfMinBound = QPointF(minX, minY);
    m_dxfMaxBound = QPointF(maxX, maxY);

    // 遍历所有孔洞中心和轮廓点，更新边界框
    // 1.创建一个可复用的匿名函数，命名为 updateBounds，用于根据传入的坐标点 p更新绘图区域的边界范围
    // [&]表示按引用捕获外部作用域的所有变量、(const QPointF& p)：参数列表
    auto updateBounds = [&](const QPointF& p) {
        if (p.x() < minX) minX = p.x();
        if (p.x() > maxX) maxX = p.x();
        if (p.y() < minY) minY = p.y();
        if (p.y() > maxY) maxY = p.y();
    };
    // 2.遍历孔洞中心
    for(const auto& h : std::as_const(weldHoles)) updateBounds(h.center);
    if (m_isRectangular) {
        for (const auto& p : m_mainPlatePolygon)                    // 如果是方形，遍历多边形的所有顶点
            updateBounds(p);
    } else {
        // 如果是圆形，加入圆的四个极点
        if (mainPlateHole.radius > 0) {
            updateBounds(QPointF(mainPlateHole.center.x() - mainPlateHole.radius, mainPlateHole.center.y()));
            updateBounds(QPointF(mainPlateHole.center.x() + mainPlateHole.radius, mainPlateHole.center.y()));
            updateBounds(QPointF(mainPlateHole.center.x(), mainPlateHole.center.y() - mainPlateHole.radius));
            updateBounds(QPointF(mainPlateHole.center.x(), mainPlateHole.center.y() + mainPlateHole.radius));
        }
    }
    if (minX > maxX) { applyCurrentTransform(painter); return; }    // 无有效点

    // 计算内容的宽高和适配窗口的缩放因子
    double contentW = maxX - minX;                                  // 宽度
    double contentH = maxY - minY;                                  // 高度
    double padding = 20.0;
    double scaleX = (width() - 2 * padding) / contentW;
    double scaleY = (height() - 2 * padding) / contentH;
    m_scaleFactor = min(scaleX, scaleY);                            // 取较小的缩放因子，保证内容完整显示

    // 计算初始居中偏移量：将内容几何中心移到原点（绝大部分情况是(0,0)）
    m_initialContentOffset = QPointF(-(minX + contentW / 2.0), -(minY + contentH / 2.0));

    // DXF边界（用于后续平移限制）
    m_dxfMinBound = QPointF(minX, minY);
    m_dxfMaxBound = QPointF(maxX, maxY);

    // 应用初始变换
    applyCurrentTransform(painter);
}

// ----------------------------------------------------
// 坐标变换：应用居缩放和用户平移量
// ----------------------------------------------------
void RenderArea::applyCurrentTransform(QPainter &painter)
{
    // 1. 平移到 Widget中心 (Widget坐标系)
    // 是 QPainter最核心的坐标变换方法，移动绘图坐标系的原点
    // width()：客户区宽度（像素）、height()：客户区高度（像素）
    painter.translate(width() / 2.0, height() / 2.0);

    // 2. 应用总缩放因子 (m_scaleFactor)和 Y轴翻转(从 DXF坐标 Y轴向上到屏幕 Y轴向下)
    painter.scale(m_scaleFactor, -m_scaleFactor);

    // 3. 应用总偏移量(初始居中 + 用户累积平移)
    QPointF totalOffset = m_initialContentOffset + m_panOffsetDXF;
    painter.translate(totalOffset);
}

// ----------------------------------------------------
// 设置用户坐标
// ----------------------------------------------------
void RenderArea::setUserCoordinatePoints(const QPointF& origin, const QPointF& xAxis, const QPointF& planePoint)
{
    m_userOrigin = origin;
    m_userXAxis = xAxis;
    m_userPlanePoint = planePoint;
    update();
}

// ----------------------------------------------------
// 是否显示用户坐标系
// ----------------------------------------------------
void RenderArea::setShowUserCoordinate(bool show)
{
    m_showUserCoordinate = show;
    update();
}

// ----------------------------------------------------
// 标记单个孔洞为已完成，并触发重绘
// ----------------------------------------------------
void RenderArea::setHoleCompleted(int index)
{
    m_completedHoles.insert(index);
    update();
}

// ----------------------------------------------------
// 清空所有孔洞的完成状态
// ----------------------------------------------------
void RenderArea::clearCompletedHoles()
{
    m_completedHoles.clear();
    update();
}
