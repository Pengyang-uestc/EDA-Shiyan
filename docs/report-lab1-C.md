# 实验一 电路板元件封装的设计与管理——实验报告（C 框架）

> 状态：框架稿，截图、运行结果和最终文字待补
> 姓名：李家铭　班级：1601　学号：2025091601018
> 本人角色：C（业务规则与算法）

---

## 封面

- 课程：EDA 软件设计 I
- 实验：实验一 电路板元件封装的设计与管理
- 姓名：李家铭
- 班级：1601
- 学号：2025091601018
- 日期：2026 年 11 月

---

## 一、本人在本实验中的工作

本人负责实验一的**封装业务规则与标准模板**，不负责 A 的模型/文件格式，也不负责 B 的主窗口/画布。具体代码：

| 文件 | 内容 |
|---|---|
| `feat_fp/footprint_validation.h/.cpp` | `validateFootprint`：封装结构 + 制造规则 |
| `feat_fp/footprint_templates.h/.cpp` | DIP-8、DIP-16、RAD-0.1、AXIAL-0.4 模板 |
| `core/manufacturing_rules.h/.cpp` | 钻孔、环宽、阻焊、间距、丝印、板边阈值 |
| `core/validation.h` | 共享 `Issue`、严重级别、错误码 |
| `tests/tst_footprint_validation.cpp` | 15 个校验用例 |
| `tests/tst_footprint_templates.cpp` | 7 个模板/文件往返用例 |
| `docs/footprint-spec.md` | 标准封装规格提取表 |

B 的 UI 只负责显示和交互；A 的 `saveFootprint` 只做结构校验和原子写入；C 的校验在保存前调用，Error 阻止落盘。

---

## 二、实验目的

围绕“把元件物理规格变成软件数据模型，并保证数据可制造、可复用”展开，具体包括：

1. 理解封装对象如何聚合焊盘、引脚、安装孔、丝印和文本。
2. 理解焊盘形状、钻孔、阻焊开窗、安全间距等属性及其制造含义。
3. 掌握标准封装（DIP-8、RAD-0.1 等）的尺寸提取和模板化。
4. 掌握封装保存前的规则校验和错误定位。
5. 理解“绘图程序”与“PCB 封装工具”的区别：不仅要画得像，还要数据合法。

---

## 三、实验原理

### 3.1 封装对象模型

封装（Footprint）不是一张图片，而是一组有语义的对象：

- `Pad`：焊盘，包含形状（圆/矩形/椭圆/长圆/圆角矩形）、中心坐标、宽高、钻孔、阻焊扩展、安全间距、所属层和稳定 `PadId`。
- `Pin`：引脚，包含编号、名称、电气类型，并通过 `PadId` 引用焊盘，不重复存坐标。
- `MountingHole`：安装孔，包含坐标、孔径、是否金属化、孔外禁布距离。
- `SilkLine/SilkArc/SilkRect/SilkPolygon/SilkText`：丝印图元，统一有层、线宽、是否填充；多态对象由 `std::unique_ptr<Primitive>` 持有。

封装使用 footprint-local 坐标，单位 mil，Y 向上。实例化到板上时，再由 `Instance.pos/rotation/side` 映射到 board-world。

### 3.2 焊盘与制造属性

焊盘的几何不只是一个矩形：

- 圆焊盘用直径；矩形用宽高；长圆和圆角矩形需要额外形状参数。
- 钻孔 `drill` 决定环宽：环宽 = `(min(padW, padH) - drill) / 2`，必须满足最小环宽。
- 阻焊开窗 `maskMargin` 是每侧扩展量，开窗尺寸 = 焊盘尺寸 + 2×扩展量。
- 安全间距 `clearance` 是焊盘边缘到其它铜体的最小距离。
- 通孔焊盘（drill > 0）在顶层和底层都有铜；SMD 焊盘只在本层。

### 3.3 校验分层

校验分两层：

1. **结构校验**（A 的 `validateStructure`）：ID 非零/唯一、引用存在、单位是 mil、字段有限且非负、枚举合法。
2. **制造规则校验**（C 的 `validateFootprint`）：在结构合法的基础上，检查钻孔范围、环宽、阻焊、间距、引脚重复引用、焊盘编号唯一、安装孔孔径、丝印线宽、圆形焊盘宽高一致。

结果统一为 `Issue { severity, code, message, padId, ... }`。`Error` 阻止保存，`Warning` 允许用户确认后继续。保存流程是“先校验，再 `QSaveFile` 原子写入”，避免半截文件。

### 3.4 标准封装模板

模板不是硬编码一张图，而是按规格表生成对象：

- DIP-8：2 列 × 4 行，100 mil 间距，300 mil 排距，63 mil 焊盘，32 mil 钻孔，1 脚方形。
- DIP-16：2 列 × 8 行，尺寸相同。
- RAD-0.1：2 脚，100 mil 间距，71 mil 焊盘，36 mil 钻孔。
- AXIAL-0.4：2 脚，400 mil 间距，71 mil 焊盘，36 mil 钻孔。

模板生成后立即跑 `validateFootprint`，避免“结构合法但制造规则不合法”的模板进入库。

---

## 四、实验内容

1. 实现封装制造校验：钻孔、环宽、阻焊、间距、引脚/焊盘引用、焊盘编号、安装孔孔径、丝印线宽、圆形焊盘宽高。
2. 实现四个标准封装模板，包含焊盘、引脚、丝印本体、1 脚缺口、文字。
3. 把模板和校验接入 A 的 `Footprint`/`Result<T>`，不另建模型。
4. 为规则和模板写 Qt Test，覆盖正常、边界、非法值、文件往返。
5. 给 B 的保存流程提供 `validateFootprint` 调用点，Error 阻止保存。
6. 维护 `docs/footprint-spec.md`，把物理规格映射到模型字段。

