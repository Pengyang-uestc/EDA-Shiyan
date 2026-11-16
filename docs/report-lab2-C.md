# 实验二 印制电路板的绘制与电气连接——实验报告（C 框架）

> 状态：框架稿，截图、运行结果和最终文字待补
> 姓名：李家铭　班级：1601　学号：2025091601018
> 本人角色：C（业务规则与算法）

---

## 封面

- 课程：EDA 软件设计 I
- 实验：实验二 印制电路板的绘制与电气连接
- 姓名：李家铭
- 班级：1601
- 学号：2025091601018
- 日期：2026 年 11 月

---

## 一、本人在本实验中的工作

本人负责实验二的**层组、布局、正交布线、覆铜、连通检查、DRC、网表导入和板级校验**，不负责 A 的 Board/EDA-BRD 底层格式，也不负责 B 的主窗口/画布。具体代码：

| 文件 | 内容 |
|---|---|
| `feat_pcb/layer_service.h/.cpp` | 默认 7 层/3 组、层组增删改、颜色、`stackOrder` |
| `feat_pcb/layer_model.h/.cpp` | 层树 `QStandardItemModel`，供 B 的 `QTreeView` 使用 |
| `feat_pcb/grid_layout.h/.cpp` | 规则网格布局 |
| `feat_pcb/layout_service.h/.cpp` | 把布局结果换算成 `Instance.pos` |
| `feat_pcb/orthogonal_router.h/.cpp` | L/Z 正交路径 |
| `feat_pcb/copper_pour.h/.cpp` | 覆铜创建/删除/编辑/校验 |
| `feat_pcb/connectivity.h/.cpp` | 物理连通检查 |
| `feat_pcb/drc.h/.cpp` | 九类间距 + 板边 DRC |
| `feat_pcb/board_validation.h/.cpp` | 板级制造校验 |
| `feat_pcb/netlist_service.h/.cpp` | 网表 → Board 导入 |
| `feat_pcb/pad_geometry.h/.cpp` | 焊盘轮廓、板级变换、距离、包围盒 |
| `tests/tst_*.cpp` | 板级、连通、DRC、网表、布局、布线、覆铜、层组、层模型测试 |

---

## 二、实验目的

1. 理解 PCB 设计的完整基本流程：建板 → 层组 → 导入网表 → 放置 → 布局 → 布线 → 覆铜 → DRC → 保存。
2. 理解层与层组、层可见性/激活层、走线/过孔归属层。
3. 掌握手动正交布线，理解自动布线的基本思路。
4. 掌握覆铜的绘制、管理、网络指定和避让检查。
5. 理解“网络标签相同”不等于“物理连通”，掌握基于几何接触的连通检查。
6. 掌握 DRC 的铜体距离计算、同网/异层过滤和板边间距。

---

## 三、实验原理

### 3.1 层与层组

PCB 不是一张二维图，而是多层铜、丝印、阻焊、板框的叠加。`BoardLayer` 记录层 ID、所属层组、名称、类型、颜色和 `stackOrder`；`LayerGroup` 记录组名和组间顺序。

- 标准层固定：0 顶层铜、1 底层铜、2 顶层丝印、3 底层丝印、4 顶层阻焊、5 底层阻焊、6 板框；内层/自定义层 ≥100。
- 层组用于组织层，组间按 `group.stackOrder`，组内按 `layer.stackOrder`。
- 可见性、激活层、选择集是**会话状态**，归 `DocumentContext`，不进板文件、不进撤销栈。

### 3.2 布局

布局的输入是每个实例旋转/底面后的轴对齐包围盒，不是原始焊盘尺寸：

- `gridLayout` 按列宽/行高取最大值，行优先排列，`gapMil` 是包围盒边缘距。
- 整组居中放在板框内；放不下返回失败，不输出半成品坐标。
- `layoutInstances` 把包围盒中心换算成 `Instance.pos`，因为 `Instance.pos` 是封装原点，不一定是包围盒中心。
- 底面实例先沿局部 Y 轴镜像，再旋转，再平移；包围盒必须按这个顺序算。

### 3.3 正交布线

手工布线只做水平/垂直线段：

- L 形：先横后竖（HV）或先竖后横（VH）。
- Z 形：中途在 x 或 y 处拐两次；mid 为空取中点，越界夹到起终点区间。
- 同点返回 1 点；同 x/y 返回 2 点；相邻重复点去重。
- 过孔换层不属于路径函数，属于 `RouteTool` 的状态；路径只产二维点列。

### 3.4 覆铜

覆铜是闭合多边形 + 层 + 网络 + 间距 + 优先级：

- 多边形必须是简单多边形，至少 3 点，不自交。
- 层必须是已定义的铜层；网络为空表示未分配，非空必须在 `Board::nets` 中。
- `clearance` 是不小于 0.2 mm 的间距；`priority` 只用于排序，不参与几何。
- 同网络允许接入；异网络做 clearance 检查。
- 本版本的 `rebuild` 只做校验，不重算填充几何；填充由视图/后续算法派生。

