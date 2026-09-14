#ifndef ROTATIONMATRIXDIALOG_H
#define ROTATIONMATRIXDIALOG_H

#include <QDialog>
#include <QVector3D>
#include <QMatrix4x4>
#include <QLineEdit>

class RotationMatrixDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RotationMatrixDialog(QWidget *parent = nullptr);

    // 获取用户输入的旋转矩阵
    QMatrix4x4 getRotationMatrix() const;

private:
    // 用于输入矩阵元素的 9个输入框
    QLineEdit* matrixInputs[3][3];

    // 验证输入是否为有效的数字
    bool validateInputs();
};

#endif // ROTATIONMATRIXDIALOG_H
