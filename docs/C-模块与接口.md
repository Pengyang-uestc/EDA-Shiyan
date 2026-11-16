# C 模块与接口说明

> 状态：2026-11-16，基于拍平后的 `integration/eda`（含 A 的 Board/EDA-BRD/网表、B 的 app/render）
> 负责人：C（业务规则与算法）

这份文档给 B 接 UI、给 A 做合并、给报告写“本人工作”用。所有模块都在 `feat_fp/`、`feat_pcb/`，测试在 `tests/`。

---

## 1. 模块清单

| 模块 | 主要 API | 职责 | 测试 |
|---|---|---|---|
| `feat_fp/footprint_validation` | `validateFootprint` | 封装结构 + 制造规则：钻孔、环宽、阻焊、间距、引脚/焊盘引用、安装孔、丝印线宽 | 15 |
| `feat_fp/footprint_templates` | `makeDip8/16`, `makeRad01`, `makeAxial04` | 标准封装模板，生成后跑制造校验 | 7 |
| `feat_pcb/board_validation` | `validateBoardManufacturing` | 板级结构/引用、标准层、层组/层顺序、实例/焊盘越界、走线/过孔/覆铜规则 | 19 |
| `feat_pcb/connectivity` | `analyzeConnectivity` | 物理连通：pin/track/via/pour，同层铜体接触、过孔跨层、THT/SMD 层 | 15 |
| `feat_pcb/pad_geometry` | `padOutline`, `distance*ToPad`, `footprintBounds`, `instanceBounds`, `padCopperLayers` | 焊盘几何、板级变换、距离、封装/实例包围盒 | 11 |
| `feat_pcb/drc` | `runDrc` | 九类间距 + 板边：pad/pad、pad/track、pad/via、pad/pour、track/track、track/via、track/pour、via/via、via/pour、pour/pour、板边 | 26 |
| `feat_pcb/netlist_service` | `NetlistService::importData/importFile` | COMP/NET → Board 实例/网络 + 初始网格 | 10 |
| `feat_pcb/grid_layout` | `gridLayout` | 规则网格、列宽/行高取最大、整组居中 | 17 |
| `feat_pcb/orthogonal_router` | `orthogonalPath` | HV/VH/Z_H/Z_V、退化、mid 越界、`RouteError` | 18 |
| `feat_pcb/layout_service` | `layoutInstances` | gridLayout → `Instance.pos`，支持旋转/底面/凹形板框 | 7 |
| `feat_pcb/copper_pour` | `CopperPourService` | 覆铜创建/删除/改多边形/层/网络/间距/优先级/校验 | 15 |
| `feat_pcb/layer_service` | `LayerService` | 默认 7 层/3 组、组/层增删改、颜色/stackOrder、校验 | 10 |
| `feat_pcb/layer_model` | `LayerModel` | 只读层树（组→层两级）、颜色/类型/顺序角色 | 5 |
| `feat_pcb/drc_report` | `collectDrcIssues` | 把 `DrcResult` 的 violations/errors 映射成可定位的 `DrcIssue` | 1 |
| `app/c_services` | `ManufacturingValidationService`、`LayerModelService` | 把 C 的校验和层模型接到 B 的 `IValidationService`/`ILayerService`（B 集成时并入 c_services） | 5 |
| `core/validation.h` | `Issue` / 错误码 | 封装、板级、层、覆铜共享结果类型 | — |
| `core/manufacturing_rules` | 阈值 | 钻孔、环宽、阻焊、间距、丝印、线宽、板边 | — |

当前 C 侧共 265 个测试入口、18 个套件，`ctest 18/18`。

---

## 2. B 接 UI 需要的接口

### 2.1 网表导入

B 的"导入网表"已经用 A 的 `loadNetlist` + `resolveNetlistPins` 跑通，保持这一套即可，不必为了统一再造一个入口。C 的 `NetlistService::importData/importFile` 是可选封装：一次调用产出 `Board`，带初始布局和字段校验；如果要换，`MainWindow::importNetlistFile` 里的三步换成一次 `importFile`，返回值仍交给 `Document::installBoard()`。

### 2.2 保存前校验（已接）

`app/c_services.h` 里的 `ManufacturingValidationService` 实现 B 的 `IValidationService`：

