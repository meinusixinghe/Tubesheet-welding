#include "mainwindow.h"
#include "pointcloudviewer.h"
#include <QApplication>
#include <QNetworkProxy>
#include <vtkOutputWindow.h>
#include <vtkFileOutputWindow.h>
#include <vtkSmartPointer.h>

int main(int argc, char *argv[])
{
    vtkSmartPointer<vtkFileOutputWindow> fileOutputWindow = vtkSmartPointer<vtkFileOutputWindow>::New();
    fileOutputWindow->SetFileName("vtk_silent_error.log");
    fileOutputWindow->SetAppend(1);
    fileOutputWindow->SetFlush(1);
    vtkOutputWindow::SetInstance(fileOutputWindow);
    QApplication a(argc, argv);
    const QStringList arguments = a.arguments();
    // Dispatch before MainWindow initializes the robot and camera.
    if (arguments.value(1) == PointCloudViewer::ModeArgument) {
        return PointCloudViewer::run(arguments.mid(2));
    }
    vtkOutputWindow::SetGlobalWarningDisplay(0);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    MainWindow w;
    w.setWindowTitle("自动焊接系统");
    w.show();
    return a.exec();
}
