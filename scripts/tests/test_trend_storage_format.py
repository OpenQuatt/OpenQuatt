from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
POLICY_DIR = ROOT / "components" / "openquatt_trends"


class TrendStorageFormatTest(unittest.TestCase):
    def test_v1_requires_reset_and_only_v2_is_readable(self) -> None:
        compiler = shutil.which("g++-15") or shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")

        source = r"""
#include "OpenQuattTrendsStoragePolicy.h"
using namespace esphome::openquatt_trends;
constexpr uint32_t magic = 0x4F545247U;
static_assert(trend_block_format(magic, 1U, 12U, 12U * 22U) == TrendBlockFormat::LEGACY_V1);
static_assert(trend_block_format(magic, 1U, 1U, 22U) == TrendBlockFormat::LEGACY_V1);
static_assert(trend_block_format(magic, 2U, 12U, 12U * 26U) == TrendBlockFormat::CURRENT_V2);
static_assert(trend_block_format(magic, 2U, 1U, 26U) == TrendBlockFormat::CURRENT_V2);
static_assert(trend_block_format(magic, 1U, 12U, 12U * 26U) == TrendBlockFormat::INVALID);
static_assert(trend_block_format(magic, 2U, 12U, 12U * 22U) == TrendBlockFormat::INVALID);
static_assert(trend_block_format(magic, 1U, 0U, 0U) == TrendBlockFormat::INVALID);
static_assert(trend_block_format(magic, 2U, 13U, 13U * 26U) == TrendBlockFormat::INVALID);
static_assert(trend_block_format(0U, 1U, 12U, 12U * 22U) == TrendBlockFormat::INVALID);
static_assert(trend_archive_load_action(true, 0U, false) == TrendArchiveLoadAction::MARK_EMPTY_AS_SEEDED);
int main() { return 0; }
"""
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "test_trend_storage_format"
            command = [compiler, "-std=c++17", "-I", str(POLICY_DIR), "-x", "c++", "-", "-o", str(output)]
            compiled = subprocess.run(command, input=source, text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(output)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
