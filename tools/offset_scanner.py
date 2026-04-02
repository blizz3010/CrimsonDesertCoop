#!/usr/bin/env python3
"""
Offset Scanner for Crimson Desert Co-op Mod

This script helps automate the discovery of game memory offsets.
It can be run standalone or integrated with Cheat Engine's Python scripting.

Usage:
    python offset_scanner.py --pid <process_id>
    python offset_scanner.py --auto  (auto-find Crimson Desert process)

Requirements:
    pip install pymem
"""

import sys
import struct
import time
import argparse
from typing import Optional

try:
    import pymem
    import pymem.process
except ImportError:
    print("Install pymem: pip install pymem")
    print("This is required for process memory access")
    sys.exit(1)


class CrimsonDesertScanner:
    PROCESS_NAME = "CrimsonDesert.exe"

    # Known signatures from community RE work (March 2026, v1.00.02 / v1.01.03)
    # Sources: CrimsonDesert-player-status-modifier, CrimsonDesertTools/EquipHide
    SIGNATURES = {
        # Player pointer capture (from player-status-modifier)
        "player_ptr_capture": "49 8B 7D 18 49 8B 44 24 40 48 8B 40 68 48 8B 70 20",
        "player_ptr_capture_fb": "49 8B 44 24 40 48 8B 40 68 48 8B 70 20",
        # Position height access (r13 = float* pos [X, Y, Z])
        "player_position_write": "49 3B F7 0F 8C ?? ?? ?? ?? 0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00",
        "player_position_write_fb": "0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00",
        # Stats access (stat entry array, entries 16 bytes, base+0x58)
        "stats_access": "48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24",
        "stats_access_fb": "48 C1 E0 04 48 03 46 58",
        # Stat write (shared by health/stamina/spirit)
        "stat_write": "48 2B 47 18 48 39 5F 18 48 0F 4F C2 48 89 47 20 48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38 66 89 6F 50",
        "stat_write_fb": "48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38",
        # Damage slot access (r15 = source, r12 = amount)
        "damage_slot": "49 8B 77 38 44 8B 24 88 48 8D 4C 24 ?? 4A 8B 1C E3",
        # Item gain
        "item_gain": "49 01 4C 38 10",
        # Durability write
        "durability_write": "66 3B CF 66 0F 4C F9 66 89 7B 50 48 8B 5C 24 20 48 8B 03 33 D2 48 8B CB FF 50 20",
        # WorldSystem singleton (RIP-relative, from EquipHide)
        "world_system": "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 8B 49 50 E8 ?? ?? ?? ?? 84 C0 0F 94 C0 48 83 C4 28 C3",
        "world_system_p2": "80 B8 49 01 00 00 00 75 ?? 48 8B 05 ?? ?? ?? ?? 48 8B 88 D8 00 00 00",
        # PartInOut transition (equipment visibility)
        "part_inout": "41 0F B6 45 1C 3C 03 74 ?? 45 84 C0 75 ?? 84 C0",
    }

    def __init__(self, pid: Optional[int] = None):
        if pid:
            self.pm = pymem.Pymem(pid)
        else:
            self.pm = pymem.Pymem(self.PROCESS_NAME)

        self.base_module = pymem.process.module_from_name(
            self.pm.process_handle, self.PROCESS_NAME
        )
        self.base_addr = self.base_module.lpBaseOfDll
        self.module_size = self.base_module.SizeOfImage

        print(f"Attached to {self.PROCESS_NAME}")
        print(f"  Base: 0x{self.base_addr:X}")
        print(f"  Size: 0x{self.module_size:X}")

    def scan_aob(self, pattern: str) -> list[int]:
        """Scan for an AOB pattern in the game module."""
        if pattern == "PLACEHOLDER":
            return []

        # Parse pattern
        tokens = pattern.split()
        sig_bytes = []
        mask = []
        for token in tokens:
            if token == "?" or token == "??":
                sig_bytes.append(0)
                mask.append(False)
            else:
                sig_bytes.append(int(token, 16))
                mask.append(True)

        # Read entire module
        data = self.pm.read_bytes(self.base_addr, self.module_size)

        results = []
        pat_len = len(sig_bytes)
        for i in range(len(data) - pat_len):
            found = True
            for j in range(pat_len):
                if mask[j] and data[i + j] != sig_bytes[j]:
                    found = False
                    break
            if found:
                results.append(self.base_addr + i)

        return results

    def find_float_value(self, value: float, tolerance: float = 0.01) -> list[int]:
        """Scan for a float value in game memory (for finding health, stamina, etc.)."""
        data = self.pm.read_bytes(self.base_addr, self.module_size)
        target = struct.pack("f", value)
        results = []

        for i in range(0, len(data) - 4, 4):
            candidate = struct.unpack("f", data[i : i + 4])[0]
            if abs(candidate - value) < tolerance:
                results.append(self.base_addr + i)

        return results

    def find_pointer_chain(self, start: int, offsets: list[int]) -> Optional[int]:
        """Follow a pointer chain and return the final address."""
        addr = start
        try:
            for offset in offsets:
                addr = self.pm.read_longlong(addr + offset)
                if addr == 0:
                    return None
            return addr
        except Exception:
            return None

    def scan_all_signatures(self):
        """Scan for all known signatures and report results."""
        print("\n=== Signature Scan Results ===\n")
        for name, sig in self.SIGNATURES.items():
            if sig == "PLACEHOLDER":
                print(f"  {name}: PLACEHOLDER (needs RE)")
                continue

            results = self.scan_aob(sig)
            if results:
                print(f"  {name}: FOUND at {', '.join(f'0x{r:X}' for r in results)}")
                if len(results) > 1:
                    print(f"    WARNING: {len(results)} matches - signature may not be unique")
            else:
                print(f"  {name}: NOT FOUND (signature may be outdated)")

    def dump_player_struct(self, player_addr: int, size: int = 0x200):
        """Dump raw bytes around a player entity for analysis."""
        data = self.pm.read_bytes(player_addr, size)
        print(f"\n=== Player Struct Dump at 0x{player_addr:X} ===\n")

        for offset in range(0, size, 16):
            hex_str = " ".join(f"{data[offset + i]:02X}" for i in range(min(16, size - offset)))
            ascii_str = "".join(
                chr(data[offset + i]) if 32 <= data[offset + i] < 127 else "."
                for i in range(min(16, size - offset))
            )
            print(f"  +0x{offset:04X}: {hex_str:<48} {ascii_str}")

    def generate_header(self, offsets: dict):
        """Generate a C++ header with discovered offsets."""
        print("\n=== Generated Offsets (paste into game_structures.h) ===\n")
        for category, values in offsets.items():
            print(f"namespace {category} {{")
            for name, offset in values.items():
                print(f"    constexpr uint32_t {name:20s} = 0x{offset:X};")
            print("}")
            print()


