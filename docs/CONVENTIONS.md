# 团队统一约定

> 状态：v1.1，六项阻塞决策已定稿（2026-11-02）
> 适用范围：本仓库全部代码、文档、提交与接口
> 修改方式：提 PR，至少 1 人 review；涉及公开接口的改动按第 9 节执行
>
> 阅读顺序：先看 §0 的已定决策和十条硬规则；细节见 [`DECISIONS.md`](DECISIONS.md)。

---

## 0. 已定决策

六项阻塞决策已定稿，细节和签名见 [`docs/DECISIONS.md`](DECISIONS.md)。本文与 DECISIONS 冲突时，以 DECISIONS 为准。

| # | 决策 | 结论 | 所有者 |
|---|---|---|---|
| 1 | 长度单位 | **mil**，模型、界面、文件统一；报告可附 mm | A |
| 2 | Qt 基线 | **6.11.1**，MSVC2022_64，v143 工具集 | A/B |
| 3 | `DocumentContext` 与命令 | 会话状态归 `app`；不做大一统工厂，工具构造具体命令并 `pushCommand` | B |
| 4 | 文件格式 | `EDA-FP 1` / `EDA-BRD 1` / `EDA-LIB 1` | A |
| 5 | 板框 | 模型和文件允许简单多边形；v1 UI 只做矩形；原点固定 `(0,0)` | A/C |
| 6 | 稳定 ID | `PadId`、`TrackId`、`ViaId`、`PourId` 全部加，只增不复用 | A |

## 0.1 十条硬规则

1. **两套坐标**：封装用 footprint-local，板级用 board-world；接口必须标注用哪套。
2. **单位**：模型、界面、文件统一 mil；换算只在 `core/units.h`，界面标签写 `(mil)`。
3. **边界**：`core/` 不出现 `QWidget`/`QPainter`；`render/` 不写业务规则。
4. **撤销**：用户编辑走 `QUndoCommand`；载入、新建、初始构建不走命令；可见性/激活层/选择集是会话状态，不进撤销栈。
5. **命令所有权**：已有对象用稳定 ID；新建/删除的对象由命令以值或 `unique_ptr` 持有。
6. **错误**：不抛异常跨模块；统一 `Status`/`Result<T>`，失败必须有可显示的中文 `message`。
7. **信号槽**：函数指针写法；禁止 `SIGNAL/SLOT` 宏和自动连接命名。
8. **接口**：每个公开签名写清单位、坐标、所有权、空值、失败、是否改模型。
9. **保存**：先结构校验，再写流，检查 `QTextStream::status()`，最后 `commit()`。
10. **协作**：AI 使用记入 `docs/ai-log.md`；接口改动提前 24 小时通知；不提交构建产物。

---

## 1. 技术基线

| 项 | 约定 |
|---|---|
| 语言 | C++17，不使用 C++20 及以上特性 |
| GUI | Qt **6.11.1** Widgets，QPainter 自绘 |
| 构建 | CMake 3.21+，开启 `AUTOMOC/AUTOUIC/AUTORCC` |
| 编译器 | MSVC 2022/2026，优先 **14.44（v143）** |
| 编码 | UTF-8 无 BOM，换行 LF；MSVC 必须加 `/utf-8` |
| 平台 | Windows 11 |

约束：

- 全组统一 Qt 6.11.1；`find_package(Qt6 6.11.1 REQUIRED)`，不维护 6.5 兼容分支。
- 新代码不允许新增编译警告；警告不清理不算完成。
- 不使用异常跨越模块边界；Qt 事件循环里抛异常一律禁止。
- Qt 6.11.1 的 `msvc2022_64` 是 v143 构建；默认用 14.44 工具集。v145 若链接失败，在 Kit 里换回 14.44，不重装 Qt。
- 只装 Qt 6.5 的机器不作为构建环境；组员必须安装同一版本。

---

## 2. 目录、依赖与文件所有权

