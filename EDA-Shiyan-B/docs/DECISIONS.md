# 已定决策记录（v1.1）

> 定稿日期：2026-11-02
> 状态：第 9 周生效；除非全组同意并走 PR，不再更改
> 与 `CONVENTIONS.md` 冲突时，以本文为准

本文把六项阻塞决策定死，并给出可直接落进头文件和文件解析器的细节。

---

## D1 单位：模型与界面统一用 mil

**决定**

- 模型、界面、文件统一使用 **mil**，类型 `double`。
- 不在界面做 mil/mm 切换；报告和规格表可以附 mm（1 mm = 39.37007874015748 mil）。
- 换算只允许出现在 `core/units.h`，供报告导出或外部数据导入使用：

```cpp
constexpr double kMilPerMm = 39.37007874015748;
double mmToMil(double mm);
double milToMm(double mil);
```

- 所有长度字段在注释里标 `// mil`；跨模块参数带 `Mil` 后缀。
- DIP-8 采用 100 mil 引脚间距、300 mil 排距、63 mil 焊盘、32 mil 钻孔。

**理由**

- 课程参考实现 gEDA/pcb 以 mil 为内部单位，报告里可以直接对应。
- 100 mil 栅格、50 mil 走线是整数，减少浮点显示误差。
- 只维护一套单位，避免“界面 mm、模型 mil、文件又是另一套”的经典 bug。

**后果**

- 界面输入框写 `线宽 (mil)`，默认值用整数。
- 规格表同时给 mm，方便对照机械尺寸；代码里不出现 mm。

---

## D2 Qt 基线：6.11.1 + MSVC v143

**决定**

- 全组统一使用 **Qt 6.11.1**，Kit 为 `Desktop Qt 6.11.1 MSVC2022 64bit`。
- 编译器使用 VS2026 的 **14.44（v143）** 工具集；v145 先不做兼容承诺。
- CMake 写死最低版本：

```cmake
find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Widgets Test)
```

- 不使用 Qt 私有头、不使用已标记 deprecated 的 API。
- 演示机就是开发机；不再准备 Qt 5.15 或 Qt 6.5 兼容分支。

**理由**

- 你的机器上已经装好 6.11.1，机房以你的环境为准，减少一套兼容代码。
- 6.11.1 的 `msvc2022_64` 是 v143 构建；VS2026 同时装了 14.44 和 14.51，选 14.44 风险最低。
- Qt 6.5 与 6.11 的差异对本项目没有实质收益，不值得维护双版本。

**后果**

- 组员必须安装同一版本；只装 6.5 的机器不能作为构建环境。
- CMake 配置失败时先检查 Kit 和 `CMAKE_PREFIX_PATH`，不要改代码降级。
- 若 v145 链接报错，在 Qt Creator 的 Kit 里把编译器换成 19.44（14.44），不要重装 Qt。

---

## D3 DocumentContext、ITool 与命令

### D3.1 职责与依赖

- `DocumentContext` 放 `app/`，由 B 实现；它只暴露**会话状态**和命令入口，不实现业务规则。
- 具体类 `Document : public DocumentContext` 也放 `app/`，持有 `Board`/`Footprint` 和 `QUndoStack`。
- `ITool` 放 `render/`，只定义交互接口；头文件只前置声明 `DocumentContext`，不 include `app/*.h`。
- 具体命令类放各自功能模块（`feat_fp/`、`feat_pcb/`），B 不预先定义所有命令。
- **不做大一统命令工厂**。B 提供 `app/commands.h` 的 `EdaCommand` 基类和 `pushCommand()` 入口；C 的工具构造具体命令并 push，`QUndoStack` 接管所有权。
- B 的面板不能直接 include C 的 `feat_*` 头文件。面板只依赖 `app/services.h` 里的抽象接口，C 在 `feat_*` 实现；`main.cpp` 负责创建并注入，避免 `app` 与 `feat` 互相依赖。

### D3.2 `app/document_context.h`

