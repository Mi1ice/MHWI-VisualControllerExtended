# MHWI-VisualControllerExtended

《怪物猎人：世界》变身插件拓展。兼容原变身插件visual_controller_v5，请在拥有visual_controller_v5的情况下使用。
当前版本：1.1.0。

## 构建

双击 build.bat（自动定位 vcvars64 并调用 cl.exe），产物在 build\MHWI-VisualControllerExtended.dll。
也可以用 CMake：cmake -B build -A x64 然后 cmake --build build --config Release。

## 安装

把 MHWI-VisualControllerExtended.dll 和 MHWI-VisualControllerExtended.ini 放进游戏根目录 nativePC\plugins\中。
需求前置 Stracker's Loader。
建议使用狩技 mod 盒子加载。

所有规则仅保存在 ini 中，DLL 不包含内置回退规则。ini 缺失、为空或没有有效的`[RuleN]` 时，规则数为 0，所有 ID 都透传给下一 Hook 或游戏原函数。因此安装或更新
DLL 时必须同时保留 MHWI-VisualControllerExtended.ini。

## 规则配置

配置文件规则示例：

    ; 双刀鬼人化示例：
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

条件键汇总：Id（必填）、WeaponType（武器类型）、Spirit（练气值）、SpiritMin/SpiritMax（练气值范围）、LmtMin/LmtMax（动作 lmt 范围）、Demon（鬼人化）、DemonMin/DemonMax（鬼人化范围）、Archdemon（鬼人强化）、HealthPercentMin/HealthPercentMax（血量百分比范围，包含边界）、HealthPercentAbove（血量百分比大于）、LatchUntilHealthZero（命中后锁存至血量归零）、Return（1/0）。
`Return=1` 表示显示该 ID 下全部网格；`Return=0` 表示隐藏该 ID 下全部网格。范围键缺省 -1 表示不限；同一 ID 可写多条，首条命中生效。

兼容原变身插件规则：

- 收刀、拔刀：ID30 对应收刀状态隐藏，ID31 对应拔刀状态隐藏。
- 太刀练气、虫棍点灯：ID32/33/34/35 分别对应无刃到红刃/无灯到三灯显示。

插件新增规则：

双刀：
- 双刀鬼人化状态：ID20/21 分别在鬼人化/鬼人强化成立时显示；ID22/23 分别在鬼人化/鬼人强化成立时隐藏。
太刀：
- 太刀累计显示：ID36 白刃及以上显示，ID37 黄刃及以上显示。
- 太刀累计隐藏：ID38 白刃及以上隐藏，ID39 黄刃及以上隐藏，ID40 红刃隐藏。
血量百分比：
- 血量分段显示：ID50=`0%`，ID51=`(0%,25%]`，ID52=`(25%,50%]`，ID53=`(50%,75%]`，ID54=`(75%,100%]`；对应区间内显示。
- 血量锁存显示：ID55/56/57 在 `[0%,25%/50%/75%]` 内显示；正血量首次命中后持续显示，血量归零时复位。
- 血量锁存隐藏：ID58 在血量为0时即时隐藏；ID59/60/61 在 `[0%,25%/50%/75%]` 内隐藏，正血量首次命中后持续隐藏，血量归零时复位。
- 若要显隐状态完全跟随血量变化，删除键LatchUntilHealthZero或将键值设为0。

- ID36-40尚未支持虫棍点灯。

## 热键与指令

| 键 / 指令 | 效果 |
|---|---|
| Ctrl+F5 | 重载 ini |
| Ctrl+F9 | 开关（关闭时全部透传） |
| /vc status | 聊天栏显示 hook 状态、血量、练气、鬼人化、鬼人强化、动作、武器及 fsm |
| /vc reload、/vc on、/vc off、/vc help | 同热键 / 帮助 |

日志写在 DLL 同目录 MHWI-VisualControllerExtended.log，含每 4 秒的状态心跳，可用于观察各状态值随游戏行为的变化。

## 已知限制

- 在存档选择界面不生效。
- 大量修改了VIsualCondition的值，可能造成游戏场景中物体错误的隐藏或显示。
- detour 当前按已验证接口转发 4 个寄存器参数；若后续游戏版本开始使用额外堆栈参数，需要同步更新函数声明。
- 内存地址针对 15.23.00；游戏更新后需更新 PlayerRoot / 消息缓冲地址，目标函数由签名扫描定位，通常不受版本影响。
- 状态条件最多有约 PollMs 毫秒的快照延迟；默认 60ms，换取加载期和跨 MOD 的稳定性。

## 许可与免责

项目自身代码采用 MIT License。MinHook、WeaponSoundEnhance 和 mhw-toolkit 的许可证及版权声明见 `THIRD_PARTY_NOTICES.md`；发布包同时附带完整的 MinHook/HDE 与 mhw-toolkit Apache-2.0 许可证文本。

使用前建议备份存档。
