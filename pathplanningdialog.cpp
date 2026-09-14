#include "pathplanningdialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

PathPlanningDialog::PathPlanningDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("自动焊接路径规划");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(400);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    QLabel* titleLabel = new QLabel("路径规划算法选择", this);
    QFont font = titleLabel->font();
    font.setBold(true);
    font.setPointSize(14);
    titleLabel->setFont(font);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 信息/提示标签
    QLabel* infoLabel = new QLabel("请从下方列表中选择一种焊接顺序算法，系统将自动重新计算管孔编号：", this);
    infoLabel->setWordWrap(true);
    infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoLabel->setStyleSheet(R"(
        QLabel {
            color: blue;
            font-size: 14px;
            padding: 10px;
            line-height: 1.5;
        }
    )");
    infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(infoLabel);

    m_algoCombo = new QComboBox(this);
    m_algoCombo->setFixedHeight(35);
    m_algoCombo->addItem("1. 中心向外扩展式 (Center Outward)", PathPlanner::CenterOutward);
    m_algoCombo->addItem("2. 行对称交替式 (Row Symmetric)", PathPlanner::RowSymmetric);
    m_algoCombo->addItem("3. 行优先之字形 (Zig-Zag)", PathPlanner::RowZigZag);
    m_algoCombo->addItem("4. 四象限分区 (Four Quadrants)", PathPlanner::FourQuadrants);
    m_algoCombo->addItem("5. 条状分区 (Vertical Strips)", PathPlanner::VerticalStrips);
    m_algoCombo->addItem("6. 跳点焊接 (Skip Row)", PathPlanner::SkipRow);
    m_algoCombo->addItem("7. 螺旋式 (Spiral)", PathPlanner::Spiral);
    m_algoCombo->setStyleSheet("QComboBox { padding: 5px; font-size: 14px; }");
    layout->addWidget(m_algoCombo);
    layout->addStretch();

    // 按钮
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnOk = new QPushButton("开始规划", this);
    btnOk->setMinimumHeight(25);
    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setMinimumHeight(25);
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    layout->addLayout(btnLayout);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

PathPlanner::AlgorithmType PathPlanningDialog::getSelectedAlgorithm() const
{
    return static_cast<PathPlanner::AlgorithmType>(m_algoCombo->currentData().toInt());
}
