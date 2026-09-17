// ============================================================================
//  MHWI-VisualControllerExtended.cpp
//  ----------------------------------------------------------------------------
//  Monster Hunter: World / Iceborne (15.23.00) 视觉项 ID 返回值控制插件。
//
//  项目代码由规则引擎、只读状态采集和通用 Hook 后端组成。
//  Hook 创建、指令搬运和线程协调由 BSL-1.0 许可的 SafetyHook v0.7.0 提供。
//  部分配置与安全内存读取结构参考 MIT 许可的 WeaponSoundEnhance；玩家/武器/聊天
//  兼容数据与聊天接收行为经该项目参考 Apache-2.0 许可的 mhw-toolkit，并已为本项目
//  修改。完整声明见 THIRD_PARTY_NOTICES.md。
//
//  查询接口按受支持游戏版本的运行时行为建模：查询对象中的 uint16 ID 默认位于
//  IdOffset=4；未配置规则或状态尚未就绪时始终调用下一处理器。
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "third_party/safetyhook/safetyhook.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <new>
#include <utility>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

// ===========================================================================
//  Memory-safe read utilities
// ===========================================================================
namespace mem {

inline bool IsReadable(std::uintptr_t address, std::size_t size)
{
    if (address == 0 || size == 0) return false;
    for (std::size_t offset = 0; offset < size; offset += 0x1000) {
        MEMORY_BASIC_INFORMATION memoryInfo{};
        const std::uintptr_t probeAddress = address + offset;
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(probeAddress), &memoryInfo, sizeof(memoryInfo)) == 0) return false;
        if (memoryInfo.State != MEM_COMMIT) return false;
        if (memoryInfo.Protect == PAGE_NOACCESS || memoryInfo.Protect == PAGE_GUARD) return false;
        const DWORD readableProtection = memoryInfo.Protect &
            (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        if (readableProtection == 0) return false;
        if (offset + 0x1000 >= size) break;
        if (probeAddress + 0x1000 < probeAddress) return false;
    }
    return true;
}

inline bool IsWritable(std::uintptr_t address, std::size_t size)
{
    if (address == 0 || size == 0) return false;
    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &memoryInfo, sizeof(memoryInfo)) == 0) return false;
    if (memoryInfo.State != MEM_COMMIT) return false;
    const DWORD writableProtection = memoryInfo.Protect &
        (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return writableProtection != 0;
}

