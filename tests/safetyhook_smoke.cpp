// Exercise the actual plugin installer against a private executable allocation.
// This executable never loads or modifies the game.
#include "../MHWI-VisualControllerExtended.cpp"

#include <array>

namespace {
int failures = 0;
int earlierCalls = 0;
hook::TargetFn earlierOriginal = nullptr;

void Check(const char* label, bool passed) {
    std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", label);
    if (!passed) ++failures;
}

bool __fastcall EarlierDetour(std::uintptr_t a, std::uintptr_t b,
                             std::uintptr_t c, std::uintptr_t d) {
    ++earlierCalls;
    return !earlierOriginal(a, b, c, d);
}

bool RemovePluginHook() {
    if (!hook::gInlineHook) return true;
    const auto disabled = hook::gInlineHook->disable();
    if (!disabled) {
        Check("plugin disable", false);
        return false;
    }
    hook::gOriginalFunction = nullptr;
    delete hook::gInlineHook;
    hook::gInlineHook = nullptr;
    hook::gTargetAddress = 0;
    return true;
}
}

int main() {
    // Windows x64: return (rcx ^ rdx ^ r8 ^ r9) == 0.
    // Place target in a separate allocation to exercise SafetyHook's page trap.
    std::array<unsigned char, 32> code{};
    code.fill(0x90);
    const unsigned char instructions[] = {
        0x48, 0x89, 0xC8, 0x48, 0x31, 0xD0, 0x4C, 0x31, 0xC0,
        0x4C, 0x31, 0xC8, 0x48, 0x85, 0xC0, 0x0F, 0x94, 0xC0, 0xC3
    };
    std::memcpy(code.data(), instructions, sizeof(instructions));
    auto* memory = static_cast<unsigned char*>(
        ::VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 1;
    std::memcpy(memory, code.data(), code.size());
    DWORD oldProtection = 0;
    if (!::VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &oldProtection) ||
        !::FlushInstructionCache(::GetCurrentProcess(), memory, code.size())) return 1;
    const auto target = reinterpret_cast<hook::TargetFn>(memory);
    const auto address = reinterpret_cast<std::uintptr_t>(memory);
    // Disable rule overrides to verify forwarding of all four arguments.
    plugin::gEnabled = false;
    Check("unhooked true", target(1, 2, 4, 7));
    Check("unhooked false", !target(1, 2, 4, 8));

    for (int iteration = 0; iteration < 3; ++iteration) {
        const bool installed = hook::Install(address);
        Check("plugin install", installed);
        if (!installed) return 1;
        Check("entry patched", std::memcmp(memory, code.data(), code.size()) != 0);
        Check("forward true with four arguments", target(1, 2, 4, 7));
        Check("forward false with four arguments", !target(1, 2, 4, 8));
        Check("repeated install is idempotent", hook::Install(address));
        const auto disabled = hook::gInlineHook->disable();
        Check("disable", disabled.has_value());
        if (!disabled) return 1;
        Check("disable restores entry bytes", std::memcmp(memory, code.data(), code.size()) == 0);
        Check("disabled function works", target(1, 2, 4, 7));
        const auto enabled = hook::gInlineHook->enable();
        Check("re-enable", enabled.has_value());
        if (!enabled) return 1;
        Check("re-enabled function works", target(1, 2, 4, 7));
        if (!RemovePluginHook()) return 1;
        Check("removal restores entry bytes", std::memcmp(memory, code.data(), code.size()) == 0);
    }

    auto earlier = safetyhook::InlineHook::create(
        memory, reinterpret_cast<void*>(&EarlierDetour), safetyhook::InlineHook::StartDisabled);
    Check("create earlier hook", earlier.has_value());
    if (!earlier) return 1;
    earlierOriginal = earlier->original<hook::TargetFn>();
    const auto earlierEnabled = earlier->enable();
    Check("enable earlier hook", earlierEnabled.has_value());
    if (!earlierEnabled) return 1;
    Check("earlier hook changes result", !target(1, 2, 4, 7));
    const bool chained = hook::Install(address);
    Check("plugin installs over existing jump", chained);
    if (!chained) return 1;
    const int before = earlierCalls;
    Check("chain preserves earlier false result", !target(1, 2, 4, 7));
    Check("chain preserves earlier true result", target(1, 2, 4, 8));
    Check("earlier hook called exactly twice", earlierCalls == before + 2);
    if (!RemovePluginHook()) return 1;
    Check("removing outer hook preserves earlier hook", !target(1, 2, 4, 7));
    const auto earlierDisabled = earlier->disable();
    Check("disable earlier hook", earlierDisabled.has_value());
    if (!earlierDisabled) return 1;
    earlier->reset();
    Check("full chain removal restores entry bytes", std::memcmp(memory, code.data(), code.size()) == 0);
    Check("original behavior restored", target(1, 2, 4, 7));
    ::VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("SafetyHook smoke: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
