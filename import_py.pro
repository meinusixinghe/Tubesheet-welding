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
    pointcloudprocessor.cpp \
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
    pointcloudprocessor.h \
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

# ==============================================================
# PCL 1.12.1 点云库环境配置
# ==============================================================
# 🌟 1. 修改为你电脑上真实的 PCL 安装路径
PCL_ROOT = D:/PCL_1.12.1

# 🌟 2. 引入 PCL 及其所有第三方巨头库的头文件
INCLUDEPATH += $$PCL_ROOT/include/pcl-1.12 \
               $$PCL_ROOT/3rdParty/Boost/include/boost-1_78 \
               $$PCL_ROOT/3rdParty/Eigen/eigen3 \
               $$PCL_ROOT/3rdParty/FLANN/include \
               $$PCL_ROOT/3rdParty/Qhull/include \
               $$PCL_ROOT/3rdParty/VTK/include/vtk-9.1

# 🌟 3. 指定 .lib 静态链接库所在的目录
LIBS += -L$$PCL_ROOT/lib \
        -L$$PCL_ROOT/3rdParty/Boost/lib \
        -L$$PCL_ROOT/3rdParty/FLANN/lib \
        -L$$PCL_ROOT/3rdParty/Qhull/lib \
        -L$$PCL_ROOT/3rdParty/VTK/lib \
        -L$$PCL_ROOT/3rdParty/OpenNI2/Lib

# 🌟 4. 按需引入 PCL 的核心模块 (为了测试，我们先只引入 common 和 io 模块)
# Qt 能够自动识别你当前是 Debug 模式还是 Release 模式，自动加载带 'd' 后缀的调试库
CONFIG(debug, debug|release) {
    LIBS += -lpcl_commond -lpcl_iod -lpcl_visualizationd \
            -lpcl_filtersd -lpcl_segmentationd -lpcl_sample_consensusd \
            -lpcl_searchd -lpcl_kdtreed
} else {
    LIBS += -lpcl_common -lpcl_io -lpcl_visualization \
            -lpcl_filters -lpcl_segmentation -lpcl_sample_consensus \
            -lpcl_search -lpcl_kdtree
}

# 🌟 5. 引入 PCL 可视化模块
VTK_LIB_PATH = $$PCL_ROOT/3rdParty/VTK/lib
VTK_LIBS_ALL = $$files($$VTK_LIB_PATH/*.lib)
VTK_LIBS_DEBUG = $$files($$VTK_LIB_PATH/*-gd.lib)

VTK_LIBS_RELEASE = $$VTK_LIBS_ALL
VTK_LIBS_RELEASE -= $$VTK_LIBS_DEBUG

CONFIG(debug, debug|release) {
    for(lib, VTK_LIBS_DEBUG) {
        LIBS += $$lib
    }
} else {
    for(lib, VTK_LIBS_RELEASE) {
        LIBS += $$lib
    }
}

# 🌟 6. 终极绝招：利用 qmake 自动遍历并链接所有的 VTK 静态库，防止 LNK2019 报错
VTK_LIB_PATH = $$PCL_ROOT/3rdParty/VTK/lib
VTK_LIBS_ALL = $$files($$VTK_LIB_PATH/*.lib)
VTK_LIBS_DEBUG = $$files($$VTK_LIB_PATH/*-gd.lib)

# 巧妙的 qmake 减法：从所有库中减去带 -gd.lib 的，剩下的就是纯 Release 库
VTK_LIBS_RELEASE = $$VTK_LIBS_ALL
VTK_LIBS_RELEASE -= $$VTK_LIBS_DEBUG

CONFIG(debug, debug|release) {
    # Debug 模式下，只链接带 -gd 尾缀的库
    for(lib, VTK_LIBS_DEBUG) {
        LIBS += $$lib
    }
} else {
    # Release 模式下，只链接不带 -gd 尾缀的库
    for(lib, VTK_LIBS_RELEASE) {
        LIBS += $$lib
    }
}
