# 自动焊接系统

Qt 桌面上位机：导入 DXF 管板图纸、建立用户坐标系、规划管孔焊接顺序，通过 Modbus TCP 下发任务；支持 VZNL 相机采集及 PCL 点云孔位识别。

## 代码位置

| 文件 | 职责 |
| --- | --- |
| `main.cpp` | 程序入口 |
| `mainwindow.cpp` | 初始化、工艺加载、机器人连接和焊接流程调度 |
| `mainwindow_ui.cpp` | 界面布局、工具栏、状态栏和信号绑定 |
| `mainwindow_drawing.cpp` | DXF 导入、表格联动、坐标转换和规划操作 |
| `mainwindow_vision.cpp` | 点云分析、孔位匹配、查看器启动和图片保存路径 |
| `drawingdata.h` | 图纸模块共用的 `Hole`、`Contour` 数据结构 |
| `runtimepaths.*` | Python 解释器的查找规则 |
| `pointcloudviewer.*` | 项目内的独立点云查看模式、文件加载和 VTK 后端注册 |
| `officialshapedetector.*`、`officialshapedialog.*` | 厂商圆孔/圆柱算法调用、直径参数及检测结果窗口 |
| `pathplanner.*` | 七种管孔焊接顺序算法 |
| `usercoordinatemanager.*`、`rotationmatrixdialog.*` | 用户坐标系向导和旋转矩阵输入 |
| `renderarea.*` | 二维管板绘制、缩放、拖动和状态高亮 |
| `modbusmanager.*`、`connectiondialog.*` | Modbus 通信状态机和连接设置 |
| `weldingprocessdialog.*` | 焊接工艺编辑及保存 |
| `vizumcamera.*`、`pointcloudprocessor.*` | 相机采集和点云识别算法 |
| `config/camera.pri`、`config/pcl.pri` | 相机 SDK 和 PCL 构建依赖 |
| `config/circle.pri`、`third_party/vizum_circle/` | 官方圆孔/圆柱算法 SDK 1.1.3.1，采用项目相对路径 |
| `import_py_test.py` | DXF → JSON 转换脚本，文件名沿用原项目命名 |
| `tests/` | 路径查找、PCL 配置、DXF 转换和点云渲染测试 |

## 新电脑准备

- Windows 64 位、Qt 6.5.3 的 **MSVC2019 64 位** Kit、Visual Studio C++ 工具链。
- PCL 1.12.1 的 MSVC SDK，包含 Boost 1.78、VTK 9.1 等随附依赖。当前配置仍对应这套 SDK 的目录结构。
- Python 3 和 `ezdxf`。Python 只作为独立进程使用，无需 Python 开发头文件或 `.lib`，不再绑定 Python 3.13。
- 保留项目中的 `include/`、`libs/Debug/`、`libs/Release/`，它们是相机 SDK。
- 保留 `third_party/vizum_circle/`：已随项目放入厂商算法的原始头文件、导入库、算法 DLL、配套 OpenCV/PCL 运行库和 `log.conf`，不需要配置原 SDK 的本机安装路径。

迁移时在新电脑重新生成 `build/` 和 `*.pro.user`，由新电脑的 Qt Creator 保存 Kit、构建目录等本机设置。

在项目目录安装 Python 依赖：

```powershell
python -m pip install -r requirements.txt
```

若有多个 Python，请使用后面 `PYTHON_EXECUTABLE` 指向的解释器安装依赖。

## 路径配置

### PCL

设置 `PCL_ROOT` 为新电脑的 SDK 安装目录。下面是路径示例，按实际位置填写：

```powershell
$env:PCL_ROOT = 'C:\SDK\PCL_1.12.1'
```

构建配置优先读取 qmake 参数，其次读取环境变量。可以在 Qt Creator 的“构建步骤 → qmake → 附加参数”填写：

```text
PCL_ROOT=C:/SDK/PCL_1.12.1
```

路径中有空格时，将整个参数包在双引号中，例如 `"PCL_ROOT=C:/Program Files/PCL 1.12.1"`。
`PCL_ROOT` 控制编译时的 SDK 位置；运行时仍需按下文部署 PCL 和 VTK 的 DLL。
未设置或 SDK 路径无效时，qmake 会直接提示修正 `PCL_ROOT`。

