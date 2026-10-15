#ifndef OFFICIALSHAPEDETECTOR_H
#define OFFICIALSHAPEDETECTOR_H

#include "VZNL_Types.h"
#include <QString>
#include <QVector>

class VzCircleDetectAlgoLib;

namespace OfficialShapeDetection {
enum class Type { Circle, Cylinder };

struct Feature {
    SVzNL3DPoint center{};
    SVzNL3DPoint normal{};
    double diameter = 0;
};

bool detect(VZNLHANDLE device, Type type, const QString& filePath,
            double minDiameter, double maxDiameter, QVector<Feature>& features, QString& error);
bool detect(VzCircleDetectAlgoLib& algorithm, Type type, const QString& filePath,
            double minDiameter, double maxDiameter, QVector<Feature>& features, QString& error);
}

#endif // OFFICIALSHAPEDETECTOR_H
