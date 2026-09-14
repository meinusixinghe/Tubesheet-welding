#include "weldingprocessdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

// =============================================================================
// 内部辅助类：简单的编辑子对话框
// =============================================================================
class ProcessEditorDialog : public QDialog {
public:
    // 编辑框成员（供外部读写数据）
    QLineEdit* idEdit;
    QLineEdit* strategyEdit;
    QLineEdit* remarkEdit;

    ProcessEditorDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setMinimumSize(300, 100);                                                       // 设置对话框最小大小，避免挤压

        QFormLayout* layout = new QFormLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);
        idEdit = new QLineEdit(this);
        strategyEdit = new QLineEdit(this);
        remarkEdit = new QLineEdit(this);

        layout->addRow("工艺编号:", idEdit);
        layout->addRow("焊接策略:", strategyEdit);
        layout->addRow("备注:", remarkEdit);

        // 对话框按钮盒
        QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(btnBox);

        connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    // 加载数据（用于“修改工艺”时，显示原有数据）
    void loadData(const WeldingProcess& p) {
        idEdit->setText(p.id);
        strategyEdit->setText(p.strategy);
        remarkEdit->setText(p.remark);
    }

    // 获取数据（用于“添加/修改”后，回读用户输入）
    WeldingProcess getData() const {
        return { idEdit->text(), strategyEdit->text(), remarkEdit->text() };
    }
};

// =============================================================================
// 主管理对话框实现
// =============================================================================
WeldingProcessDialog::WeldingProcessDialog(QVector<WeldingProcess>* processList, QWidget *parent)
    : QDialog(parent), m_processList(processList)
{
    setWindowTitle("焊接工艺管理");
    setMinimumSize(600, 400);
    setupUi();                                                                      // 初始化界面布局
    refreshTable();                                                                 // 初始化表格数据
}

// =============================================================================
// 初始化界面布局
// =============================================================================
void WeldingProcessDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 1. 表格区域
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->verticalHeader()->setVisible(false);                                   // 隐藏行头
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);        // 让列宽自动平分填满窗口
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);                   // 选中整行
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);                  // 单选模式
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);                    // 禁止直接编辑
    m_table->setFocusPolicy(Qt::NoFocus);                                           // 去掉选中时的虚线框
    m_table->setShowGrid(false);                                                    // 隐藏网格线 (配合交替背景色更现代)
    m_table->setAlternatingRowColors(true);                                         // 开启交替行背景色
    m_table->setStyleSheet(R"(
        QTableWidget {
            background-color: #FFFFFF;
            alternate-background-color: #FAFAFA;  /* 交替行的颜色(极淡的灰) */
            border: 1px solid #E0E0E0;            /* 整体边框 */
            border-radius: 6px;                   /* 圆角 */
        }
        QHeaderView::section {
            background-color: #FFFFFF;            /* 表头背景白 */
            border: none;                         /* 去掉默认立体边框 */
            border-bottom: 2px solid #2196F3;     /* 核心：添加蓝色下边框 (可改颜色) */
            padding: 6px;
            font-weight: bold;                    /* 字体加粗 */
            color: #333333;
        }
        QTableWidget::item {
            padding: 8px;                         /* 增加单元格内边距，不那么拥挤 */
            border-bottom: 1px solid #F0F0F0;     /* 只有下边框，形成分割线效果 */
            color: #333333;                       /* 字体颜色 */
        }
        QTableWidget::item:selected {
            background-color: #E3F2FD;            /* 选中时的背景色(淡蓝) */
            color: #1976D2;                       /* 选中时的文字颜色(深蓝) */
        }
    )");

    m_table->setHorizontalHeaderLabels({"工艺编号", "焊接策略", "备注"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); // 整行选中
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 禁止直接双击编辑
    mainLayout->addWidget(m_table);

    // 2. 按钮区域
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnAdd = new QPushButton("添加工艺", this);
    QPushButton* btnEdit = new QPushButton("修改选中", this);
    QPushButton* btnDelete = new QPushButton("删除选中", this);
    QPushButton* btnSave = new QPushButton("保存数据", this);
    QPushButton* btnClose = new QPushButton("关闭", this);
    // 美化按钮
    btnAdd->setStyleSheet("background-color: #4CAF50; color: white; padding: 5px 10px; border-radius: 4px;");
    btnEdit->setStyleSheet("background-color: #2196F3; color: white; padding: 5px 10px; border-radius: 4px;");
    btnDelete->setStyleSheet("background-color: #F44336; color: white; padding: 5px 10px; border-radius: 4px;");
    btnSave->setStyleSheet("background-color: #FF9800; color: white; padding: 5px 10px; border-radius: 4px;");
    btnClose->setStyleSheet("background-color: #F44336; color: white; padding: 5px 10px; border-radius: 4px;");

    // 按钮加入水平布局：添加/修改/删除靠左，关闭靠右
    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnEdit);
    btnLayout->addWidget(btnDelete);
    btnLayout->addWidget(btnSave);
    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    // 3. 信号连接
    connect(btnAdd, &QPushButton::clicked, this, &WeldingProcessDialog::onAddProcess);
    connect(btnEdit, &QPushButton::clicked, this, &WeldingProcessDialog::onEditProcess);
    connect(btnDelete, &QPushButton::clicked, this, &WeldingProcessDialog::onDeleteProcess);
    connect(btnSave, &QPushButton::clicked, this, &WeldingProcessDialog::onSaveProcess);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
}