```text
core/      QtCore/QtGui 值类型：模型、几何、单位、文件、网表
render/    CanvasWidget、QPainter、QTransform、拾取、ITool 接口
app/       主窗口、Dock、ToolBar、QUndoStack、DocumentContext、Document、服务接口
feat_fp/   实验一：封装编辑、库管理、校验、模板
feat_pcb/  实验二：层组、放置、布局、布线、覆铜、DRC
tests/     Qt Test 与手工用例
docs/      方案、约定、接口记录、测试计划、AI 记录
```

依赖方向必须单向：

```text
core  <-  render  <-  app  <-  feat_fp
                          <-  feat_pcb
```

- `core/` 只能使用 QtCore/QtGui 的值类型，不得出现 `QWidget`、`QPainter`、`QMainWindow`。CMake 必须链接 `Qt6::Core` 和 `Qt6::Gui`（`QColor`、`QPolygonF` 在 QtGui）。
- `render/` 只负责绘制、坐标变换、拾取，不得写 DRC、校验、网表等业务规则。
- `feat_fp/` 与 `feat_pcb/` 不得互相依赖；共用逻辑上移到 `core/`。
- 业务类不得继承 `QWidget`；需要交互时实现 `ITool`，由 `CanvasWidget` 调用。
- 面板控件归 B，面板里的业务规则归 C；控件只发信号，不改模型。
- 面板只依赖 `app/services.h` 的 `ILibraryService`/`ILayerService` 抽象接口，不 include `feat_fp/*`、`feat_pcb/*`；C 在 `feat_*` 实现，`main.cpp` 负责创建和注入。这样 `app` 与 `feat` 不会互相依赖。
- `render/` 头文件只**前置声明** `DocumentContext`，不 include `app/*.h`，避免 `render` 与 `app` 形成依赖环；`CanvasWidget` 只透传指针，不解引用。
- 激活层、层可见性、栅格、选择集是**会话状态**，由 `app` 的 `DocumentContext` 持有；`feat_pcb` 的 `LayerService` 只负责层定义、颜色、`stackOrder` 和层业务，需要激活层/可见性时从 context 读，不缓存。
- `CanvasWidget` 只提供 `setActiveLayer()`/`setLayerVisible()` 等 setter；`app` 把 `DocumentContext` 的信号接到这些 setter，`render` 不读 context、不 include `feat_pcb`。

### 2.1 文件所有权

| 所有者 | 目录/文件 | 说明 |
|---|---|---|
| A | `core/**`、`CMakeLists.txt`、`CMakePresets.json` | 模型、几何、单位、文件、网表、构建 |
| B | `render/**`、`app/**` | 画布、绘制、主窗口、命令框架、面板控件 |
| C | `feat_fp/**`、`feat_pcb/**`、`tests/**`、`docs/test-plan.md` | 业务规则、算法、工具实现、测试 |
| 全员 | `docs/CONVENTIONS.md`、`README.md`、`docs/ai-log.md` | 改动提 PR |

规则：

- 只改自己负责的文件；跨模块改动提 issue 或 PR，由所有者 review。
- 新增源文件由所有者通知 A 加入 CMake，不能依赖 glob 自动收集。
- 接口头文件的所有者优先；使用方只提需求，不直接改。

---

## 3. 命名规范

项目统一风格，借鉴 Qt 命名习惯，不另立门派。

| 对象 | 规则 | 示例 |
|---|---|---|
| 类、结构体、枚举 | PascalCase | `Footprint`、`PadShape` |
| 函数、方法 | lowerCamel | `validateFootprint()` |
| 成员变量 | `m_` + lowerCamel | `m_activeLayer` |
| 局部变量、参数 | lowerCamel | `padCount` |
| 常量 | `k` + PascalCase | `kMaxDrillMil` |
| 文件名 | snake_case | `footprint_validator.h` |
| 信号 | 过去式 | `layerChanged`、`selectionChanged` |
| 槽 | 祈使式 | `setActiveLayer()` |
| 布尔 | `is/has/can` 开头 | `isVisible()`、`hasOutline()` |
| 计数 | `xxxCount` | `padCount()` |
| 缩写 | 按单词处理，不全大写 | `PadId`、`refdes`、`url` |