```cpp
#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <Qt>

#include "core/status.h"
#include "core/types.h"

class Footprint;
class Board;
class EdaCommand;

enum class ObjectType { None, Pad, Pin, Instance, Track, Via, Pour };

struct ObjectRef {
    ObjectType type = ObjectType::None;
    PadId padId{};          // Pad / Pin
    QString refdes;         // Instance
    quint64 objectId = 0;   // Track / Via / Pour
};

class DocumentContext : public QObject {
    Q_OBJECT
public:
    explicit DocumentContext(QObject* parent = nullptr);
    ~DocumentContext() override;

    // 可写指针只用于构造命令；直接改字段违反约定 §7
    virtual Footprint* footprint() = 0;   // 封装模式，否则 nullptr
    virtual Board* board() = 0;           // 板模式，否则 nullptr

    virtual LayerId activeLayer() const = 0;
    virtual void setActiveLayer(LayerId id) = 0;

    virtual bool isLayerVisible(LayerId id) const = 0;
    virtual void setLayerVisible(LayerId id, bool visible) = 0;

    virtual double gridMil() const = 0;
    virtual void setGridMil(double mil) = 0;
    virtual bool snapEnabled() const = 0;
    virtual void setSnapEnabled(bool enabled) = 0;
    virtual QPointF snap(QPointF worldMil) const = 0;

    virtual QVector<ObjectRef> selection() const = 0;
    virtual void setSelection(const QVector<ObjectRef>& refs) = 0;

    // 所有权转移；canApply() 失败时删除命令、不 push，并返回错误
    virtual Status pushCommand(EdaCommand* command) = 0;
    virtual void requestRepaint() = 0;
    virtual void showStatus(const QString& text) = 0;

signals:
    void activeLayerChanged(LayerId id);
    void layerVisibilityChanged(LayerId id, bool visible);
    void selectionChanged();
    void modelChanged();
};
```

约定：

- `footprint()` 与 `board()` 同时只有一个非空；工具在 `activate()` 时检查。
- `footprint()`/`board()` 返回可写指针是为了构造命令；工具只能把指针传给命令构造函数，不得直接改字段。代码评审重点检查这一条。
- `DocumentContext` 是激活层和可见性的**唯一状态源**；C 的 `LayerService` 需要时从它读，不缓存。
- 不提供 `undoStack()`，工具只能通过 `pushCommand()` 进入撤销栈。
- `pushCommand(nullptr)` 必须运行时判断并返回错误，不能只靠 `Q_ASSERT`。

### D3.3 `render/tool.h`

```cpp
#pragma once

#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QPointF>
#include <QTransform>
#include <Qt>

class DocumentContext;

struct ToolEvent {
    QPointF worldMil;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool snapped = false;
};

struct CanvasView {
    QTransform worldToScreen;

    QPointF toScreen(QPointF worldMil) const {
        return worldToScreen.map(worldMil);
    }
    QPointF toWorld(QPointF screen) const {
        return worldToScreen.inverted().map(screen);
    }
    qreal scale() const { return worldToScreen.m11(); }  // 均匀缩放
};

class ITool {
public:
    virtual ~ITool() = default;

    virtual void activate(DocumentContext* ctx) = 0;
    virtual void deactivate() = 0;

    virtual void mousePress(const ToolEvent& e) = 0;
    virtual void mouseMove(const ToolEvent& e) = 0;
    virtual void mouseRelease(const ToolEvent& e) = 0;
    virtual void keyPress(QKeyEvent* e) = 0;

    virtual void paintOverlay(QPainter* p, const CanvasView& view) const = 0;
    virtual QCursor cursor() const = 0;
    virtual void cancel() = 0;
};
```

- `ToolEvent` 和 `CanvasView` 都定义在 `render/tool.h`，`render` 不 include `app/*.h`。
- `CanvasView` 是唯一允许做屏幕/世界坐标转换的地方；工具只拿 `worldMil`。
- 工具不直接调用 `update()`，用 `ctx->requestRepaint()`。

### D3.4 `app/commands.h` 与命令示例

```cpp
#pragma once

#include <QUndoCommand>
#include "core/status.h"

class EdaCommand : public QUndoCommand {
public:
    using QUndoCommand::QUndoCommand;
    ~EdaCommand() override = default;

    // 入栈前校验；失败的命令不会进入撤销栈
    virtual Status canApply() const = 0;
};
```

`pushCommand()` 的实现：

```cpp
Status Document::pushCommand(EdaCommand* command)
{
    if (!command) {
        return {false, QStringLiteral("内部错误：空命令")};
    }
    const Status check = command->canApply();
    if (!check.ok) {
        delete command;
        showStatus(check.message);
        return check;
    }
    m_undoStack->push(command);   // 栈接管所有权
    return {};
}
```

命令示例，注意 ID 稳定性：

