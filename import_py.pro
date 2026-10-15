QT += core gui widgets serialbus serialport network
CONFIG += c++17
TARGET = MXEditer
RC_ICONS = icons1.ico

# The supplied camera and PCL SDK libraries require the Windows MSVC kit.
!win32: error("This project requires Windows and MSVC.")
win32-g++: error("Select a Qt MSVC 64-bit kit; the supplied SDK libraries are not MinGW libraries.")

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    mainwindow_ui.cpp \
    mainwindow_drawing.cpp \
    mainwindow_vision.cpp \
    runtimepaths.cpp \
    connectiondialog.cpp \
    modbusmanager.cpp \
    pathplanner.cpp \
    pathplanningdialog.cpp \
    pointcloudprocessor.cpp \
    pointcloudviewer.cpp \
    officialshapedetector.cpp \
    officialshapedialog.cpp \
    renderarea.cpp \
    rotationmatrixdialog.cpp \
    usercoordinatemanager.cpp \
    vizumcamera.cpp \
    weldingprocessdialog.cpp

HEADERS += \
    mainwindow.h \
    drawingdata.h \
    runtimepaths.h \
    connectiondialog.h \
    modbusmanager.h \
    pathplanner.h \
    pathplanningdialog.h \
    pointcloudprocessor.h \
    pointcloudviewer.h \
    officialshapedetector.h \
    officialshapedialog.h \
    renderarea.h \
    rotationmatrixdialog.h \
    usercoordinatemanager.h \
    vizumcamera.h \
    weldingprocessdialog.h

FORMS += mainwindow.ui
RESOURCES += resources.qrc
DISTFILES += import_py_test.py requirements.txt README.md

include(config/camera.pri)
include(config/pcl.pri)
include(config/circle.pri)

# Keep the DXF converter next to the executable, including shadow builds.
CONFIG(debug, debug|release) {
    BUILD_MODE = debug
} else {
    BUILD_MODE = release
}
# Keep DESTDIR relative: qmake substitutes debug/release names in absolute paths.
isEmpty(DESTDIR): DESTDIR = $$BUILD_MODE
PRE_TARGETDEPS += $$PWD/import_py_test.py
QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$PWD/import_py_test.py)) $$shell_quote($$shell_path($$DESTDIR/import_py_test.py))
# Relink when the deployment list changes, even if vendor binaries are older.
PRE_TARGETDEPS += $$PWD/config/circle.pri $$CIRCLE_RUNTIME_FILES
for(file, CIRCLE_RUNTIME_FILES) {
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) $$QMAKE_COPY $$shell_quote($$shell_path($$file)) $$shell_quote($$shell_path($$DESTDIR))
}
