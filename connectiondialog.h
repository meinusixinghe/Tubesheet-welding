#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QString>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <vector>
#include "VZNL_Common.h"

class ConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ~ConnectionDialog();

    QString getIp() const;
    int getPort() const;

    void setIp(const QString& ip);
    void setPort(int port);

    int getAction() const;                                      // 获取用户的操作类型 (1: 连接, 2: 断开连接)
    VZNLHANDLE getCameraHandle() const { return m_hCamera; }

private slots:
    void onConnectRobotClicked();
    void onSearchCameraClicked();
    void onConnectCameraClicked();

private:
    QLineEdit *ipLineEdit;
    QLineEdit *portLineEdit;
    QPushButton *connectBtn;
    QPushButton *cancelBtn;

    QComboBox *m_cameraCombo;
    QPushButton *m_searchCamBtn;
    QPushButton *m_connectCamBtn;
    QLabel *m_camStatusLabel;
    VZNLHANDLE m_hCamera;                     // 相机句柄
    std::vector<SVzNLEyeCBInfo> m_cameraList; // 存储搜索到的设备列表
    int m_action;
};

#endif // CONNECTIONDIALOG_H
