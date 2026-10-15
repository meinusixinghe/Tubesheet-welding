#ifndef OFFICIALSHAPEDIALOG_H
#define OFFICIALSHAPEDIALOG_H

#include "officialshapedetector.h"
#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class OfficialShapeDialog : public QDialog
{
    Q_OBJECT
public:
    OfficialShapeDialog(VZNLHANDLE device, OfficialShapeDetection::Type type,
                        const QString& directory, QWidget* parent = nullptr);

private:
    void runDetection();
    VZNLHANDLE m_device;
    OfficialShapeDetection::Type m_type;
    QLineEdit* m_filePath;
    QDoubleSpinBox* m_minDiameter;
    QDoubleSpinBox* m_maxDiameter;
    QTableWidget* m_results;
    QLabel* m_status;
};

#endif // OFFICIALSHAPEDIALOG_H
