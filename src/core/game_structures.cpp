#include <cdcoop/core/game_structures.h>

namespace cdcoop {

uintptr_t resolve_ptr_chain(uintptr_t base, std::initializer_list<uint32_t> offsets) {
    uintptr_t addr = base;
    for (auto offset : offsets) {
        if (addr == 0) return 0;
        addr = *reinterpret_cast<uintptr_t*>(addr + offset);
    }
    return addr;
}

} // namespace cdcoop