细则：

- getter 不带 `get`：`padCount()`，不是 `getPadCount()`；setter 用 `set`。
- 接口里不写缩写：`footprint`，不用 `fp`；局部变量可以短。
- 枚举一律 `enum class`，值用 PascalCase：`PadShape::RoundRect`。
- 丝印图元加 `Silk` 前缀：`SilkLine`、`SilkArc`、`SilkRect`、`SilkPolygon`、`SilkText`。原因是 Win32 GDI 有 `Arc`、`Polygon`、`Rectangle` 等函数和宏，裸名容易冲突；不是怕 `QLine`。
- 头文件一律 `#pragma once`；头文件里禁止 `using namespace`。
- include 顺序：本文件对应头文件 → Qt → 标准库 → 项目内头文件，各组之间空一行。
- 能在头文件前置声明就不 include；实现放 `.cpp`。
- 缩进 4 空格，不用 Tab；左大括号不换行；指针写成 `Type* p`。
- 单行不超过 120 字符。

### 统一术语

代码、界面、报告使用同一套词，不要同义词混用：

| 概念 | 统一用词 | 不要用 |
|---|---|---|
| 封装（库条目） | `Footprint` | package、footprint 混写 |
| 放置后的元件 | `Instance` | component、part |
| 焊盘 | `Pad` | pad、land |
| 引脚 | `Pin` | pin、lead |
| 安装孔 | `MountingHole` | hole、mount |
| 走线 | `Track` | wire、trace |
| 过孔 | `Via` | hole、through |
| 覆铜 | `CopperPour` | fill、zone、polygon |
| 层组 | `LayerGroup` | layer set |
| 网络 | `Net` | netlist（网表是 `Netlist`） |
| 位号 | `refdes` | reference、designator |
| 板框 | `outline` | frame、edge |
| 库 | `FootprintLibrary` | lib |

---

## 4. 单位、坐标、角度、精度

### 4.1 长度单位

- 模型、界面、文件统一用 **mil**，类型 `double`；v1 不做界面单位切换。
- 换算只允许在 `core/units.h` 中实现（已定稿）：

```cpp
constexpr double kMilPerMm = 39.37007874015748;
double mmToMil(double mm);
double milToMm(double mil);
```

- 跨模块的参数和字段，名字带单位后缀：`widthMil`、`gapMil`、`clearanceMil`。
- 结构体字段可以在注释里标单位，例如 `double drill; // mil`，但同一个结构体内不得混用 mm。
- 界面标签统一写 `线宽 (mil)`；报告和规格表可以附 mm，代码里不出现 mm。
- 禁止一个表达式里同时出现 mm 和 mil；禁止散落 `0.0254` 之类的魔数。

### 4.2 坐标

必须区分两套坐标，接口注释写清楚用哪套：

| 坐标系 | 使用场景 | 原点 |
|---|---|---|
| footprint-local | 封装编辑器、`validateFootprint`、封装文件 | 默认取焊盘包围盒中心；模板可另行指定，并在模板说明里写清 |
| board-world | 板编辑器、`Instance`、`Track`、`Via`、`CopperPour`、DRC、`validatePlacement` | 固定 `(0,0)`；新建板时把 outline 包围盒左下角放在 `(0,0)`，之后不再隐含这个假设 |

- 世界坐标 X 向右，Y 向上。
- `Instance` 把局部坐标映射到板坐标：先按 `rotation`（度，逆时针）旋转，再平移 `pos`；这个变换只在放置/渲染层做，封装内部数据不改。
- 板框不要求是矩形，也不要求从 `(0,0)` 开始；非矩形 `outline` 没有“左下角”，所以不要把“板框左下角”写死进代码。
- 屏幕坐标转换只发生在 `CanvasWidget` 的 `QTransform` 里；业务代码不得调用 `mapToScene`、`mapFromScene` 之类的视图函数。
- 拾取容差按屏幕像素除以缩放比换算成世界坐标，不能直接拿像素当 mil。

