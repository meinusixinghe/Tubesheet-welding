#ifndef RENDERAREA_H
#define RENDERAREA_H

#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include "mainwindow.h"
#include <QSet>

struct Hole;
struct Contour;

class RenderArea : public QWidget
{
    Q_OBJECT
public:
    explicit RenderArea(QWidget *parent = nullptr);

    void setData(const QVector<Hole> &h,const Hole &mPH,const QPolygonF &platePoly, bool isRect, bool resetView = true);

    // 设置要高亮显示的孔洞索引
    void setHighlightedIndex(int index);

    void setUserCoordinatePoints(const QPointF& origin, const QPointF& xAxis, const QPointF& planePoint);
    void setShowUserCoordinate(bool show);

    void setHoleCompleted(int index);
    void clearCompletedHoles();
protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    // 用于平移的鼠标事件
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // 管板数据
    QVector<Hole> weldHoles;                                // 仅焊接管孔（不含主体圆）
    Hole mainPlateHole;                                     // 管板主体圆（最大半径)
    QVector<Contour> contours;
    QPolygonF m_mainPlatePolygon;                           // 存储方形轮廓
    bool m_isRectangular = false;                           // 标记

    // 缩放和平移参数
    double m_scaleFactor;                                   // 总缩放比例 (包含初始自适应)
    QPointF m_panOffsetDXF;                                 // 用户累积的平移量 (DXF坐标单位)
    QPointF m_initialContentOffset;                         // 初始自适应居中所需的偏移量 (DXF坐标单位)

    // DXF数据的边界 (用于限制平移)
    QPointF m_dxfMinBound;
    QPointF m_dxfMaxBound;

    // 当前要高亮显示的孔洞索引
    int m_highlightIndex = -1;

    // 鼠标拖拽
    QPoint m_lastMousePos;                                  // 记录鼠标上一次的位置 (Widget像素单位)

    void calculateInitialTransform(QPainter &painter);
    void applyCurrentTransform(QPainter &painter);          // 应用所有变换

    // 用户坐标系显示相关
    bool m_showUserCoordinate = false;                      // 显示/隐藏用户坐标系
    QPointF m_userOrigin;                                   // 用户坐标系原点
    QPointF m_userXAxis;                                    // 用户坐标系 x轴
    QPointF m_userPlanePoint;                               // 用户坐标系 y轴

    QSet<int> m_completedHoles;
};

#endif // RENDERAREA_H
