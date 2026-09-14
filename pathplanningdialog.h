#ifndef PATHPLANNINGDIALOG_H
#define PATHPLANNINGDIALOG_H

#include <QDialog>
#include <QComboBox>
#include "pathplanner.h"

class PathPlanningDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PathPlanningDialog(QWidget *parent = nullptr);

    // 获取用户选择的算法
    PathPlanner::AlgorithmType getSelectedAlgorithm() const;

private:
    QComboBox* m_algoCombo;
};

#endif // PATHPLANNINGDIALOG_H