### 4.3 角度与圆弧

- 角度统一用**度**，逆时针为正。
- 圆弧存“起始角 + 跨度”，不存弧度。
- 绘制时再转换：`QPainter::drawArc` 的单位是 1/16 度，`QPainterPath::arcTo` 的单位是度。
- Y 轴翻转会反转圆弧方向，span 的正负要在绘制层处理，模型层不改。

### 4.4 精度

- 坐标、距离一律 `double`，不用 `float`。
- 坐标相等比较用 `kEpsilonMil = 1e-3`（约 25 nm），不要用 `1e-6` 这种过紧的值。
- 命中测试、DRC 的几何容差必须作为参数显式传入，不用全局魔数。
- 保存/round-trip 比较也用 `kEpsilonMil`，不要直接比 `double`。

---

## 5. 所有权与生命周期

- 模型对象由 `app` 的 `Document` 持有：`Document : public DocumentContext`，内部持有 `Board`/`Footprint` 和 `QUndoStack`；其他模块只持**非拥有**的裸指针或引用。
- `Document` 负责实现 `DocumentContext` 的纯虚函数、发 `modelChanged` 信号；不要另建第二份模型。
- `QObject`/`QWidget` 用父子关系管理生命周期；不要手动 `delete` 有 parent 的对象。
- 多态丝印图元用 `std::vector<std::unique_ptr<Primitive>>`；`QVector` 只放值类型，不放多态对象。
- `QUndoStack` 由主窗口持有；命令由工具或画布构造并 `push`，栈接管所有权。
- 命令引用对象分两种：
  - **已存在对象**：用稳定 ID（`PadId`、`TrackId`、`ViaId`、`PourId`、`refdes`）查找，不存裸指针。
  - **新建/删除对象**：命令用值或 `std::unique_ptr` 自己持有，撤销/重做期间对象必须存活。
- `QUndoStack::clear()` 只在关闭/重载文档时调用，不是每次删除对象都清栈。
- 下列键创建后不可变，否则撤销栈、网表和库引用会断：
  - `Footprint.id`（`name` 可改，`id` 不改）
  - `refdes`
  - `Net.name`（v1 不提供重命名 UI；若以后支持，用一条原子命令更新所有引用）
- `Pad`、`Track`、`Via`、`CopperPour` 全部带稳定 ID（`PadId`/`TrackId`/`ViaId`/`PourId`），只增不复用；详见 `docs/DECISIONS.md` D6。
- 函数参数约定：
  - 只读：`const T&`
  - 可空：`T*`，并在注释写明“可为 nullptr”
  - 必须可写：`T&`
  - 所有权转移：`std::unique_ptr<T>`
- 不允许返回裸拥有指针；不允许 `new` 之后没有明确所有者。

---

## 6. 错误处理

- 不抛异常跨模块；错误通过返回值传递。
- 统一错误类型放在 `core/status.h`，已定稿；A 实现时可增加辅助函数，但 `ok`/`message` 字段名不变：

```cpp
struct Status {
    bool ok = false;       // 默认失败，避免漏设错误被当成成功
    QString message;       // 失败时必填，可直接显示
};

template <class T>
struct Result {
    Status status;
    std::optional<T> value;
};
```

- 无返回值的操作返回 `Status`；有返回值的返回 `Result<T>`。
- 算法专用的结果结构必须内嵌 `Status status`，字段名统一用 `message`，不要另起 `ok/why/error` 三套。
- 返回值可能失败的函数加 `[[nodiscard]]`。
- 文件解析错误必须带行号：`第 12 行：未知的引脚编号 "U1.9"`。
- 错误信息用中文、可显示；调试细节用 `qWarning()` 记录，但不能只靠日志。
- 在边界校验：界面输入、文件载入、网表导入。内部函数可以假定输入已合法，但要用 `Q_ASSERT` 标出前置条件。
- `Q_ASSERT` 只用于程序员的错误，不用于用户输入。
- 禁止静默失败：不能出现“返回 false 但没有任何提示”的路径。

