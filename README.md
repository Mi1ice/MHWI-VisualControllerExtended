# MHWI-VisualControllerExtended

《怪物猎人：世界》变身插件拓展。兼容原变身插件visual_controller_v5，请在拥有visual_controller_v5的情况下使用。
当前版本：1.3.1。

### v1.3.1 Group ID 迁移对照

更新现有外观 MOD 时，请将原 `Group_ID` 替换为对应的新 ID。ID30–40 保持不变。

| 功能 | 原 ID | 新 ID |
|---|---:|---:|
| 双刀鬼人化时显示 | 20 | 22 |
| 双刀鬼人强化时显示 | 21 | 23 |
| 双刀鬼人化时隐藏 | 22 | 24 |
| 双刀鬼人强化时隐藏 | 23 | 25 |
| 太刀红刃或双刀鬼人强化时显示 | 29 | 50 |
| 动作及太刀规则 | 30–40 | 30–40（不变） |
| 血量等于 0% 时显示 | 50 | 42 |
| 血量在 (0%,25%] 时显示 | 51 | 43 |
| 血量在 (25%,50%] 时显示 | 52 | 44 |
| 血量在 (50%,75%] 时显示 | 53 | 45 |
| 血量在 (75%,100%] 时显示 | 54 | 46 |
| 血量不高于 25% 后锁存显示 | 55 | 47 |
| 血量不高于 50% 后锁存显示 | 56 | 48 |
| 血量不高于 75% 后锁存显示 | 57 | 49 |
| 血量等于 0% 时隐藏 | 58 | 52 |
| 血量不高于 25% 后锁存隐藏 | 59 | 53 |
| 血量不高于 50% 后锁存隐藏 | 60 | 54 |
| 血量不高于 75% 后锁存隐藏 | 61 | 55 |

## 构建

双击 build.bat（自动定位 vcvars64 并调用 cl.exe），产物在 build\MHWI-VisualControllerExtended.dll。
也可以用 CMake：cmake -B build -A x64 然后 cmake --build build --config Release。

## 安装

把 MHWI-VisualControllerExtended.dll 和 MHWI-VisualControllerExtended.ini 放进游戏根目录\nativePC\plugins\中。
需求前置 Stracker's Loader。

所有规则仅保存在 ini 中，DLL 不包含内置回退规则。ini 缺失、为空或没有有效的`[RuleN]` 时，规则数为 0，所有 ID 都透传给下一 Hook 或游戏原函数。因此安装或更新
DLL 时必须同时保留 MHWI-VisualControllerExtended.ini。

## 规则配置

配置文件规则示例：

    ; 双刀鬼人化示例：
    [Rule1]
    Id=22
    WeaponType=2      ; 双刀
    Demon=1           ; 鬼人化开启
    Return=1
    [Rule2]
    Id=22
    Return=0

    ; 双刀鬼人强化控制 ID 23：
    [Rule3]
    Id=23
    WeaponType=2
    Archdemon=1
    Return=1
    [Rule4]
    Id=23
    Return=0

    ; 反向控制：进入对应状态时隐藏
    [Rule5]
    Id=24
    WeaponType=2
    Demon=1
    Return=0
    [Rule6]
    Id=24
    Return=1

    [Rule7]
    Id=25
    WeaponType=2
    Archdemon=1
    Return=0
    [Rule8]
    Id=25
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

跨武器状态：
- ID50 在太刀进入红刃，或双刀进入鬼人强化时显示；其他状态隐藏。同一 ID 的多条条件规则可用于表达“或”。

双刀：
- 双刀鬼人化状态：ID22/23 分别在鬼人化/鬼人强化成立时显示；ID24/25 分别在鬼人化/鬼人强化成立时隐藏。

太刀：
- 太刀累计显示：ID36 白刃及以上显示，ID37 黄刃及以上显示。
- 太刀累计隐藏：ID38 白刃及以上隐藏，ID39 黄刃及以上隐藏，ID40 红刃隐藏。
- ID36-40尚未支持虫棍点灯。

血量百分比：
- 血量分段显示：ID42=`0%`，ID43=`(0%,25%]`，ID44=`(25%,50%]`，ID45=`(50%,75%]`，ID46=`(75%,100%]`；对应区间内显示。
- 血量锁存显示：ID47/48/49 在 `[0%,25%/50%/75%]` 内显示；正血量首次命中后持续显示，血量归零时复位。
- 血量锁存隐藏：ID52 在血量为0时即时隐藏；ID53/54/55 在 `[0%,25%/50%/75%]` 内隐藏，正血量首次命中后持续隐藏，血量归零时复位。
- 可自定义区间更小的血量百分比条件，以实现更细致的变化。*需修改对应外观MOD的Group_ID以匹配规则*
- 锁存：即触发后无论血量如何变化，显隐状态不变，猫车后重置显隐状态为初始态，继续根据条件变化判断是否触发并锁存。若要显隐状态完全跟随血量变化，删除键LatchUntilHealthZero或将键值设为0。

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