```cpp
class AddTrackCommand : public EdaCommand {
public:
    AddTrackCommand(Board* board, const QVector<QPointF>& points)
        : m_board(board)
    {
        m_track.id = m_board->allocateTrackId();
        m_track.points = points;
    }

    Status canApply() const override;
    void redo() override { m_board->insertTrack(m_track); }  // 保留 ID
    void undo() override { m_board->removeTrack(m_track.id); }

private:
    Board* m_board;
    Track m_track;   // 值持有，撤销期间不丢；ID 不变
};
```

### D3.5 `app/services.h`

面板只依赖三个抽象接口，C 在 `feat_*` 实现并注入 `MainWindow`：

```cpp
#pragma once

#include <QAbstractItemModel>
#include <QString>
#include <QVector>
#include "core/board.h"
#include "core/footprint.h"
#include "core/status.h"

class DocumentContext;

namespace ServiceRoles {
enum Role { IdRole = Qt::UserRole + 1, NameRole, LayerIdRole, LayerColorRole, LayerTypeRole };
}

enum class ValidationSeverity { Error, Warning };
struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    QString message;
    eda::PadId padId = eda::kInvalidId;
};

class ILibraryService {
public:
    virtual ~ILibraryService() = default;
    virtual QAbstractItemModel* model() const = 0;
    virtual eda::Status refresh() = 0;
    virtual eda::Result<eda::Footprint> load(const QString& id) const = 0;
    virtual eda::Status save(const eda::Footprint& footprint) = 0;
    virtual eda::Status importFile(const QString& sourcePath) = 0;
    virtual eda::Status remove(const QString& id) = 0;
    virtual QString rootPath() const = 0;
};

class ILayerService {
public:
    virtual ~ILayerService() = default;
    virtual QAbstractItemModel* model() const = 0;
    virtual void setDocumentContext(DocumentContext* context) = 0;
    virtual eda::Status refresh() = 0;
};

class IValidationService {
public:
    virtual ~IValidationService() = default;
    virtual QVector<ValidationIssue> validateFootprint(const eda::Footprint& footprint) const = 0;
    virtual QVector<ValidationIssue> validateBoard(const eda::Board& board) const = 0;
};
```

- `app` 只 include `app/services.h`；`main.cpp` 创建三个服务并通过 `MainWindow` 构造函数注入。
- `LibraryPanel` 使用 `ILibraryService::model()` 作为 `QSortFilterProxyModel` 的源模型；面板不再持有库条目副本。
- `LayerPanel` 从 `ILayerService::model()` 投影显示模型；图层服务通过 `setDocumentContext()` 读取当前封装或 Board 的真实层定义，活动层和可见性仍以 `DocumentContext` 为唯一状态源。
- 保存前调用 `IValidationService` 的封装或 Board 校验。Error 阻止保存，Warning 经用户确认后继续。
- B 提供本地适配器作为 C 服务接入前的可运行默认实现，不改变面板接口。
- 面板不 include `feat_fp/*`、`feat_pcb/*`，避免依赖环。
- 具体服务接口若需要增加方法，先改 `app/services.h` 并通知 C。

---

## D4 文件格式

封装、板、库索引共三套格式，互不兼容；都用 UTF-8、LF、`#` 注释、空白分隔。

### D4.1 公共词法

- 第一条非注释行必须是 magic + 版本，例如 `EDA-FP 1`。
- 每行一条记录；空行和 `#` 开头行忽略；`#` 出现在行首（允许前导空白）才表示注释。
- 字符串含空格时用双引号，支持 `\"`、`\\`、`\n` 转义；其余反斜杠按字面处理。
- 数字只用十进制小数，不接受 `1e3`、`NaN`、`Inf`；坐标、角度保留 3 位小数。
- 标识符（`id`、`refdes`、`net`）只允许 `[A-Za-z0-9_+-]`，区分大小写；含其它字符时必须加引号。
- 未知关键字、字段数不符、版本不符：报错并带行号，不猜测。
- 文件末尾必须有 `end`，`end` 之后只允许空行和注释。

### D4.2 封装文件 `EDA-FP 1`

扩展名 `.fp`。

```text
EDA-FP 1
units mil
id dip8_w762
name "DIP-8 W7.62mm"
pad 1 1 circle 0 0 63 63 32 4 10 0 top
pad 2 2 circle 100 0 63 63 32 4 10 0 top
pin 1 "1" passive 1
pin 2 "2" passive 2
hole 0 0 125 0 50
silk line top_silk 10 -150 -50 150 -50
silk arc top_silk 10 0 0 40 0 180
silk rect top_silk 10 -150 -50 300 100 0
silk poly top_silk 10 0 3 0 50 20 80 -20 80
silk text top_silk 0 120 50 0 "DIP-8"
end
```