---

## 7. 模型修改与撤销

必须先区分两类状态：

| 类别 | 例子 | 是否持久化 | 是否进撤销栈 |
|---|---|---|---|
| 文档模型 | 焊盘、引脚、走线、覆铜、层定义、颜色、`stackOrder` | 是 | 用户编辑时是 |
| 会话状态 | `visible`、`active`、缩放、平移、选择集、当前工具 | 否（或单独视图配置） | 否 |

规则：

- 用户对**文档模型**的编辑必须通过 `QUndoCommand`；直接改字段视为 bug。
- 载入文件、新建文档、初始化模型不走命令，构建完清空撤销栈。
- 文件系统操作不进撤销栈：库导入/导出、删除库文件、打开/保存文件都不算模型编辑。
- 层可见性、激活层、栅格、选择集属于会话状态，由 `app` 的 `DocumentContext` 持有，不进撤销栈，不写进板文件；`Layer` 里的 `visible/active` 只作为运行时字段，保存时跳过。
- 不做大一统命令工厂：`DocumentContext::pushCommand(EdaCommand*)` 是唯一入口；命令继承 `app/commands.h` 的 `EdaCommand`，实现 `canApply()`，具体命令类放各自功能模块。
- 命令在入栈前完成校验：`canApply()` 返回失败时，`pushCommand()` 删除命令、不 push、显示错误；`redo()` 本身不再返回失败。
- 命令必须可重复撤销/重做，且恢复完全一致的状态。
- 面板不直接改模型：面板发请求信号，服务或工具构造命令。
- 批量操作合并为一条命令，避免一次拖动产生几十条撤销记录。
- `QUndoCommand` 不是 `QObject`，不能 emit 信号。模型变更信号由 `app` 的 `Document`（继承 `QObject`）或 `QUndoStack::indexChanged` 发出；命令只负责 mutate，不负责通知视图。

---

## 8. 接口定义规则

任何公开函数/方法，签名上方必须写清 6 件事：

1. 单位与坐标系（footprint-local 还是 board-world）
2. 所有权与生命周期
3. 是否允许空
4. 失败如何返回
5. 是否修改模型、是否必须走命令
6. 线程约束（本项目默认全部在 UI 线程）

模板：

```cpp
// 算法专用结果结构内嵌 Status，字段名统一用 message
struct LayoutResult {
    Status status;
    QVector<QPointF> positions;
};

// 单位: mil；坐标: footprint-local；不修改模型；UI 线程
// 所有权: 不持有 fp；返回的 issue 列表由调用方持有
// 空值: fp 不可为空
// 失败: 校验本身不失败；空列表表示通过，Error 项阻止保存
[[nodiscard]] QVector<Issue> validateFootprint(const Footprint& fp);

// 单位: mil；坐标: board-world；不修改模型；UI 线程
// 失败: status.ok=false 时 positions 为空，message 可直接显示
[[nodiscard]] LayoutResult gridLayout(const QVector<QSizeF>& sizesMil,
                                      const QPolygonF& outlineMil,
                                      double gapMil,
                                      int columns);
```

其他规则：

- 公开接口不暴露下标；用稳定 ID（`PadId`、`LayerId`）或 `refdes`。
- 枚举一律 `enum class`；布尔参数超过一个时改用枚举，避免 `foo(true, false)`。
- 默认参数不得隐藏单位或所有权；能不用就不用。
- 公开头文件保持最小；实现细节放 `.cpp` 或私有头。
- 允许返回 `const T&`，但必须注释生命周期（“引用在文档关闭前有效”）；禁止返回非 const 引用，禁止返回局部对象的引用。
- `pushCommand()` 接收的 `EdaCommand*` 是所有权转移：失败时由 `pushCommand()` 删除，成功时由 `QUndoStack` 接管；调用方不得再 `delete`。
- 接口函数不返回内部容器的可写引用；需要遍历时提供 `const` 范围或拷贝。

