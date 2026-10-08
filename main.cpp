#include "mainwindow.h"
#include <QApplication>
#include <QNetworkProxy>
#include <vtkOutputWindow.h>
#include <vtkFileOutputWindow.h>
#include <vtkSmartPointer.h>

int main(int argc, char *argv[])
{
    vtkOutputWindow::SetGlobalWarningDisplay(0);
    vtkSmartPointer<vtkFileOutputWindow> fileOutputWindow = vtkSmartPointer<vtkFileOutputWindow>::New();
    fileOutputWindow->SetFileName("vtk_silent_error.log"); // 报错信息全被静默塞进这个文件
    fileOutputWindow->SetAppend(1);
    vtkOutputWindow::SetInstance(fileOutputWindow);
    QApplication a(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    MainWindow w;
    w.setWindowTitle("自动焊接系统");
    w.show();
    return a.exec();
}
