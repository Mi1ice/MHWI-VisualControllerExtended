# MHWI-VisualControllerExtended

给《怪物猎人：世界 / 冰原》（15.23.00）的「视觉项 ID 返回值控制」原生 DLL 插件。
当前版本：1.1.0。

## 构建

双击 build.bat（自动定位 vcvars64 并调用 cl.exe），产物在 build\MHWI-VisualControllerExtended.dll。
也可以用 CMake：cmake -B build -A x64 然后 cmake --build build --config Release。

## 安装

把 MHWI-VisualControllerExtended.dll 和 MHWI-VisualControllerExtended.ini 放进游戏 nativePC\plugins\，
使用 Stracker's Loader / 狩技 mod 盒子加载。

所有规则仅保存在 ini 中，DLL 不包含内置回退规则。ini 缺失、为空或没有有效的
`[RuleN]` 时，规则数为 0，所有 ID 都透传给下一 Hook 或游戏原函数。因此安装或更新
DLL 时必须同时保留 MHWI-VisualControllerExtended.ini。

### 与其他 inline hook MOD 共存

保持 ini 中 `WaitForEarlierHook=1`。插件默认等待 `HookWaitMs=2000`，让更早加载的 Hook
先完成安装，再由 MinHook 基于当时的函数入口创建 trampoline。未命中的 ID 会调用
MinHook 返回的下一处理器。

如果没有其他 MOD 挂钩同一函数，可将 `HookWaitMs=0`。如果加载顺序不稳定，可把等待时间
提高到 5000；允许范围是 0–30000 毫秒。不同 Hook 库之间无法保证绝对兼容，日志出现
`hook installed via MinHook` 才表示本插件安装成功。

## 工作原理

1. 注入后在 MonsterHunterWorld.exe 内扫描受支持版本的函数定位签名（可用 ini 的
   TargetRva 或 Signature 覆盖）；若入口已被更早的通用 Hook 改写，则尝试通过唯一的
   签名尾部恢复入口地址，最终由 MinHook 解析并创建 trampoline；
2. 挂钩的是游戏内"按 ID 查询布尔开关"的函数（ID = *(uint16*)(r8+IdOffset)）；
3. 后台线程每 PollMs 毫秒安全读取以下状态并发布一致快照：
   练气 spirit = *(*(Entity+0x76B0)+0x2370)；
   鬼人化 demon = *(*(Entity+0x76B0)+0x2368)（开=1 关=0，实测确认）；
   鬼人强化 archdemon = *(*(Entity+0x76B0)+0x2369)（开=1 关=0）；
   动作 lmt  = *(*(Entity+0x468)+0xE9C4)；
   当前血量 = *(*(Entity+0x7630)+0x64)，最大血量 = *(*(Entity+0x7630)+0x60)；
   武器类型  = *(*(*(*(Entity+0xC0)+0x8)+0x78)+0x2E8)。

   detour 不再直接遍历这些游戏指针，只读取已发布的快照。人物加载期间快照未就绪时，
   有状态条件的规则会透传原游戏函数；血量百分比会限制在 0%–100%，最大血量无效时
   不发布血量状态。这也避免与安装向量异常处理器的 MOD（如 MHWSS）冲突。

> 练气、鬼人化与鬼人强化位于同一个武器状态子对象；鬼人化和鬼人强化是
> `+0x2368/+0x2369` 两个相邻字节。武器信息对象的类型字段在 Entity+0x76B0+0x9F8。

4. detour 按 [RuleN] 顺序匹配（Id + WeaponType/Spirit/Lmt/Demon/Archdemon/HealthPercent 条件），
   命中即返回配置值；未命中任何规则的 ID 透传给原函数。

