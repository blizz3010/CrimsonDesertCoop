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

    # Known signatures (PLACEHOLDERS - update from RE work)
    SIGNATURES = {
        "player_health_write": "PLACEHOLDER",
        "player_position_write": "PLACEHOLDER",
        "companion_spawn": "PLACEHOLDER",
        "damage_calc": "PLACEHOLDER",
        "game_tick": "PLACEHOLDER",
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
