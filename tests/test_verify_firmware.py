#!/usr/bin/env python3
"""Host tests for the mini-program firmware compatibility parser."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_firmware", ROOT / "tools" / "verify_firmware.py"
)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFY
SPEC.loader.exec_module(VERIFY)


def sample_table() -> bytes:
    entries = (
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 1, 0xF000, 0x1000, "phy_init"),
        (0, 0, 0x10000, 0x300000, "factory"),
        (1, 2, 0x356000, 0x4000, "cardid"),
        (0, 0x20, 0x700000, 0x100000, "recovery"),
    )
    raw = bytearray(b"\xff" * VERIFY.PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        VERIFY.ENTRY.pack_into(
            raw,
            index * VERIFY.ENTRY.size,
            0x50AA,
            kind,
            subtype,
            offset,
            size,
            label.encode().ljust(16, b"\0"),
            0,
        )
    marker = len(entries) * VERIFY.ENTRY.size
    struct.pack_into("<H", raw, marker, 0xEBEB)
    raw[marker + 16 : marker + 32] = hashlib.md5(raw[:marker]).digest()
    return bytes(raw)


class PartitionParserTest(unittest.TestCase):
    def test_parses_protected_layout_and_md5(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(sample_table())
        self.assertTrue(found_md5)
        self.assertEqual(partitions[-2].label, "cardid")
        self.assertEqual(partitions[-1].offset, VERIFY.RECOVERY_OFFSET)

    def test_rejects_bad_md5(self) -> None:
        raw = bytearray(sample_table())
        raw[28] ^= 1
        with self.assertRaisesRegex(ValueError, "MD5"):
            VERIFY.parse_partition_table(bytes(raw))


class RecoveryHookTest(unittest.TestCase):
    def check_symbols(self, symbols: str) -> None:
        with patch.object(VERIFY.subprocess, "run", return_value=CompletedProcess(
                args=[], returncode=0, stdout=symbols, stderr="")):
            VERIFY.verify_recovery_hook(Path("bootloader.elf"))

    def test_accepts_strong_release_hook_without_log_marker(self) -> None:
        self.check_symbols("40380000 T bootloader_after_init\n"
                           "40380100 T bootloader_hooks_include\n")

    def test_rejects_default_weak_hook(self) -> None:
        with self.assertRaisesRegex(ValueError, "Recovery hook"):
            self.check_symbols("40380000 W bootloader_after_init\n"
                               "40380100 T bootloader_hooks_include\n")

    def test_rejects_missing_component(self) -> None:
        with self.assertRaisesRegex(ValueError, "Recovery hook"):
            self.check_symbols("40380000 T bootloader_after_init\n")

    def test_rejects_symbol_tool_failure(self) -> None:
        with patch.object(VERIFY.subprocess, "run", return_value=CompletedProcess(
                args=[], returncode=1, stdout="", stderr="invalid ELF")):
            with self.assertRaisesRegex(ValueError, "cannot inspect"):
                VERIFY.verify_recovery_hook(Path("bootloader.elf"))


if __name__ == "__main__":
    unittest.main()
