# HLE 阶段耗时拆分

基于 BANK-BATCH，保持算法、NAT、客机周期和绘图提交方式不变。
沿用每 16 个执行片段的 256 Hz 宿主时钟采样；只在采样开启时统计。

`hle_function_profile_version=1`，重复 `hle_sample_*` 记录：

- id 0..28 对应已有 HLE path ID；pc 是诊断入口代表地址，游戏模板可能匹配多个函数，因此不是独立物理地址函数榜。
- id 31：入口搜索、分派以及没有挂接 path ID 的 HLE 工作，不能当成某个绘图函数。
- attempts/accepted/condition_rejects/budget_rejects 是采样范围内事件次数。批量完成只计一个 accepted，不把迭代数当调用数。
- exclusive_ticks 包含入口检查及执行，排除嵌套 IO/NAT 等阶段。拒绝后立即转回查找分类。
- 全部分类 ticks 之和应等于 host_exclusive_hle_ticks。分析器输出差值以核验。

不能把采样 ticks 当成整个会话耗时，也不能用客机帧数计算调用平均耗时。
3.90625 ms 时钟量化及新增计时开销未扣除；短函数零 ticks 不代表免费。
关闭调试/未抽中采样时不读取时钟、不增加这些计数。
本轮只增强归因，不宣称性能提升。

## 验证和产物

宿主时钟回绕、嵌套 IO 排除、拒绝后查找归属、采样关闭零计时、
分类与总数对账测试通过；分析器 4 项、bank 等价测试、加载器 2 项通过。
9288 SDK 目标构建和 IRAM 审计通过，IRAM 5332 字节不增加。
本轮未重新进行模拟器游戏流程验证，真机性能尚待测量。

`build/hle-path-profile/GAM4980.exe`，718753 字节，SHA256
`8fa0197baf68d36b88f6600f470d0e7d57e4997d2d9742812e557ad7db9861ea`。
NAT 与 BANK-BATCH 相同，SHA256
`765ab58fe727d71225f671a6696153c9190a9d1ba957816a94697dec3cf24bfc`。
打包 `build/GAM4980-HLE-PATH-PROFILE.zip`，只有 EXE、NAT 和安装说明，无游戏/ROM。
