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

2026-11-09：补全实验二 A 接口。

- 新增 `decodeNetlist/loadNetlist` 和 `resolveNetlistPins`，按 COMP/NET 导入独立网表并解析到稳定焊盘 ID。
- 新增线段间、点到多边形、线段到多边形距离及包围盒预筛。板文件的 D4.3 字段保持兼容。
- 新增 `eda_board_demo` 和配套样例，供 B/C 用相同板数据联调。

2026-11-09：B 接入实验二网表导入与双实验交付。

- `MainWindow::importNetlistFile()` 接 A 的 `loadNetlist()` 和 `resolveNetlistPins()`。
- 导入时验证库封装与稳定焊盘引用，生成 Board 实例和网络；重复 refdes/引脚去重。
- 默认网格放置作为 C 布局服务接入前的兜底，不写入 `render/` 或 `core/`。
- 新增 `EDA_DEFAULT_BOARD_DEMO` CMake 选项，供实验二独立交付默认打开 PCB 编辑器。