inline bool IsExecutable(std::uintptr_t address)
{
    if (address == 0) return false;
    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &memoryInfo, sizeof(memoryInfo)) == 0) return false;
    if (memoryInfo.State != MEM_COMMIT || (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD executableProtection = memoryInfo.Protect &
        (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
    return executableProtection != 0;
}

template <typename T>
inline bool ReadVal(std::uintptr_t address, T& output)
{
    if (!IsReadable(address, sizeof(T))) return false;
    std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// GetAddress 语义：addr = base；对每个 off：addr = *(addr + off)
inline std::uintptr_t Walk(std::uintptr_t baseAddress, const std::uint32_t* offsets, int offsetCount)
{
    if (baseAddress == 0) return 0;
    std::uintptr_t currentAddress = baseAddress;
    for (int offsetIndex = 0; offsetIndex < offsetCount; ++offsetIndex) {
        if (currentAddress == 0) return 0;
        std::uintptr_t nextAddress = 0;
        if (!ReadVal(currentAddress + offsets[offsetIndex], nextAddress)) return 0;
        currentAddress = nextAddress;
    }
    return currentAddress;
}

inline std::int32_t ReadI32(std::uintptr_t address, std::int32_t defaultValue)
{
    std::int32_t value = defaultValue;
    if (IsReadable(address, sizeof(value)))
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

} // namespace mem

void ResetRuleLatches();
void ObserveHealthForLatchReset(float healthPercent);

// ===========================================================================
//  玩家实时状态（轮询线程安全读取并发布快照，供 detour、日志和聊天显示）
//    练气 spirit = *(*(Entity+0x76B0) + 0x2370)
//    鬼人化 demon = *(*(Entity+0x76B0) + 0x2368)   （开启=1 关闭=0，实测确认）
//    鬼人强化 archdemon = *(*(Entity+0x76B0) + 0x2369)
//        注：Entity+0x76B0 处是指向武器状态子对象的指针，三种状态同处其中，
//        偏移相差 8 字节。武器信息对象的类型字段在 Entity+0x76B0+0x9F8。
//    动作 lmt  = *(*(Entity+0x468) + 0xE9C4)
//    武器 weapon = *(*(*(*(Entity+0xC0)+0x8)+0x78) + 0x2E8)
//    动作状态机 fsm = *(Entity+0x6278)
//    当前血量 health = *(*(Entity+0x7630) + 0x64)
//    最大血量 maxHealth = *(*(Entity+0x7630) + 0x60)
//    当前地图 map = uint16(*(*(*(MapRoot)+0x80)+0xEEC0)+0x118)
// ===========================================================================
namespace player {

std::uintptr_t gPlayerRootAddress = 0x1450139A0ULL;  // ini 可覆盖
std::uintptr_t gMapRootAddress = 0;                   // gameBase + MapRootRva
std::uintptr_t gManagerAddress = 0;                  // *(Root)，调试用
std::uintptr_t gEntityAddress = 0;                   // *(Manager+0x50)，调试用

volatile int gSpiritLevel = -1;
volatile int gActionLmt = -1;
volatile int gWeaponType = -1;
volatile int gFsmId = -1;
volatile int gDemonMode = -1;
volatile int gArchdemonMode = -1;
volatile float gCurrentHealth = -1.0f;
volatile float gMaxHealth = -1.0f;
volatile float gHealthPercent = -1.0f;
volatile int gCurrentMapId = -1;

struct StateSnapshot {
    std::uintptr_t managerAddress = 0;
    std::uintptr_t entityAddress = 0;
    int spiritLevel = -1;
    int actionLmt = -1;
    int weaponType = -1;
    int fsmId = -1;
    int demonMode = -1;
    int archdemonMode = -1;
    float currentHealth = -1.0f;
    float maxHealth = -1.0f;
    float healthPercent = -1.0f;
    int currentMapId = -1;
};

SRWLOCK gStateLock = SRWLOCK_INIT;
StateSnapshot gPublishedState;
int gLastValidMapId = -1; // 由 gStateLock 保护；读取失败时保留上一次有效值

void ResetMapTracking()
{
    ::AcquireSRWLockExclusive(&gStateLock);
    gLastValidMapId = -1;
    gPublishedState.currentMapId = -1;
    ::ReleaseSRWLockExclusive(&gStateLock);
    gCurrentMapId = -1;
}

void SetMapRootAddress(std::uintptr_t address)
{
    if (gMapRootAddress == address) return;
    gMapRootAddress = address;
    ResetMapTracking();
}

bool ReadCurrentMap(int& currentMapId)
{
    currentMapId = -1;
    // CE: [[[*MapRoot + 0x80] + 0xEEC0] + 0x118], uint16
    static const std::uint32_t mapOffsets[] = {0x0, 0x80, 0xEEC0};
    const std::uintptr_t mapData = mem::Walk(gMapRootAddress, mapOffsets, 3);
    if (mapData == 0) return false;
    std::uint16_t value = 0;
    if (!mem::ReadVal(mapData + 0x118, value)) return false;
    currentMapId = static_cast<int>(value);
    return true;
}

void PublishState(const StateSnapshot& state)
{
    ::AcquireSRWLockExclusive(&gStateLock);
    const bool entityChanged = gPublishedState.entityAddress != state.entityAddress;
    bool mapChanged = false;
    if (state.currentMapId >= 0) {
        mapChanged = gLastValidMapId >= 0 && gLastValidMapId != state.currentMapId;
        gLastValidMapId = state.currentMapId;
    }
    gPublishedState = state;
    // 地址链暂时不可读时继续公开最后一个有效值，也不把读取失败当作切图。
    if (gPublishedState.currentMapId < 0)
        gPublishedState.currentMapId = gLastValidMapId;
    ::ReleaseSRWLockExclusive(&gStateLock);

    if (entityChanged || mapChanged) ResetRuleLatches();
    ObserveHealthForLatchReset(state.healthPercent);

    // 保留这些展开字段供日志和聊天状态显示。
    gManagerAddress = state.managerAddress;
    gEntityAddress = state.entityAddress;
    gSpiritLevel = state.spiritLevel;
    gActionLmt = state.actionLmt;
    gWeaponType = state.weaponType;
    gFsmId = state.fsmId;
    gDemonMode = state.demonMode;
    gArchdemonMode = state.archdemonMode;
    gCurrentHealth = state.currentHealth;
    gMaxHealth = state.maxHealth;
    gHealthPercent = state.healthPercent;
    if (state.currentMapId >= 0) gCurrentMapId = state.currentMapId;
}

StateSnapshot GetStateSnapshot()
{
    StateSnapshot snapshot;
    ::AcquireSRWLockShared(&gStateLock);
    snapshot = gPublishedState;
    ::ReleaseSRWLockShared(&gStateLock);
    return snapshot;
}

bool CalculateHealthPercent(float currentHealth, float maxHealth, float& healthPercent)
{
    healthPercent = -1.0f;
    if (!std::isfinite(currentHealth) || !std::isfinite(maxHealth) || maxHealth <= 0.0f)
        return false;
    healthPercent = currentHealth * 100.0f / maxHealth;
    if (healthPercent < 0.0f) healthPercent = 0.0f;
    if (healthPercent > 100.0f) healthPercent = 100.0f;
    return true;
}

void Refresh()
{
    StateSnapshot state;
    ReadCurrentMap(state.currentMapId);
    std::uintptr_t manager = 0;
    if (mem::ReadVal(gPlayerRootAddress, manager) && manager != 0) {
        state.managerAddress = manager;
        const std::uint32_t entityOffsets[] = {0x50};
        const std::uintptr_t entity = mem::Walk(manager, entityOffsets, 1);
        if (entity != 0) {
            state.entityAddress = entity;

            // 练气 / 鬼人化 / 鬼人强化（同一武器状态子对象）
            std::uintptr_t weaponState = 0;
            if (mem::ReadVal(entity + 0x76B0, weaponState) && weaponState) {
                state.spiritLevel = mem::ReadI32(weaponState + 0x2370, -1);
                std::uint8_t demonMode = 0;
                if (mem::ReadVal(weaponState + 0x2368, demonMode)) state.demonMode = demonMode;
                std::uint8_t archdemonMode = 0;
                if (mem::ReadVal(weaponState + 0x2369, archdemonMode))
                    state.archdemonMode = archdemonMode;
            }

            std::uintptr_t actionState = 0;
            if (mem::ReadVal(entity + 0x468, actionState) && actionState)
                state.actionLmt = mem::ReadI32(actionState + 0xE9C4, -1);

            static const std::uint32_t weaponDataOffsets[] = {0xC0, 0x8, 0x78};
            const std::uintptr_t weaponData = mem::Walk(entity, weaponDataOffsets, 3);
            if (weaponData)
                state.weaponType = mem::ReadI32(weaponData + 0x2E8, -1);

            state.fsmId = mem::ReadI32(entity + 0x6278, -1);

            std::uintptr_t vitality = 0;
            if (mem::ReadVal(entity + 0x7630, vitality) && vitality != 0) {
                float currentHealth = -1.0f;
                float maxHealth = -1.0f;
                if (mem::ReadVal(vitality + 0x64, currentHealth) &&
                    mem::ReadVal(vitality + 0x60, maxHealth)) {
                    float healthPercent = -1.0f;
                    if (CalculateHealthPercent(currentHealth, maxHealth, healthPercent)) {
                        state.currentHealth = currentHealth;
                        state.maxHealth = maxHealth;
                        state.healthPercent = healthPercent;
                    }
                }
            }
        }
    }
    PublishState(state);
}

bool IsInScene()
{
    return GetStateSnapshot().entityAddress != 0;
}

} // namespace player

// ===========================================================================
//  ID 规则（ini 驱动，双缓冲无锁切换）
// ===========================================================================
struct IdRule {
    std::int32_t id;          // 必填
    std::int32_t weaponType;  // 武器类型条件，-1 不限
    std::int32_t spirit;      // 练气 精确匹配，-1 不用
    std::int32_t spiritMin;   // 练气 下限(含)，-1 不限
    std::int32_t spiritMax;   // 练气 上限(含)，-1 不限
    std::int32_t lmtMin;      // 动作 lmt 下限(含)，-1 不限
    std::int32_t lmtMax;      // 动作 lmt 上限(含)，-1 不限
    std::int32_t demon;       // 鬼人化 精确匹配(0/1)，-1 不用
    std::int32_t demonMin;    // 鬼人化 下限(含)，-1 不限
    std::int32_t demonMax;    // 鬼人化 上限(含)，-1 不限
    std::int32_t archdemon;   // 鬼人强化 精确匹配(0/1)，-1 不用
    float healthPercentMin;   // 血量百分比下限(含)，负数不限
    float healthPercentMax;   // 血量百分比上限(含)，负数不限
    float healthPercentAbove; // 血量百分比严格下限(不含)，负数不限
    std::int32_t latchUntilReset; // 命中后锁存返回值；任一复位条件发生时复位
    std::int32_t result;      // 1=true 0=false
};

const int kMaxRules = 128;
IdRule gRuleBuffers[2][kMaxRules];
int gRuleCounts[2] = {0, 0};
volatile LONG gActiveRuleBuffer = 0;

// 每个查询 ID 一个锁存槽。低两位编码状态（0=未锁存、1=false、2=true），
// 高位保存复位代次；递增代次即可让所有旧锁存一次性失效。
const int kLatchIdCount = 1 << 16;
const LONG kLatchEpochMask = 0x1FFFFFFF;
volatile LONG gLatchEpoch = 1;
volatile LONG gLatchStates[kLatchIdCount] = {};
volatile LONG gHealthZeroSeen = 0;

LONG CurrentLatchEpoch()
{
    return ::InterlockedCompareExchange(&gLatchEpoch, 0, 0);
}

void ResetRuleLatches()
{
    LONG oldEpoch = CurrentLatchEpoch();
    for (;;) {
        LONG nextEpoch = (oldEpoch + 1) & kLatchEpochMask;
        if (nextEpoch == 0) nextEpoch = 1;
        const LONG observed = ::InterlockedCompareExchange(&gLatchEpoch, nextEpoch, oldEpoch);
        if (observed == oldEpoch) return;
        oldEpoch = observed;
    }
}

void ObserveHealthForLatchReset(float healthPercent)
{
    if (healthPercent < 0.0f) return;
    if (healthPercent <= 0.0f) {
        if (::InterlockedCompareExchange(&gHealthZeroSeen, 1, 0) == 0)
            ResetRuleLatches();
    } else {
        ::InterlockedExchange(&gHealthZeroSeen, 0);
    }
}

int ReadLatchedResult(unsigned short id, LONG epoch)
{
    const LONG encoded = ::InterlockedCompareExchange(&gLatchStates[id], 0, 0);
    if ((encoded >> 2) != epoch) return -1;
    const LONG state = encoded & 3;
    if (state == 1) return 0;
    if (state == 2) return 1;
    return -1;
}

void LatchResult(unsigned short id, bool result, LONG epoch)
{
    const LONG encoded = (epoch << 2) | (result ? 2 : 1);
    ::InterlockedExchange(&gLatchStates[id], encoded);
}

// ===========================================================================
//  Hook backend
//  SafetyHook 负责 trampoline、指令重定位以及安装期间的线程协调。
// ===========================================================================
namespace hook {

using TargetFn = bool (__fastcall*)(std::uintptr_t, std::uintptr_t,
                                    std::uintptr_t, std::uintptr_t);
TargetFn gOriginalFunction = nullptr;
// 成功安装后保留到进程结束，避免在 DLL 卸载锁内析构并修改游戏代码。
// 本插件不支持运行中卸载 DLL；切换版本必须先退出游戏。
safetyhook::InlineHook* gInlineHook = nullptr;
std::uintptr_t gTargetAddress = 0;
volatile int gWaitForEarlierHook = 1;
int gHookWaitMs = 2000;

// 该定位字节序列来自受支持的 MonsterHunterWorld.exe 15.23.00。
// 它只用于查找游戏查询函数，不包含其他 MOD 的代码。
std::vector<std::uint8_t> gSignature = {
    0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18, 0x57, 0x41,0x56, 0x41,0x57,
    0x48,0x83,0xEC,0x20, 0x45,0x0F,0xB7,0x50,0x04
};

constexpr std::size_t kPrefixProbeLength = 14;

void Log(const char* format, ...);

int ScanModule(const std::uint8_t* pattern, std::size_t patternLength,
               std::uintptr_t& foundAddress)
{
    if (!pattern || patternLength == 0) return 0;

    HMODULE gameModule = ::GetModuleHandleW(L"MonsterHunterWorld.exe");
    if (!gameModule) return 0;

    MODULEINFO moduleInfo{};
    if (!::K32GetModuleInformation(::GetCurrentProcess(), gameModule,
                                   &moduleInfo, sizeof(moduleInfo))) {
        return 0;
    }

    const std::uintptr_t moduleStart =
        reinterpret_cast<std::uintptr_t>(moduleInfo.lpBaseOfDll);
    const std::uintptr_t moduleEnd = moduleStart + moduleInfo.SizeOfImage;
    int matchCount = 0;

    for (std::uintptr_t currentAddress = moduleStart;
         currentAddress < moduleEnd;) {
        MEMORY_BASIC_INFORMATION memoryInfo{};
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(currentAddress), &memoryInfo,
                           sizeof(memoryInfo)) == 0) {
            break;
        }

        std::uintptr_t regionEnd =
            reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress) +
            memoryInfo.RegionSize;
        if (memoryInfo.State == MEM_COMMIT &&
            !(memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            const auto* regionStart =
                reinterpret_cast<const std::uint8_t*>(memoryInfo.BaseAddress);
            std::size_t regionSize = memoryInfo.RegionSize;
            if (regionEnd > moduleEnd) {
                regionSize -= regionEnd - moduleEnd;
                regionEnd = moduleEnd;
            }

            std::size_t searchOffset = 0;
            while (searchOffset + patternLength <= regionSize) {
                const void* firstByte = std::memchr(
                    regionStart + searchOffset, pattern[0],
                    regionSize - searchOffset - patternLength + 1);
                if (!firstByte) break;

                const std::size_t matchOffset =
                    static_cast<const std::uint8_t*>(firstByte) - regionStart;
                if (std::memcmp(regionStart + matchOffset, pattern,
                                patternLength) == 0) {
                    ++matchCount;
                    foundAddress =
                        reinterpret_cast<std::uintptr_t>(regionStart) + matchOffset;
                }
                searchOffset = matchOffset + 1;
            }
        }

        currentAddress = regionEnd;
        if (currentAddress <=
            reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress)) {
            break;
        }
    }
    return matchCount;
}

std::uintptr_t FindTarget()
{
    if (gSignature.empty()) return 0;

    std::uintptr_t targetAddress = 0;
    const int fullMatches =
        ScanModule(gSignature.data(), gSignature.size(), targetAddress);
    if (fullMatches == 1) return targetAddress;

    // 若另一个通用 inline hook 已替换入口，使用未被入口跳转覆盖的签名尾部
    // 重新定位原函数。后续链路由 SafetyHook 的 trampoline 处理，兼容性需实测。
    if (fullMatches == 0 && gWaitForEarlierHook &&
        gSignature.size() > kPrefixProbeLength) {
        std::uintptr_t suffixAddress = 0;
        const int suffixMatches = ScanModule(
            gSignature.data() + kPrefixProbeLength,
            gSignature.size() - kPrefixProbeLength, suffixAddress);
        if (suffixMatches == 1) {
            const std::uintptr_t candidate =
                suffixAddress - kPrefixProbeLength;
            Log("function entry changed; using unique signature suffix at 0x%llX",
                static_cast<unsigned long long>(candidate));
            return candidate;
        }
        Log("signature suffix matches=%d (need exactly 1)", suffixMatches);
    }

    Log("signature matches=%d (need exactly 1); hook not installed",
        fullMatches);
    return 0;
}

bool __fastcall Detour(std::uintptr_t firstArgument,
                       std::uintptr_t secondArgument,
                       std::uintptr_t queryObject,
                       std::uintptr_t fourthArgument);

void LogHookError(const char* stage, std::uintptr_t target,
                  const safetyhook::InlineHook::Error& error)
{
    using Error = safetyhook::InlineHook::Error;
    const char* name = "UNKNOWN";
    switch (error.type) {
    case Error::BAD_ALLOCATION: name = "BAD_ALLOCATION"; break;
    case Error::FAILED_TO_DECODE_INSTRUCTION: name = "FAILED_TO_DECODE_INSTRUCTION"; break;
    case Error::SHORT_JUMP_IN_TRAMPOLINE: name = "SHORT_JUMP_IN_TRAMPOLINE"; break;
    case Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: name = "IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE"; break;
    case Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: name = "UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE"; break;
    case Error::FAILED_TO_UNPROTECT: name = "FAILED_TO_UNPROTECT"; break;
    case Error::NOT_ENOUGH_SPACE: name = "NOT_ENOUGH_SPACE"; break;
    }
    if (error.type == Error::BAD_ALLOCATION) {
        const char* detail = error.allocator_error == safetyhook::Allocator::Error::BAD_VIRTUAL_ALLOC
            ? "BAD_VIRTUAL_ALLOC" : "NO_MEMORY_IN_RANGE";
        Log("SafetyHook %s failed at 0x%llX: %s (%s)", stage,
            static_cast<unsigned long long>(target), name, detail);
    } else {
        Log("SafetyHook %s failed at 0x%llX: %s (ip=%p)", stage,
            static_cast<unsigned long long>(target), name, static_cast<void*>(error.ip));
    }
}

bool Install(std::uintptr_t directTarget = 0)
{
    if (gTargetAddress != 0) return true;

    const std::uintptr_t targetAddress =
        directTarget != 0 ? directTarget : FindTarget();
    if (targetAddress == 0 || !mem::IsExecutable(targetAddress)) {
        Log("target is unavailable or not executable");
        return false;
    }

    auto created = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(targetAddress), reinterpret_cast<void*>(&Detour),
        safetyhook::InlineHook::StartDisabled);
    if (!created) {
        LogHookError("create", targetAddress, created.error());
        return false;
    }

    gInlineHook = new (std::nothrow) safetyhook::InlineHook(std::move(*created));
    if (!gInlineHook) {
        Log("SafetyHook owner allocation failed");
        return false;
    }
    // 回调可在 enable 返回前执行，必须先发布原函数指针。
    gOriginalFunction = gInlineHook->original<TargetFn>();
    const auto enabled = gInlineHook->enable();
    if (!enabled) {
        LogHookError("enable", targetAddress, enabled.error());
        gOriginalFunction = nullptr;
        delete gInlineHook;
        gInlineHook = nullptr;
        return false;
    }

    gTargetAddress = targetAddress;
    Log("hook installed via SafetyHook 0.7.0 at 0x%llX",
        static_cast<unsigned long long>(targetAddress));
    return true;
}

} // namespace hook

