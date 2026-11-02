# 实验一 A 部分：使用与对接

实现范围：封装对象模型、带版本文件格式、原子保存/载入、元件库底层存取、索引快照、规格到属性映射、构建与模块测试。没有实现 B 的窗口/画布或 C 的工艺规则/模板生成器，也没有把实验二的功能伪装成已完成。

## 本机运行

组内构建基线按 [CONVENTIONS.md](CONVENTIONS.md) D2 使用 Qt 6.11.1 + MSVC v143。当前机器安装的是 Qt 6.11.1 MinGW 套件，本次本地测试使用它和 GCC 13.1；MinGW 只作为验证，不能代替组内指定的 MSVC 构建验收。MSVC 必须搭配 MSVC 版 Qt，不能链接 MinGW 版 Qt。

本机 moc 在中文绝对路径中失败。推荐把 GitHub 仓库 clone 到英文路径，例如 `F:/Develop/EDA-Shiyan`。保留当前中文目录时，在 PowerShell 执行：

```powershell
cd 'F:/EDA实验课/实验一'
./scripts/build.ps1 -QtRoot 'E:/LeStoreDownload/6.11.1/mingw_64' -CompilerBin 'E:/LeStoreDownload/Tools/mingw1310_64/bin' -CMake 'E:/LeStoreDownload/Tools/CMake_64/bin/cmake.exe' -Ninja 'E:/LeStoreDownload/Tools/Ninja/ninja.exe'
```

脚本将源码复制到新的英文临时目录，构建、运行测试、运行保存/重载演示，并打印结果位置；不覆盖源码，不更改系统 PATH，不删除已有目录。修改源码后重新运行即可。Qt Creator 直接构建请使用英文项目路径。

其他机器在匹配编译器、CMake、Ninja、Qt bin 已配置的终端：

```text
cmake --preset dev -DCMAKE_PREFIX_PATH=<Qt kit directory>
cmake --build --preset dev
ctest --preset dev
```

Windows 运行测试/演示时，PATH 必须包含对应 Qt bin 和编译器运行库目录。

## B 的接入

链接 `eda_core`；包括 `core/footprint.h` 和 `core/io.h`。模型由文档拥有，UI 编辑通过 B 的撤销命令。加载返回新模型，只有成功时才替换当前文档并清空撤销栈。所有调用默认 UI 线程。

```cpp
auto loaded = eda::loadFootprint(path);
if (!loaded.status.ok) {
    // 把 loaded.status.message 显示给用户，原文档不变
    return;
}
documentFootprint = std::move(*loaded.value);

auto saved = eda::saveFootprint(path, documentFootprint);
if (!saved.ok) { /* 显示 saved.message */ }
```

`Footprint` 支持深拷贝，丝印对象独立拥有；不要在命令中长期保存 QVector 元素地址。`findPad()` 返回的只读指针在容器修改后可能失效；长期引用存 `PadId`，每次重新查找。

`addPad()` 返回 `Result<PadId>`，成功时分配 ID；在当前文档生命周期内，删除后不复用。按 D6，载入文件时分配器设为当前最大 PadId + 1。`removePad()` 在有 Pin 引用时失败。撤销命令应恢复原 ID 和所需数据，不重新分配。多对象删除应作为一个命令保存与恢复关联 Pin。

## C 的接入

包括 `core/library.h`；`FootprintLibrary` 提供 `list/load/save/remove/rebuildIndex`。调用方拥有 Result.value；失败时 value 为空，必须先判断 status.ok。

`list()` 总是扫描当前 `.fp` 文件，因此保存/删除后列表立即准确。`index.idx` 是显式 `rebuildIndex()` 生成的 EDA-LIB 1 可重建快照，不是事实来源；不读取损坏/过期索引，避免“两文件事务”只成功一半。需要导出索引时，在文件操作完成后单独调用 rebuildIndex，并分别处理两个状态。

ID 使用 1～64 位小写 ASCII 字母、数字、短横线、下划线，首位为字母或数字；拒绝 Windows 保留名。名称支持中文。ID 创建后保持不变，重命名只修改 name。

