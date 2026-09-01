# BBK 9288 GAM4980 Player

[![Validate 9288 port](https://github.com/HelloClyde/bbk9288-gam4980-player/actions/workflows/validate-9288.yml/badge.svg)](https://github.com/HelloClyde/bbk9288-gam4980-player/actions/workflows/validate-9288.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](COPYING)

将 A 系列 GAM4980 模拟器移植到 BBK 9288。程序使用 9288 原生 KF2 格式和
9288 SDK，并已在
[`bbk9288-emulator`](https://github.com/HelloClyde/bbk9288-emulator) 中验证
《伏魔记》的文件选择、启动、主菜单和开场剧情。

## 游戏截图

![《伏魔记》开场剧情](assets/screenshots/fumozhuan-dialog.png)

> 截图仅展示兼容性；仓库与发布包均不包含《伏魔记》或其他 `.gam` 游戏文件。

## 快速开始

1. 从 [Releases](https://github.com/HelloClyde/bbk9288-gam4980-player/releases/latest)
   下载 `bbk9288-gam4980-player.zip`。
2. 解压后保持目录结构，把 `系统` 和 `gam4980` 两个目录复制到 9288 的
   `A:\` 根目录。
3. 将自己有权使用的 `.gam` 游戏文件复制到 `A:\gam4980\`。
4. 在 9288 的“娱乐”分类中运行 `GAM4980`，然后从文件选择界面打开游戏。

文件列表第一项是 `[设置]`：可以选择是否在加载游戏时扫描 AOT 模板、是否启用
实验性固件 HLE、是否记录性能日志，以及使用正常或 2 倍运行速度。设置保存在
`A:\gam4980\GAM4980.CFG`；开启“性能调试”后，每次正常退出游戏都会向
`A:\gam4980\PERF.LOG` 写入本次记录并覆盖上一次记录，因此可以直接提交整个
日志文件。

安装完成后的主要文件如下：

```text
A:\系统\程序\GAM4980.exe
A:\gam4980\8.BIN
A:\gam4980\E.BIN
A:\gam4980\GAM4980.NAT
A:\gam4980\你的游戏.gam
A:\gam4980\你的游戏.GNA        （可选，电脑端离线全量原生包）
```

## 功能

- 9288 的 320×240 屏幕上，将 159×96 游戏 LCD 精确放大两倍为 318×192，
  显示在 `(1, 24)`，不裁切、不插值。
- 直接把 1 bpp 游戏画面转换到 9288 的 320×240、2 bpp 离屏画面，再通过
  GUI 整帧接口显示。
- 应用安装在“娱乐”分类，使用带 9288 原生圆角投影框的 40×40、16×16
  四级灰度图标。
- 每次启动都显示 `.gam` 文件选择界面，即使目录中只有一个游戏；界面只使用
  9288 SDK 的窗口、绘图、字体和键盘 API。
- 选择游戏后显示分阶段 Loading 文本，明确区分运行库准备、文件读取、HLE
  匹配、CFG 分析、AOT 索引、存档读取、原生模块预载和游戏启动；AOT 阶段
  显示 pass 进度。
- 支持完整 9288 键盘，包括方向键、数字、QWERTY、翻页键和功能键。
- `.sav` 与 `.gam` 位于同一目录并使用相同主文件名；正常退出时写回变化。
- 两片 2 MiB ROM 使用 4 KiB bank 缓存，Flash 按游戏实际大小分配，适配
  9288 的 8 MiB SDRAM。
- 固定 E.BIN 的 926 个已识别 AOT 块全部编译为 S1C33 机器码，并按虚拟窗口和
  物理 bank 分装进 `GAM4980.NAT` 的 20 个模块。原 512 KiB ROM cache/工作区
  总预算拆为 256 KiB ROM cache 与 256 KiB 四槽原生代码 arena；启动只预载
  首个模块，冷模块命中时从磁盘按需装入并按 LRU 覆盖。模块缺失、hash/函数
  指纹不符或未驻留时自动回退 IRAM/解释器，不影响游戏兼容性。进入模块后会让
  客机寄存器保持在宿主寄存器/局部量中，连续执行同模块基本块，直到 deadline、
  bank 变化或跨模块/HLE 边界才一次性写回；固定模块内分支/调用会直接跳目标标签，
  普通 RAM/ROM 读取使用页表快路径，减少中心分派和通用访存 callback。
- 支持电脑端离线生成与游戏同名的 `.GNA` 全量原生包。生成器按 GAM 头中的
  16 KiB C6502 bank 布局扫描全部 `.bf_call` 编译器入口，再递归恢复直接调用、
  分支和跳转 CFG，把每个可静态证明的基本块编译成无重定位 S1C33 模块；9288
  启动游戏时只校验完整 GAM 大小/hash/入口并按页从磁盘装载，不在真机上编译。
  未知间接目标、动态生成代码和不支持的入口仍原子回退 IRAM 执行器。Flash 若
  改写已编译的真实指令跨度，会立即撤销本次游戏的 GNA 页入口，避免执行陈旧
  原生代码；`GAME.GNA` 也可作为 FAT 长文件名异常时的兼容别名。
- 使用 9288 原生 GUI `MSG_TIMER`（ID 1、speed 1）驱动主循环；系统节拍约
  24.9 ms，正常模式以相位累加器交替推进 1、2 个客机帧，平均约 60 帧/秒。
- 仿照 9288 版《雷霆战机》，以 `SysBltFrame` 后接 `InvalidateRect` 的顺序
  提交离屏画面，不直接写显存。
- 6502 解释器默认使用 computed-goto 和直接栈访问；主循环不会再把 Timer
  的 `speed` 参数误当成消息频率。
- 9288 正式构建默认使用 `-O2`，让编译器内联解释器的指令读取热路径；相比
  先前偏向模拟器表现的 `-Os` 构建，EXE 约增加 11 KiB，不增加运行时内存。
- 工具链补丁会识别 computed-goto 形成的大型间接跳转状态机，避免把数百个
  立即数提升成贯穿整个解释器的长生命周期寄存器；当前 `s6502_exec` 的栈帧
  在纯解释器构建中从 34 个字缩到 12 个字，启用 AOT 后为 15 个字；纯解释器
  的静态栈读取点从 1222 个降到 598 个。
- 正式构建默认启用受保护的 E.BIN 离线 AOT：从启动到开场剧情的 8000 帧
  完整轨迹中发现 926 个“物理 PC + 虚拟 PC”固件基本块，现已全部从 KF2
  移到 20 个可分页 S1C33 原生模块。`8.BIN` 是字库/数据 ROM，同一轨迹没有
  6502 指令从中取出，因此不做伪代码翻译。模块进入前核对物理映射和全部
  函数指纹；HLE 入口仍优先走整函数替换，未识别或签名不符的代码回退 IRAM
  执行器。当前 KF2 约 669 KiB、`GAM4980.NAT` 约 257 KiB，模块 arena 不增加
  原 512 KiB 工作预算。通用发行包不绑定或打包任何 `.gam`；可选 `.GNA` 只含
  特定 GAM 的原生代码和绑定元数据，不包含原始 `.gam`。详见
  [`docs/aot-profiling.md`](docs/aot-profiling.md)。
- 游戏加载时 AOT 已升级为 C6502 编译器感知的整程序翻译。离线生成器从旧 A 系列
  开发包恢复段布局、ABI 和机器码模板；加载 GAM 时恢复 CFG，识别七类控制流
  超级指令和 13 类 16 位运算、伪寄存器、参数、间接访存及 `.bf_call` 语义，
  并把跨 bank 调用直接链接到真实目标。直链会精确构造客机栈和 bank 5–8 映射，
  目标 `RTS` 仍返回原固件后缀恢复旧 bank；周期预算、签名或调用表不匹配时自动
  回退。匹配结果调用 KF2 内预编译的通用 S1C33 语义模板，不在真机上运行
  编译器，也不绑定或打包某个游戏；任意尚未识别的 GAM 指令仍由 IRAM 执行器
  处理。三款 GAM 的语义 AOT
  开/关、直链开/关状态回归一致。加载器只扫描 GAM 头声明的代码区，各优先级
  只运行对应模板分类；关闭 AOT/HLE 时会彻底跳过相应扫描。详见
  [`docs/c6502-whole-program-aot.md`](docs/c6502-whole-program-aot.md)。
- 设置中提供默认开启、可关闭的“固件 HLE”开关。当前快速路径替换已确认
  E.BIN 签名下的普通/32 像素宽字形合成、直接存储器填充和两段高频位图循环，
  以及 `$D1A2-$D200` 的 16×16 位无符号乘法，并保留原 6502 周期数和
  时间片边界；游戏函数匹配还会把 2 位位图的取数、子像素、宽度判断、换行和
  下一行地址计算融合，并能从周期切片落在循环中部的位置继续，
  并批量执行对象数组扫描、三字节记录查找、二级对象表查询链及跨乘法调用的
  完整对象处理流程；相邻的反向三字节记录处理也会整段批量执行。这些调用者级
  路径在《伏魔记》8000 帧轨迹中把剩余逐指令画像从约 400.1 万降到 160.3 万；
  固件字节、bank、CPU 模式或运行条件不匹配时自动回退
  AOT/解释器。详见
  [`docs/firmware-hle.md`](docs/firmware-hle.md)。
- 设置中提供默认关闭的“运行速度：2倍”。它保持 9288 GUI Timer 和送屏路径
  不变，只把每个 Timer 批次推进的客机帧数从 3 增加到 6；剧情和游戏逻辑均会
  以两倍速度运行。
- 早期 47 块版本在主机核心微基准中相对纯解释器约提升 1.52 倍；926 块版本
  已通过 8000 帧全状态与 S1C33 模拟器等价回归，但性能只以相同场景的 9288
  真机计时和日志为准，不能用主机或模拟器墙钟时间代替。
- 2× 屏幕输出把位展开、水平对齐和双行复制合并成一次查表循环；在输出逐字节
  一致的主机微基准中，应用侧画面预处理约为旧实现的 4.2 倍，同时仍保持
  20 fps GUI 提交频率和 `SysBltFrame` 真机兼容路径。查表占用 2 KiB 静态
  scratch，不消耗堆内存。
- 图片/字体 HLE 使用 32 项映射感知只读跨度缓存；LCD RAM 写入只标记 dirty 行，
  提交时只重新展开变化行。核心仍由 GUI `MSG_TIMER` 驱动，但每次按最近客机
  Timer/IRQ 截止周期连续执行，不再固定每 `$800` 周期退出解释器。

## 仓库结构

```text
assets/9288/                 9288 四级灰度图标源文件和设备资源
src/gam4980_9288*.c         9288 窗口、运行时和启动代码
src/gam4980_core.*          GAM4980 模拟核心
src/s6502.c                 6502 解释器
tests/                      核心、模拟器和 2× 几何验证
toolchain/                  9288 GNU33 ABI 的 S1C33 LLVM 补丁
应用/数据/游戏/gam4980/    运行所需的 8.BIN、E.BIN 和 GAM4980.NAT
```

旧 9588 前端和打包脚本不属于本仓库；上游实现仍可在
[`gam4980-player-for9588`](https://github.com/HelloClyde/gam4980-player-for9588)
获取。

本次移植中的 SDK/ABI、内存、KF2、GUI 刷屏、Timer、输入和真机验证经验
已整理到 [`docs/9288-porting-lessons.md`](docs/9288-porting-lessons.md)。
9288 SDK 的重定位表、调用约定、窗口生命周期、Timer、RTC 和 V1.5 固件兼容槽位
集中记录在 [`docs/9288_sdk.md`](docs/9288_sdk.md)。

## 安装

把发布包中的文件复制到 9288 的 `A:\`：

```text
A:\系统\程序\GAM4980.exe
A:\gam4980\8.BIN
A:\gam4980\E.BIN
A:\gam4980\GAM4980.NAT
A:\gam4980\你的游戏.gam
A:\gam4980\你的游戏.GNA        （使用离线全量原生包时）
```

仓库和发布 ZIP 均包含运行所需的 `8.BIN`、`E.BIN`，但不包含游戏文件。
请自行复制有权使用的 `.gam`。

## 构建

### 1. 准备 9288 SDK

取得 `app_env_9288`，将其放在仓库的 `sdk/`，或构建时通过 `--sdk` 指定路径。
`sdk/` 被 Git 忽略，构建不会读取 9588 SDK。

### 2. 准备 GNU33 ABI 工具链

开源 S1C33 LLVM 后端的默认 ABI 与 9288 固件不同。9288 使用 R6–R9 传递前
四个参数、R4 返回，并由加载器保留 R15。本项目补丁还修正大型直接线程解释器
中的立即数提升导致的寄存器压力。下面的 WSL/Linux 示例固定到本项目验证过的
工具链提交：

```bash
git clone https://github.com/autch/piece-toolchain-llvm.git
cd piece-toolchain-llvm
git checkout afd7c6bdac84314c30da4609eb7a2a47cc3ad3a0
git submodule update --init llvm
git -C llvm apply /path/to/bbk9288-gam4980-player/toolchain/9288-gnu33-abi.patch

cmake -G Ninja -S llvm/llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD= \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=S1C33 \
  -DLLVM_DEFAULT_TARGET_TRIPLE=s1c33-none-elf \
  -DLLVM_ENABLE_PROJECTS='clang;lld' \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_PARALLEL_LINK_JOBS=1
ninja -C build clang lld llvm-objcopy llvm-readelf
```

### 3. 构建 KF2 和发布 ZIP

```bash
python3 build_9288.py \
  --sdk /path/to/app_env_9288 \
  --toolchain /path/to/piece-toolchain-llvm/build/bin
python3 package_release_9288.py
```

为某个 GAM 在电脑上生成同名全量原生包并随构建输出：

```bash
python3 build_9288.py \
  --sdk /path/to/app_env_9288 \
  --toolchain /path/to/piece-toolchain-llvm/build/bin \
  --native-game /path/to/你的游戏.gam
```

这个步骤由电脑上的 S1C33 LLVM 完成；9288 只加载 `.GNA`，不会在 Loading 阶段
重新翻译。游戏文件改变后必须重新生成对应 `.GNA`，否则 hash 校验失败并自动回退
`GAM4980.NAT`/IRAM 路径。

输出：

```text
build/9288/GAM4980.exe
build/bbk9288-gam4980-player.zip
```

`build_9288.py` 会规范化旧 SDK 头文件中的 Windows 路径和大小写，构建
freestanding S1C33 ELF，再封装 KF2 头和图标。`package_release_9288.py` 会
校验 KF2 布局、分类、图标、ROM 大小和 SHA-256，并生成可重复的 ZIP。

如需排查编译器兼容性，可向 `build_9288.py` 传入 `--switch-dispatch`，使用
较慢但可移植的 `switch` 分派。AOT 默认开启；需要做纯解释器对照时可传入
`--no-aot`。游戏加载时 AOT、运行时设置和按需性能日志默认启用；需要构建
不含这些功能的对照版本时可传入 `--no-game-load-aot`。设计、回退条件、
日志字段和基准结果见
[`docs/game-load-aot.md`](docs/game-load-aot.md)。

`--aggressive-region-hle` 是真机评估用的可选实验：它把 E.BIN `$5C5D` 开始的
完整位图行合并成一次原生调用，但只处理当前 CPU 时间片容得下的完整行，不跨过
Timer/IRQ 调度边界。该实验不会改变默认发布构建，验证和覆盖数据见
[`docs/firmware-hle.md`](docs/firmware-hle.md)。

## 图标

最终源图是 `assets/9288/gam4980-icon-imagegen-v3.png`。修改后可重新生成设备
资源：

```bash
python3 -m pip install Pillow
python3 tools/convert_9288_icon.py \
  --frame-root /path/to/app_env_9288/apmk
```

## 操作

- 方向键：游戏方向键
- Enter：游戏 Enter
- Backspace：游戏 Delete
- Tab：游戏 Input
- Space、Shift、0–9、A–Z：对应原机按键
- Page Up / Page Down：对应原机翻页键
- F1–F12：Speak、CE、汉英、双解、Power、Menu、Modify、Shift、Search、
  Download、Help、Exit
- 短按 Esc 或 F12：发送游戏 Exit
- 长按 Esc 或 F12 一秒：关闭 9288 应用

## 验证

6502 ADC/SBC 算术与寻址回归测试：

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Isrc \
  tests/s6502_arithmetic_test.c -o build/s6502_arithmetic_test
./build/s6502_arithmetic_test
```

桌面核心冒烟测试：

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
  -Wno-unused-parameter -DDL_DOWN -D_RLS_ -Isrc \
  tests/core_smoke.c src/gam4980_core.c -o build/core_smoke
./build/core_smoke \
  应用/数据/游戏/gam4980/8.BIN \
  应用/数据/游戏/gam4980/E.BIN \
  /path/to/test.gam
```

AOT 热点分析工具只在电脑端启用逐指令钩子，不会进入 9288 构建。下面的例子
运行 5000 个客机帧，并在指定帧注入 Enter：

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
  -Wno-unused-parameter -DDL_DOWN -D_RLS_ \
  -DGAM4980_ENABLE_PROFILING -Isrc \
  tools/gam4980_aot_profile.c src/gam4980_core.c \
  -o build/gam4980-aot-profile
./build/gam4980-aot-profile \
  --frames 5000 --start-frame 3300 \
  --key 3300:enter --key 3480:enter --key 4380:enter \
  --frame-output build/aot-final-frame.pbm \
  --output build/aot-profile.csv \
  应用/数据/游戏/gam4980/8.BIN \
  应用/数据/游戏/gam4980/E.BIN \
  /path/to/test.gam
```

输出按“物理 PC + 虚拟 PC”归并基本块，并给出 50%～99% 的动态指令覆盖率。
设计、限制和《伏魔记》开场阶段的基准见
[`docs/aot-profiling.md`](docs/aot-profiling.md)。

模拟器验证时先复制原始 NAND，再用增量安装脚本写入应用、ROM 和自备游戏。
脚本会保留 `kernel.bin`，不要把唯一的原始 NAND 作为输出：

```bash
python3 tests/prepare_emulator_nand.py \
  --image-tool /path/to/emulator/scripts/bbk9288s_nand_image.py \
  --base /path/to/emulator/runtime/nand-user.raw \
  --source build/emulator-staging \
  --output /path/to/emulator/runtime/gam4980-test.raw \
  --flat build/gam4980-test-flat.img
```

以 QMP 端口 4444 启动模拟器后：

```bash
python3 tests/emulator_qmp_smoke.py --output build/emulator-smoke
python3 tests/check_2x_frame.py build/emulator-smoke/05-story-dialog.ppm
```

冒烟脚本会进入“娱乐”、启动 GAM4980，并走到《伏魔记》开场剧情。几何检查
要求每个源像素精确对应一个 2×2 像素块，客户区左右和底部边距保持白色。

## 上游与许可

模拟器核心源自：

- [无云 / BBK-simulator](https://gitee.com/BA4988/BBK-simulator/tree/BA4988/BA4988)
- [iyzsong / gam4980](https://codeberg.org/iyzsong/gam4980)

源代码采用 GPLv3，详见 [`COPYING`](COPYING)。仓库中的 `8.BIN`、`E.BIN`
随项目提供，但不属于 GPLv3 源码许可范围。9288 SDK、固件、NAND、游戏和
第三方工具链不包含在本许可中。
