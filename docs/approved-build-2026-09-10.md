# 2026-09-10 真机确认版本

用户已确认本轮播放器性能可接受，并确认手绘 BBK 学习机图标。
性能结论来自用户真机反馈，不代表所有游戏达到满速。

- 核心实现与回归范围见 [HLE 原生合并](hle-native-consolidation.md)。
- 主图标来源：`assets/9288/manual-grid-v4/dictionary-30x30.png`。
- 正式资源：`assets/9288/ico1.bin`、`ico2.bin`；主图标保留原版 9288 外框。
- 图标生成：`python tools/draw_manual_9288_icon.py`，转换资源：`python tools/convert_9288_icon.py`。
- 确认图标版 EXE SHA256：`c97d0ec65d40b9cfb4d4e8c535e4e48f336c013d14b58146bda58a7da9257df4`。
- EXE 执行载荷与 HLE native consolidation 版本逐字节一致，仅替换图标资源。
- NAT 不变：SHA256 `d1bfb5d32bf7c97f4e44a6a3bbe33ce0c92588ce2f96de3d8e08e2d977b4882b`。

本次仅提交源码、测试、文档和图标，不发布 Release，不加入 GAM、存档、日志、SDK 或生成的游戏专用 EXE。
