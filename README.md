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

## 实验二 A 数据与存储

- `core/board.h`、`core/geometry.h`：板级数据模型、稳定对象 ID、通用几何和封装实例坐标变换。
- `core/io.h`：独立 `.net` 导入、引脚到焊盘映射，以及 `EDA-BRD 1` 板文件读写。
- `eda_board_demo`：读取 [网表样例](examples/board-demo.net) 和 [板文件样例](examples/board-demo.brd)。运行方式及接口约定见 [A 组使用与对接](docs/A-使用与对接.md)。