---

## 9. 接口冻结与变更

### 9.1 首批冻结文件

第一次课后 48 小时内冻结**签名**，函数体可以是 stub：

| 文件 | 所有者 | 使用方 | 冻结内容 |
|---|---|---|---|
| `core/types.h` | A | 全员 | `PadId`/`TrackId`/`ViaId`/`PourId`/`LayerId`、枚举、基础类型 |
| `core/units.h` | A | 全员 | mil/mm 换算 |
| `core/status.h` | A | 全员 | `Status`、`Result<T>` |
| `core/footprint.h` | A | B/C | `Footprint`、`Pad`、`Pin`、`MountingHole` |
| `core/board.h` | A | B/C | `Board`、`LayerGroup`、`Instance`、`Track`、`Via`、`CopperPour`、`Net` |
| `core/geometry.h` | A | C | 距离、包围盒、`padOutline` |
| `core/io.h` | A | B/C | 封装/板/库/网表的 load/save/parse 签名（格式已定稿，实现可晚） |
| `render/canvas.h` | B | A/C | `CanvasWidget`、坐标变换、拾取、`setActiveLayer` |
| `render/tool.h` | B | C | `ITool`、`ToolEvent`、`CanvasView`；只前置声明 `DocumentContext` |
| `app/document_context.h` | B | A/C | `DocumentContext`、`ObjectRef`、会话状态、模型变更信号 |
| `app/document.h` | B | A/C | `Document : DocumentContext`，持有模型与 `QUndoStack` |
| `app/commands.h` | B | A/C | `EdaCommand` 基类与 `canApply()` |
| `app/services.h` | B | C | `ILibraryService`、`ILayerService` 抽象接口 |
| `feat_fp/library_service.h` | C | B（经 `ILibraryService`） | `LibraryService` 实现 |
| `feat_pcb/layer_service.h` | C | B/A（经 `ILayerService`） | `LayerService` 实现 |

### 9.2 冻结流程

1. 使用方写“我需要什么”，不替所有者写头文件。
2. 所有者把签名落进头文件，函数体先 `return {};` 或空实现。
3. 三人拉同一分支，能一起编译。
4. 跑通最小链路：建封装 → 加焊盘 → 校验 → 布局两个矩形。
5. 链路通过后才算冻结，在 `docs/interface-changelog.md` 记录日期与版本。

`core/io.h` 的存/读链路按已定稿的 D4 格式单独验证，不塞进第一次 smoke test。

### 9.3 变更规则

- **加字段**：只能追加到末尾，且提供默认成员初始化；仍需通知使用方并保证所有调用方编译通过。C++17 没有 designated initializers，中间插字段会静默改变位置初始化含义。
- 公开结构体优先提供构造函数或工厂，不鼓励位置初始化。
- **改函数签名、单位、所有权、错误类型**：提前 24 小时在群里说明，提 PR，更新所有调用方。
- 每次接口变更记入 `docs/interface-changelog.md`：日期、文件、改了什么、原因、影响谁。
- 接口冻结后，不允许“先合并再补调用方”。

---

## 10. 信号槽与界面

- 一律用函数指针写法：

```cpp
connect(m_layerPanel, &LayerPanel::activeLayerRequested,
        m_documentContext, &DocumentContext::setActiveLayer);
```

