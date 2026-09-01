# 9288 IRAM 汇编执行引擎：构建与回归

`--iram-exec-engine` 默认选择寄存器驻留的 S1C33 汇编实现，并定义
`GAM4980_IRAM_EXEC_ASM`。汇编单元为 `src/s6502_iram_asm.S`。旧的 C IRAM
执行循环仍保留，可用 `--iram-exec-c` 显式选择，便于等价回归和真机 A/B。

目标版本构建命令：

```powershell
python build_9288.py `
  --sdk sdk `
  --toolchain D:\path\to\s1c33-llvm\bin `
  --lightweight-performance `
  --aggressive-region-hle `
  --iram-hot-core `
  --bare-session `
  --iram-exec-engine `
  --iram-exec-asm `
  --optimization 2 `
  --output build\9288\GAM4980-IRAM-ASM.exe
```

省略 `--iram-exec-asm` 结果相同；保留显式参数可让实验命令准确记录执行
引擎类型。构建旧 C 对照版时，仅将它替换为 `--iram-exec-c`。

最小构建回归：

```powershell
python tests\run_9288_iram_exec_regression.py `
  --sdk sdk `
  --toolchain D:\path\to\s1c33-llvm\bin `
  --also-build-c-control
```

脚本先运行审计器自身测试，再构建汇编目标；可选项会额外生成 C 对照版。
目标构建自身会生成 `build/9288/GAM4980.iram.dis.txt`，并强制检查：

- 完整 `.iram` 不超过真机验证过的 5832 字节；
- 汇编入口 `s6502_iram_exec_burst_asm` 精确链接在 IRAM `0x800`；
- 执行引擎不引用固件加载器保留的 R15；
- 除 `pushn`/`popn` 保存 ABI 寄存器外，不出现 SP 访问或编译器式栈 spill；
- 所有直接 `call`/`jp` 都不离开已安装的 IRAM overlay；默认真机版本不允许
  间接调用。只有显式实验参数 `--dynamic-native-all` 才允许一条经审计的
  `call %r13`，用于进入加载阶段已经过机器码签名验证的外部原生块；
- 256 项 opcode 跳转表完整；当前表项为 16 位 IRAM 绝对地址、每两个表项
  打包在一个 32 位字中，且每个目标均位于执行引擎内的真实指令边界，不能
  指向半条指令或跳转表数据。

当前汇编循环把 16 位 PC 截断和宿主页映射集中在页边界、控制流和写回路径。
普通页内指令直接执行“取 opcode、16 位表查找、间接跳转”，只有 opcode 位于
`xxff` 时才走跨页尾路径；`ffff` 回绕 page 0 会原子退回 C 路径，避免绕过
page-0 的 DATA/BK 副作用。AOT/HLE 控制入口使用 64 KiB 的一字节 PC 表，控制
转移只需一次按 PC 索引的 byte load，不再做 bitmap 的移位和掩码。默认
`GAM4980_IRAM_V2` 把待提交的 N/Z 结果编码在 R9 的高位，R9 低 8 位始终保留
其余客机状态位；只有 BNE/BEQ/BMI/BPL、PHP 和引擎写回真正观察 N/Z 时才物化。
这与早期占用 R11 高半区的失败实验不同：R11 仍只负责诊断计数，不再混入 flags。
V2 已用完整《伏魔记》在模拟器中验证启动、菜单、开场剧情、裸机常驻和退出恢复。

页内条件分支也保留并修正宿主 PC 指针：未取分支继续使用已经递增的 R2；同页
取分支同时给客机 PC 与 R2 加相对位移；只有跨页分支才清空 R2 并重新查页表。
因此控制边界仍精确检查 deadline、关机和 AOT/HLE 入口，却不必为普通同页循环
重复做地址到宿主页的映射。V2 还把真机热点 `$05/$84/$A4/$A6` 纳入 resident
handler，当前覆盖 81 个 opcode。

IRAM 因 AOT/HLE 入口表命中时，使用 PC 高 8 位查询 256 项原生页入口表。
命中的模块放在普通应用程序 RAM，通过唯一的 `call %r13` 进入。IRAM 在调用前
把寄存器驻留的 A/X/Y/SP/PC/P、周期和指令计数发布到 100 字节 context；磁盘模块
使用普通 S1C33 C ABI，但进入后把客机状态保持在函数局部量/宿主寄存器中，直接
分派并连续执行同一模块内的多个基本块，不再每个块都返回 IRAM。只有达到周期
deadline、客机关机、bank 映射 epoch 改变，或下一 PC 跨模块/HLE 边界时，模块才
一次性写回 context，IRAM 随即重新载入状态并执行原控制边界检查。通用访存通过
两个已验证 callback 保留 bank、I/O 和页 3 副作用；直接 RAM、零页和栈仍由模块
直接寻址。callback 引发 bank 切换时会递增映射 epoch，当前链在 callback 返回后
立即退出；loader 也不允许覆盖当前正在执行的代码槽。

ABI 4 又把已知控制出口和访存分成快慢两级。固定分支、JSR、JMP 若目标仍在当前
模块，会直接跳到对应的 C 标签；926 个普通块共有 854 条这种静态模块内边，只有
跨模块/HLE 目标以及 RTS、RTI、间接 JMP 回中心分派。读操作先查询 `page_kind`，
可直接读取的 RAM/ROM page 直接解引用当前页表；普通 RAM 写通过每模块共享的
`native_write8` helper 完成，保留 LCD dirty/诊断计数、PB 强制清零和自动关机计数
强制 `0xff`。页 0 特殊寄存器、I/O、bank 控制及不可直接写页仍调用原 callback。
共享写 helper 与模块入口处于同一无重定位 section，NAT 的非零 `entry_offset` 保证
loader 跳到模块主入口，而不是误入 helper。

构建器用现有 AOT 解码器重新生成 926 个块的 C6502 语义 IR，并以 S1C33 LLVM
编译成 20 个无重定位模块；模块按“虚拟 4 KiB 窗口 + 物理 4 KiB bank”分组。
`GAM4980.NAT` 包含格式/ABI 版本（当前 ABI 4）、模块表、926 项函数指纹、客机页链接表和代码
hash。所有已识别普通固件 AOT 块只存在于 NAT，不再在 KF2 内保留副本；已经验证
的整函数 HLE 入口故意从模块 switch 中漏出，使更高收益的 HLE 继续优先。

播放器把原 128 页 ROM cache 缩为 64 页，总 malloc 大小不变，并把释放的
256 KiB 分成四个可覆盖原生代码槽。首个公共模块启动时预载，其他模块在
相应 bank/PC 首次命中时通过裸机文件系统读取并按 LRU 覆盖。读取、版本、hash、
函数指纹或 bank 条件任一失败时，该页入口保持为 0，IRAM 原子回退到已有执行
路径。包格式支持 192 个模块、4096 项函数指纹和 2048 项页链接。

格式 3 的同名 `<游戏名>.GNA` 在电脑端把 GAM 的全部静态可恢复 CFG 编译为
S1C33 模块。游戏模块可按 2 KiB 代码组拆分以满足单槽上限，但 manifest 仍以真实
4 KiB bank 映射验证和发布客机页入口。包扩展头绑定完整 GAM 大小、代码区大小、
入口和整文件 hash；9288 只做绑定校验与缺页装载，不运行编译器。现有 HLE/语义
AOT 入口拥有更高优先级，动态间接入口或模块 miss 回到 IRAM。格式 3 还记录实际
翻译出的指令跨度；只有 Flash program/erase 与这些跨度重叠时才撤销游戏模块入口，
并用 mapping epoch 终止正在执行的模块链，固件模块仍可继续使用。

部署时必须把 `GAM4980.exe` 与 `GAM4980.NAT` 一起复制到 `A:\gam4980`；可选的
`<游戏名>.GNA` 与 `.gam` 放在同一目录。缺少
模块不是启动错误，只会失去固件 AOT 加速。`PERF.LOG` 的 `native_module_*`
字段记录包状态、模块/匹配项数、包大小、预载、加载/换出/回退次数、已读代码
字节、arena 和单槽大小；`native_shared_*` 记录实际进入模块、完成块、替代周期、
原子 miss、内部链链接数、直接标签链接数和最长链。`native_shared_blocks / native_shared_calls`
就是每次跨 ABI 平均完成的基本块数。没有同名 GNA 时，未被这 926 块或 GAM
通用语义模板识别的代码仍由 IRAM/完整解释器执行；有 GNA 时，静态恢复块优先
运行原生模块，无法静态证明的间接/动态入口仍保留完整回退。

可对现有 ELF 单独重跑静态审计：

```powershell
python tests\audit_9288_iram_exec.py `
  --map build\9288\GAM4980.map `
  --disassembly build\9288\GAM4980.iram.dis.txt `
  --max-size 0x16c8 `
  --expect-asm `
  --required-symbol s6502_iram_exec_burst_asm `
  --dispatch-table-symbol s6502_iram_dispatch_table `
  --allow-indirect-call-register r13 `
  --expected-indirect-calls 1