### 3.5 物理连通

连通检查不能按 `net` 标签合并，必须建物理图：

- 节点：实例引脚、整条走线、过孔、覆铜。
- 边：同层铜体接触；过孔连接 `from/to` 层；通孔焊盘（drill > 0）同时连接顶层/底层铜；SMD 只连本层。
- 接触按铜体距离：走线用宽度、过孔用直径、圆形焊盘用半径；长圆/圆角矩形用多边形近似。
- 并查集求分量，再判断每个 net 的引脚是否落在同一分量；同一分量出现多个 net 名就是短路。

### 3.6 DRC

DRC 在铜体层面算距离：

- pad-track：焊盘铜体到走线中心线 - `track.width/2`，required = `max(pad.clearance, minClearance)`。
- track-track：中心线距离 - `(w1+w2)/2`。
- via-track：中心距离 - `via.diameter/2 - track.width/2`。
- pad-pad、pad-via、pad-pour、track-pour、via-pour、pour-pour 同理。
- 板边：焊盘/走线/过孔/覆铜到板框边 ≥ 20 mil（项目约定）。
- 同网络跳过；未分配网络仍检查；不同层跳过；圆形焊盘精确，其它形状用多边形近似。

---

## 四、实验内容

1. 层组管理：默认 7 层/3 组，组/层增删、改名、颜色、`stackOrder`、标准层保护、校验。
2. 层树模型：`LayerModel` 给 B 的 `QTreeView` 提供组/层两级结构、颜色、类型、顺序角色。
3. 网表导入：解析 `COMP/NET`，解析封装，建 `Board::instances` 和 `Board::nets`，给初始网格坐标。
4. 布局：`gridLayout` + `layoutInstances`，支持旋转/底面、安装孔/丝印包围盒、凹形板框。
5. 布线：`orthogonalPath` 的 HV/VH/Z_H/Z_V，退化、mid 越界、错误码。
6. 覆铜：创建/删除/改多边形/层/网络/间距/优先级/校验，失败不改 Board。
7. 连通：并查集 + 铜体接触，判断每个 net 是否单连通、是否短路。
8. DRC：九类间距 + 板边，带对象定位。
9. 板级校验：结构/引用、标准层、层组顺序、实例/焊盘越界、走线/过孔/覆铜规则。
10. 测试：每个模块 Qt Test，`ctest 16/16`。

---

## 五、实验步骤

### 5.1 需求分析

- 从 PPT 提取：层组管理、组织元件/走线/过孔、多元件简单布局、引脚电气连接、覆铜与避让。
- 从分工方案确认：A 负责 Board/文件，B 负责画布/面板，C 负责业务规则和算法。
- 明确口径：手动 L/Z 布线必做；一键自动布线只做加分；完整迷宫布线只写算法理解。

### 5.2 详细设计

- 层服务只管持久化层定义；active/visible 归 `DocumentContext`。
- 布局输入是旋转后的包围盒尺寸，输出是包围盒中心；实例原点偏移由 `layoutInstances` 处理。
- 布线路径函数只产点列；换层、命令、吸附由工具处理。
- 连通不按 net 标签，按几何接触；短路由分量网络集合判断。
- DRC 统一用铜体距离；同网/异层过滤；板边单独一组规则。
- 所有直接改 Board 的服务，B 必须包在 `QUndoCommand` 里。

### 5.3 代码实现

| 步骤 | 文件 |
|---|---|
| 层组/层定义 | `feat_pcb/layer_service.*` |
| 层树模型 | `feat_pcb/layer_model.*` |
| 网表导入 | `feat_pcb/netlist_service.*` |
| 网格布局 | `feat_pcb/grid_layout.*` |
| 实例布局 | `feat_pcb/layout_service.*` |
| 正交路径 | `feat_pcb/orthogonal_router.*` |
| 覆铜 | `feat_pcb/copper_pour.*` |
| 连通 | `feat_pcb/connectivity.*`、`union_find.*` |
| DRC | `feat_pcb/drc.*` |
| 焊盘几何 | `feat_pcb/pad_geometry.*` |
| 板级校验 | `feat_pcb/board_validation.*` |

### 5.4 测试用例