字段：

| 记录 | 字段 |
|---|---|
| `pad` | `padId number shape x y w h drill maskMargin clearance cornerRadius layer` |
| `pin` | `number name electricalType padId` |
| `hole` | `x y drill plated(0/1) keepout` |
| `silk line` | `layer width x1 y1 x2 y2` |
| `silk arc` | `layer width cx cy radius startDeg spanDeg` |
| `silk rect` | `layer width x y w h filled(0/1)` |
| `silk poly` | `layer width filled(0/1) n x1 y1 ... xn yn` |
| `silk text` | `layer x y height rotationDeg "text"` |

取值：

- `shape`: `circle`、`rect`、`oval`、`roundrect`；`roundrect` 用 `cornerRadius`，其它形状该字段为 0。
- `electricalType`: `input`、`output`、`bidir`、`passive`、`power`、`nc`。
- `layer`: `top`、`bottom`、`top_silk`、`bottom_silk`、`top_mask`、`bottom_mask`，映射到 `core/types.h` 的固定 `LayerId`。
- `padId` 为正整数，封装内唯一、不复用。
- 坐标全部是 footprint-local，单位 mil，原点 `(0,0)` 由模板定义。

### D4.3 板文件 `EDA-BRD 1`

扩展名 `.brd`。

```text
EDA-BRD 1
units mil
group 1 "Top" 0
group 2 "Bottom" 1
layer 0 1 "Top Copper" copper #C83232 0
layer 1 2 "Bottom Copper" copper #3264C8 0
layer 2 1 "Top Silk" silk #F0F0F0 1
outline 4 0 0 2000 0 2000 1000 0 1000
instance U1 dip8_w762 500 500 0 top
track 1 0 12 N001 2 500 500 700 500
via 1 700 500 24 40 0 1 N001
pour 1 1 N001 10 0 3 0 0 1000 0 1000 500
net N001 2 U1.1 U1.2
end
```

字段：

| 记录 | 字段 |
|---|---|
| `group` | `groupId name stackOrder` |
| `layer` | `layerId groupId name type color stackOrder` |
| `outline` | `n x1 y1 ... xn yn`，闭合多边形，不重复首点 |
| `instance` | `refdes footprintId x y rotationDeg side` |
| `track` | `trackId layerId width net n x1 y1 ... xn yn` |
| `via` | `viaId x y drill diameter fromLayer toLayer net` |
| `pour` | `pourId layerId net clearance priority n x1 y1 ... xn yn` |
| `net` | `name n refdes.pinNumber ...` |

取值：

- `type`: `copper`、`silk`、`mask`、`outline`。
- `side`: `top`、`bottom`。
- `color`: `#RRGGBB`。
- `net` 为 `-` 表示未分配网络。
- `layerId` 固定：0 顶层铜、1 底层铜、2 顶层丝印、3 底层丝印、4 顶层阻焊、5 底层阻焊、6 板框；≥100 为内层/自定义。
- `groupId`、`layerId` 必须先在 `group`/`layer` 记录中定义；组间按 `group.stackOrder` 排序，组内按 `layer.stackOrder` 排序。
- `instance` 只引用 `footprintId`，板文件不自带封装库；打开板文件时必须能在库中找到对应封装，否则整体失败并列出缺失项。
- 记录顺序不限，引用在解析完整个文件后统一校验。

### D4.4 库索引 `EDA-LIB 1`

扩展名 `.idx`，由 A 维护：

```text
EDA-LIB 1
footprint dip8_w762 "DIP-8 W7.62mm" footprints/dip8_w762.fp
end
```

- 路径相对索引文件所在目录；解析后必须规范化，拒绝绝对路径和 `..` 逃逸到库目录之外。
- `footprintId` 和 `name` 在库内唯一；重复直接报错。
- 索引不内嵌封装内容，只引用 `.fp` 文件。

### D4.5 解析校验清单

A 和 C 的解析器必须一致，以下都按“错误”处理：