```

静态审计只证明 overlay 可安全安装，不能代替客机状态等价回归。汇编 handler
扩充后，还应使用相同 ROM、GAM、按键序列和切片边界，对比 C 参考路径的 CPU、
RAM、bank、LCD 与周期状态；模拟器负责等价和退出恢复，真机负责最终性能 A/B。

开启 Debug 后，`PERF.LOG` 还会记录 burst 长度分布、zero/slow/dispatch 的
Top-N 虚拟/物理 PC、dispatch 目标分类、slow opcode Top-16 和慢路径原因。
这些统计只在 Debug 模式采集。关闭 Debug 时，IRAM 安装器会先验证 V2 的四个
计数点（旧 shadow 探针为七个）的机器码，再把 `add r11,*` 改成两字节 S1C33
`NOP`；外层也停止逐 burst 累加
指令、周期和退出计数。这个双路径没有给每条客机指令增加运行时开关分支。

## C6502 影子解码超级指令（仅保留为旧版等价探针）

真机日志显示旧影子模板在多次 bank 重建后仍无有效命中，自适应逻辑最终会把
它关闭。默认 V2 因此在编译期直接移除 `$02` handler 和三个模板，并从实时
`sys.mem_r` 取指，不再分配/复制 64 KiB shadow，也不再执行 marker 遍历。
腾出的 IRAM 用于延迟 flags 和更通用的 opcode；旧实现仍由独立等价探针构建，
用于防止历史路径腐化，但不进入真机默认 EXE。

加载 AOT CFG 后，虚拟 `$5000-$8fff` 的取指页会建立 16 KiB 影子副本。客机
数据读取仍使用原始 `sys.mem_r`，因此游戏读取自身代码时看见的字节完全不变；
只有确认位于 CFG 指令边界、且整段不跨 256 字节页的编译器模板，才在影子页
入口写入内部 `$02` 标记。普通 opcode 仍直接查原来的紧凑跳转表，不增加模式检查。

首批 resident 模板为：

- `LOAD_OPER1_IMM16` / `LOAD_OPER2_IMM16`；
- `STACK_ADD16` / `STACK_SUB16`；
- `ADD16_OPER1_OPER2`。

加载 AOT CFG 和每次 bank 重映射建立 shadow 时，builder 都会对原始页完整核对
模板、长度、指令边界和“不跨页”条件；只有验证通过才发布私有 `$02`。共享
handler 因而只读取原始首字节分类和动态立即数，不再在每次命中时扫描 8/13/16
字节模板。真实客机 `$02` 会因原始首字节分类不符原子回退；十进制算术也仍在
改变状态前回退。命中时一次完成多条 6502 指令，精确累计 PC、周期、最终
寄存器、flags、零页和栈写入；Debug 模式还保留精确指令数。
加载阶段会按物理 bank 建立 `head + next` 候选索引。动态 bank 重映射时只复制
对应的 4 KiB 影子窗口并访问该 bank 的少量候选，不再扫描整页机器码；物理
bank 和源映射都未变化时直接复用。Flash byte-program/erase 会先关闭 AOT 并
恢复原始取指页，避免陈旧标记。Debug 模式记录 `iram_shadow_rebuilds`、
`iram_shadow_marker_visits` 和各模板命中数；关闭 Debug 时命中计数指针为零，
handler 不写外部统计 RAM。加入普通 C ABI 模块桥后的当前执行引擎共 5716 字节，
低于真机验证上限 5832 字节，剩余 116 字节；外部原生模块不计入 IRAM overlay。

影子解码还带有自适应止损：每 16 次 4 KiB 重建核对一次实际 super 命中收益，
低于每次重建 16 次命中就关闭本次游戏会话的 shadow super；关闭 Debug 时没有
命中计数写入，最多允许 32 次重建后关闭。关闭后汇编直接使用实时 `sys.mem_r`
页表，后续 bank refresh 不再复制或遍历 marker。`iram_shadow_disable_reason=1`
表示低收益，`=2` 表示无统计模式下达到重建上限。深度 exit Top-N、dispatch
分类和慢 opcode 分类固定按 1/16 抽样，burst 分布仍逐次精确记录；PERF 中的
`iram_exit_sample_rate` 给出当前抽样率。

电脑端可先验证旧 C resident engine 与普通解释器等价，并确认测试确实执行过 resident
路径：

```powershell
python tests\run_iram_exec_host_regression.py `
  D:\path\to\8.BIN D:\path\to\E.BIN `
  D:\path\to\伏魔记.gam `
  D:\path\to\三国霸业6980-方案一.gam `
  D:\path\to\魔塔之怀旧终曲.gam
```

这个测试不能直接执行 S1C33 汇编，但会覆盖相同的 `s6502.c` burst 接口、零周期
fallback、deadline/dispatch 返回协议，以及 C 参考微内核的状态写回。
