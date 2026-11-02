# AI 使用记录

## 2026-11-02 · A 部分

- 工具：Codex；模型：GPT-6 Luna。
- 用户请求：按三人分工方案完成实验一 A 部分，并提供现有 GitHub 仓库及 Qt 安装信息。
- 生成内容：core 模型、行式文件读写、目录库、Qt Test、CMake、命令行演示和使用文档。
- 根据仓库资料修订：Status/Result 错误约定、Silk 命名、EDA-FP 头、独立 units.h。
- 验证：实际编译与测试结果见 verification.md；发现本机工具在中文路径下的问题，使用英文目录验证。
- 人工修改与审阅：尚待本人完成，不将 AI 自查记成人工 review；提交/合并前需理解核心实现并与 B/C 核对接口。

## 2026-11-02 · B 续接最新 Board 基线

- 用户请求：基于 A 最新 GitHub 内容继续完成 B。
- 拉取结果：A 新增 `core/board.*`、`core/geometry.*` 和 `EDA-BRD 1` 读写；远端 B 目录分支仍基于旧 main。
- 修改：以 A 最新分支为基线，保留服务化 B，新增 Board 模式、Board 命令、层服务上下文、板框/实例/走线/过孔/覆铜渲染、对象命中拖动、`.brd` 打开保存和 PCB 示例。
- 验证：MinGW 与 MSVC 构建；`test_b` 增加 Board 撤销、板层服务、对象命中和 EDA-BRD round-trip。
- 人工审阅：仍需 B 本人理解代码并由 A/C 检查 Board 只读接入边界。