// ===========================================================================
//  Plugin 主体
// ===========================================================================
namespace plugin {

constexpr char kVersion[] = "1.4.0";

HMODULE gModule = nullptr;
volatile LONG gStop = 0;
std::wstring gModuleDir;
std::wstring gIniPath;
std::wstring gLogPath;

std::uintptr_t gGameBase = 0;

// --- 15.23.00 游戏内系统消息函数 / 聊天缓冲（沿用 WeaponSoundEnhance 确认的地址） ---
const std::uintptr_t kSystemMessageRva    = 0x1A540D0;
const std::uintptr_t kSystemMessageMgrRva = 0x500CE70;
typedef void (*SystemMessageFn)(void* manager, const char* utf8Buffer,
                                float duration, std::int32_t messageId, bool emphasized);
SystemMessageFn gSystemMessage = nullptr;
const std::uintptr_t kMessageBaseRva = 0x4F87FF0;
const std::uintptr_t kMessageLenOff  = 0xBC;
const std::uintptr_t kMessageBodyOff = 0xC0;

std::uintptr_t gPlayerRoot = 0x1450139A0ULL;
std::uintptr_t gMapRootRva = 0x500CDA0ULL; // CT 2.0.6 / GameVersion 421810
std::uintptr_t gTargetRva  = 0;        // ini: 直接指定目标函数 RVA（跳过签名扫描）
int gPollMs   = 60;
int gIdOffset = 4;
volatile int gEnabled = 1;
volatile int gUseChatEcho = 1;
volatile int gUseChatCommands = 1;

int gModifierKey = VK_CONTROL;
int gReloadKey   = VK_F5;
int gToggleKey   = VK_F9;

void Log(const char* fmt, ...);

void LogInit()
{
    gLogPath = gModuleDir + L"MHWI-VisualControllerExtended.log";
    ::DeleteFileW(gLogPath.c_str());
    Log("MHWI-VisualControllerExtended %s starting", kVersion);
}
void Log(const char* fmt, ...)
{
    char buf[2048] = {};
    va_list ap; va_start(ap, fmt); vsnprintf_s(buf, _TRUNCATE, fmt, ap); va_end(ap);
    ::OutputDebugStringA(buf);
    FILE* f = nullptr;
    if (_wfopen_s(&f, gLogPath.c_str(), L"ab") == 0 && f) {
        SYSTEMTIME st{}; ::GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u.%03u] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

std::wstring ReplaceExt(const std::wstring& path, const wchar_t* newExt)
{
    std::wstring p = path;
    std::size_t dot = p.find_last_of(L'.');
    std::size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return p + newExt;
    return p.substr(0, dot) + newExt;
}

bool ReadFileUtf8(const std::wstring& path, std::string& out)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16LL << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out.resize(static_cast<std::size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = ::ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    out.resize(rd);
    if (out.size() >= 3 &&
        (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

inline std::string Trim(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

using IniValueCallback = void (*)(const std::string& section, const std::string& key,
                                  const std::string& value, void* context);

void WalkIni(const std::string& text, IniValueCallback callback, void* context)
{
    std::string section;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (!key.empty()) callback(section, key, val, context);
    }
}

// 解析十进制或 0x 十六进制整数
int ParseInt(const std::string& v)
{
    const char* s = v.c_str();
    while (*s == ' ' || *s == '\t') ++s;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    return static_cast<int>(std::strtol(s, nullptr, base));
}

float ParseFloat(const std::string& value)
{
    return std::strtof(value.c_str(), nullptr);
}

std::uintptr_t ParseU64(const std::string& v)
{
    const char* s = v.c_str();
    while (*s == ' ' || *s == '\t') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    unsigned long long hv = 0;
    if (sscanf_s(s, "%llx", &hv) == 1) return static_cast<std::uintptr_t>(hv);
    return 0;
}

struct RuleBuildContext {
    std::vector<IdRule> rules;
};

void OnIniValue(const std::string& section, const std::string& key,
                const std::string& value, void* context)
{
    auto* buildContext = static_cast<RuleBuildContext*>(context);
    const bool isGlobalSection = (section == "VisualController");
    const bool isHotkeySection = (section == "Hotkeys");
    const bool isRuleSection = (section.size() > 4 && section.compare(0, 4, "Rule") == 0);

    if (isGlobalSection) {
        if (key == "PlayerRoot")            gPlayerRoot = ParseU64(value);
        else if (key == "MapRootRva")       gMapRootRva = ParseU64(value);
        else if (key == "PollMs")        { int v = atoi(value.c_str()); if (v >= 5) gPollMs = v; }
        else if (key == "Enabled")          gEnabled = atoi(value.c_str()) != 0;
        else if (key == "IdOffset")         gIdOffset = ParseInt(value);
        else if (key == "TargetRva")        gTargetRva = ParseU64(value);
        else if (key == "WaitForEarlierHook") hook::gWaitForEarlierHook = atoi(value.c_str()) != 0;
        else if (key == "HookWaitMs") {
            int waitMs = atoi(value.c_str());
            if (waitMs >= 0 && waitMs <= 30000) hook::gHookWaitMs = waitMs;
        }
        else if (key == "ChatEcho")         gUseChatEcho = atoi(value.c_str()) != 0;
        else if (key == "ChatCommands")     gUseChatCommands = atoi(value.c_str()) != 0;
        else if (key == "Signature") {
            // 空格/逗号分隔的十六进制字节序列
            std::vector<std::uint8_t> sig;
            std::string cur;
            for (std::size_t i = 0; i <= value.size(); ++i) {
                char c = (i < value.size()) ? value[i] : '\0';
                if (c == '\0' || c == ' ' || c == ',' || c == '\t') {
                    if (!cur.empty()) {
                        sig.push_back(static_cast<std::uint8_t>(std::strtol(cur.c_str(), nullptr, 16)));
                        cur.clear();
                    }
                    if (c == '\0') break;
                } else cur += c;
            }
            if (!sig.empty()) hook::gSignature = sig;
        }
        return;
    }

    if (isHotkeySection) {
        if      (key == "ModifierKey") gModifierKey = atoi(value.c_str());
        else if (key == "ReloadKey")   gReloadKey   = atoi(value.c_str());
        else if (key == "ToggleKey")   gToggleKey   = atoi(value.c_str());
        return;
    }

    if (!isRuleSection) return;

    int idx = atoi(section.c_str() + 4);
    if (idx < 1) return;
    while (static_cast<int>(buildContext->rules.size()) < idx) {
        IdRule r{};
        r.id = -1; r.weaponType = -1; r.spirit = -1;
        r.spiritMin = -1; r.spiritMax = -1;
        r.lmtMin = -1; r.lmtMax = -1; r.result = 0;
        r.demon = -1; r.demonMin = -1; r.demonMax = -1;
        r.archdemon = -1;
        r.healthPercentMin = -1.0f;
        r.healthPercentMax = -1.0f;
        r.healthPercentAbove = -1.0f;
        r.latchUntilReset = 0;
        buildContext->rules.push_back(r);
    }
    IdRule& rule = buildContext->rules[idx - 1];
    if      (key == "Id")         rule.id = ParseInt(value);
    else if (key == "WeaponType") rule.weaponType = ParseInt(value);
    else if (key == "Spirit")     rule.spirit = ParseInt(value);
    else if (key == "SpiritMin")  rule.spiritMin = ParseInt(value);
    else if (key == "SpiritMax")  rule.spiritMax = ParseInt(value);
    else if (key == "LmtMin")     rule.lmtMin = ParseInt(value);
    else if (key == "LmtMax")     rule.lmtMax = ParseInt(value);
    else if (key == "Demon")      rule.demon = ParseInt(value);
    else if (key == "DemonMin")   rule.demonMin = ParseInt(value);
    else if (key == "DemonMax")   rule.demonMax = ParseInt(value);
    else if (key == "Archdemon")  rule.archdemon = ParseInt(value);
    else if (key == "HealthPercentMin")   rule.healthPercentMin = ParseFloat(value);
    else if (key == "HealthPercentMax")   rule.healthPercentMax = ParseFloat(value);
    else if (key == "HealthPercentAbove") rule.healthPercentAbove = ParseFloat(value);
    else if (key == "LatchUntilReset")
        rule.latchUntilReset = atoi(value.c_str()) != 0 ? 1 : 0;
    else if (key == "Return")     rule.result = atoi(value.c_str()) != 0 ? 1 : 0;
}

void PublishRules(const std::vector<IdRule>& rules)
{
    // 双缓冲：写入非活动侧，再一次原子切换；detour 永远读到完整一致的一侧
    const LONG activeBuffer = ::InterlockedCompareExchange(&gActiveRuleBuffer, 0, 0);
    const int targetBuffer = activeBuffer == 0 ? 1 : 0;
    int publishedCount = 0;
    for (const auto& rule : rules) {
        if (rule.id < 0) continue;           // 段里没有 Id= 的占位跳过
        if (publishedCount >= kMaxRules) break;
        gRuleBuffers[targetBuffer][publishedCount++] = rule;
    }
    gRuleCounts[targetBuffer] = publishedCount;
    ::InterlockedExchange(&gActiveRuleBuffer, targetBuffer);
    ResetRuleLatches();
}

void LoadConfig()
{
    std::string configText;
    RuleBuildContext buildContext;
    if (!ReadFileUtf8(gIniPath, configText) || configText.empty()) {
        Log("config file missing or empty; no rules loaded (all IDs passthrough)");
    } else {
        WalkIni(configText, &OnIniValue, &buildContext);
    }
    PublishRules(buildContext.rules);
    player::gPlayerRootAddress = gPlayerRoot;
    player::SetMapRootAddress(gGameBase != 0 && gMapRootRva != 0
        ? gGameBase + gMapRootRva : 0);
    Log("config: PlayerRoot=0x%llX MapRootRva=0x%llX PollMs=%d Enabled=%d IdOffset=%d TargetRva=0x%llX "
        "WaitForEarlierHook=%d HookWaitMs=%d rules=%d sigLen=%zu",
        (unsigned long long)gPlayerRoot, (unsigned long long)gMapRootRva,
        gPollMs, gEnabled, gIdOffset,
        (unsigned long long)gTargetRva,
        hook::gWaitForEarlierHook, hook::gHookWaitMs,
        gRuleCounts[gActiveRuleBuffer], hook::gSignature.size());
}

void ReloadConfig()
{
    // 两次重载间隔限制，避免与正在读旧规则缓冲的 detour 竞争
    static std::uint64_t lastReload = 0;
    std::uint64_t now = ::GetTickCount64();
    if (now - lastReload < 1000) return;
    lastReload = now;
    LoadConfig();
}

void ResolveGameBase()
{
    HMODULE gameModule = ::GetModuleHandleW(L"MonsterHunterWorld.exe");
    if (!gameModule) return;
    gGameBase = reinterpret_cast<std::uintptr_t>(gameModule);
    player::SetMapRootAddress(gMapRootRva != 0 ? gGameBase + gMapRootRva : 0);
    gSystemMessage = reinterpret_cast<SystemMessageFn>(gGameBase + kSystemMessageRva);
    Log("game base=0x%llX", (unsigned long long)gGameBase);
}

void ShowMessage(const char* utf8, bool emphasized = false)
{
    if (gUseChatEcho == 0) { Log("[echo] %s", utf8); return; }
    if (gSystemMessage == nullptr || gGameBase == 0) return;
    if (!player::IsInScene()) return;
    void* messageManager = nullptr;
    if (!mem::ReadVal(gGameBase + kSystemMessageMgrRva, messageManager) || messageManager == nullptr) return;
    char messageBuffer[0x180] = {};
    _snprintf_s(messageBuffer, _TRUNCATE, "%s", utf8);
    gSystemMessage(messageManager, messageBuffer, 0.0f, -1, emphasized);
}

enum StateRequirement : unsigned {
    kNeedsSpirit = 1U << 0,
    kNeedsActionLmt = 1U << 1,
    kNeedsWeaponType = 1U << 2,
    kNeedsDemonMode = 1U << 3,
    kNeedsArchdemonMode = 1U << 4,
    kNeedsHealthPercent = 1U << 5,
};

// Detour 只读取后台线程已验证并原子发布的快照，不直接解引用游戏指针。
// 任一必需字段尚未就绪时返回 false，调用方必须透传原游戏函数。
bool ReadCachedState(unsigned requirements, int& spiritLevel, int& actionLmt,
                     int& weaponType, int& demonMode, int& archdemonMode,
                     float& healthPercent)
{
    const player::StateSnapshot state = player::GetStateSnapshot();
    spiritLevel = state.spiritLevel;
    actionLmt = state.actionLmt;
    weaponType = state.weaponType;
    demonMode = state.demonMode;
    archdemonMode = state.archdemonMode;
    healthPercent = state.healthPercent;

    if (state.entityAddress == 0) return false;
    if ((requirements & kNeedsSpirit) && spiritLevel < 0) return false;
    if ((requirements & kNeedsActionLmt) && actionLmt < 0) return false;
    if ((requirements & kNeedsWeaponType) && weaponType < 0) return false;
    if ((requirements & kNeedsDemonMode) && demonMode < 0) return false;
    if ((requirements & kNeedsArchdemonMode) && archdemonMode < 0) return false;
    if ((requirements & kNeedsHealthPercent) && healthPercent < 0.0f) return false;
    return true;
}

// 状态读取入口（函数指针，离线自测时可替换为桩）
using StateReader = bool (*)(unsigned, int&, int&, int&, int&, int&, float&);
StateReader gStateReader = &ReadCachedState;

} // namespace plugin

// Detour 放在 hook 命名空间，调用约定与目标函数一致。
// 查询函数使用四个寄存器参数；其余调用现场由 SafetyHook trampoline 保持。
bool __fastcall hook::Detour(std::uintptr_t firstArgument, std::uintptr_t secondArgument,
                             std::uintptr_t queryObject, std::uintptr_t fourthArgument)
{
    using namespace plugin;
    if (gEnabled && gOriginalFunction && queryObject != 0) {
        // 原函数入口也会立即读取 queryObject+IdOffset；这里不再用异常作为兜底控制流。
        const unsigned short id = *reinterpret_cast<const unsigned short*>(
            queryObject + static_cast<std::uintptr_t>(gIdOffset));
        const LONG activeBuffer = ::InterlockedCompareExchange(&gActiveRuleBuffer, 0, 0);
        const IdRule* rules = gRuleBuffers[activeBuffer];
        const int ruleCount = gRuleCounts[activeBuffer];
        const LONG latchEpoch = CurrentLatchEpoch();
        // 先算出该 ID 的规则需要哪些状态，再读取后台线程发布的安全快照。
        unsigned requirements = 0;
        for (int ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex) {
            const IdRule& rule = rules[ruleIndex];
            if (rule.id != static_cast<int>(id)) continue;
            if (rule.spirit >= 0 || rule.spiritMin >= 0 || rule.spiritMax >= 0) requirements |= kNeedsSpirit;
            if (rule.lmtMin >= 0 || rule.lmtMax >= 0) requirements |= kNeedsActionLmt;
            if (rule.weaponType >= 0) requirements |= kNeedsWeaponType;
            if (rule.demon >= 0 || rule.demonMin >= 0 || rule.demonMax >= 0) requirements |= kNeedsDemonMode;
            if (rule.archdemon >= 0) requirements |= kNeedsArchdemonMode;
            if (rule.healthPercentMin >= 0.0f || rule.healthPercentMax >= 0.0f ||
                rule.healthPercentAbove >= 0.0f || rule.latchUntilReset)
                requirements |= kNeedsHealthPercent;
        }
        int spiritLevel = -1;
        int actionLmt = -1;
        int weaponType = -1;
        int demonMode = -1;
        int archdemonMode = -1;
        float healthPercent = -1.0f;
        if (requirements &&
            !gStateReader(requirements, spiritLevel, actionLmt, weaponType,
                          demonMode, archdemonMode, healthPercent)) {
            return gOriginalFunction(firstArgument, secondArgument, queryObject, fourthArgument);
        }
        ObserveHealthForLatchReset(healthPercent);
        if (healthPercent > 0.0f) {
            const int latchedResult = ReadLatchedResult(id, latchEpoch);
            if (latchedResult >= 0) return latchedResult != 0;
        }
        for (int ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex) {
            const IdRule& rule = rules[ruleIndex];
            if (rule.id != static_cast<int>(id)) continue;
            if (rule.weaponType >= 0 && rule.weaponType != weaponType) continue;
            if (rule.spirit >= 0 && rule.spirit != spiritLevel) continue;
            if (rule.spiritMin >= 0 && spiritLevel < rule.spiritMin) continue;
            if (rule.spiritMax >= 0 && spiritLevel > rule.spiritMax) continue;
            if (rule.lmtMin >= 0 && actionLmt < rule.lmtMin) continue;
            if (rule.lmtMax >= 0 && actionLmt > rule.lmtMax) continue;
            if (rule.demon >= 0 && rule.demon != demonMode) continue;
            if (rule.demonMin >= 0 && demonMode < rule.demonMin) continue;
            if (rule.demonMax >= 0 && demonMode > rule.demonMax) continue;
            if (rule.archdemon >= 0 && rule.archdemon != archdemonMode) continue;
            if (rule.healthPercentMin >= 0.0f && healthPercent < rule.healthPercentMin) continue;
            if (rule.healthPercentMax >= 0.0f && healthPercent > rule.healthPercentMax) continue;
            if (rule.healthPercentAbove >= 0.0f && healthPercent <= rule.healthPercentAbove) continue;
            const bool result = rule.result != 0;
            if (rule.latchUntilReset) {
                // 0% 会复位已有锁存，但当前查询仍按规则返回；仅正血量重新锁存。
                if (healthPercent > 0.0f) LatchResult(id, result, latchEpoch);
            }
            return result;
        }
    }
    return gOriginalFunction
        ? gOriginalFunction(firstArgument, secondArgument, queryObject, fourthArgument)
        : true;
}

namespace plugin {

// ---- 游戏内聊天框 /vc 指令 ----
bool ParseCommand(const std::string& line);

bool HandleCommand(const std::string& rest)
{
    if (rest.empty() || rest == "status") {
        char msg[0x180] = {};
        _snprintf_s(msg, _TRUNCATE,
                    "vc: hook=%s hp=%.1f/%.1f(%.1f%%) weapon=%d spirit=%d demon=%d "
                    "archdemon=%d lmt=%d fsm=%d map=%d rules=%d",
                    hook::gTargetAddress ? "on" : "OFF",
                    player::gCurrentHealth, player::gMaxHealth, player::gHealthPercent,
                    player::gWeaponType, player::gSpiritLevel,
                    player::gDemonMode, player::gArchdemonMode,
                    player::gActionLmt, player::gFsmId, player::gCurrentMapId,
                    gRuleCounts[gActiveRuleBuffer]);
        ShowMessage(msg, true);
    } else if (rest == "reload" || rest == "re") {
        ReloadConfig();
        ShowMessage("vc config reloaded", true);
    } else if (rest == "on" || rest == "enable") {
        gEnabled = 1; ShowMessage("vc enabled", true);
    } else if (rest == "off" || rest == "disable") {
        gEnabled = 0; ShowMessage("vc disabled (passthrough)", true);
    } else if (rest == "help" || rest == "h") {
        ShowMessage("/vc status | reload | on | off | help");
    } else {
        ShowMessage("vc unknown command; try /vc help");
        return false;
    }
    return true;
}

bool ParseCommand(const std::string& line)
{
    std::string s = Trim(line);
    if (s.empty() || s[0] != '/') return false;
    const std::size_t separatorPosition = s.find_first_of(" \t");
    std::string command = (separatorPosition == std::string::npos)
        ? s.substr(1) : s.substr(1, separatorPosition - 1);
    const std::string arguments = (separatorPosition == std::string::npos)
        ? "" : Trim(s.substr(separatorPosition + 1));
    for (auto& character : command) if (character >= 'A' && character <= 'Z') character += 32;
    if (command == "vc" || command == "visual") {
        HandleCommand(arguments);
        return true;
    }
    return false;
}

// 读游戏聊天消息缓冲，若以 /vc 开头则执行并清空缓冲（mhw-toolkit 同款机制）。
bool PollChatCommand()
{
    if (gUseChatCommands == 0) return false;
    if (gGameBase == 0) return false;
    if (!player::IsInScene()) return false;
    std::uintptr_t base = 0;
    if (!mem::ReadVal(gGameBase + kMessageBaseRva, base) || base == 0) return false;

    std::uintptr_t lenPtr  = base + kMessageLenOff;
    std::uintptr_t bodyPtr = base + kMessageBodyOff;
    if (!mem::IsReadable(lenPtr, 4) || !mem::IsReadable(bodyPtr, 4)) return false;

    std::int32_t len = 0;
    if (!mem::ReadVal(lenPtr, len) || len <= 0 || len > 1024) return false;

    std::string msg;
    if (mem::IsReadable(bodyPtr, static_cast<std::size_t>(len) + 1)) {
        msg.assign(reinterpret_cast<const char*>(bodyPtr), static_cast<std::size_t>(len));
        while (!msg.empty() && (msg.back() == '\0' || msg.back() == '\n' || msg.back() == '\r'))
            msg.pop_back();
    }
    if (msg.empty()) return false;

    std::string trimmed = Trim(msg);
    if (trimmed.rfind("/vc", 0) == 0) {
        static std::string lastHandled;
        static std::uint64_t lastHandledAt = 0;
        std::uint64_t now = ::GetTickCount64();
        if (lastHandled == trimmed && (now - lastHandledAt) < 1000) return false;
        lastHandled = trimmed;
        lastHandledAt = now;
        Log("[chat] '%s'", msg.c_str());
        if (mem::IsWritable(bodyPtr, static_cast<std::size_t>(len) + 1))
            std::memset(reinterpret_cast<void*>(bodyPtr), 0, static_cast<std::size_t>(len) + 1);
        if (mem::IsWritable(lenPtr, 4))
            std::memset(reinterpret_cast<void*>(lenPtr), 0, 4);
        return ParseCommand(msg);
    }
    return false;
}

// ===========================================================================
//  工作线程：等待游戏模块 -> 安装 hook -> 轮询刷新状态(练气/动作/武器/地图) + 处理指令
// ===========================================================================
DWORD WINAPI WorkerProc(LPVOID)
{
    while (::GetModuleHandleW(L"MonsterHunterWorld.exe") == nullptr &&
           ::InterlockedCompareExchange(&gStop, 0, 0) == 0)
        ::Sleep(500);
    if (gStop) return 0;

    Log("game module found");
    ResolveGameBase();

    if (hook::gWaitForEarlierHook && hook::gHookWaitMs > 0) {
        Log("waiting %dms before hook installation", hook::gHookWaitMs);
        int remainingWaitMs = hook::gHookWaitMs;
        while (remainingWaitMs > 0 && ::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
            const DWORD sleepMs = static_cast<DWORD>((remainingWaitMs < 100) ? remainingWaitMs : 100);
            ::Sleep(sleepMs);
            remainingWaitMs -= static_cast<int>(sleepMs);
        }
        if (::InterlockedCompareExchange(&gStop, 0, 0) != 0) return 0;
    }

    // 安装 Hook：失败持续重试（前 6 次每 5 秒，之后每 30 秒），
    // 覆盖插件先于游戏就绪的时序；签名长期不命中时日志会记录原因。
    bool installed = false;
    int attempt = 0;
    while (!installed && ::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        installed = (gTargetRva != 0) ? hook::Install(gGameBase + gTargetRva)
                                      : hook::Install();
        if (!installed) {
            ++attempt;
            const DWORD waitMs = (attempt <= 6) ? 5000 : 30000;
            Log("hook install attempt %d failed; retry in %us", attempt, waitMs / 1000);
            ::Sleep(waitMs);
        }
    }

    std::uint64_t lastHeartbeat = 0;
    bool firstState = true;
    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(static_cast<DWORD>(gPollMs));
        if (::InterlockedCompareExchange(&gStop, 0, 0) != 0) break;

        player::Refresh();
        PollChatCommand();

        // 状态变化即时记录：切换武器状态（如双刀鬼人化）时在日志里立刻可见，
        // 用于确认规则条件的实际取值。
        {
            static int prevSp = -2, prevLmt = -2, prevWt = -2, prevFsm = -2,
                       prevDm = -2, prevArchdemon = -2, prevMap = -2;
            const int spiritLevel = player::gSpiritLevel;
            const int actionLmt = player::gActionLmt;
            const int weaponType = player::gWeaponType;
            const int fsmId = player::gFsmId;
            const int demonMode = player::gDemonMode;
            const int archdemonMode = player::gArchdemonMode;
            const int currentMapId = player::gCurrentMapId;
            if (spiritLevel != prevSp || actionLmt != prevLmt || weaponType != prevWt ||
                fsmId != prevFsm || demonMode != prevDm || archdemonMode != prevArchdemon ||
                currentMapId != prevMap) {
                Log("state change: weapon %d->%d  spirit %d->%d  demon %d->%d  "
                    "archdemon %d->%d  lmt %d->%d  fsm %d->%d  map %d->%d",
                    prevWt, weaponType, prevSp, spiritLevel, prevDm, demonMode,
                    prevArchdemon, archdemonMode,
                    prevLmt, actionLmt, prevFsm, fsmId, prevMap, currentMapId);
                prevSp = spiritLevel; prevLmt = actionLmt; prevWt = weaponType;
                prevFsm = fsmId; prevDm = demonMode; prevArchdemon = archdemonMode;
                prevMap = currentMapId;
            }
        }

        const std::uint64_t nowMs = ::GetTickCount64();
        if (firstState || (nowMs - lastHeartbeat >= 4000)) {
            Log("state: hook=%s hp=%.1f/%.1f(%.1f%%) weapon=%d spirit=%d demon=%d archdemon=%d "
                "lmt=%d fsm=%d map=%d en=%d rules=%d mgr=0x%llX ent=0x%llX",
                hook::gTargetAddress ? "on" : "OFF",
                player::gCurrentHealth, player::gMaxHealth, player::gHealthPercent,
                player::gWeaponType, player::gSpiritLevel,
                player::gDemonMode, player::gArchdemonMode,
                player::gActionLmt, player::gFsmId, player::gCurrentMapId,
                gEnabled, gRuleCounts[gActiveRuleBuffer],
                (unsigned long long)player::gManagerAddress,
                (unsigned long long)player::gEntityAddress);
            lastHeartbeat = nowMs;
            firstState = false;
        }
    }
    return 0;
}

// ===========================================================================
//  热键线程：Ctrl+F5 重载 ini；Ctrl+F9 开关
// ===========================================================================
DWORD WINAPI HotkeyProc(LPVOID)
{
    bool reloadWasPressed = false;
    bool toggleWasPressed = false;
    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(30);
        const bool modifierPressed = (gModifierKey == 0) ||
                                     ((::GetAsyncKeyState(gModifierKey) & 0x8000) != 0);
        if (!modifierPressed) { reloadWasPressed = toggleWasPressed = false; continue; }
        const bool reloadDown = (::GetAsyncKeyState(gReloadKey) & 0x8000) != 0;
        const bool togDown    = (::GetAsyncKeyState(gToggleKey) & 0x8000) != 0;

        if (reloadDown && !reloadWasPressed) {
            ReloadConfig();
            ShowMessage("vc reloaded", true);
            Log("[hotkey] config reloaded (enabled=%d rules=%d)", gEnabled, gRuleCounts[gActiveRuleBuffer]);
        }
        reloadWasPressed = reloadDown;

        if (togDown && !toggleWasPressed) {
            gEnabled = gEnabled ? 0 : 1;
            ShowMessage(gEnabled ? "vc enabled" : "vc disabled (passthrough)", true);
            Log("[hotkey] enabled toggled -> %d", gEnabled);
        }
        toggleWasPressed = togDown;
    }
    return 0;
}

bool gStarted = false;
void Start(HMODULE module)
{
    if (gStarted) return;
    gStarted = true;
    gModule = module;
    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(module, self, MAX_PATH);
    const std::wstring modulePath(self);
    const std::size_t directorySeparator = modulePath.find_last_of(L"\\/");
    gModuleDir = (directorySeparator == std::wstring::npos)
        ? std::wstring() : modulePath.substr(0, directorySeparator + 1);
    gIniPath = ReplaceExt(modulePath, L".ini");
    LogInit();
    Log("build=%s hook_backend=SafetyHook/0.7.0 Zydis=4.1.0 compiled=%s %s",
        kVersion, __DATE__, __TIME__);
    LoadConfig();
    HANDLE t = ::CreateThread(nullptr, 0, &WorkerProc, nullptr, 0, nullptr);
    if (t) ::CloseHandle(t);
    HANDLE hk = ::CreateThread(nullptr, 0, &HotkeyProc, nullptr, 0, nullptr);
    if (hk) ::CloseHandle(hk);
}

} // namespace plugin

// hook 命名空间里的 Log 前置声明解析到 plugin::Log
void hook::Log(const char* fmt, ...)
{
    char buf[2048] = {};
    va_list ap; va_start(ap, fmt); vsnprintf_s(buf, _TRUNCATE, fmt, ap); va_end(ap);
    plugin::Log("%s", buf);
}

// ===========================================================================
//  导出与入口
// ===========================================================================
extern "C" __declspec(dllexport) BOOL VisualControllerExtended_IsInstalled() { return TRUE; }
extern "C" __declspec(dllexport) BOOL MHWI_VisualControllerExtended_IsInstalled() { return TRUE; }

// 插件加载器入口；重复调用由 plugin::Start 内部保护
__declspec(dllexport) bool Load()
{
    plugin::Start(plugin::gModule);
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(module);
        plugin::Start(module);
    } else if (reason == DLL_PROCESS_DETACH) {
        ::InterlockedExchange(&plugin::gStop, 1);
    }
    return TRUE;
}

// ===========================================================================
//  离线自测（cl /DVCE_TEST 编译为 exe 后运行）：
//  验证 ini 解析、规则匹配、detour 分支——不需要游戏环境。
// ===========================================================================
#ifdef VCE_TEST

static int testSpiritLevel = -1;
static int testActionLmt = -1;
static int testWeaponType = -1;
static int testDemonMode = -1;
static int testArchdemonMode = -1;
static float testHealthPercent = -1.0f;
static int gOriginalCallCount = 0;

static bool TestStateReader(unsigned, int& spiritLevel, int& actionLmt,
                            int& weaponType, int& demonMode, int& archdemonMode,
                            float& healthPercent)
{
    spiritLevel = testSpiritLevel;
    actionLmt = testActionLmt;
    weaponType = testWeaponType;
    demonMode = testDemonMode;
    archdemonMode = testArchdemonMode;
    healthPercent = testHealthPercent;
    return true;
}
static bool __fastcall FakeOrig(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t)
{
    ++gOriginalCallCount;
    return false;
}

static int RunId(unsigned short id)
{
    unsigned char obj[32] = {};
    *reinterpret_cast<unsigned short*>(obj + 4) = id;
    const int callsBefore = gOriginalCallCount;
    const bool r = hook::Detour(0, 0, reinterpret_cast<std::uintptr_t>(obj), 0);
    if (gOriginalCallCount != callsBefore) return -1;   // 走了透传
    return r ? 1 : 0;
}

static int gFailureCount = 0;
static void Expect(const char* name, int got, int want)
{
    const bool ok = (got == want);
    if (!ok) ++gFailureCount;
    printf("  [%s] %-22s got=%2d want=%2d\n", ok ? "PASS" : "FAIL", name, got, want);
}

int main()
{
    plugin::gIniPath = L"test_rules.ini";   // 自测专用配置（含双刀和血量规则）
    plugin::gLogPath = L"test_run.log";
    plugin::LogInit();
    plugin::LoadConfig();

    // ---- 原始生命值到百分比的校验与 0..100 钳制 ----
    {
        float healthPercent = -1.0f;
        Expect("health 50/200 valid", player::CalculateHealthPercent(50.0f, 200.0f, healthPercent), 1);
        Expect("health 50/200 pct", static_cast<int>(healthPercent * 100.0f), 2500);
        Expect("health negative clamp", player::CalculateHealthPercent(-5.0f, 100.0f, healthPercent), 1);
        Expect("health negative pct", static_cast<int>(healthPercent), 0);
        Expect("health over clamp", player::CalculateHealthPercent(120.0f, 100.0f, healthPercent), 1);
        Expect("health over pct", static_cast<int>(healthPercent), 100);
        Expect("health max zero", player::CalculateHealthPercent(0.0f, 0.0f, healthPercent), 0);
    }

    // ---- CT Current Map 指针链：MapRoot -> +80 -> +EEC0 -> +118(uint16) ----
    {
        std::vector<unsigned char> level0(0x88, 0);
        std::vector<unsigned char> level1(0xEEC8, 0);
        std::vector<unsigned char> mapData(0x11A, 0);
        const std::uintptr_t level0Address = reinterpret_cast<std::uintptr_t>(level0.data());
        const std::uintptr_t level1Address = reinterpret_cast<std::uintptr_t>(level1.data());
        const std::uintptr_t mapDataAddress = reinterpret_cast<std::uintptr_t>(mapData.data());
        std::uintptr_t mapRootValue = level0Address;
        std::memcpy(level0.data() + 0x80, &level1Address, sizeof(level1Address));
        std::memcpy(level1.data() + 0xEEC0, &mapDataAddress, sizeof(mapDataAddress));
        const std::uint16_t expectedMapId = 123;
        std::memcpy(mapData.data() + 0x118, &expectedMapId, sizeof(expectedMapId));
        player::SetMapRootAddress(reinterpret_cast<std::uintptr_t>(&mapRootValue));
        int currentMapId = -1;
        Expect("map chain valid", player::ReadCurrentMap(currentMapId), 1);
        Expect("map chain value", currentMapId, 123);
        player::SetMapRootAddress(0);
        Expect("map chain unavailable", player::ReadCurrentMap(currentMapId), 0);
    }

    const int ruleCount = gRuleCounts[gActiveRuleBuffer];
    printf("rules loaded: %d (expect 58)\n", ruleCount);
    if (ruleCount != 58) ++gFailureCount;
    for (int ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex) {
        const IdRule& rule = gRuleBuffers[gActiveRuleBuffer][ruleIndex];
        printf("  rule%02d id=%d wt=%2d sp=%2d lmt=[%6d,%6d] ret=%d\n",
               ruleIndex + 1, rule.id, rule.weaponType, rule.spirit,
               rule.lmtMin, rule.lmtMax, rule.result);
    }

    hook::gOriginalFunction = &FakeOrig;

    // 人物状态尚未发布时，有条件规则必须透传，不能执行危险的现场指针读取。
    plugin::gStateReader = &plugin::ReadCachedState;
    player::PublishState(player::StateSnapshot{});
    Expect("state unavailable id30", RunId(30), -1);

    player::StateSnapshot partialState;
    partialState.entityAddress = 1;
    player::PublishState(partialState);
    Expect("action unavailable id30", RunId(30), -1);

    partialState.weaponType = 2;
    player::PublishState(partialState);
    Expect("arch unavailable id23", RunId(23), -1);
    Expect("health unavailable id42", RunId(42), -1);

    player::StateSnapshot readyState;
    readyState.entityAddress = 1;
    readyState.actionLmt = 100;
    player::PublishState(readyState);
    Expect("cached lmt=100 id30", RunId(30), 1);

    plugin::gStateReader = &TestStateReader;

    // ---- 优先保障：ID 30/31（仅依赖动作 lmt） ----
    testSpiritLevel = -1; testWeaponType = -1;
    testActionLmt = 100;
    Expect("lmt=100   id30", RunId(30), 1);
    Expect("lmt=100   id31", RunId(31), 0);
    testActionLmt = 49265;   // 气刃斩1 的 LMT
    Expect("lmt=49265 id30", RunId(30), 0);
    Expect("lmt=49265 id31", RunId(31), 1);
    testActionLmt = 0x8000;  // 边界
    Expect("lmt=32768 id30", RunId(30), 0);
    Expect("lmt=32768 id31", RunId(31), 1);

    // ---- 太刀练气（ID 32..35） ----
    testActionLmt = -1; testWeaponType = 3;
    testSpiritLevel = 0; Expect("wt=3 sp=0 id32", RunId(32), 1);
    testSpiritLevel = 1; Expect("wt=3 sp=1 id32", RunId(32), 0);
            Expect("wt=3 sp=1 id33", RunId(33), 1);
    testSpiritLevel = 2; Expect("wt=3 sp=2 id34", RunId(34), 1);
            Expect("wt=3 sp=2 id33", RunId(33), 0);
    testSpiritLevel = 3; Expect("wt=3 sp=3 id35", RunId(35), 1);
            Expect("wt=3 sp=3 id34", RunId(34), 0);

    // ---- 太刀练气范围（ID36=白刃及以上，ID37=黄刃及以上） ----
    testSpiritLevel = 0;
    Expect("wt=3 sp=0 id36", RunId(36), 0);
    Expect("wt=3 sp=0 id37", RunId(37), 0);
    testSpiritLevel = 1;
    Expect("wt=3 sp=1 id36", RunId(36), 1);
    Expect("wt=3 sp=1 id37", RunId(37), 0);
    testSpiritLevel = 2;
    Expect("wt=3 sp=2 id36", RunId(36), 1);
    Expect("wt=3 sp=2 id37", RunId(37), 1);
    testSpiritLevel = 3;
    Expect("wt=3 sp=3 id36", RunId(36), 1);
    Expect("wt=3 sp=3 id37", RunId(37), 1);

    // ---- 太刀练气反向显示（达到对应等级时隐藏） ----
    testSpiritLevel = 0;
    Expect("wt=3 sp=0 id38", RunId(38), 1);
    Expect("wt=3 sp=0 id39", RunId(39), 1);
    Expect("wt=3 sp=0 id40", RunId(40), 1);
    testSpiritLevel = 1;
    Expect("wt=3 sp=1 id38", RunId(38), 0);
    Expect("wt=3 sp=1 id39", RunId(39), 1);
    Expect("wt=3 sp=1 id40", RunId(40), 1);
    testSpiritLevel = 2;
    Expect("wt=3 sp=2 id38", RunId(38), 0);
    Expect("wt=3 sp=2 id39", RunId(39), 0);
    Expect("wt=3 sp=2 id40", RunId(40), 1);
    testSpiritLevel = 3;
    Expect("wt=3 sp=3 id38", RunId(38), 0);
    Expect("wt=3 sp=3 id39", RunId(39), 0);
    Expect("wt=3 sp=3 id40", RunId(40), 0);

    // ---- 非太刀武器的规则回退行为 ----
    testWeaponType = 10; testSpiritLevel = 0;
    Expect("wt=10     id32", RunId(32), 1);
    Expect("wt=10     id33", RunId(33), 0);
    Expect("wt=10     id35", RunId(35), 0);
    testSpiritLevel = 3;
    Expect("wt=10 sp=3 id36", RunId(36), 0);
    Expect("wt=10 sp=3 id37", RunId(37), 0);
    Expect("wt=10 sp=3 id38", RunId(38), 1);
    Expect("wt=10 sp=3 id39", RunId(39), 1);
    Expect("wt=10 sp=3 id40", RunId(40), 1);

    // ---- 双刀鬼人化（ID 22：weapon==2 且 demon==1 -> true，否则 false） ----
    testActionLmt = -1; testSpiritLevel = -1;
    testWeaponType = 2; testDemonMode = 0;
    Expect("wt=2 dm=0 id22", RunId(22), 0);
    testWeaponType = 2; testDemonMode = 1;
    Expect("wt=2 dm=1 id22", RunId(22), 1);
    testWeaponType = 3; testDemonMode = 1;    // 太刀不受此规则影响
    Expect("wt=3 dm=1 id22", RunId(22), 0);

    // ---- 双刀鬼人强化（ID 23：weapon==2 且 archdemon==1 -> true，否则 false） ----
    testDemonMode = 0;
    testWeaponType = 2; testArchdemonMode = 0;
    Expect("wt=2 arch=0 id23", RunId(23), 0);
    testArchdemonMode = 1;
    Expect("wt=2 arch=1 id23", RunId(23), 1);
    testWeaponType = 3;
    Expect("wt=3 arch=1 id23", RunId(23), 0);

    // ID 22 仅由 Demon 控制，不受 Archdemon 影响。
    testWeaponType = 2; testDemonMode = 0; testArchdemonMode = 1;
    Expect("arch=1 dm=0 id22", RunId(22), 0);

    // ---- 双刀状态反向控制（ID24/25：状态成立时隐藏，否则显示） ----
    testWeaponType = 2; testDemonMode = 0; testArchdemonMode = 0;
    Expect("wt=2 dm=0 id24", RunId(24), 1);
    Expect("wt=2 arch=0 id25", RunId(25), 1);
    testDemonMode = 1;
    Expect("wt=2 dm=1 id24", RunId(24), 0);
    testDemonMode = 0; testArchdemonMode = 1;
    Expect("wt=2 arch=1 id25", RunId(25), 0);
    testWeaponType = 3; testDemonMode = 1; testArchdemonMode = 1;
    Expect("wt=3 dm=1 id24", RunId(24), 1);
    Expect("wt=3 arch=1 id25", RunId(25), 1);

    // ---- 同一 ID 多条规则构成“或”（ID50：太刀红刃 或 双刀鬼人强化） ----
    testWeaponType = 3; testSpiritLevel = 3; testArchdemonMode = 0;
    Expect("wt=3 red id50", RunId(50), 1);
    testSpiritLevel = 2; testArchdemonMode = 1;
    Expect("wt=3 yellow id50", RunId(50), 0);
    testWeaponType = 2; testSpiritLevel = 3; testArchdemonMode = 1;
    Expect("wt=2 arch=1 id50", RunId(50), 1);
    testArchdemonMode = 0;
    Expect("wt=2 arch=0 id50", RunId(50), 0);
    testWeaponType = 10; testSpiritLevel = 3; testArchdemonMode = 1;
    Expect("wt=10 states id50", RunId(50), 0);

    // ---- 人物血量百分比边界（42=0，43=(0,25]，44=(25,50]，45=(50,75]，46=(75,100]） ----
    testHealthPercent = 0.0f;
    Expect("hp=0 id42", RunId(42), 1);
    Expect("hp=0 id43", RunId(43), 0);
    testHealthPercent = 0.01f;
    Expect("hp=.01 id42", RunId(42), 0);
    Expect("hp=.01 id43", RunId(43), 1);
    testHealthPercent = 25.0f;
    Expect("hp=25 id43", RunId(43), 1);
    Expect("hp=25 id44", RunId(44), 0);
    testHealthPercent = 25.01f;
    Expect("hp=25.01 id43", RunId(43), 0);
    Expect("hp=25.01 id44", RunId(44), 1);
    testHealthPercent = 50.0f;
    Expect("hp=50 id44", RunId(44), 1);
    Expect("hp=50 id45", RunId(45), 0);
    testHealthPercent = 50.01f;
    Expect("hp=50.01 id44", RunId(44), 0);
    Expect("hp=50.01 id45", RunId(45), 1);
    testHealthPercent = 75.0f;
    Expect("hp=75 id45", RunId(45), 1);
    Expect("hp=75 id46", RunId(46), 0);
    testHealthPercent = 75.01f;
    Expect("hp=75.01 id45", RunId(45), 0);
    Expect("hp=75.01 id46", RunId(46), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id46", RunId(46), 1);
    testHealthPercent = 100.01f;
    Expect("hp=100.01 id46", RunId(46), 0);

    // ---- 人物血量锁存显示（47<=25，48<=50，49<=75；归零复位） ----
    ResetRuleLatches();
    testHealthPercent = 100.0f;
    Expect("hp=100 id47 initial", RunId(47), 0);
    Expect("hp=100 id48 initial", RunId(48), 0);
    Expect("hp=100 id49 initial", RunId(49), 0);

    testHealthPercent = 25.0f;
    Expect("hp=25 id47 trigger", RunId(47), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id47 latched", RunId(47), 1);
    testHealthPercent = 0.0f;
    Expect("hp=0 id47 reset", RunId(47), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id47 reset", RunId(47), 0);

    testHealthPercent = 50.0f;
    Expect("hp=50 id48 trigger", RunId(48), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id48 latched", RunId(48), 1);
    testHealthPercent = 0.0f;
    Expect("hp=0 id48 reset", RunId(48), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id48 reset", RunId(48), 0);

    testHealthPercent = 75.0f;
    Expect("hp=75 id49 trigger", RunId(49), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id49 latched", RunId(49), 1);
    testHealthPercent = 0.0f;
    Expect("hp=0 id49 reset", RunId(49), 1);
    testHealthPercent = 100.0f;
    Expect("hp=100 id49 reset", RunId(49), 0);

    // 配置重载、玩家实体切换和有效地图变化都必须清除锁存。
    testHealthPercent = 25.0f;
    Expect("hp=25 id47 relatch", RunId(47), 1);
    plugin::LoadConfig();
    testHealthPercent = 100.0f;
    Expect("reload resets id47", RunId(47), 0);
    testHealthPercent = 25.0f;
    Expect("hp=25 id47 relatch2", RunId(47), 1);
    player::StateSnapshot changedEntity;
    changedEntity.entityAddress = 2;
    changedEntity.healthPercent = 100.0f;
    player::PublishState(changedEntity);
    testHealthPercent = 100.0f;
    Expect("entity resets id47", RunId(47), 0);

    // 首个有效地图只建立基线；读取失败和相同地图不复位；有效地图变化才复位。
    player::ResetMapTracking();
    testHealthPercent = 25.0f;
    Expect("hp=25 id47 map latch", RunId(47), 1);
    player::StateSnapshot mapState;
    mapState.entityAddress = 2;
    mapState.healthPercent = 100.0f;
    mapState.currentMapId = 101;
    player::PublishState(mapState);
    testHealthPercent = 100.0f;
    Expect("first map keeps latch", RunId(47), 1);
    mapState.currentMapId = -1;
    player::PublishState(mapState);
    Expect("map read fail keeps id", player::gCurrentMapId, 101);
    Expect("map read fail keeps latch", RunId(47), 1);
    mapState.currentMapId = 101;
    player::PublishState(mapState);
    Expect("same map keeps latch", RunId(47), 1);
    mapState.currentMapId = 102;
    player::PublishState(mapState);
    Expect("map change resets id47", RunId(47), 0);

    // ---- 反向锁存（52=0即时隐藏；53/54/55 达阈值后隐藏至归零） ----
    testHealthPercent = 100.0f;
    Expect("hp=100 id52", RunId(52), 1);
    Expect("hp=100 id53 initial", RunId(53), 1);
    Expect("hp=100 id54 initial", RunId(54), 1);
    Expect("hp=100 id55 initial", RunId(55), 1);

    testHealthPercent = 25.0f;
    Expect("hp=25 id53 trigger", RunId(53), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id53 latched", RunId(53), 0);
    testHealthPercent = 0.0f;
    Expect("hp=0 id52 instant", RunId(52), 0);
    Expect("hp=0 id53 reset", RunId(53), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id53 reset", RunId(53), 1);

    testHealthPercent = 50.0f;
    Expect("hp=50 id54 trigger", RunId(54), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id54 latched", RunId(54), 0);
    testHealthPercent = 0.0f;
    Expect("hp=0 id54 reset", RunId(54), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id54 reset", RunId(54), 1);

    testHealthPercent = 75.0f;
    Expect("hp=75 id55 trigger", RunId(55), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id55 latched", RunId(55), 0);
    testHealthPercent = 0.0f;
    Expect("hp=0 id55 reset", RunId(55), 0);
    testHealthPercent = 100.0f;
    Expect("hp=100 id55 reset", RunId(55), 1);

    // ---- 未配置的 ID 透传 ----
    Expect("id99 passthrough", RunId(99), -1);

    // DLL 不再包含回退规则；INI 缺失时必须发布空规则集并全部透传。
    plugin::gIniPath = L"__vce_missing_rules__.ini";
    plugin::LoadConfig();
    Expect("missing ini no rules", gRuleCounts[gActiveRuleBuffer], 0);
    Expect("missing ini passthrough", RunId(30), -1);

    printf(gFailureCount ? "FAILED: %d case(s)\n" : "ALL PASS\n", gFailureCount);
    return gFailureCount ? 1 : 0;
}
#endif
