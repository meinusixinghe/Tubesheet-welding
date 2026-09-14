#ifndef PATHPLANNER_H
#define PATHPLANNER_H

#include "mainwindow.h"
#include <QVector>
#include <QObject>

class PathPlanner
{
public:
    enum AlgorithmType {
        CenterOutward = 0,      // 1. 中心向外扩展
        RowSymmetric,           // 2. 行对称交替
        RowZigZag,              // 3. 行优先之字形
        FourQuadrants,          // 4. 四象限分区
        VerticalStrips,         // 5. 条状分区
        SkipRow,                // 6. 跳行
        Spiral                  // 7. 螺旋式
    };

    // 核心函数：根据选择的算法重新排序
    static void extracted(QVector<Hole> &tempHoles, double &minX, double &maxX);
    static QVector<Hole> planPath(const QVector<Hole> &inputHoles,
                                  const Hole &mainPlate, AlgorithmType type);

private:
    // 辅助：将管孔按行分组 (允许一定的 Y轴误差 epsilon)
    static QVector<QVector<Hole>> groupHolesByRows(const QVector<Hole>& holes, double epsilon = 5.0);

    // 具体的算法实现函数
    static QVector<Hole> planCenterOutward(QVector<Hole> holes, QPointF center);
    static QVector<Hole> planRowSymmetric(const QVector<QVector<Hole>>& rows);
    static QVector<Hole> planRowZigZag(QVector<QVector<Hole>>& rows);
    static QVector<Hole> planFourQuadrants(QVector<Hole> holes, QPointF center);
    static QVector<Hole> planVerticalStrips(QVector<Hole> holes, double minX, double maxX);
    static QVector<Hole> planSkipRow(const QVector<QVector<Hole>>& rows);
    static QVector<Hole> planSpiral(QVector<Hole> holes, QPointF center);
};

#endif // PATHPLANNER_H
