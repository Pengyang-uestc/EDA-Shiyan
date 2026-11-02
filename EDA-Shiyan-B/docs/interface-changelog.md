# 接口变更记录

2026-11-02：新增实验一 A 模块首版（待团队评审，未宣称已冻结）。

- 按仓库 CONVENTIONS v1.1 / DECISIONS v1.1 采用 Status/Result、Silk 前缀、FootprintLibrary、EDA-FP 1 / EDA-LIB 1。
- Pin 使用 PadId，不采用 PDF 早期页的数组下标说法。
- `nextPadId` 由模型维护；按 D6，文件载入后设置为现存最大 PadId + 1。文档打开期间 ID 递增且不复用。
- Footprint 显式深拷贝，支持 B 的命令快照；所有运行时指针均为短期借用。
- 安装孔字段严格按 D4 定稿。
- 库 list 以扫描 footprints/ 为实时索引；rebuildIndex 写出 EDA-LIB 1 到 index.idx。
- io、library 是低层结构/存储接口，C 的工艺校验必须先于保存调用。

2026-11-02：新增实验二 A 板级核心（待 B/C review，接口仍未冻结）。

- 新增 `Board`/层组/板层/实例/走线/过孔/覆铜/网络模型、稳定 ID 分配和几何 helper。
- 新增 `EDA-BRD 1` 编码、解析、引用校验、原子保存；网络清单按板文件 `net` 记录解析。
- `FootprintResolver` 以同步 `Result<Footprint>` 值回传库封装，避免跨层借用生命周期。
- 未新增独立网表格式、DRC、制造规则、GUI 或 `app/services.h`；C 的 `validation.h` 提案待 A/C 单独确认。

2026-11-02：B 接入 Board 并完成服务注入（本地交付，未推送）。

- 新增 `IValidationService`、`ValidationIssue`、`ValidationSeverity`，同时校验 Footprint 和 Board。
- `ILibraryService` 提供模型、刷新、加载、保存、导入、删除和根路径操作。
- `ILayerService` 增加 `setDocumentContext()`；模型角色统一为 `ServiceRoles`。
- `MainWindow` 构造时注入库、图层、校验三个服务。
- `Document` 支持 Footprint/Board 两种模式；`Document::board()` 在 PCB 模式下返回真实 A `Board`。
- 新增 `ReplaceBoardCommand`，以值快照保证实例/走线/过孔/覆铜编辑可撤销并保留稳定 ID。
- `CanvasWidget` 支持 FootprintLookup、Board 渲染、对象命中和拖动。
- `.brd` 打开/保存使用 A 的 `loadBoard/saveBoard` 和 `ILibraryService` resolver。
