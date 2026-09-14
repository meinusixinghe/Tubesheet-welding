#include "pathplanner.h"
#include <QtMath>
#include <algorithm>
#include <QMap>

// -----------------------------------------------------------------------------
// 核心入口：负责预处理、调用对应算法、后处理（重新编号）
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planPath(const QVector<Hole>& inputHoles, const Hole& mainPlate, AlgorithmType type)
{
    if (inputHoles.isEmpty()) return inputHoles;

    QVector<Hole> result;
    QVector<Hole> tempHoles = inputHoles;                                               // 拷贝一份进行处理

    // 针对需要行分组的算法预先分组
    QVector<QVector<Hole>> rows;
    if (type == RowSymmetric || type == RowZigZag || type == SkipRow) {
        rows = groupHolesByRows(tempHoles);                                             // 调用行分组辅助函数
    }

    // 计算包围盒 (用于条状分区)
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    if (type == VerticalStrips) {
        for (const auto& h : std::as_const(tempHoles)) {
            if (h.center.x() < minX) minX = h.center.x();
            if (h.center.x() > maxX) maxX = h.center.x();
        }
    }

    switch (type) {
    case CenterOutward:     result = planCenterOutward(tempHoles, mainPlate.center); break;
    case RowSymmetric:      result = planRowSymmetric(rows); break;
    case RowZigZag:         result = planRowZigZag(rows); break;
    case FourQuadrants:     result = planFourQuadrants(tempHoles, mainPlate.center); break;
    case VerticalStrips:    result = planVerticalStrips(tempHoles, minX, maxX); break;
    case SkipRow:           result = planSkipRow(rows); break;
    case Spiral:            result = planSpiral(tempHoles, mainPlate.center); break;
    }

    // 重新编号
    for (int i = 0; i < result.size(); ++i) {
        result[i].id = i + 1;
    }

    return result;
}

// -----------------------------------------------------------------------------
// 辅助：按行分组
// -----------------------------------------------------------------------------
QVector<QVector<Hole>> PathPlanner::groupHolesByRows(const QVector<Hole>& holes, double epsilon)
{
    // 1. 先按 Y 坐标排序 (从上到下，即 Y 值从大到小，假设 CAD 坐标系)
    // 注意：ezdxf解析时 Y向上。若 Y向下则逻辑相反。此处假设 Y值相近视为同一行。
    QVector<Hole> sortedHoles = holes;
    std::sort(sortedHoles.begin(), sortedHoles.end(), [](const Hole& a, const Hole& b) {
        return a.center.y() > b.center.y(); // 从上到下
    });

    QVector<QVector<Hole>> rows;
    if (sortedHoles.isEmpty()) return rows;

    // 2. 分组
    QVector<Hole> currentRow;
    currentRow.append(sortedHoles[0]);
    double currentY = sortedHoles[0].center.y();

    for (int i = 1; i < sortedHoles.size(); ++i) {
        if (qAbs(sortedHoles[i].center.y() - currentY) <= epsilon) {
            currentRow.append(sortedHoles[i]);
        } else {
            rows.append(currentRow);
            currentRow.clear();
            currentRow.append(sortedHoles[i]);
            currentY = sortedHoles[i].center.y();
        }
    }
    if (!currentRow.isEmpty()) rows.append(currentRow);

    return rows;
}

// -----------------------------------------------------------------------------
// 算法1：中心向外扩展
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planCenterOutward(QVector<Hole> holes, QPointF center)
{
    // 按距离中心的距离排序
    std::sort(holes.begin(), holes.end(), [center](const Hole& a, const Hole& b) {
        double d1 = std::hypot(a.center.x() - center.x(), a.center.y() - center.y());
        double d2 = std::hypot(b.center.x() - center.x(), b.center.y() - center.y());
        return d1 < d2;
    });
    return holes;
}

// -----------------------------------------------------------------------------
// 算法2：行对称交替 (第1行->最后1行->第2行->倒数第2行...)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planRowSymmetric(const QVector<QVector<Hole>>& rows)
{
    QVector<Hole> result;
    int top = 0;
    int bottom = rows.size() - 1;

    while (top <= bottom) {
        // 处理顶部行 (左->右)
        QVector<Hole> topRow = rows[top];
        std::sort(topRow.begin(), topRow.end(), [](const Hole& a, const Hole& b){ return a.center.x() < b.center.x(); });
        result.append(topRow);

        if (top != bottom) {
            // 处理底部行 (右->左)
            QVector<Hole> bottomRow = rows[bottom];
            std::sort(bottomRow.begin(), bottomRow.end(), [](const Hole& a, const Hole& b){ return a.center.x() > b.center.x(); }); // 降序
            result.append(bottomRow);
        }
        top++;
        bottom--;
    }
    return result;
}

// -----------------------------------------------------------------------------
// 算法3：行优先之字形 (S型)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planRowZigZag(QVector<QVector<Hole>>& rows)
{
    QVector<Hole> result;
    for (int i = 0; i < rows.size(); ++i) {
        QVector<Hole> row = rows[i];
        if (i % 2 == 0) {
            // 偶数行 (0, 2...) 左->右
            std::sort(row.begin(), row.end(), [](const Hole& a, const Hole& b){ return a.center.x() < b.center.x(); });
        } else {
            // 奇数行 (1, 3...) 右->左
            std::sort(row.begin(), row.end(), [](const Hole& a, const Hole& b){ return a.center.x() > b.center.x(); });
        }
        result.append(row);
    }
    return result;
}