一个损坏 `.fp` 会令 list 返回带文件名的错误，而非静默漏掉条目；C 可提示用户修复/移出该文件。

**保存闸门**：先由 C 调用 `validateFootprint`，Error 阻止保存，Warning 经用户确认后继续；然后调用 A 的低层 save。A 保证结构合法、引用有效和原子写入，不替代焊盘间距、孔径配合等制造规则。

## 规格到属性映射

| 规格数据 | 模型字段 | 约定 |
|---|---|---|
| 引脚间距/排距 | 各 Pad.pos 差值 | 使用 units.h 从 mm 转 mil |
| 焊盘直径/宽高 | Pad.size | mil，中心为 Pad.pos |
| 焊盘形状 | Pad.shape | Circle/Rectangle/Oblong/RoundRect；圆角半径写入 cornerRadius |
| 钻孔直径 | Pad.drill | mil，0 为无孔 |
| 阻焊扩展 | Pad.maskMargin | 每侧径向扩展量，开窗宽高=焊盘宽高+2×扩展量 |
| 安全间距 | Pad.clearance | 焊盘边界外的最小间距，C 判定规则 |
| 管脚编号/电气类型 | Pin.number/type | Pin.padId 指向稳定焊盘 ID |
| 安装孔直径 | MountingHole.drill | mil |
| 孔外禁布距离 | MountingHole.keepout | 从孔边缘向外，禁布半径=drill/2+keepout |

| 丝印线宽 | Primitive.width | mil |

封装坐标为 footprint-local，X 向右、Y 向上；模板决定原点，不在载入时自动平移。文字 pos 为基线起点，height 为模型高度；B 决定字体像素换算。矩形 rect 的 x/y 是最小 X/Y，宽高为正（不要按屏幕 topLeft 语义再次翻转）。圆弧从 +X 轴起算，逆时针为正，单位为度。

`exampleFootprint()` 是包含四种焊盘和五种丝印图元的集成样例，不是经制造验证的 DIP-8。实际 DIP-8 与 RAD 模板由 C 按规格生成。

## 实验二 A 的板级基础（增量）

`core/board.h` 提供 `Board`、层组、板层、实例、走线、过孔、覆铜、网络和网络引脚值模型。`core/geometry.h` 提供 mil 坐标下的面积、简单多边形、点包含、线段相交/距离，以及封装局部坐标与板世界坐标的 D4.6 正反变换和底面层映射。

`core/io.h` 提供 `encodeBoard/decodeBoard/saveBoard/loadBoard`，采用 [DECISIONS.md D4.3](DECISIONS.md#d43-板文件-eda-brd-1) 的 `EDA-BRD 1`。解析支持前向引用，整体校验层组/层、对象 ID、网络、实例、封装和引脚引用；板框载入后规范为逆时针。网络清单是板文件的 `net` 记录，不另设网表文件格式。加载失败不会替换调用方现有模型；保存使用 `QSaveFile` 原子提交。

`FootprintResolver` 返回 `Result<Footprint>`，可直接转接 `FootprintLibrary::load`：

```cpp
auto boardResult = eda::loadBoard(boardPath, [&](const QString& footprintId) {
    return library.load(footprintId);
});
```

Board 结构检查不执行焊盘间距、最小线宽等制造规则；这些规则仍由 C 实现。track/via/pour 分配器只增不减；耗尽时返回 `kInvalidId`。显式 `insert*` 保留传入 ID，供撤销命令用同一 ID 恢复对象。

## GitHub 衔接

目标仓库： https://github.com/Pengyang-uestc/EDA-Shiyan

本目录保留从仓库读取的 README 和 CONVENTIONS，并新增 A 的实现。提交前由本人理解代码、由 B/C 核对公开接口；不要直接覆盖 main。建议个人分支 `feature/pengyang-core`，PR 指向 dev（如团队尚未创建 dev，先由仓库维护者建立）。