| 编号 | 场景 | 预期 |
|---|---|---|
| UT-LAYER-01 | 空 Board ensureDefaults | 3 组/7 层，重复调用幂等 |
| UT-LAYER-02 | 标准层/组删除 | 拒绝 |
| UT-LAYER-03 | 重复名称/order | 拒绝 |
| UT-MODEL-01 | 层树 | 组/层两级、角色、颜色、顺序正确 |
| UT-NET-01 | 正常网表导入 | 实例/网络/层组/板框正确 |
| UT-NET-02 | 缺封装/错误 ID/未知引脚 | 失败 |
| UT-GRID-01 | 四个元件 2 列 | 精确中心坐标、不重叠 |
| UT-GRID-02 | 刚好铺满/凹形板框 | 通过/失败 |
| UT-ROUTE-01 | HV/VH/Z_H/Z_V | 点列正确、严格正交 |
| UT-ROUTE-02 | 同点/同 x/y/mid 越界/NaN/非法枚举 | 退化/错误码 |
| UT-LAYOUT-01 | 两实例布局 | 在板框内、不重叠 |
| UT-LAYOUT-02 | 底面 90°/凹形板框 | 包围盒正确/失败 |
| UT-POUR-01 | 创建/编辑/删除 | ID 稳定、字段正确 |
| UT-POUR-02 | 非法多边形/层/网络/间距/优先级/板外 | 拒绝、失败不改 |
| UT-CONN-01 | 同网走线/过孔换层/THT | 连通 |
| UT-CONN-02 | 间隙/不同网交叉 | 断开/短路 |
| UT-DRC-01 | pad-track/track-track/via-track | 违规与对象定位 |
| UT-DRC-02 | pad-pad/pad-via/pad-pour/track-pour/via-pour/pour-pour | 违规 |
| UT-DRC-03 | 板边/同网/异层 | 违规/跳过 |
| UT-BOARD-01 | Board 结构/EDA-BRD/几何 | 39 个用例通过 |

实际测试命令：

```text
ctest --test-dir build --output-on-failure
```

结果：`ctest 16/16` 通过，C 侧共 235 个用例。

---

## 六、实验结果

> 以下截图待补，至少 2 幅本人模块；可以共用演示板。

| 编号 | 内容 | 截图 |
|---|---|---|
| 图 1 | 层组面板：Top/Bottom/Mechanical、颜色、顺序 | 【待补】 |
| 图 2 | 导入网表后的实例与网络任务列表 | 【待补】 |
| 图 3 | 多选元件 → 自动布局，实例不重叠且在板框内 | 【待补】 |
| 图 4 | L 形/Z 形手动布线，同网连通 | 【待补】 |
| 图 5 | 覆铜区，异网走线被 DRC 拒绝 | 【待补】 |
| 图 6 | DRC 列表：pad-track / track-track / 板边 | 【待补】 |
| 图 7 | 保存 `.brd` 后重载，实例/走线/过孔/覆铜一致 | 【待补】 |

数据结果：

- 层组：3 组/7 层默认，标准层/组保护，层树模型角色正确。
- 布局：DIP-8 等元件在 2000×1000 板框内不重叠。
- 布线：L/Z 点列严格正交，过孔换层后连通。
- 连通：同网单连通、异网交叉短路、SMD/THT 层映射正确。
- DRC：九类间距 + 板边，违规带 `refdes/padId/trackId/viaId/pourId`。
- 覆铜：创建/编辑/删除/校验，板外/非法网络/间距拒绝。

---

## 七、心得体会

> 写自己的坑，不要空话。可写：
> - 为什么连通不能按 net 标签合并；并查集节点怎么设计。
> - 为什么焊盘距离要扣走线宽度/过孔半径，中心线判断会漏。
> - 为什么 `Instance.pos` 不等于包围盒中心，旋转/底面时怎么换算。
> - 为什么凹形板框只查四角会漏，边相交怎么补。
> - 为什么覆铜 `rebuild` 只校验不填充，报告里怎么解释。
> - 服务直接改 Board 时，为什么必须用命令包起来。

【待补 300–500 字】

---

## 八、AI 使用记录

| 日期 | 工具 | 提示词摘要 | 生成/修改内容 | 自己改了什么 | 如何验证 |
|---|---|---|---|---|---|
| 2026-11-02 | Claude Code | 板级模型/DRC/连通/布局/布线 | 初稿 | 修正铜体距离、同网/异层、板边、凹形板框 | ctest 16/16 |
| 2026-11-09 | Claude Code | 层组/覆铜/网表/报告框架 | 增量 | 修正 ID 回滚、标准层保护、失败原子性 | 235 个用例 |

> 详细记录见仓库 `docs/ai-log.md`（如团队统一维护）。

---

## 九、附录

- `feat_pcb/layer_service.h/.cpp`
- `feat_pcb/layer_model.h/.cpp`
- `feat_pcb/grid_layout.h/.cpp`
- `feat_pcb/layout_service.h/.cpp`
- `feat_pcb/orthogonal_router.h/.cpp`
- `feat_pcb/copper_pour.h/.cpp`
- `feat_pcb/connectivity.h/.cpp`
- `feat_pcb/drc.h/.cpp`
- `feat_pcb/board_validation.h/.cpp`
- `feat_pcb/netlist_service.h/.cpp`
- `feat_pcb/pad_geometry.h/.cpp`
- `tests/tst_*.cpp`
- `docs/C-模块与接口.md`