### Python

默认依次在 `PATH` 中查找 `python`、`python3`。需要指定某个解释器时设置：

```powershell
$env:PYTHON_EXECUTABLE = 'C:\Tools\Python\python.exe'
& $env:PYTHON_EXECUTABLE -m pip install -r requirements.txt
```

该变量填写解释器路径，不带外层引号或命令行参数。若设置了无效路径，程序会提示修正，避免意外使用另一个 Python 环境。

PowerShell 的 `$env:` 设置只影响当前终端及其启动的程序。日常使用可在 Windows“环境变量”或 Qt Creator 的构建/运行环境中设置；修改后重启 Qt Creator 和应用。

## 构建和运行

在 Qt Creator 打开 `import_py.pro`，选择 MSVC2019 64 位 Kit，配置 `PCL_ROOT`，重新运行 qmake，再构建。
相机 SDK 和 PCL 库不能与 MinGW Kit 混用。

命令行构建需先进入已加载 MSVC x64 工具链、并将对应 Qt `bin` 加入 `PATH` 的终端：

```powershell
New-Item -ItemType Directory -Path build/local -Force
Set-Location build/local
qmake ../../import_py.pro
nmake release
```

程序输出到构建目录下的 `release/` 或 `debug/`；构建会自动把 `import_py_test.py` 复制到可执行文件旁边。
构建也会将 `config/circle.pri` 中的官方算法运行文件复制到该目录：
`VzCircleDetectAlgoLib.dll`、七个 `opencv_*450.dll`、四个 `pcl_*_release.dll` 和 `log.conf`。
这些配套库供相机 SDK 动态加载内部算法使用；项目使用的 PCL 1.12 DLL 名称不同，不能替代它们。
算法包中的 `opencv_highgui450.dll` 还依赖 `opencv_videoio450.dll`；后者从厂商 `VzHJCSSoftWare`
相机软件中补齐，其配套的核心 OpenCV 文件与算法包相同，已通过独立目录的原生加载验证。

运行和分发时还需满足原生 DLL 依赖：

- Qt DLL：可使用同一 Kit 的 `windeployqt` 部署。
- 相机 DLL：把对应 Debug/Release 的 `libs` 目录中的 DLL 放在可执行文件旁边。
- PCL 及其依赖 DLL：部署到程序目录，或把对应 PCL、FLANN、VTK、OpenNI2 的运行库目录加入 `PATH`。
- Python：目标电脑安装 Python 和 `ezdxf`，按上述规则配置。

点云查看使用项目自身的 `MXEditer.exe`，无需另行查找或部署 `pcl_viewer.exe`。
点击查看按钮会以 `--pointcloud-viewer` 模式启动独立进程，在创建主窗口之前分流，因此不会再次初始化机器人或相机。
查看模式注册 VTK 的 OpenGL、交互、字体和二维上下文后端，保留多层点云颜色、点大小及鼠标旋转、缩放和平移。
读取失败或空点云会显示具体提示；运行输出写入 `pointcloud_viewer.log`，VTK 诊断写入 `vtk_silent_error.log`。
机器人连接和图片保存目录保存在程序目录的 `config.ini`；工艺记录保存在 `welding_processes.json`。

## 官方圆孔与圆柱检测

1. 切换到“视觉”，点击“打开设备”连接相机。
2. 完成采图并停止扫描，或准备已有的 PCD / 官方 TXT 点云文件。
3. 点击“官方圆孔检测”或“官方圆柱检测”，选择文件，填写最小、最大目标直径（mm）。默认 20～100 mm 沿用官方示例，可根据工件修改。
4. 点击“开始检测”，结果表显示目标数量、相机坐标下的中心 XYZ、直径及 SDK 返回的法向量。

两个入口分别调用厂商 `CircleDetect()`、`CylinderDetect()`；圆柱方法的 `bNoPlane` 保持官方默认 `false`。
输入通过厂商文件接口读取，按官方示例组织为 `SVzNL3DLaserLine`；非有限坐标点不送入算法。
结果复制完成后调用 `DetectResRelease()`，检测会话结束时依次 `Release()`、销毁算法对象。

