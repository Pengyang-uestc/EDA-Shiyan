# 文件格式说明（遵循团队决策 D4）

封装和元件库索引使用仓库定稿的逐行语法。两者均 UTF-8、LF、首个非注释行是 magic/version，空行和行首 # 注释忽略，字符串按双引号和反斜杠转义，最后必须是 end。数字只接受十进制小数，不接受指数、NaN、Inf；坐标与角度固定三位小数。

## 封装：EDA-FP 1（.fp）

格式与字段定义以 [docs/DECISIONS.md](DECISIONS.md) D4.2 为准，支持 pad、pin、hole、silk line/arc/rect/poly/text 记录。Pad ID 用正十进制整数；解析完成后校验重复 ID、悬空 pin 引用、必需 id/name/units 和非法尺寸。载入完成后 nextPadId 按团队 D6 决定设为最大Pad ID + 1。文本图元字体由视图决定，文件只保存 D4 约定字段。

圆、矩形、椭圆、长圆、圆角矩形映射到 circle/rect/oval/oval/roundrect；D4 的 oval 加载为 PadShape::Oblong。ElectricalType 按 input/output/bidir/passive/power/nc 映射。丝印颜色由所在层绘制样式提供，文件字段遵照 D4，不重复储存颜色。

saveFootprint 先进行结构校验，再用 QSaveFile 和 UTF-8 QTextStream 原子写入；检查写流状态并提交。生产规则校验仍由 C 在调用前执行。解析错误包含文件行号，失败不改变输出模型。

## 库索引：EDA-LIB 1（.idx）

根目录索引由 FootprintLibrary::rebuildIndex() 写出，条目使用 D4.4 的 footprint <id> "name" "relative/path" 语法；所有封装文件放在 footprints/<id>.fp。相对路径保存在索引中，不含绝对路径。

库列表直接扫描 footprints/ 作为当前状态来源，因此即使索引需要重建，条目也能被枚举；.idx 是可再生索引。损坏的封装文件会带文件名返回错误，不会静默跳过。库 ID 使用安全的 ASCII 子集，并拒绝路径穿越和符号链接。

Pad ID 加载为 quint64，保持完整的 64 位整数精度。坐标和尺寸用三位小数序列化；几何 round-trip 容差按团队精度约定使用 kEpsilonMil。
