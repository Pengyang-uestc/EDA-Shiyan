# EDA-Shiyan

电子科技大学《EDA 软件设计 I》课程实验仓库。

实验一“电路板元件封装的设计与管理”与实验二“印制电路板的绘制与电气连接”合并为一个简易 PCB 设计工具，分两轮迭代交付：

- 迭代 1：封装编辑器 + 元件库
- 迭代 2：PCB 编辑器（层组、放置布局、布线、覆铜）

技术栈：C++17 · Qt 6.11.1 Widgets · QPainter · CMake。

## 分工

| 成员 | 角色 | 主要负责 |
|---|---|---|
| A | 数据与存储 | 模型、几何、文件格式、网表、构建 |
| B | 图形与交互 | 画布、绘制、主窗口、命令框架、面板控件 |
| C | 业务规则与算法 | 封装校验与模板、库业务、层组、布局、布线、覆铜、DRC、测试 |

## 节奏

第 9 周冻结接口，第 10 周实验一闭环，第 11 周实验二前半（层组、放置、布局），第 12 周布线、覆铜、DRC、验收。每周一次 4 学时实验课，主体代码课外完成。

## 文档

- [团队统一约定 v1.1](docs/CONVENTIONS.md)
- [定稿接口与文件格式](docs/DECISIONS.md)：单位、坐标、命名、所有权、错误处理、撤销边界、接口冻结与 Git 规范。

> 约定文档目前是草案，待三人评审后冻结为 v1.0；冻结前以文末“仍需讨论”和 §0 的决策为准。

## 目录（规划）

```text
core/      模型、几何、单位、文件、网表
render/    画布、绘制、拾取、ITool 接口
app/       主窗口、菜单、撤销栈、DocumentContext
feat_fp/   实验一：封装编辑、库管理、校验
feat_pcb/  实验二：层组、布局、布线、覆铜、DRC
tests/     Qt Test 与手工用例
docs/      方案、约定、接口记录、测试计划、AI 记录
```

## 实验一 A 模块（已实现）

- [本机运行与 B/C 接入](docs/A-使用与对接.md)
- [封装文件格式](docs/footprint-format.md)
- [接口变更记录](docs/interface-changelog.md)
- [验证记录](docs/verification.md)

入口是 `CMakeLists.txt`；核心库目标 `eda_core`，命令行演示 `eda_core_demo`，Qt Test 目标 `test_core`。

## 实验二 A 核心基础（增量）

- `core/board.h`、`core/geometry.h`：板级数据模型、稳定对象 ID、通用几何和封装实例坐标变换。
- `core/io.h`：`EDA-BRD 1` 板文件和网络记录读写，使用 resolver 校验封装/引脚引用。
- 当前增量尚未在组内 MSVC v143 套件验收；C 需补齐板文件、稳定 ID 和 D4.6 坐标 round-trip 自动化用例。

## 实验一、实验二 B 模块（已接入）

- 封装编辑器、PCB 编辑器、主窗口、QPainter 画布、属性和图层面板、撤销/重做。
- `Document` 支持 Footprint/Board 双模式，`.fp` 和 `.brd` 均可打开和保存。
- PCB 画布支持板框、层组、实例、走线、过孔、覆铜、多层可见性、对象命中和拖动。
- `ILibraryService`、`ILayerService`、`IValidationService` 通过构造函数注入。
- `LibraryPanel`/`LayerPanel` 使用服务模型驱动；本地适配器可在 C 服务未就绪时运行。

详细说明见 [B 使用与对接](docs/B-使用与对接.md) 和 [B 测试记录](docs/B-test-plan.md)。
