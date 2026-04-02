#include <cdcoop/core/game_structures.h>

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

} // namespace cdcoop
