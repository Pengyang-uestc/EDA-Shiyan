# 实验一 B 交付

本目录是实验一“封装设计与元件库”的独立 B 版本，默认启动封装编辑器。

## 启动

用 Qt Creator 打开本目录的 `CMakeLists.txt`，选择 Qt 6.11.1 MSVC 2022 x64 Kit，运行 `eda_gui`。

命令行构建：

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH=<Qt kit directory>
cmake --build --preset dev
ctest --preset dev
.\build\dev\eda_gui.exe
```

## 实验一功能

- 封装文件新建、打开、保存。
- 元件库搜索、刷新、导入和删除。
- 图层面板、焊盘属性编辑。
- 线、圆弧、矩形、多边形、文字、焊盘、引脚和安装孔。
- 单击、Ctrl 多选、框选、拖动、控制点改尺寸和删除。
- `QUndoStack` 撤销/重做。

使用说明：`docs/B-使用与对接.md`  
测试记录：`docs/B-test-plan.md`  
界面截图：`docs/images/b-main-window.png`
