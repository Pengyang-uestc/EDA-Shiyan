# 实验一 B 部分：图形与交互

本目录在 A 已实现的 `core/` 基础上增加 B 的 Qt Widgets 应用层。B 不修改 A 的封装模型和文件格式，也不代替 C 实现封装工艺规则、标准模板、自动布局、布线和 DRC。

## 已实现范围

- `QMainWindow`、菜单、工具栏、状态栏和停靠面板。
- 元件库面板：搜索、刷新、导入和双击打开。
- 属性面板：显示封装信息，编辑焊盘编号、形状、坐标、尺寸、钻孔、阻焊、间距、圆角和图层。
- 图层面板：可见性、当前层和颜色标识。
- 中央 `CanvasWidget`：使用 `QPainter` 绘制网格、标尺、焊盘、钻孔、安装孔和全部丝印图元。
- 模型 Y 向上、QPainter Y 向下，通过统一 `QTransform` 转换，不直接混用两套坐标。
- 滚轮缩放、中键或空格加左键平移、适合窗口、网格吸附。
- 单击选择、Ctrl 多选、框选、拖动焊盘、拖动选中框右下角控制点调整尺寸。
- 删除焊盘及其关联引脚、属性编辑、添加焊盘/引脚/安装孔。
- 交互添加丝印线、矩形、多边形、文字，对话框添加丝印圆弧。
- `QUndoStack` 撤销/重做，所有用户文档编辑都通过 `EdaCommand`。
- New/Open 成功后替换文档并清空撤销栈，Save 使用注入的 `IValidationService` 校验，再由 A 原子写入。
- 已接入 A 的 `Board`：支持 `.brd` 打开/保存、板框、层组、实例、走线、过孔、覆铜和多层可见性渲染。
- 已接入 A 的 `COMP/NET` 网表导入：验证元件库和引脚映射后生成/更新 Board 实例与网络。
- PCB 模式支持实例、走线、过孔、覆铜的单击/框选、拖动、删除和撤销/重做。
- `ITool` 接口及 `CanvasWidget::setTool()` 扩展点，供 C 接入布局、布线、覆铜和 DRC 工具。
- `ILibraryService`、`ILayerService`、`IValidationService` 抽象接口和构造函数注入。
- `LocalLibraryService`、`LocalLayerService`、`StructuralValidationService` 作为 C 服务接入前的本地适配器。

## 主要文件

| 文件 | 责任 |
|---|---|
| `app/document_context.h/.cpp` | 会话状态、选择集、信号和命令入口 |
| `app/document.h/documents.cpp` | 封装文档、活动层、网格和撤销栈 |
| `app/main_window.h/.cpp` | 主窗口、菜单、工具栏、文件操作和命令构造 |
| `app/panels.h/.cpp` | 元件库、属性和图层面板 |
| `app/commands.h` | `EdaCommand` 基类 |
| `app/services.h` | 库、图层、校验服务抽象接口和模型角色 |
| `app/local_services.h/.cpp` | 基于 A 低层接口的可运行本地服务适配器 |
| `app/demo_documents.h/.cpp` | 可重复的 PCB 演示板和视觉/交互测试夹具 |
| `render/tool.h` | `ITool`、`ToolEvent`、`CanvasView` |
| `render/canvas.h/.cpp` | QPainter 画布、坐标转换、拾取和交互 |
| `feat_fp/basic_commands.h/.cpp` | 当前 UI 使用的快照式封装编辑命令 |
| `feat_pcb/board_commands.h/.cpp` | Board 快照编辑命令，保留稳定 Track/Via/Pour ID |
| `tests/tst_b.cpp` | 命令、吸附、画布映射、窗口结构和截图测试 |

## 构建与运行

在 Qt Creator 中打开仓库根目录，选择 `Desktop Qt 6.11.1 MSVC2022 64bit` Kit，构建目标 `eda_gui` 并运行。

命令行构建示例：

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH=<Qt kit directory>
cmake --build --preset dev
ctest --preset dev
.\build\dev\eda_gui.exe
```

本机验证使用 Qt 6.11.1。MinGW 构建使用 GCC 13.1；MSVC 测试使用本机已有的 14.51 工具集。团队规范指定的 v143 工具集仍需在组内统一机器上复核。

无窗口截图自检：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
.\build\dev\eda_gui.exe --screenshot .\b-window.png
```