def main():
    parser = argparse.ArgumentParser(description="Crimson Desert Offset Scanner")
    parser.add_argument("--pid", type=int, help="Process ID to attach to")
    parser.add_argument("--auto", action="store_true", help="Auto-find game process")
    parser.add_argument("--scan-sigs", action="store_true", help="Scan all known signatures")
    parser.add_argument(
        "--find-float", type=float, help="Search for a float value (e.g., health)"
    )
    parser.add_argument(
        "--dump", type=lambda x: int(x, 0), help="Dump memory at address (hex)"
    )
    args = parser.parse_args()

    try:
        scanner = CrimsonDesertScanner(pid=args.pid)
    except Exception as e:
        print(f"Failed to attach: {e}")
        print("Make sure Crimson Desert is running and you have admin privileges")
        sys.exit(1)

    if args.scan_sigs:
        scanner.scan_all_signatures()

    if args.find_float is not None:
        results = scanner.find_float_value(args.find_float)
        print(f"\nFound {len(results)} matches for float {args.find_float}:")
        for addr in results[:20]:  # Show first 20
            print(f"  0x{addr:X}")
        if len(results) > 20:
            print(f"  ... and {len(results) - 20} more")

    if args.dump:
        scanner.dump_player_struct(args.dump)

    if not any([args.scan_sigs, args.find_float, args.dump]):
        scanner.scan_all_signatures()
        print("\nUse --help for more options")


if __name__ == "__main__":
    main()