---

## 五、实验步骤

### 5.1 需求分析

- 从 PPT 提取：焊盘形状/钻孔/阻焊/间距、丝印线宽、封装保存/载入、元件库、工业标准。
- 从分工方案确认：A 负责模型和文件，B 负责 UI，C 负责规则和模板。
- 明确不在本实验范围：SMD 封装、3D 高度、拼板、热焊盘。

### 5.2 详细设计

- `validateFootprint` 先调用 A 的 `validateStructure`，失败直接返回一条 `STRUCTURE`。
- 每条制造规则独立生成 `Issue`，带 `padId/pinNumber`，方便 UI 定位。
- 阈值集中在 `core/manufacturing_rules.h`，不散落魔数。
- 模板用 `Footprint::addPad` 分配稳定 `PadId`，再建 `Pin.padId` 引用。
- 模板 `finish()` 调 `validateFootprint`，有 Error 就不返回模板。

### 5.3 代码实现

| 步骤 | 文件 |
|---|---|
| 定义 Issue/错误码 | `core/validation.h` |
| 定义阈值 | `core/manufacturing_rules.h/.cpp` |
| 实现校验 | `feat_fp/footprint_validation.cpp` |
| 实现模板 | `feat_fp/footprint_templates.cpp` |
| 接入 CMake | `feat_fp/CMakeLists.txt` |
| 测试 | `tests/tst_footprint_validation.cpp`、`tests/tst_footprint_templates.cpp` |

### 5.4 测试用例

| 编号 | 场景 | 预期 |
|---|---|---|
| UT-FP-01 | DIP-8 模板 | 8 焊盘/8 引脚，`nextPadId=9`，无 Issue |
| UT-FP-02 | 钻孔 3 mil | `DRILL` Error |
| UT-FP-03 | 长条焊盘 100×40，钻孔 32 | `ANNULAR_RING` Error |
| UT-FP-04 | 阻焊 10 mil | `MASK_MARGIN` Error |
| UT-FP-05 | 间距 1 mil | `CLEARANCE` Error |
| UT-FP-06 | 两个 Pin 引用同一 Pad | `PIN_PAD_DUPLICATE` Error |
| UT-FP-07 | 同层焊盘过近 | `PAD_SPACING` Error |
| UT-FP-08 | 不同层焊盘同坐标 | 不报 `PAD_SPACING` |
| UT-FP-09 | 非正方形 Circle | `CIRCLE_NOT_SQUARE` Error |
| UT-FP-10 | 未引用焊盘 | `PAD_UNREFERENCED` Warning |
| UT-FP-11 | 安装孔 3 mil | `HOLE_DRILL` Error |
| UT-FP-12 | 丝印线宽 1 mil | `SILK_WIDTH` Error |
| UT-FP-13 | 四个模板 encode/decode | 字段一致 |
| UT-FP-14 | 模板生成后跑制造校验 | 全部通过 |

实际测试命令：

```text
ctest --test-dir build --output-on-failure
```

结果：`footprint_validation`、`footprint_templates` 均通过。

---

## 六、实验结果

> 以下截图待补，至少 2 幅本人模块。

| 编号 | 内容 | 截图 |
|---|---|---|
| 图 1 | DIP-8 模板在编辑器中打开，8 个焊盘、1 脚方形、丝印本体 | 【待补】 |
| 图 2 | 属性面板显示焊盘钻孔/阻焊/间距 | 【待补】 |
| 图 3 | 非法钻孔被拦截，提示定位到焊盘 | 【待补】 |
| 图 4 | 保存后重载，封装一致 | 【待补】 |

数据结果：

- 4 个模板全部通过 `validateFootprint`。
- 15 个封装校验用例、7 个模板用例通过。
- DIP-8 文件 round-trip 后 `Pad` 的 id/number/shape/pos/size/drill/mask/clearance/cornerRadius/layer、`Pin` 的 number/name/type/padId、丝印 kind/layer/width/filled 全部一致。

---

## 七、心得体会

> 写自己的坑，不要空话。可写：
> - 为什么环宽要按短边算，长条焊盘怎么被误判。
> - 为什么圆焊盘要禁止非正方形，否则显示和 DRC 语义不一致。
> - 为什么 `Pin` 用 `PadId` 而不是下标。
> - 模板生成后为什么要立刻跑制造校验，而不是只跑结构校验。
> - 文件 round-trip 中文字宽度不落盘的取舍。

【待补 300–500 字】

---

## 八、AI 使用记录

| 日期 | 工具 | 提示词摘要 | 生成/修改内容 | 自己改了什么 | 如何验证 |
|---|---|---|---|---|---|
| 2026-11-02 | Claude Code | 按规格生成封装校验与模板 | 初稿 | 修正环宽按短边、非正方形圆焊盘、文字宽度 | Qt Test 15+7 |
| 2026-11-09 | Claude Code | 补板级/DRC/布局等 | 后续模块 | 见实验二报告 | ctest 16/16 |

> 详细记录见仓库 `docs/ai-log.md`（如团队统一维护）。

---

## 九、附录

- `feat_fp/footprint_validation.h/.cpp`
- `feat_fp/footprint_templates.h/.cpp`
- `core/validation.h`
- `core/manufacturing_rules.h/.cpp`
- `tests/tst_footprint_validation.cpp`
- `tests/tst_footprint_templates.cpp`
- `docs/footprint-spec.md`