#include "rotationmatrixdialog.h"
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMatrix4x4>

RotationMatrixDialog::RotationMatrixDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("输入旋转矩阵");

    QGridLayout* layout = new QGridLayout(this);

    // 创建输入框
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            matrixInputs[i][j] = new QLineEdit("0", this);
            if (i == j) {
                matrixInputs[i][j]->setText("1"); // 初始化为单位矩阵
            }
            layout->addWidget(matrixInputs[i][j], i, j);
        }
    }

    // 添加确认和取消按钮
    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    layout->addWidget(buttonBox, 3, 0, 1, 3);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (validateInputs()) {
            accept();
        }
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QMatrix4x4 RotationMatrixDialog::getRotationMatrix() const
{
    QMatrix4x4 matrix;

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            matrix(i, j) = matrixInputs[i][j]->text().toDouble();
        }
    }

    return matrix;
}

// ----------------------------------------------------
// 输入有效性验证
// 解决输入错误的问题
// ----------------------------------------------------
bool RotationMatrixDialog::validateInputs()
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            bool ok;
            matrixInputs[i][j]->text().toDouble(&ok);
            if (!ok) {
                QMessageBox::warning(this, "输入错误",
                                     QString("请在第%1行第%2列输入有效的数字").arg(i+1).arg(j+1));
                matrixInputs[i][j]->setFocus();
                return false;
            }
        }
    }
    return true;
}
