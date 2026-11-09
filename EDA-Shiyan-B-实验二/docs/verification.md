# 验证记录

日期：2026-11-02。Qt 6.11.1 MinGW 64 位（本地验证套件），GCC 13.1.0，CMake 3.30.5；组内规范主构建基线为 MSVC v143，Windows 11。

## 已执行

- 从独立英文目录配置并编译 eda_core、eda_core_demo、test_core；成功。
- 编译选项 `-Wall -Wextra -Wpedantic`；最终构建没有 C++ 编译警告。
- Qt Test：8 个业务测试，加 init/cleanup，共 10 passed，0 failed。
- CTest 注册运行：1/1 测试程序通过；一键脚本从新英文目录完整配置、构建、测试、演示通过。
- 命令行演示：4 焊盘、4 引脚、1 安装孔、5 类丝印；保存、索引、重载、规范编码比较通过。
- 中文文件路径、名称、引号、反斜杠、换行测试通过。
- 稳定 ID、超过 2^53 的 ID、深拷贝、非法引用、重复 ID、损坏数据、错误版本、文件截断、保存失败不覆盖、库重建及路径穿越拒绝通过。

测试源码：`tests/tst_core.cpp`。样例文件：`examples/footprints/example.fp`。文件格式按团队最终 D4，不使用 JSON 记录。

## 环境问题及处理

- 中文源码目录下，Qt moc 报 Cannot create / Invalid argument；切换到英文源码镜像后通过。
- 本机 CMake 在部分中文目录重复配置中以 -1073740791 退出；英文新目录完整构建通过。
- 已提供 scripts/build.ps1 自动创建英文临时源码副本，保留原目录且不覆盖既有构建。
- QTemporaryDir 默认沙箱临时位置在首次测试中无法原子保存；测试改为在当前可写测试工作目录创建临时目录后通过。
- CMake 的可选 WrapVulkanHeaders 未找到；本模块不使用 Vulkan，不影响配置、构建或运行。

## 未验证/不在范围

- 尚未在组内指定的 MSVC v143 套件上重建；本次实际验证使用已安装 MinGW Qt 套件。
- 未进行 GUI 展示、C 的工艺规则或完整 DIP-8 联调；需要 B/C 接入后共同验证。

## 实验二 A 核心增量

- Qt 6.11.1 MinGW 64 位，ASCII 路径副本配置成功；`eda_core` 与 `eda_core_demo` 编译通过，无编译警告。
- `test_core` 目标已成功编译但本轮没有运行自动化测试；Board/EDA-BRD/几何与 D4.6 round-trip 自动化覆盖仍需由 C 的测试任务补齐。
- 独立静态审阅发现的 2 项 Important 与 3 项 Minor 问题均已修复；修复后重新构建 core、demo 和 `test_core`，无编译警告。
- 中文源路径下 CMake 3.30.5 自动重新配置异常退出；英文路径构建成功。组内 MSVC v143 构建尚未验证。
- 独立 AI 代码审查不代替团队人工 review；未宣称接口已经三人冻结。
- 未推送到 GitHub，也未修改 main/dev 的远程状态。

独立代码审查：已由另一个 AI 审阅模型、读写、库管理、构建与测试，未发现阻止交付的实质性问题；该审查为静态审阅，运行验证由主执行者完成，仍需团队人工 review。

## 2026-11-09 实验二 A 验证

- Qt 6.11.1 MSVC x64、MSVC 19.44（v143）Release 成功构建 `test_core`、`eda_core_demo` 和 `eda_board_demo`。
- MSVC 和 MinGW 的 CTest 均通过。MSVC 的 Qt Test 报告 13 项通过（含初始化与清理），覆盖网表解析和引脚映射、几何距离、D4.6 底面变换、板文件读写及非法保存不覆盖。
- 样例演示输出 2 个元件、1 个网络、7 个板层、2 条走线、1 个过孔和 1 个覆铜区。

## 2026-11-09 B 网表导入与实验二独立版

- 基线更新到 A `8336ee7`，包含独立网表导入和几何距离接口。
- `.net` 导入使用 A 的解析和 resolver，生成 Board 实例与网络；重复导入保持幂等。
- `test_b` 增加 `netlistImportBuildsBoard`，总计 11 个业务用例、13 passed。
- 实验二交付通过 `EDA_DEFAULT_BOARD_DEMO=ON` 默认打开 PCB 编辑器；实验一交付保持封装编辑器默认入口。
- 修复 `MainWindow` 析构期间 `Document` 的 `QUndoStack` 仍回调 UI 的生命周期崩溃；MSVC CTest 连续重复 20 次通过。
- 最新 MinGW/MSVC 构建无 B 模块编译或链接警告；A 的 C4310 测试警告已由其最新提交修复。
