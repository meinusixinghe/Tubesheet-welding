#include "officialshapedialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableWidget>
#include <QVBoxLayout>
#include <exception>

OfficialShapeDialog::OfficialShapeDialog(VZNLHANDLE device, OfficialShapeDetection::Type type,
                                         const QString& directory, QWidget* parent)
    : QDialog(parent), m_device(device), m_type(type)
{
    setWindowTitle(type == OfficialShapeDetection::Type::Circle ? "官方圆孔检测" : "官方圆柱检测");
    resize(700, 440);
    auto layout = new QVBoxLayout(this);
    auto hint = new QLabel("请选择相机采集的点云文件，并设置目标直径范围。检测期间请保持相机连接。", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto form = new QFormLayout();
    auto fileRow = new QHBoxLayout();
    m_filePath = new QLineEdit(this);
    m_filePath->setObjectName("officialShapeFilePath");
    m_filePath->setReadOnly(true);
    m_filePath->setPlaceholderText("PCD 或官方 TXT 点云文件");
    auto browse = new QPushButton("选择文件…", this);
    fileRow->addWidget(m_filePath);
    fileRow->addWidget(browse);
    form->addRow("点云文件：", fileRow);
    auto diameterRow = new QHBoxLayout();
    m_minDiameter = new QDoubleSpinBox(this);
    m_maxDiameter = new QDoubleSpinBox(this);
    m_minDiameter->setObjectName("officialMinDiameter");
    m_maxDiameter->setObjectName("officialMaxDiameter");
    for (auto spin : {m_minDiameter, m_maxDiameter}) {
        spin->setRange(0.001, 10000);
        spin->setDecimals(3);
        spin->setSuffix(" mm");
    }
    m_minDiameter->setValue(20);
    m_maxDiameter->setValue(100);
    diameterRow->addWidget(m_minDiameter);
    auto separator = new QLabel("至", this);
    separator->setAlignment(Qt::AlignCenter);
    separator->setFixedWidth(24);
    diameterRow->addWidget(separator);
    diameterRow->addWidget(m_maxDiameter);
    form->addRow("目标直径：", diameterRow);
    layout->addLayout(form);

    m_results = new QTableWidget(0, 4, this);
    m_results->setObjectName("officialDetectionResults");
    m_results->setHorizontalHeaderLabels({"序号", "中心 XYZ (mm)", "直径 (mm)", "法向量"});
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->verticalHeader()->hide();
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_results->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_results);
    m_status = new QLabel("等待检测。坐标为相机坐标，单位 mm。", this);
    m_status->setObjectName("officialDetectionStatus");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    auto detect = buttons->addButton("开始检测", QDialogButtonBox::ActionRole);
    detect->setObjectName("runOfficialDetection");
    detect->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(detect, &QPushButton::clicked, this, &OfficialShapeDialog::runDetection);
    connect(browse, &QPushButton::clicked, this, [this, directory]() {
        const QString path = QFileDialog::getOpenFileName(this, "选择点云文件", directory,
                                                         "点云文件 (*.pcd *.txt)");
        if (!path.isEmpty()) m_filePath->setText(path);
    });
    connect(m_filePath, &QLineEdit::textChanged, detect, [detect](const QString& path) {
        detect->setEnabled(!path.isEmpty());
    });
}

void OfficialShapeDialog::runDetection()
{
    m_results->setRowCount(0);
    m_status->setStyleSheet("");
    m_status->setText("正在执行官方检测，请稍候…");
    setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto restoreUi = qScopeGuard([this] {
        QApplication::restoreOverrideCursor();
        setEnabled(true);
    });
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QVector<OfficialShapeDetection::Feature> features;
    QString error;
    bool ok = false;
    try {
        ok = OfficialShapeDetection::detect(m_device, m_type, m_filePath->text(),
                                             m_minDiameter->value(), m_maxDiameter->value(), features, error);
    } catch (const std::exception& e) {
        error = "官方检测出现异常：" + QString::fromLocal8Bit(e.what());
    }
    if (!ok) {
        m_status->setStyleSheet("color: #B71C1C;");
        m_status->setText(error);
        return;
    }

    const auto vectorText = [](const SVzNL3DPoint& point, int decimals) {
        return QString("(%1, %2, %3)").arg(point.x, 0, 'f', decimals)
                   .arg(point.y, 0, 'f', decimals).arg(point.z, 0, 'f', decimals);
    };
    m_results->setRowCount(features.size());
    for (int row = 0; row < features.size(); ++row) {
        const auto& feature = features[row];
        m_results->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_results->setItem(row, 1, new QTableWidgetItem(vectorText(feature.center, 3)));
        m_results->setItem(row, 2, new QTableWidgetItem(QString::number(feature.diameter, 'f', 3)));
        m_results->setItem(row, 3, new QTableWidgetItem(vectorText(feature.normal, 6)));
    }
    m_status->setText(QString("检测完成，检出 %1 个目标。坐标为相机坐标，单位 mm。").arg(features.size()));
}
