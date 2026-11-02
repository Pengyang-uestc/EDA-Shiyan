# 接口变更记录

2026-11-02：新增实验一 A 模块首版（待团队评审，未宣称已冻结）。

- 按仓库 CONVENTIONS v1.1 / DECISIONS v1.1 采用 Status/Result、Silk 前缀、FootprintLibrary、EDA-FP 1 / EDA-LIB 1。
- Pin 使用 PadId，不采用 PDF 早期页的数组下标说法。
- `nextPadId` 由模型维护；按 D6，文件载入后设置为现存最大 PadId + 1。文档打开期间 ID 递增且不复用。
- Footprint 显式深拷贝，支持 B 的命令快照；所有运行时指针均为短期借用。
- 安装孔字段严格按 D4 定稿。
- 库 list 以扫描 footprints/ 为实时索引；rebuildIndex 写出 EDA-LIB 1 到 index.idx。
- io、library 是低层结构/存储接口，C 的工艺校验必须先于保存调用。
- 实验二 Board/geometry/netlist 尚不在此交付范围；不能作为已实现接口引用。
