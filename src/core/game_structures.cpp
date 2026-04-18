#include <cdcoop/core/game_structures.h>

#include <cmath>

namespace cdcoop {

RuntimeOffsets& get_runtime_offsets() {
    static RuntimeOffsets offsets;
    return offsets;
}

uintptr_t resolve_ptr_chain(uintptr_t base, std::initializer_list<uint32_t> offsets) {
    uintptr_t addr = base;
    for (auto offset : offsets) {
        if (addr == 0) return 0;
        auto next = *reinterpret_cast<uintptr_t*>(addr + offset);
        if (!is_valid_ptr(next)) return 0;
        addr = next;
    }
    return addr;
}

uint32_t dynamic_scan_float(uintptr_t base, uint32_t min_off, uint32_t max_off,
                            uint32_t stride, float plausible_min, float plausible_max) {
    if (!is_valid_ptr(base) || stride == 0 || max_off <= min_off) return 0;
    for (uint32_t off = min_off; off < max_off; off += stride) {
        float v = *reinterpret_cast<float*>(base + off);
        if (!std::isfinite(v)) continue;
        if (v < plausible_min || v > plausible_max) continue;
        return off;
    }
    return 0;
}

} // namespace cdcoop
