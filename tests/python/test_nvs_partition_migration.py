"""Run the production migration engine and verify its IDF partition-table bytes."""

import csv
import hashlib
import io
from pathlib import Path
import re
import struct
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
TABLE_SIZE = 0xC00
# Released Q layout: compatibility must not drift with the current build CSV.
LEGACY_CSV = """nvs,data,nvs,0x9000,0x6000,
otadata,data,ota,0xF000,0x2000,
phy_init,data,phy,0x11000,0x1000,
app0,app,ota_0,0x20000,0x500000,
app1,app,ota_1,0x520000,0x500000,
openquatt_data,data,0x40,0xA20000,0x5E0000,
"""


def partition_rows(text: str) -> list[list[str]]:
    return [
        [field.strip() for field in row]
        for row in csv.reader(io.StringIO(text))
        if row and not row[0].lstrip().startswith("#")
    ]


def encode_idf_table(rows: list[list[str]]) -> bytes:
    """ESP-IDF's 32-byte entries, MD5 trailer, then erased 0xC00-byte padding."""
    types = {"app": 0, "data": 1}
    subtypes = {"ota": 0, "phy": 1, "nvs": 2, "ota_0": 0x10, "ota_1": 0x11}
    entries = bytearray()
    for name, kind, subtype, offset, size, flags in rows:
        kind_value = types[kind] if kind in types else int(kind, 0)
        subtype_value = subtypes[subtype] if subtype in subtypes else int(subtype, 0)
        if flags not in ("", "encrypted"):
            raise ValueError(f"Unsupported partition flags: {flags}")
        entries.extend(struct.pack(
            "<HBBII16sI", 0x50AA, kind_value, subtype_value,
            int(offset, 0), int(size, 0), name.encode("ascii"), int(flags == "encrypted"),
        ))
    digest = hashlib.md5(entries).digest()
    entries.extend(b"\xEB\xEB" + b"\xFF" * 14 + digest)
    if len(entries) > TABLE_SIZE:
        raise ValueError("Partition entries exceed the ESP-IDF table size")
    return bytes(entries).ljust(TABLE_SIZE, b"\xFF")


class NvsPartitionMigrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler_args = compiler_command("-O2", "-I", str(ROOT))
        cls.directory = tempfile.TemporaryDirectory(prefix="openquatt-nvs-migration-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.temp = Path(cls.directory.name)
        # Export the C++ values rather than parsing formatting-dependent hex literals.
        helper = cls.temp / "tables.cpp"
        helper.write_text('''#include <cstddef>
#include <cstdio>
#include "components/openquatt_partition_migration/partition_tables.h"
int main() {
  using namespace esphome::openquatt_partition_migration;
  for (const auto* table : {&OLD_TABLE, &NEW_TABLE}) {
    if (std::fwrite(table->data(), 1, table->size(), stdout) != table->size()) return 1;
  }
}
''')
        binary = cls.temp / "tables"
        compiled = run_process(
            [*cls.compiler_args, str(helper), "-o", str(binary)],
            timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        exported = run_process([str(binary)], capture_output=True, check=True, timeout=15).stdout
        if len(exported) != 2 * TABLE_SIZE:
            raise AssertionError("Unexpected embedded partition-table length")
        cls.old_table, cls.new_table = exported[:TABLE_SIZE], exported[TABLE_SIZE:]

    def test_current_csv_matches_embedded_new_table_exactly(self) -> None:
        rows = partition_rows((ROOT / "openquatt/partitions/openquatt_16mb.csv").read_text())
        self.assertEqual(len(rows), 6)
        self.assertEqual(self.new_table, encode_idf_table(rows))
        self.assertEqual(rows[0], ["nvs", "data", "nvs", "0x9000", "0x10000", ""])
        # OTA slots and user data keep their original locations and capacities.
        self.assertEqual(rows[3:], partition_rows(LEGACY_CSV)[3:])

    def test_released_24k_layout_and_prefix_remain_compatible(self) -> None:
        self.assertEqual(self.old_table, encode_idf_table(partition_rows(LEGACY_CSV)))
        old_entries = [struct.unpack_from("<HBBII16sI", self.old_table, index * 32) for index in range(6)]
        new_entries = [struct.unpack_from("<HBBII16sI", self.new_table, index * 32) for index in range(6)]
        self.assertEqual(old_entries[0][3:5], (0x9000, 0x6000))
        self.assertEqual(new_entries[0][3:5], (0x9000, 0x10000))
        self.assertEqual(new_entries[1][3:5], (0x19000, 0x2000))
        self.assertEqual(new_entries[2][3:5], (0x1B000, 0x1000))
        self.assertEqual(old_entries[3:], new_entries[3:])
        self.assertEqual(new_entries[-1][3] + new_entries[-1][4], 16 * 1024 * 1024)

    def test_mutation_cuts_restarts_and_failed_flash_requests(self) -> None:
        fixture = ROOT / "tests/fixtures/nvs_partition_migration.cpp"
        binary = self.temp / "migration"
        compiled = run_process(
            [*self.compiler_args, str(fixture), "-o", str(binary)],
            timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        executed = run_process([str(binary)], capture_output=True, text=True, timeout=60)
        self.assertEqual(executed.returncode, 0, executed.stderr)
        summary = re.search(
            r"PASS: both slots, 6 checkpoint resets, replay/no-op and (\d+) mutation cuts "
            r"\((\d+) resumed, (\d+) fail-closed USB boundaries\)", executed.stdout,
        )
        self.assertIsNotNone(summary, executed.stdout)
        cuts, recoveries, usb_boundaries = map(int, summary.groups())
        self.assertGreaterEqual(cuts, 310)
        self.assertGreater(recoveries, 0)
        self.assertGreater(usb_boundaries, 0)
        self.assertEqual(recoveries + usb_boundaries, cuts)


if __name__ == "__main__":
    unittest.main()
