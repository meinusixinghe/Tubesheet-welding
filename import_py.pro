QT       += core gui
# Modbus TCP
QT += serialbus serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    connectiondialog.cpp \
    main.cpp \
    mainwindow.cpp \
    modbusmanager.cpp \
    pathplanner.cpp \
    pathplanningdialog.cpp \
    renderarea.cpp \
    rotationmatrixdialog.cpp \
    usercoordinatemanager.cpp \
    vizumcamera.cpp \
    weldingprocessdialog.cpp

HEADERS += \
    connectiondialog.h \
    include/VZNL_AppUtils.h \
    include/VZNL_Common.h \
    include/VZNL_DetectConfig.h \
    include/VZNL_DetectLaser.h \
    include/VZNL_DustCover.h \
    include/VZNL_ErrorCode.h \
    include/VZNL_Export.h \
    include/VZNL_ExtLaserLight.h \
    include/VZNL_ExtStrobeLaser.h \
    include/VZNL_EyeConfig.h \
    include/VZNL_FileUtils.h \
    include/VZNL_Graphics.h \
    include/VZNL_RGBConfig.h \
    include/VZNL_SwingMotor.h \
    include/VZNL_Types.h \
    include/VZNL_Utils.h \
    mainwindow.h \
    modbusmanager.h \
    pathplanner.h \
    pathplanningdialog.h \
    renderarea.h \
    rotationmatrixdialog.h \
    usercoordinatemanager.h \
    vizumcamera.h \
    weldingprocessdialog.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    import_py_test.py \
    libs/Debug/VzKernel.dll \
    libs/Debug/VzLog.dll \
    libs/Debug/VzNLDetect.dll \
    libs/Debug/VzNLDetect.lib \
    libs/Debug/VzNLGraphics.dll \
    libs/Debug/VzNLGraphics.lib \
    libs/Release/VzKernel.dll \
    libs/Release/VzLog.dll \
    libs/Release/VzNLDetect.dll \
    libs/Release/VzNLDetect.lib \
    libs/Release/VzNLGraphics.dll \
    libs/Release/VzNLGraphics.lib \
    libs/x64/Debug/VzKernel.dll \
    libs/x64/Debug/VzLog.dll \
    libs/x64/Debug/VzNLDetect.dll \
    libs/x64/Debug/VzNLDetect.lib \
    libs/x64/Debug/VzNLGraphics.dll \
    libs/x64/Debug/VzNLGraphics.lib

win32: LIBS += -LC:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/libs/ -lpython313

INCLUDEPATH += C:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/include
DEPENDPATH += C:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/include

win32:!win32-g++: PRE_TARGETDEPS += C:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/libs/python313.lib
else:win32-g++: PRE_TARGETDEPS += C:/Users/zhangpeng/AppData/Local/Programs/Python/Python313/libs/libpython313.a

RESOURCES += \
    resources.qrc

TARGET = MXEditer

RC_ICONS=icons1.ico

# ==========================================
# 相机 SDK 配置
# ==========================================

# 1. 指定头文件搜索路径 ($$PWD 代表当前 .pro 文件所在根目录)
INCLUDEPATH += $$PWD/include
DEPENDPATH += $$PWD/include

# 2. 链接静态库 (.lib)
# 根据编译的是 Debug 还是 Release 模式，自动选择对应的库文件夹
CONFIG(debug, debug|release) {
    LIBS += -L$$PWD/libs/Debug/ -lVzNLDetect -lVzNLGraphics
} else {
    LIBS += -L$$PWD/libs/Release/ -lVzNLDetect -lVzNLGraphics
}
