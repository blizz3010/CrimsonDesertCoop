#include <cdcoop/core/memory.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <filesystem>

// MSVC linker-defined symbol pointing at our own DLL's image base. Stable
// across every TU in this module, so we can recover the DLL's on-disk path
// without having to thread HMODULE through from DllMain.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace cdcoop {

std::string self_module_dir() {
    wchar_t wpath[MAX_PATH];
    DWORD len = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),
                                   wpath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return {};
    std::filesystem::path p(std::wstring(wpath, len));
    return (p.parent_path() / "").string();
}


MemoryScanner::Pattern MemoryScanner::parse_pattern(const std::string& sig_str) {
    Pattern pat;
    std::istringstream stream(sig_str);
    std::string token;

    while (stream >> token) {
        if (token == "?" || token == "??") {
            pat.bytes.push_back(0);
            pat.mask.push_back(false);
        } else {
            pat.bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
            pat.mask.push_back(true);
        }
    }

    return pat;
}

uintptr_t MemoryScanner::scan(uintptr_t start, size_t size, const Pattern& pattern) {
    if (pattern.bytes.empty()) return 0;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(start);
    const size_t pat_size = pattern.bytes.size();

    for (size_t i = 0; i <= size - pat_size; ++i) {
        bool found = true;
        for (size_t j = 0; j < pat_size; ++j) {
            if (pattern.mask[j] && data[i + j] != pattern.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return start + i;
        }
    }

    return 0;
}

uintptr_t MemoryScanner::scan_module(const Pattern& pattern) {
    HMODULE mod = GetModuleHandleA(nullptr);
    if (!mod) return 0;

    auto base = reinterpret_cast<uintptr_t>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    size_t size = nt->OptionalHeader.SizeOfImage;

    return scan(base, size, pattern);
}

uintptr_t MemoryScanner::scan_module(const std::string& sig_str) {
    return scan_module(parse_pattern(sig_str));
}

uintptr_t MemoryScanner::follow_rel32(uintptr_t instruction_addr, int offset) {
    int32_t rel = *reinterpret_cast<int32_t*>(instruction_addr + offset);
    return instruction_addr + offset + 4 + rel;
}

bool MemoryScanner::nop_bytes(uintptr_t addr, size_t count) {
    DWORD old_protect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), count, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;

    memset(reinterpret_cast<void*>(addr), 0x90, count);

    VirtualProtect(reinterpret_cast<void*>(addr), count, old_protect, &old_protect);
    return true;
}

bool MemoryScanner::write_protected(uintptr_t addr, const void* data, size_t size) {
    DWORD old_protect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;

    memcpy(reinterpret_cast<void*>(addr), data, size);

    VirtualProtect(reinterpret_cast<void*>(addr), size, old_protect, &old_protect);
    return true;
}

} // namespace cdcoop
