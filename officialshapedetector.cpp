#include "officialshapedetector.h"

#include "VzCircleDetectAlgoLib.h"
#include "VZNL_FileUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <cmath>
#include <vector>

namespace {
QString sdkError(const QString& operation, int code)
{
    const QStringList reasons{
        "成功", "输入参数为空", "内存分配失败", "算法未初始化", "无法加载内部算法",
        "相机 SDK 内部算法版本不匹配，请联系厂商提供匹配的算法库",
        "直径范围不正确", "算法检测失败", "无法获取检测结果"
    };
    const QString reason = code >= 0 && code < reasons.size() ? reasons[code] : "SDK 返回未知错误";
    return QString("官方算法%1失败（错误码 %2）：%3").arg(operation).arg(code).arg(reason);
}

bool validateInput(const QString& filePath, double minDiameter, double maxDiameter, QString& error)
{
    if (!std::isfinite(minDiameter) || !std::isfinite(maxDiameter) || minDiameter <= 0 || maxDiameter <= minDiameter) {
        error = "直径范围不正确：最小直径须大于 0，最大直径须大于最小直径。";
        return false;
    }
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (!QFileInfo::exists(filePath) || (suffix != "pcd" && suffix != "txt")) {
        error = "请选择有效的 PCD 或官方 TXT 点云文件：\n" + filePath;
        return false;
    }
    return true;
}

bool loadPoints(const QString& filePath, std::vector<SVzNL3DPosition>& points, QString& error)
{
    const QByteArray nativePath = QDir::toNativeSeparators(filePath).toLocal8Bit();
    const EVzLaserFileType type = VzNL_IsSupportLaserType(nativePath.constData());
    VZNLFILE file = VzNL_CreateLaserFile(type);
    if (!file) {
        error = "无法创建官方点云读取工具：\n" + filePath;
        return false;
    }
    const auto closeFile = qScopeGuard([&] { VzNL_CloseLaserFile(file); });
    const int openResult = VzNL_OpenLaserFile(file, nativePath.constData(), keFileModeRead, keResultDataType_Position);
    if (openResult != 0) {
        error = QString("无法读取点云文件（错误码 %1）：\n%2").arg(openResult).arg(filePath);
        return false;
    }
    int lineCount = 1;
    while (SVzLaserLineData* line = VzNL_ReadLaserFile(file, &lineCount)) {
        const auto input = static_cast<const SVzNL3DPosition*>(line->p3DPoint);
        if (line->nPointCount > 0 && !input) {
            error = "点云文件中的三维数据无效：\n" + filePath;
            return false;
        }
        for (int i = 0; i < line->nPointCount; ++i) {
            const auto& point = input[i].pt3D;
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
                points.push_back(input[i]);
        }
        lineCount = 1;
    }
    if (points.empty()) {
        error = "文件中没有有效的三维点：\n" + filePath;
        return false;
    }
    return true;
}
}

bool OfficialShapeDetection::detect(VZNLHANDLE device, Type type, const QString& filePath,
                                    double minDiameter, double maxDiameter, QVector<Feature>& features, QString& error)
{
    features.clear();
    error.clear();
    if (!device) {
        error = "请先打开相机，再执行官方检测。";
        return false;
    }
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (!validateInput(absolutePath, minDiameter, maxDiameter, error)) return false;

    // The vendor constructor reads log.conf from the process working directory.
    const QString previousDirectory = QDir::currentPath();
    QDir::setCurrent(QCoreApplication::applicationDirPath());
    const auto restoreDirectory = qScopeGuard([&] { QDir::setCurrent(previousDirectory); });
    VzCircleDetectAlgoLib* algorithm = nullptr;
    VzCircleDetectAlgoLib_Create(&algorithm);
    if (!algorithm) {
        error = "无法创建官方圆孔/圆柱算法。";
        return false;
    }
    bool initialized = false;
    const auto releaseAlgorithm = qScopeGuard([&] {
        if (initialized) algorithm->Release();
        VzCircleDetectAlgoLib_Destroy(&algorithm);
    });
    const int code = algorithm->Init(device);
    if (code != VZCIRCLELIB_ERR_OK) {
        error = sdkError("初始化", code);
        return false;
    }
    initialized = true;
    return detect(*algorithm, type, absolutePath, minDiameter, maxDiameter, features, error);
}

bool OfficialShapeDetection::detect(VzCircleDetectAlgoLib& algorithm, Type type, const QString& filePath,
                                    double minDiameter, double maxDiameter, QVector<Feature>& features, QString& error)
{
    features.clear();
    error.clear();
    if (!validateInput(filePath, minDiameter, maxDiameter, error)) return false;
    std::vector<SVzNL3DPosition> points;
    if (!loadPoints(filePath, points, error)) return false;
    // Match the official demo: flatten the loaded points into a single laser-line input.
    SVzNL3DLaserLine line{};
    line.p3DPosition = points.data();
    line.nPositionCnt = static_cast<int>(points.size());
    VzCircleDetectCfgParaStr config{minDiameter, maxDiameter};
    VzCircleDetectResStr result{};
    const auto releaseResult = qScopeGuard([&] { algorithm.DetectResRelease(&result); });
    const int code = type == Type::Circle
                         ? algorithm.CircleDetect(&config, 1, &line, &result)
                         : algorithm.CylinderDetect(&config, 1, &line, &result, false);
    if (code != VZCIRCLELIB_ERR_OK) {
        error = sdkError("检测", code);
        return false;
    }
    if (result.nNum > 0 && !result.pCircleList) {
        error = "官方算法未返回有效的检测结果。";
        return false;
    }
    for (unsigned int i = 0; i < result.nNum; ++i) {
        const auto& item = result.pCircleList[i];
        features.append({item.sCenter, item.sNormalVec, item.dDiameter});
    }
    return true;
}
