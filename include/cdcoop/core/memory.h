#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace cdcoop {

// Pattern scanning utilities for finding game functions and data
class MemoryScanner {
public:
    // Parse an IDA-style signature string like "48 89 5C 24 ? 57 48 83 EC 20"
    // '?' represents wildcard bytes
    struct Pattern {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask; // true = must match, false = wildcard
    };

    static Pattern parse_pattern(const std::string& sig_str);

    // Scan a memory region for a pattern
    static uintptr_t scan(uintptr_t start, size_t size, const Pattern& pattern);

    // Scan the main game module
    static uintptr_t scan_module(const Pattern& pattern);
    static uintptr_t scan_module(const std::string& sig_str);

    // Read a relative call/jump target (for following call instructions)
    static uintptr_t follow_rel32(uintptr_t instruction_addr, int offset = 1);

    // NOP a range of bytes (for disabling game code)
    static bool nop_bytes(uintptr_t addr, size_t count);

    // Write protected memory
    static bool write_protected(uintptr_t addr, const void* data, size_t size);
};

} // namespace cdcoop