厂商说明书要求用已打开相机的设备句柄执行 `Init()`，因此本地文件检测也需要相机保持连接。
未连接相机或正在扫描时，这两个检测按钮禁用。坐标结果使用相机坐标系。
若 SDK 返回零法向量，界面原样显示，不将其解释为有效方向。

该算法包只有 Release x64 库，Debug/Release 构建均使用此算法组件；相机基础库仍按原构建模式选择。
原始包基于 VZNLSDK 3.6.20.79、内部算法 1.1.3；当前项目沿用自身相机 SDK。
官方说明允许复用已有 VZNLSDK；若设备初始化返回版本不匹配，界面显示具体错误码，需向厂商获取适配的算法包。
算法构造时会读取工作目录的 `log.conf`，检测会话临时使用程序目录并在结束时恢复原目录。

若初始化提示错误码 4，检查厂商日志 `myeasylog.txt`。其中 `QueryAlgo Failed: err: -79935`
表示相机 SDK 未找到内部库；先检查上述官方运行文件是否完整部署在程序目录。
确认依赖可加载后仍失败时，需核对相机 SDK 与内部算法版本；修改直径范围或点云文件不能解决初始化失败。
2026-10-15 的实机对照中，项目 SDK 3.6.20.120 和原厂配套 SDK 3.6.20.79 按官方初始化顺序
均复现 `-79935`，需要厂商核查相机是否能提供 `litiyuankongjiance` 内部组件（版本不低于 1.1.3）。
具体返回值与日志位置见 [初始化失败诊断](docs/official-shape-initialization-diagnosis.md)。

## 验证

在已配置工具链、Qt 和 Python 的终端，从项目根目录运行 Python 测试：

```powershell
python -B -m unittest discover -s tests -p 'test_*.py' -v
```

PCL 配置测试使用 `PATH` 中的 qmake，也可通过 `QMAKE` 环境变量指定 qmake 可执行文件。
测试会创建临时 SDK 目录，检查带空格的路径、qmake 参数优先级、Debug/Release 库选择及无效路径提示。
官方运行库测试会按实际 qmake 部署清单创建独立目录，仅使用该目录与 Windows 系统库加载 OpenCV/PCL 依赖，
检查运行时是否错误依赖原 SDK 的安装路径。

Qt 路径查找测试：

```powershell
New-Item -ItemType Directory -Path build/runtimepaths-tests -Force
Set-Location build/runtimepaths-tests
qmake ../../tests/runtimepaths.pro
nmake release
./release/tst_runtimepaths.exe -o results.txt,txt
Get-Content results.txt
```

点云查看器测试（同样需要配置 PCL/VTK DLL 的运行路径）：

```powershell
New-Item -ItemType Directory -Path build/pointcloudviewer-tests -Force
Set-Location build/pointcloudviewer-tests
qmake ../../tests/pointcloudviewer.pro
nmake release
./release/tst_pointcloudviewer.exe -o results.txt,txt
Get-Content results.txt
```

测试覆盖 VTK 后端注册、多层颜色和点大小、实际渲染、鼠标旋转/缩放/平移、无颜色点云以及空数据和读取失败。

官方检测接入测试（运行环境同样需要 Qt、PCL 和对应模式的相机 DLL）：

```powershell
New-Item -ItemType Directory -Path build/officialshapes-tests -Force
Set-Location build/officialshapes-tests
qmake ../../tests/officialshapes.pro
nmake release
./release/tst_officialshapes.exe -o results.txt,txt
Get-Content results.txt
```

测试使用真实厂商文件读取接口验证 PCD / 官方 TXT 输入、直径参数、结果复制/释放及界面错误状态；
依赖实体相机的检测计算用官方抽象类的测试实现替代，因此不代表带相机的识别准确率验证。
测试不连接机器人或相机。

本次受限执行环境中，MSVC 的 `/Zi` 与 `/FS` 组合触发了 `C1902`，最小 C++ 程序也能复现。
Debug 编译验证临时使用 qmake 参数 `QMAKE_CXXFLAGS-=-FS` 后通过；该参数未写入项目的默认配置。