// =============================================================================
// 刷新表格数据
// =============================================================================
void WeldingProcessDialog::refreshTable()
{
    if (!m_processList) return;

    m_table->setRowCount(0); // 清空表格

    // 遍历工艺列表，逐行添加到表格
    for (int i = 0; i < m_processList->size(); ++i) {
        const auto& p = (*m_processList)[i];
        m_table->insertRow(i);
        auto createCenteredItem = [](const QString& text) {
            QTableWidgetItem* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);                        // 设置居中对齐
            return item;
        };
        m_table->setItem(i, 0, createCenteredItem(p.id));
        m_table->setItem(i, 1, createCenteredItem(p.strategy));
        m_table->setItem(i, 2, createCenteredItem(p.remark));
    }
}

// ------------------------------------
// 槽函数(添加工艺)
// ------------------------------------
void WeldingProcessDialog::onAddProcess()
{
    WeldingProcess newProcess;
    if (openProcessEditor(newProcess, "添加新工艺")) {
        // 简单查重
        const auto& processList = *m_processList;
        for (const auto& p : processList) {
            if (p.id == newProcess.id) {
                QMessageBox::warning(this, "错误", "工艺编号已存在！");
                return;
            }
        }
        m_processList->append(newProcess);
        refreshTable();
    }
}

// ------------------------------------
// 槽函数(修改选中工艺)
// ------------------------------------
void WeldingProcessDialog::onEditProcess()
{
    int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择一行要修改的数据。");
        return;
    }

    // 获取当前行对应的数据
    WeldingProcess& currentProcess = (*m_processList)[row];
    QString oldId = currentProcess.id;

    // 打开编辑
    if (openProcessEditor(currentProcess, "修改工艺")) {
        // 如果改了ID，检查是否冲突（排除自己）
        if (currentProcess.id != oldId) {
            for (int i = 0; i < m_processList->size(); ++i) {
                if (i != row && (*m_processList)[i].id == currentProcess.id) {
                    QMessageBox::warning(this, "错误", "新的工艺编号与其他工艺冲突！");
                    currentProcess.id = oldId; // 恢复
                    return;
                }
            }
        }
        refreshTable();
    }
}

// ------------------------------------
// 槽函数(删除选中工艺)
// ------------------------------------
void WeldingProcessDialog::onDeleteProcess()
{
    int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择一行要删除的数据。");
        return;
    }

    auto btn = QMessageBox::question(this, "确认删除",
                                     QString("确定要删除工艺编号 [%1] 吗？").arg((*m_processList)[row].id),
                                     QMessageBox::Yes | QMessageBox::No);
    if (btn == QMessageBox::Yes) {
        m_processList->removeAt(row);
        refreshTable();
    }
}

// ------------------------------------
// 打开编辑对话框
// ------------------------------------
bool WeldingProcessDialog::openProcessEditor(WeldingProcess& process, const QString& title)
{
    ProcessEditorDialog dlg(this);                                                  // 创建编辑子对话框
    dlg.setWindowTitle(title);                                                      // 设置对话框标题
    dlg.loadData(process);                                                          // 加载初始数据

    if (dlg.exec() == QDialog::Accepted) {
        WeldingProcess input = dlg.getData();
        if (input.id.isEmpty()) {
            QMessageBox::warning(this, "警告", "工艺编号不能为空");
            return false;
        }
        process = input; // 写回数据
        return true;
    }
    return false;
}

void WeldingProcessDialog::onSaveProcess()
{
    if (!m_processList) return;

    // 路径：exe 同级目录下的 welding_processes.json
    QString path = QCoreApplication::applicationDirPath() + "/welding_processes.json";
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件进行写入，请检查权限。");
        return;
    }

    // 序列化数据
    QJsonArray arr;
    for (const auto& p : *m_processList) {
        QJsonObject obj;
        obj["id"] = p.id;
        obj["strategy"] = p.strategy;
        obj["remark"] = p.remark;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    file.write(doc.toJson());
    file.close();

    QMessageBox::information(this, "成功", "工艺数据已成功保存！\n下次启动软件时将自动加载。");
}
