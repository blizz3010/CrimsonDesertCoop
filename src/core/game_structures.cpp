#include <cdcoop/core/game_structures.h>

#include <cmath>
#include <Windows.h>

namespace cdcoop {

RuntimeOffsets& get_runtime_offsets() {
    static RuntimeOffsets offsets;
    return offsets;
}

bool is_readable(uintptr_t addr, size_t size) {
    if (size == 0 || !is_valid_ptr(addr)) return false;
    // Reject wrap-around (addr + size overflowing past the address space).
    if (addr + size < addr) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi)) == 0)
        return false;
    if (!(mbi.State & MEM_COMMIT)) return false;

    // A guard page faults on first touch just like NOACCESS, so treat both as
    // unreadable. Then require one of the read-permitting protections — this
    // rejects PAGE_NOACCESS and execute-only pages that grant no read access.
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & kReadable)) return false;

    // The whole span must stay inside this one committed region; a read that
    // straddles into an adjacent unmapped region would still fault.
    uintptr_t region_end =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return addr + size <= region_end;
}

uintptr_t safe_read_ptr(uintptr_t addr) {
    if (!is_readable(addr, sizeof(uintptr_t))) return 0;
    return *reinterpret_cast<uintptr_t*>(addr);
}

uintptr_t resolve_ptr_chain(uintptr_t base, std::initializer_list<uint32_t> offsets) {
    uintptr_t addr = base;
    for (auto offset : offsets) {
        // Guard the read itself: is_valid_ptr only bounds the numeric range,
        // so a stale chain (e.g. after a game patch changed offsets) can point
        // a range-valid address at unmapped memory. Dereferencing it would
        // fault and take the whole game process down — the crash reported in
        // issue #49. safe_read_ptr returns 0 for any address that isn't backed
        // by committed, readable memory, so a broken chain degrades to a null
        // result instead of a crash.
        uintptr_t next = safe_read_ptr(addr + offset);
        if (!is_valid_ptr(next)) return 0;
        addr = next;
    }
    return addr;
}

uint32_t dynamic_scan_float(uintptr_t base, uint32_t min_off, uint32_t max_off,
                            uint32_t stride, float plausible_min, float plausible_max) {
    if (!is_valid_ptr(base) || stride == 0 || max_off <= min_off) return 0;

    // Verify the entire scan region lives in committed, readable memory
    // before we start dereferencing. dynamic_scan_float runs on a fresh
    // marker pointer (e.g. dragon mount HP resolution) whose exact
    // allocation size is unknown — scanning past the end of the struct
    // into an unmapped page would AV the game process. Scan ranges are
    // small (a few hundred bytes), so a single VirtualQuery covers the
    // typical case; if the range spans region boundaries we conservatively
    // bail instead of iterating (dragon HP resolution is non-critical).
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(base + min_off),
                     &mbi, sizeof(mbi)) == 0) {
        return 0;
    }
    if (!(mbi.State & MEM_COMMIT) || (mbi.Protect & PAGE_NOACCESS)) {
        return 0;
    }
    const uintptr_t region_end =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (region_end < base + max_off) {
        return 0; // scan would cross a region boundary; refuse
    }

    for (uint32_t off = min_off; off < max_off; off += stride) {
        float v = *reinterpret_cast<float*>(base + off);
        if (!std::isfinite(v)) continue;
        if (v < plausible_min || v > plausible_max) continue;
        return off;
    }
    return 0;
}

} // namespace cdcoop
