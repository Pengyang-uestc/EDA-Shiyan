# 实验二 B 交付

本目录是实验二“PCB 绘制与电气连接”的独立 B 版本，默认启动即进入 PCB 示例。

## 启动

用 Qt Creator 打开本目录的 `CMakeLists.txt`，选择 Qt 6.11.1 MSVC 2022 x64 Kit，运行 `eda_gui`。

本目录的 `EDA_DEFAULT_BOARD_DEMO` 默认为 `ON`，因此不需要额外参数就会打开 PCB 示例。

命令行构建：

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH=<Qt kit directory>
cmake --build --preset dev
ctest --preset dev
.\build\dev\eda_gui.exe
```

## 实验二功能

- `.brd` 打开和保存。
- 板框、层组、顶层/底层实例、走线、过孔和覆铜渲染。
- 图层可见性和当前层、对象单击/Ctrl 多选/框选/拖动/删除。
- Board 编辑撤销/重做。
- `.net` 导入：使用 A 的 COMP/NET 解析和库 resolver，生成实例与网络。
- 无 C 布局服务时使用规则网格初始放置；再次导入不会重复创建实例和引脚。

使用说明：`docs/B-使用与对接.md`  
测试记录：`docs/B-test-plan.md`  
PCB 截图：`docs/images/b-board-window.png`