- 禁止 `SIGNAL/SLOT` 宏；禁止依赖 `on_<object>_<signal>` 自动连接。
- 信号用过去式描述已发生的事，槽用祈使式描述要做的动作。
- 状态只能有一个源：激活层和可见性由 `DocumentContext` 持有，面板只发请求，工具从 context 读；`DocumentContext::activeLayerChanged`/`layerVisibilityChanged` 接到 `CanvasWidget` 的 setter。`LayerService` 不缓存这两个状态，需要时直接读 context。
- 界面文字一律 `tr()` 或 `QStringLiteral`；不要拼接翻译片段。
- 业务代码不弹对话框；返回错误，由界面决定怎么显示。
- 高 DPI（125%/150%）下必须测一次缩放、拾取和文字位置。
- Y 轴翻转后文字会倒置，正确顺序是：

```cpp
painter->save();
painter->translate(textPos);
painter->scale(1, -1);
painter->drawText(QPointF(0, 0), text);
painter->restore();
```

只 `scale(1, -1)` 不 `translate` 会把绘制原点也翻走。

---

## 11. 文件、编码与保存

- 仓库内文本文件统一 UTF-8 无 BOM、LF 换行，由 `.gitattributes` 约束。
- 封装、板、库索引使用三套格式：`EDA-FP 1`、`EDA-BRD 1`、`EDA-LIB 1`；语法和字段见 [`docs/DECISIONS.md`](DECISIONS.md) D4。
- 保存用 `QSaveFile` + `QTextStream`：
  1. `stream.setEncoding(QStringConverter::Utf8)`；
  2. 写完检查 `stream.status() == QTextStream::Ok`；
  3. 让流 `flush()` 或析构；
  4. 调用 `commit()` 并检查返回值；
  5. 失败时取 `QSaveFile::errorString()` 作为错误信息。
- 载入时校验版本、单位、字段数量；未知版本直接报错，不猜。
- 浮点数写出时固定小数位（建议 3 位），round-trip 比较用 `kEpsilonMil`。
- 文件里不写绝对路径、用户名、机器名。
- 保存闸门：
  - 封装保存/导出前必须跑 `validateFootprint`；
  - 存在 `Error` 时阻止 `commit()`，界面定位到具体焊盘/引脚；
  - `Warning` 允许用户确认后继续；
  - 板文件保存只阻止结构错误（悬空引用、非法坐标），DRC 违规只提示、不阻止保存。

---

## 12. Git 与提交

### 12.1 分支

- `main`：保护分支，只接受 PR，保持可演示。
- `dev`：集成分支，日常合并目标。
- `feature/<姓名拼音>-<模块>`：个人开发分支，例如 `feature/liming-drc`。

### 12.2 提交

提交信息格式：

```text
type(scope): 简述

为什么改、影响哪个接口、怎么验证。
```

`type` 取值：`feat`、`fix`、`docs`、`test`、`refactor`、`build`、`chore`。

- 一个提交只做一件事；格式化与功能修改分开。
- 不提交 `build*/`、`out/`、`*.user`、`CMakeUserPresets.json`、`compile_commands.json`、`moc_*`、`ui_*`、`qrc_*`、`.vs/`、`*.obj`、`*.ilk`、`*.exp`、`*.exe`、`*.dll`、`*.pdb`。
- 不提交 Qt DLL、演示视频、大二进制；截图放 `docs/images/` 并控制大小。
- 不 force push 共享分支；不重写 `main`/`dev` 历史。

### 12.3 PR

- 目标分支默认 `dev`；里程碑时 `dev` → `main`。
- 至少 1 人 review；作者自己合并。
- PR 描述写：改了什么、为什么、怎么测、截图（界面改动必须附）。
- 跨模块改动必须通知对应所有者，不能“先斩后奏”。

---

## 13. 构建

- CMake + `CMakePresets.json`，一条命令完成配置和构建。
- Qt 路径通过 `CMAKE_PREFIX_PATH` 或 Qt Creator Kit 指定，不写死进仓库。
- 开启 `AUTOMOC/AUTOUIC/AUTORCC`；不提交生成文件。
- MSVC 加 `/utf-8`；Debug/Release 的 Qt DLL 不能混用。
- `core` 目标链接 `Qt6::Core` 和 `Qt6::Gui`；`app` 链接 `Qt6::Widgets`。
- 演示机用 `windeployqt` 打包 Release，或直接在 Qt Creator 里运行。
- 本机若同时装了多个 Qt，用全路径或 Kit，不依赖 PATH 里的 `qmake`。