- 构造时传入 `eda::FootprintResolver`，通常包一层库服务：`[library](const QString& id) { return library->load(id); }`
- `validateFootprint` → `eda::validateFootprint`
- `validateBoard` → `eda::validateBoardManufacturing`，并把 `eda::validateFootprint` 作为封装规则回调传入，实例焊盘不会退化成"未检查"
- `eda::Issue` 映射成 `ValidationIssue`，错误码以 `[CODE] message` 的形式放在消息前，`padId` 保留

B 只需要在 `main.cpp` 把 `StructuralValidationService` 换成：

```cpp
ManufacturingValidationService validationService(
    [&libraryService](const QString& id) { return libraryService.load(id); });
```

`LocalLibraryService` 保留，它已经包装 A 的 `FootprintLibrary`。

### 2.3 层服务（已接）

`LayerModelService::model()` 返回两级树：

- 顶层是组：`IsGroupRole = true`，带 `GroupIdRole`、`StackOrderRole`
- 子行是层：`LayerIdRole`、`LayerColorRole`、`LayerTypeRole`、`StackOrderRole`

B 的 `LayerPanel` 用 `QTreeView` 直接消费；封装模式下没有 Board 时退回 6 个标准层的平铺列表。`active/visible` 仍归 `DocumentContext`；增删改层调 `eda::LayerService`，结果包进 `ReplaceBoardCommand`。

### 2.4 DRC / 连通（已接）

`feat_pcb/drc_report.h` 把 `DrcResult` 映射成列表项：

```cpp
QVector<DrcIssue> collectDrcIssues(const DrcResult&);
QVector<DrcIssue> collectDrcIssues(const Board&, const FootprintResolver&, const DrcRules& = {});
```

`DrcIssueKind::Clearance` 来自 `violations`，`Resolution` 来自 `errors`（封装/焊盘解析失败，code 固定为 `DRC_RESOLVE_ERROR`）。每条带 `refdes / padId / trackId / viaId / pourId` 和实际、要求间距，B 的列表双击即可定位。

连通直接用 `ConnectivityResult`：`nets` 给出每个网络的连通性与分量数，`shortedNets` 是短路，`errors` 是解析失败，不需要再包一层。

### 2.5 工具

- `PlaceTool`：调用 `layoutInstances` 得到新 `positions`，构造 `ReplaceBoardCommand`。
- `RouteTool`：调用 `orthogonalPath`，构造添加走线/过孔命令。
- `PourTool`：调用 `CopperPourService` 的 add/set*，包在命令里。
- 所有服务直接改 `Board` 的入口，都必须在 `QUndoCommand` 内调用；`undo/redo` 用前后 Board 快照。

---

## 3. 合并注意

- `core/validation.h`、`core/manufacturing_rules.*` 是 C 放进 A 的 core 的共享类型/阈值，需要 A 确认接收。
- `feat_pcb` / `feat_fp` 只依赖 `core`；唯一依赖 B 的 `app/services.h` 的是 `app/c_services.*`，把它算作 app 层胶水，不要挪进 `feat_pcb`。
- `feature/b-integration-tasks` 已 rebase 到 `integration/eda`，`a12cffd` 的修复随之落到根路径；文档、`drc_report` 和 C 的文档全部保留。
- 两份适配器合并时保留了 B 的树模型结构，并补回错误码前缀与空 resolver 兜底；`manufacturing_services.*` 已删除，避免同名类重复定义。

---

## 4. 已知限制

- 长圆/圆角矩形用多边形近似；圆形和轴对齐矩形精确。
- 非正方形 `Circle` 被封装校验阻止保存。
- 板边 DRC 默认 20 mil 是项目约定，不是 IPC 强制值。
- DRC 不含丝印/阻焊规则；覆铜服务 `rebuild` 只校验，不重算填充几何。
- 网表初始布局和 `layoutInstances` 是两套语义：前者 origin 左上角、全局最大 cell；后者居中后平移到 origin。
- 服务直接改 Board，B 必须用命令包起来。
- 板边/覆铜在板外会在 `board_validation`、`drc`、`copper_pour` 中重复报，调用方需去重。
- `DrcIssue` 没有单独的 severity：间距违规（`Clearance`）和解析失败（`Resolution`）都算 Error，UI 按 `kind` 分组显示。
- `LayerModelService::refresh()` 每次清空重建整棵树，层数很少，没有做增量更新；层多时再考虑按行更新。