- magic/版本不匹配；缺少 `end`；`end` 后还有数据。
- `units` 缺失或不是 `mil`。
- 必需记录缺失：FP 的 `id`、`name`；BRD 的 `outline`。
- `n` 与实际点/引用数量不符。
- 重复的 `padId`、`trackId`、`viaId`、`pourId`、`groupId`、`layerId`、`net` 名、`refdes`。
- 悬空引用：`pin.padId`、`instance.footprintId`、`net` 的 `refdes.pin`、`track/via/pour.net`、`layer.groupId`。
- `via.fromLayer`/`toLayer` 不是铜层，或二者相同。
- `outline`/`pour` 少于 3 点、相邻点重复、自交、绝对面积接近 0。
- 未知关键字、字段数不符、数字非法、字符串未闭合。

警告（不阻止载入，但界面提示）：

- 封装存在未被 `Pin` 引用的 `Pad`。
- `Net` 少于两个引脚。
- 板框外存在实例或走线。

### D4.6 底面实例的坐标变换

`side == bottom` 时，按以下顺序把 footprint-local 映射到 board-world：

1. 沿局部 Y 轴镜像：`x' = -x`；
2. 按 `rotationDeg` 逆时针旋转；
3. 平移 `pos`。

层映射：`top` ↔ `bottom`、`top_silk` ↔ `bottom_silk`、`top_mask` ↔ `bottom_mask`；`outline` 不变。丝印文字在镜像后要反向读，渲染时按 §10 的文本翻转规则处理。该变换必须有一条 round-trip 测试用例。

---

## D5 板框：允许非矩形，原点固定为 (0,0)

**决定**

- `Board::outline` 是 `QPolygonF`，允许任意简单多边形；至少 3 点，不自交，按 shoelace 计算的**绝对值**面积 > 0。
- 统一按逆时针存储；载入时若为顺时针，反转点序后再使用。
- board-world 原点 `(0,0)` 是固定坐标，不随板框移动；新建板时把 outline 包围盒左下角放在 `(0,0)`，之后不再隐含这个假设。
- 文件里存绝对坐标，不存“原点”字段。
- v1 界面支持矩形板框的新建、移动、改尺寸；多边形可以载入、显示、参与越界判断，但顶点编辑列为 P2。
- `BoardOutlineTool` 归 C 实现（走 `ITool`）；B 只负责绘制和命中测试；C 同时提供 `validateOutline()`。

**理由**

- 模型和文件先支持多边形，避免以后改格式。
- UI 先做矩形，保住演示主线；顶点编辑不影响验收。
- 原点固定，非矩形板框也能用坐标，不存在“左下角”歧义。

---

## D6 稳定 ID：Pad / Track / Via / Pour 全部加 ID

**决定**

- `core/types.h` 定义：

```cpp
using PadId = quint64;
using TrackId = quint64;
using ViaId = quint64;
using PourId = quint64;
constexpr quint64 kInvalidId = 0;
```

- `PadId` 由 `Footprint` 分配，封装内唯一；`TrackId`、`ViaId`、`PourId` 由 `Board` 分配，板内唯一；全部**只增不复用**。
- `Footprint` 维护 `nextPadId`，`Board` 维护 `nextTrackId`/`nextViaId`/`nextPourId`；载入时取各自容器内最大 ID + 1。
- `Board` 提供 `allocateTrackId()`、`insertTrack(const Track&)`、`removeTrack(TrackId)`；`insertTrack` 保留传入 ID，不重新分配。`Via`、`CopperPour` 同理。
- `Track`、`Via`、`CopperPour` 结构体首字段分别为对应 ID。
- 命令、选择集、`ObjectRef` 一律用 ID 引用这些对象，不用下标、不用指针。
- `refdes`、`Net.name`、`Footprint.id` 创建后不可变：
  - `Footprint.name` 可改，`Footprint.id` 不改；
  - `refdes` 若必须支持重命名，用一条原子命令更新 `Instance`、`Net` 的 `PinRef`、走线/覆铜的网络归属；
  - `Net.name` 同理，v1 不提供重命名 UI。

**理由**

- 没有 ID，走线/过孔/覆铜的编辑命令只能靠下标或指针，删除后必悬空。
- 文件里显式写 ID，round-trip 和调试都简单。
- `refdes`/网络名不可变，网表和撤销栈不会因为改名断链。

**后果**

- A 的 `Footprint`/`Board` 增加 ID 分配器；文件解析时校验重复 ID。
- C 的命令和 `ObjectRef` 直接用 `TrackId`/`ViaId`/`PourId`。
- 迁移旧文件（如果有）时按出现顺序补 ID。
