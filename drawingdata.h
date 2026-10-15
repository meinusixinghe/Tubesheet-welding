#ifndef DRAWINGDATA_H
#define DRAWINGDATA_H

#include <QPointF>
#include <QVector>
#include <QVector3D>

struct Hole {
    int id;
    QPointF center;
    QVector3D center3D;
    double radius;
};

struct Contour {
    QVector<QPointF> points;
};

#endif // DRAWINGDATA_H
