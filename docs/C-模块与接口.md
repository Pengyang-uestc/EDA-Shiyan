# C 模块与接口说明

> 状态：2026-11-09，基于 A 的 `feature/pengyang-core`（含 Board、EDA-BRD、网表、几何距离）
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
| `core/validation.h` | `Issue` / 错误码 | 封装、板级、层、覆铜共享结果类型 | — |
| `core/manufacturing_rules` | 阈值 | 钻孔、环宽、阻焊、间距、丝印、线宽、板边 | — |

当前 C 侧共 230 个用例，`ctest 15/15`。

---

## 2. B 接 UI 需要的接口

### 2.1 网表导入

B 的 `app/services.h` 目前没有网表接口。建议加：

```cpp
class INetlistService {
  public:
    virtual ~INetlistService() = default;
    virtual eda::Result<eda::NetlistImportResult> importFile(
        const QString& path,
        const eda::FootprintResolver& resolver,
        const eda::NetlistImportOptions& options = {}) const = 0;
};
```

具体实现用 `eda::NetlistService`，由 `main.cpp` 创建并注入。返回的 `Board` 直接交给 `Document::installBoard()`。

### 2.2 保存前校验

B 的 `IValidationService::validateBoard(const Board&)` 没有 `FootprintResolver`，接不了 C 的 `validateBoardManufacturing`。建议改成：

```cpp
virtual eda::Status validateBoard(
    const eda::Board& board,
    const eda::FootprintResolver& resolver) const = 0;
```

或者让服务持有 `ILibraryService`，内部用 `library.load()` 作为 resolver。

封装校验：

```cpp
virtual QVector<eda::Issue> validateFootprint(
    const eda::Footprint& footprint) const = 0;
```

B 的 `ValidationIssue` 和 `eda::Issue` 需要做一次适配；建议直接用 `eda::Issue`，减少两套类型。

### 2.3 层服务

B 的 `ILayerService` 要求 `QAbstractItemModel* model()`。当前 `LayerService` 只有 `QVector<LayerGroup>/<BoardLayer>`，B 需要一层适配：

- 方案 A：C 在 `feat_pcb` 加 `LayerModel : QStandardItemModel`，`ILayerService` 直接返回它；
- 方案 B：B 的层面板自己根据 `groups/layers` 建模型，C 只提供数据。

建议方案 A，面板不用重复写模型。`active/visible` 仍归 `DocumentContext`，`LayerService` 不持有。

### 2.4 工具

- `PlaceTool`：调用 `layoutInstances` 得到新 `positions`，构造 `ReplaceBoardCommand`。
- `RouteTool`：调用 `orthogonalPath`，构造添加走线/过孔命令。
- `PourTool`：调用 `CopperPourService` 的 add/set*，包在命令里。
- 所有服务直接改 `Board` 的入口，都必须在 `QUndoCommand` 内调用；`undo/redo` 用前后 Board 快照。

---

## 3. 合并注意

- `core/validation.h`、`core/manufacturing_rules.*` 是 C 放进 A 的 core 的共享类型/阈值，需要 A 确认接收。
- C 的 `feat_pcb` 只依赖 `core`，不依赖 `app`；B 的 `ITool` 在 `render/`，C 的工具实现会放在 B 的 `feat_pcb` 目录，但接口仍由 B 提供。
- `main` 目前还包含 B 的嵌套 `EDA-Shiyan-B-实验一/`；合并前需要 B 拍平目录，否则根 CMake 会有两套 `core`。
- C 的 PR #4、#7 已合入 `feature/pengyang-core`；板边 DRC、布局、布线、实例布局、覆铜、层服务在 PR #8。

---

## 4. 已知限制

- 长圆/圆角矩形用多边形近似；圆形和轴对齐矩形精确。
- 非正方形 `Circle` 被封装校验阻止保存。
- 板边 DRC 默认 20 mil 是项目约定，不是 IPC 强制值。
- DRC 不含丝印/阻焊规则；覆铜服务 `rebuild` 只校验，不重算填充几何。
- 网表初始布局和 `layoutInstances` 是两套语义：前者 origin 左上角、全局最大 cell；后者居中后平移到 origin。
- 服务直接改 Board，B 必须用命令包起来。
- 板边/覆铜在板外会在 `board_validation`、`drc`、`copper_pour` 中重复报，调用方需去重。