// -----------------------------------------------------------------------------
// 算法4：四象限分区 (左上->右下->右上->左下)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planFourQuadrants(QVector<Hole> holes, QPointF center)
{
    QVector<Hole> q1, q2, q3, q4; // 1:右上, 2:左上, 3:左下, 4:右下 (笛卡尔)
    // 根据 UI 需求：左上 -> 右下 -> 右上 -> 左下
    // 对应象限逻辑：II -> IV -> I -> III

    for (const auto& h : holes) {
        double dx = h.center.x() - center.x();
        double dy = h.center.y() - center.y();
        if (dx >= 0 && dy >= 0) q1.append(h);      // 右上
        else if (dx < 0 && dy >= 0) q2.append(h);  // 左上
        else if (dx < 0 && dy < 0) q3.append(h);   // 左下
        else q4.append(h);                         // 右下
    }

    auto sortQuad = [](QVector<Hole>& q) {
        // 象限内行优先S型
        QVector<QVector<Hole>> r = groupHolesByRows(q);
        return planRowZigZag(r);
    };

    QVector<Hole> result;
    result.append(sortQuad(q2)); // 左上
    result.append(sortQuad(q4)); // 右下
    result.append(sortQuad(q1)); // 右上
    result.append(sortQuad(q3)); // 左下
    return result;
}

// -----------------------------------------------------------------------------
// 算法5：条状分区 (3条)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planVerticalStrips(QVector<Hole> holes, double minX, double maxX)
{
    double width = maxX - minX;
    double stripW = width / 3.0;

    QVector<Hole> s1, s2, s3;
    for (const auto& h : holes) {
        if (h.center.x() < minX + stripW) s1.append(h);
        else if (h.center.x() < minX + 2 * stripW) s2.append(h);
        else s3.append(h);
    }

    // 辅助 lambda: 按 X 排序
    auto sortXAsc = [](QVector<Hole>& s){
        std::sort(s.begin(), s.end(), [](const Hole& a, const Hole& b){
            if(abs(a.center.y() - b.center.y()) > 5.0) return a.center.y() > b.center.y(); // 先按行
            return a.center.x() < b.center.x();
        });
    };
    auto sortXDesc = [](QVector<Hole>& s){
        std::sort(s.begin(), s.end(), [](const Hole& a, const Hole& b){
            if(abs(a.center.y() - b.center.y()) > 5.0) return a.center.y() > b.center.y();
            return a.center.x() > b.center.x();
        });
    };

    // 第1条 左->右
    sortXAsc(s1);
    // 第2条 右->左
    sortXDesc(s2);
    // 第3条 左->右
    sortXAsc(s3);

    QVector<Hole> result;
    result << s1 << s2 << s3;
    return result;
}

// -----------------------------------------------------------------------------
// 算法6：跳行 (奇数行 -> 偶数行)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planSkipRow(const QVector<QVector<Hole>>& rows)
{
    QVector<Hole> result;
    QVector<QVector<Hole>> oddRows, evenRows; // 1,3,5... 和 2,4,6...

    for (int i = 0; i < rows.size(); ++i) {
        if ((i + 1) % 2 != 0) oddRows.append(rows[i]);
        else evenRows.append(rows[i]);
    }

    // 处理奇数行 (全部左->右)
    for (auto& row : oddRows) {
        std::sort(row.begin(), row.end(), [](const Hole& a, const Hole& b){ return a.center.x() < b.center.x(); });
        result.append(row);
    }

    // 处理偶数行 (全部右->左)
    for (auto& row : evenRows) {
        std::sort(row.begin(), row.end(), [](const Hole& a, const Hole& b){ return a.center.x() > b.center.x(); });
        result.append(row);
    }

    return result;
}

// -----------------------------------------------------------------------------
// 算法7：螺旋式 (近似处理：按半径排序，但为了连续性，极坐标角度排序辅助)
// -----------------------------------------------------------------------------
QVector<Hole> PathPlanner::planSpiral(QVector<Hole> holes, QPointF center)
{
    // 真正的螺旋可以用 极坐标 r 排序。
    std::sort(holes.begin(), holes.end(), [center](const Hole& a, const Hole& b) {
        double d1 = std::hypot(a.center.x() - center.x(), a.center.y() - center.y());
        double d2 = std::hypot(b.center.x() - center.x(), b.center.y() - center.y());
        // 如果半径非常接近，按角度排序，实现“画圈”
        if (std::abs(d1 - d2) < 2.0) { // 2.0 为同层误差
            double ang1 = std::atan2(a.center.y() - center.y(), a.center.x() - center.x());
            double ang2 = std::atan2(b.center.y() - center.y(), b.center.x() - center.x());
            return ang1 < ang2;
        }
        return d1 < d2;
    });
    return holes;
}