实验二独立构建默认打开 PCB 示例：

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH=<Qt kit directory> -DEDA_DEFAULT_BOARD_DEMO=ON
cmake --build --preset dev
.\build\dev\eda_gui.exe
```

## 操作

| 操作 | 方式 |
|---|---|
| 选择对象 | 左键单击焊盘 |
| 多选 | Ctrl + 左键 |
| 框选 | 在空白处拖动 |
| 移动焊盘 | 拖动已选焊盘 |
| 调整焊盘尺寸 | 拖动选中框右下角控制点 |
| 平移画布 | 中键拖动，或按住空格再左键拖动 |
| 缩放 | 鼠标滚轮，或工具栏放大/缩小 |
| 适合窗口 | `F` 或工具栏按钮 |
| 删除 | `Delete` |
| 撤销/重做 | `Ctrl+Z` / `Ctrl+Y` |
| 添加焊盘 | `P` 或“插入”菜单 |
| 完成多边形 | 右键、双击，或点击第一个顶点 |
| 取消当前绘制 | `Esc` |
| PCB 实例/走线/过孔/覆铜 | 单击、Ctrl 多选、框选或拖动 |
| 打开 PCB 示例 | 文件菜单“打开 PCB 示例” |
| 导入网表 | 文件菜单“导入网表”，选择 `.net` |

## C 的接入

C 的具体工具只需实现 `ITool`，通过以下入口注入：

```cpp
canvas->setTool(std::make_unique<RouteTool>(/* C 的参数 */));
```

工具在 `activate()` 中保存 `DocumentContext*`，通过 `pushCommand()` 修改模型，通过 `requestRepaint()` 请求刷新。`render/` 不包含 `app/*.h`。

图层面板只发请求：

```text
activeLayerRequested(LayerId)
layerVisibilityRequested(LayerId, bool)
```

属性面板只发出完整的 `eda::Pad` 编辑请求，不直接修改模型。主窗口构造命令并调用 `Document::pushCommand()`。

### 服务注入

`MainWindow` 的构造函数显式接收三个服务：

```cpp
LocalLibraryService libraryService(libraryRoot);
LocalLayerService layerService;
StructuralValidationService validationService;
MainWindow window(&libraryService, &layerService, &validationService);
```

C 实现自己的服务后，只需在 `main.cpp` 替换实例，不改 `LibraryPanel` 或 `LayerPanel`。

`ILibraryService::model()` 的行使用以下角色：

```text
ServiceRoles::IdRole
ServiceRoles::NameRole
```

`ILayerService::model()` 的行使用：

```text
ServiceRoles::LayerIdRole
ServiceRoles::NameRole
ServiceRoles::LayerColorRole
ServiceRoles::LayerTypeRole
```

`IValidationService::validateFootprint()` 返回 `ValidationIssue` 列表。`Error` 阻止保存，`Warning` 由界面询问用户是否继续。

`IValidationService::validateBoard()` 对 PCB 使用同样的 Error/Warning 语义。A 的 `saveBoard()` 仍会解析封装引用，B 的校验服务不替代 A 的结构校验。

图层服务必须通过 `setDocumentContext(&m_document)` 取得当前文档。B 的 `LocalLayerService` 在封装模式下生成固定封装层，在 PCB 模式下读取 `Board::groups/layers`，因此 C 替换服务时也应保持同一模型角色。

### 网表导入

“文件 → 导入网表”使用 A 的 `loadNetlist()` 和 `resolveNetlistPins()`：

1. 解析 `COMP` 和 `NET` 记录。
2. 通过 `ILibraryService` resolver 验证封装和引脚到稳定 `PadId` 的映射。
3. 当前不是 PCB 文档时，以默认板框创建 Board。
4. 新元件按规则网格放置；同 refdes 不重复创建。
5. 网络按名称合并，重复引脚不会重复追加。

默认网格放置只是无 C 布局服务时的可运行兜底。C 实现自动布局后可替换放置步骤，B 的导入、错误提示和模型安装流程不需要重写。

## 当前边界

- 当前快照式命令便于拖拽和属性修改后完整撤销，但每次命令保存整份封装或 Board。后续可以改为按稳定 ID 的增量命令，公开命令入口不变。
- `LibraryPanel` 和 `LayerPanel` 已改为服务模型驱动，不再自行扫描库目录或硬编码图层集合。
- 默认注入的 `StructuralValidationService` 只调用 A 的结构校验。C 的制造规则应实现 `IValidationService` 并在 `main.cpp` 替换；界面保存流程不需要再改。
- 布局、布线和 DRC 的算法仍归 C。B 已提供画布、拾取、命令和 `ITool` 接入点；网表导入里的规则网格只是演示用初始放置。
