# 裸机画面直接展开到 framebuffer

本文记录 DIRECT-FRAMEBUFFER 基线版本。后续已新增 [动态 NAT 图形与文字](native-graphics-framebuffer.md)：142函数包中的13个绘图/字形计算入口会在NAT内部即时镜像LCD，并单独记录宿主耗时；下面的“129函数、不含图形”描述只适用于基线包。

## 输出路径

旧裸机版本已绕过 GUI 绘图 SDK，但仍执行：客机 LCD RAM → 1920 字节逻辑快照 → 在 `g_screen_frame` 中展开变化行 → 将整个 19200 字节屏幕复制到 `0x003c0000`。

本次改为：客机 LCD RAM → 同一逻辑快照和变化行检测 → 查表直接写 `0x003c0000` 的两条物理行。普通提交不再经过 `g_screen_frame`，也不再整屏复制。每条变化的客机行写 160 字节；一次完整恢复写 19200 字节。

仍保持 159×96、2×显示、物理位置 `(1,24)`，左右各一像素白边和上下各24行白边；最后一个客机字节的隐藏第160像素不显示。客机可读写显存及其别名不变，IRAM、NAT、HLE、游戏计时和帧批次不变。

## 恢复和兼容

- 第一次输出完整恢复物理屏幕。
- SDK 服务临时恢复系统后（`bare_rom_suspends` 变化）再次完整恢复，即使客机快照没有变化，也不能跳过；系统可能覆盖物理屏幕。
- GUI Timer 兼容路径继续维护自己的 `g_screen_frame` 和 SDK 提交，不把它改成无保护的直接写屏。
- Loading 清屏、退出前等待按键释放、桌面备份和恢复流程保持原样。

离线编译器的新运行库还会在客机 LCD 字节/图形行写入时立即镜像到宿主屏幕。本次只改变模拟器的统一提交路径，**不是**将 IRAM/NAT/HLE 的所有客机存储点改为即时镜像；两者都直写物理 framebuffer，但更新时间点不同。

## 日志和性能解释

`framebuffer_direct_version=1` 表示启用这条裸机路径。`framebuffer_direct_rows` 是实际写入的物理行数，`framebuffer_direct_bytes` 等于行数乘80；`framebuffer_direct_full_refreshes` 是包含白边的完整恢复次数。`screen_submissions` 包含恢复提交，`render_updates` 只统计客机画面变化。

上一版真机日志中屏幕转换和提交合计约占2.48%，因此本次减少复制流量，并不承诺整体大幅提速。比较新旧日志时，不能把提交次数增加的恢复帧当成游戏运行速度提升。

## 验证工具

- `tests/test_direct_framebuffer.py`：运行真实 C 展开函数，对照独立逐像素参考。
- `tests/check_emulator_lcd_frame.py`：暂停指定的隔离模拟器，读取客机逻辑快照，对全部76800个物理屏幕像素进行核对；恢复原来的运行状态。这不是性能测试。

2026-09-08 验证：1611 个 C 逐像素案例通过，普通构建和 ASAN/UBSAN 构建均通过；隔离模拟器片头、主菜单、开场剧情各检查76800像素，差异均为0，长按退出恢复桌面。不是全流程游戏回归，也没有用模拟器帧率代表真机性能。

目标 EXE：675009 字节，SHA256 `eccde54e80ad6f697257db8c31727d4d0924c1661842432db89a03dff8e7d44b`。IRAM 仍为5392字节，NAT 内容与 ROM-INDEX 版一致。

## 不要把输出耗时当作全部绘图耗时

`bare_render_ticks` 是逻辑 LCD 快照/变化检测，`bare_present_ticks` 是2×展开和宿主屏幕写入；这些是宿主256 Hz计数器测得的耗时。图片解码、移位、遮罩、文字合成等游戏/固件绘图运算仍计入 `bare_core_ticks`，当前轻量日志不能从中单独分离这些函数的宿主耗时。

当前129函数的 FUNCTION NAT 包主要是 bank、算术、字符串、内存及 C 运行库入口，没有 `SysPicture`/`SysChinese` 专用图形入口。图形 HLE 仍在 EXE 内，更新客机 LCD RAM；NAT 的 `SysMemcpy` 即使目的地是显存，也经 `write8` 更新客机内存，而非写 `0x003c0000`。`native_shared_calls/blocks` 是次数，`native_shared_guest_cycles` 是客机周期，不能据此计算这些函数的宿主耗时占比。