## 规则配置

    ; 双刀鬼人化示例（已写入附带 ini）：
    [Rule1]
    Id=20
    WeaponType=2      ; 双刀
    Demon=1           ; 鬼人化开启
    Return=1
    [Rule2]
    Id=20
    Return=0

    ; 双刀鬼人强化控制 ID 21：
    [Rule3]
    Id=21
    WeaponType=2
    Archdemon=1
    Return=1
    [Rule4]
    Id=21
    Return=0

    ; 反向控制：进入对应状态时隐藏
    [Rule5]
    Id=22
    WeaponType=2
    Demon=1
    Return=0
    [Rule6]
    Id=22
    Return=1

    [Rule7]
    Id=23
    WeaponType=2
    Archdemon=1
    Return=0
    [Rule8]
    Id=23
    Return=1

    ; 动作 LMT 示例：
    [Rule9]
    Id=30
    LmtMax=32767      ; 当前动作 LMT <= 32767 (< 0x8000) 时
    Return=1          ; 显示 ID 30 下全部网格
    [Rule10]
    Id=30
    Return=0          ; 否则隐藏 ID 30 下全部网格

条件键汇总：Id（必填）、WeaponType（武器类型精确匹配）、Spirit（练气精确匹配）、
SpiritMin/SpiritMax（练气范围）、LmtMin/LmtMax（动作 lmt 范围）、
Demon（鬼人化精确匹配 0/1）、DemonMin/DemonMax（鬼人化范围）、
Archdemon（鬼人强化精确匹配 0/1）、HealthPercentMin/HealthPercentMax
（血量百分比范围，包含边界）、HealthPercentAbove（严格大于指定百分比）、Return（1/0）。
`Return=1` 表示显示该 ID 下全部网格；`Return=0` 表示隐藏该 ID 下全部网格。
范围键缺省 -1 表示不限；同一 ID 可写多条，首条命中生效。

附带 ini 当前包含 55 条规则，按功能分为：

- 双刀状态（规则 1–8）：ID20/21 分别在鬼人化/鬼人强化成立时显示；ID22/23 分别在
  鬼人化/鬼人强化成立时隐藏。
- 动作 LMT（规则 9–12）：ID30 对应 LMT `<0x8000`，ID31 对应 LMT `>=0x8000`。
- 太刀精确练气（规则 13–21）：ID32/33/34/35 分别对应无刃、白刃、黄刃、红刃。
- 太刀累计显示（规则 22–25）：ID36 白刃及以上，ID37 黄刃及以上。
- 太刀达标隐藏（规则 26–31）：ID38 白刃及以上隐藏，ID39 黄刃及以上隐藏，ID40 红刃隐藏。
- 血量互斥分段（规则 32–41）：ID50=`0%`，ID51=`(0%,25%]`，ID52=`(25%,50%]`，
  ID53=`(50%,75%]`，ID54=`(75%,100%]`；对应区间内显示。
- 血量累计显示（规则 42–47）：ID55/56/57 分别在血量 `<=25%/50%/75%` 时显示。
- 血量累计隐藏（规则 48–55）：ID58 在血量为0时隐藏；ID59/60/61 分别在血量
  `<=25%/50%/75%` 时隐藏。

## 热键与指令

| 键 / 指令 | 效果 |
|---|---|
| Ctrl+F5 | 重载 ini |
| Ctrl+F9 | 开关（关闭时全部透传） |
| /vc status | 聊天栏显示 hook 状态、血量、练气、鬼人化、鬼人强化、动作、武器及 fsm |
| /vc reload、/vc on、/vc off、/vc help | 同热键 / 帮助 |

日志写在 DLL 同目录 MHWI-VisualControllerExtended.log，含每 4 秒的状态心跳，
可用于观察各状态值随游戏行为的变化。

## 已知限制

- detour 当前按已验证接口转发 4 个寄存器参数；若后续游戏版本开始使用额外堆栈参数，
  需要同步更新函数声明。
- 内存地址针对 15.23.00；游戏更新后需更新 PlayerRoot / 消息缓冲地址，
  目标函数由签名扫描定位，通常不受版本影响。
- 状态条件最多有约 PollMs 毫秒的快照延迟；默认 60ms，换取加载期和跨 MOD 的稳定性。

## 许可与免责

项目自身代码采用 MIT License。MinHook、WeaponSoundEnhance 和 mhw-toolkit 的许可证及
版权声明见 `THIRD_PARTY_NOTICES.md`；发布包同时附带完整的 MinHook/HDE 与
mhw-toolkit Apache-2.0 许可证文本。
使用前建议备份存档。