---

## 14. 测试与完成标准

- 纯函数（布局、路径、校验、几何）必须有 Qt Test 用例，文件命名 `tests/tst_*.cpp`。
- 不依赖 GUI 的测试用 `QTEST_APPLESS_MAIN`。
- 每个修过的逻辑 bug，补一条回归用例。
- 集成路径写进 `docs/test-plan.md`：步骤、预期、截图编号。
- Definition of Done：
  1. 干净环境能编译；
  2. 无新增警告；
  3. 相关测试通过；
  4. 有截图或录屏；
  5. 接口文档/变更记录已更新；
  6. 至少 1 人 review。

---

## 15. AI 使用记录

课程明确要求写清 AI 使用记录，从第一天开始记，不要事后补。

- 每人维护 `docs/ai-log.md`，每条包含：
  - 日期、工具与模型；
  - 提示词摘要；
  - 生成/修改了什么（文件、函数）；
  - 自己改了哪里、为什么；
  - 如何验证（编译、测试、截图）。
- AI 生成的代码必须人工审查、编译、测试后才能合并。
- 报告附录附 AI 使用摘要。
- 不允许把 AI 生成的整段代码未经理解直接提交。

---

## 16. 接口评审清单

评审时逐条勾，不靠印象：

- [ ] 单位写了吗？是 mil 吗？有没有混入 mm？
- [ ] 坐标系写了吗？footprint-local 还是 board-world？Y 向哪？
- [ ] 所有权写了吗？谁创建、谁删除、谁持有？
- [ ] 允许空吗？空了会怎样？
- [ ] 失败怎么返回？`message` 能直接显示吗？
- [ ] 会改模型吗？改模型走命令吗？
- [ ] 用的是稳定 ID 还是下标？这个键可变吗？
- [ ] 参数是 `const&` 吗？有没有不必要的拷贝？
- [ ] 布尔参数是否超过一个？要不要改枚举？
- [ ] 头文件是否引入了不该引入的 Qt 模块？
- [ ] 业务代码里有没有 `QWidget`？
- [ ] `render/` 里有没有业务规则？
- [ ] 新增文件是否已加入 CMake？
- [ ] 有没有对应的测试或手工用例？

---

## 17. 禁止事项

以下行为直接打回：

1. `core/` 里 include `<QWidget>` 或写 `QPainter` 绘制代码。
2. 在 `render/` 里写 DRC、校验、网表规则。
3. 绕过 `QUndoCommand` 直接改文档模型。
4. 命令里长期保存模型对象裸指针（新建/删除对象除外，见 §5）。
5. 同一个概念两套数据（模型一份、Scene Item 一份）。
6. mm 和 mil 混用，或单位靠猜。
7. 业务代码里做屏幕坐标转换。
8. 用 `SIGNAL/SLOT` 宏或依赖自动连接命名。
9. 静默失败、只 `qDebug()` 不返回错误。
10. 提交构建产物、Qt DLL、IDE 配置。
11. 未通知就改别人负责的头文件。
12. 把 `feat_fp` 和 `feat_pcb` 互相 include。
13. 把层可见性、激活层、选择集塞进撤销栈或板文件。
14. 保存前不跑结构校验。

---

## 18. 仍需讨论

以下问题不阻塞当前文档，但要在第 10 周前定：

1. 非矩形板框的顶点编辑 P2 是否做；若做，归 C 还是 B。
2. `refdes`/`Net.name` 的 v1 重命名 UI 是否做；若做，按 §5 的原子重命名执行。
3. 自动布局的范围：只做网格，还是加力导向。
4. 一键 L/Z 自动布线是否作为加分项实现。