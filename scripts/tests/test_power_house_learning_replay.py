import csv
import json
import math
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "scripts" / "power_house_learning_replay.cpp"
HEADER = (
    "monotonic_ms,epoch_s,source_generation,physical_context_generation,"
    "control_generation,invalid_reasons,room_c,setpoint_c,outside_c,"
    "heat_to_water_w,heat_uncertainty_w,mean_water_c"
)


def write_csv(path, rows, header=HEADER):
    with path.open("w", newline="") as stream:
        stream.write(header + "\n")
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerows(rows)


def replay(binary, csv_path, now_epoch=2_000_000_000, active_h="150", active_t0="16", extra_args=()):
    return subprocess.run(
        [str(binary), str(csv_path), "--active-h", str(active_h), "--active-t0", str(active_t0), "--reference-room-c", "20",
         "--reference-setpoint-c", "20", "--now-epoch", str(now_epoch), *extra_args],
        capture_output=True,
        text=True,
        check=False,
    )


def sufficient_rows():
    rows = []
    monotonic = 1_000
    epoch = 1_700_000_000
    temperatures = (-6, -3, 0, 3, 6, 9, 12, 15, 10, 5, 1, -4)
    for segment in range(54):
        outside = float(temperatures[segment % len(temperatures)])
        heat = 200.0 * (18.0 - outside)
        for sample in range(241):
            rows.append(
                [
                    monotonic,
                    epoch,
                    1,
                    1,
                    1,
                    0,
                    20.0,
                    20.0,
                    outside,
                    heat,
                    10.0,
                    35.0,
                ]
            )
            monotonic += 60_000
            epoch += 60
        # Keep the next segment coherent while allowing the previous one to close.
    return rows, epoch + 1


def dynamic_and_stationary_rows():
    """An exact U=200, C=6000 house: excitation followed by stable heating days."""
    start_epoch = 20000 * 86400
    for minute in range(11 * 24 * 60 + 1):
        hours = minute / 60.0
        if hours < 48:
            room = 20.0 + 0.25 * math.sin(2 * math.pi * hours / 6.0)
            room_rate = 0.25 * (2 * math.pi / 6.0) * math.cos(2 * math.pi * hours / 6.0)
            outside = 4.0 + 9.0 * math.sin(2 * math.pi * hours / 30.0)
        else:
            room, room_rate = 20.0, 0.0
            outside = -5.0 + 2.0 * min(8, (minute - 48 * 60) // (24 * 60))
        heat = 200.0 * (room - outside) + 6000.0 * room_rate
        yield [1000 + minute * 60000, start_epoch + minute * 60, 1, 1, 1, 0,
               room, 20.0, outside, heat, 10.0, 35.0]


class LearningReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.tempdir.name) / "power_house_learning_replay"
        compiler = os.environ.get("CXX", "c++")
        result = subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I.", "-Iopenquatt", str(SOURCE), "-o", str(cls.binary)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def test_replay_uses_cpp_aggregator_and_fit(self):
        rows, now_epoch = sufficient_rows()
        path = pathlib.Path(self.tempdir.name) / "valid.csv"
        write_csv(path, rows)
        result = replay(self.binary, path, now_epoch)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(result.stdout)
        self.assertEqual(output["batch_status"], "advice_ready")
        self.assertTrue(output["batch_advice_ready"])
        self.assertFalse(output["advice_ready"])
        self.assertFalse(output["auto_apply_allowed"])
        self.assertFalse(output["rls_ready"])
        self.assertIsNone(output["rls_unmodeled_gain_bound_w"])
        self.assertGreaterEqual(output["accepted_windows"], 40)
        self.assertEqual(output["rejected_observations"], 9)
        self.assertAlmostEqual(output["candidate_h"], 200.0, delta=1.0)
        self.assertAlmostEqual(output["candidate_t0"], 18.0, delta=0.2)
        self.assertLess(output["holdout_candidate_mae_w"], output["holdout_active_mae_w"])
        self.assertAlmostEqual(output["holdout_mean_uncertainty_w"], 10.0, delta=0.1)

    def test_implausible_active_line_cannot_produce_advice(self):
        rows, now_epoch = sufficient_rows()
        path = pathlib.Path(self.tempdir.name) / "bad-active.csv"
        write_csv(path, rows)
        result = replay(self.binary, path, now_epoch, active_h="1e-20")
        self.assertEqual(result.returncode, 0)
        output = json.loads(result.stdout)
        self.assertEqual(output["status"], "invalid_active_model")
        self.assertFalse(output["advice_ready"])

    def test_dynamic_and_batch_models_validate_without_control_authority(self):
        path = pathlib.Path(self.tempdir.name) / "both-models.csv"
        write_csv(path, dynamic_and_stationary_rows())
        now_epoch = 20011 * 86400 + 1
        result = replay(self.binary, path, now_epoch, extra_args=("--rls-max-unmodeled-gain-w", "0"))
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(result.stdout)
        self.assertTrue(output["batch_advice_ready"], output)
        self.assertTrue(output["rls_ready"], output)
        self.assertTrue(output["advice_ready"], output)
        self.assertEqual(output["status"], "models_consistent")
        self.assertAlmostEqual(output["candidate_h"], 200.0, delta=1.0)
        self.assertAlmostEqual(output["candidate_t0"], 20.0, delta=0.1)
        self.assertAlmostEqual(output["u_rls"], 200.0, delta=3.0)
        self.assertAlmostEqual(output["c_rls_wh_per_k"], 6000.0, delta=200.0)
        self.assertFalse(output["auto_apply_allowed"])

        # The same numbers cannot prove unmeasured solar/internal gains absent.
        unbounded = json.loads(replay(self.binary, path, now_epoch).stdout)
        self.assertFalse(unbounded["advice_ready"])
        self.assertTrue(unbounded["batch_advice_ready"])
        self.assertIsNone(unbounded["rls_unmodeled_gain_bound_w"])

        # Evidence also expires when analysis is performed long after the samples.
        stale = json.loads(replay(self.binary, path, now_epoch + 86400,
                                  extra_args=("--rls-max-unmodeled-gain-w", "0")).stdout)
        self.assertFalse(stale["advice_ready"])
        self.assertFalse(stale["auto_apply_allowed"])

    def test_header_extra_cell_and_missing_value_are_input_errors(self):
        path = pathlib.Path(self.tempdir.name) / "bad.csv"
        write_csv(path, [[1, 1, 1, 1, 1, 0, 20, 20, 5, 2600, 10, 35, 0]])
        result = replay(self.binary, path)
        self.assertEqual(result.returncode, 2)
        self.assertGreater(json.loads(result.stdout)["malformed_rows"], 0)

        path.write_text(HEADER + "\n1,1,1,1,1,0,20,20,5,2600,10\n")
        result = replay(self.binary, path)
        self.assertEqual(result.returncode, 2)

    def test_empty_and_temporal_rejections_never_report_success(self):
        empty = pathlib.Path(self.tempdir.name) / "empty.csv"
        empty.write_text(HEADER + "\n")
        result = replay(self.binary, empty)
        self.assertEqual(result.returncode, 2)

        rows = [
            [1, 1, 1, 1, 1, 0, 20, 20, 5, 2600, 10, 35],
            [2, 2_000_000_001, 1, 1, 1, 0, 20, 20, 5, 2600, 10, 35],
            [1, 2, 1, 1, 1, 0, 20, 20, 5, 2600, 10, 35],
        ]
        path = pathlib.Path(self.tempdir.name) / "temporal.csv"
        write_csv(path, rows)
        result = replay(self.binary, path, 2_000_000_000)
        self.assertEqual(result.returncode, 2)
        output = json.loads(result.stdout)
        self.assertFalse(output["advice_ready"])
        self.assertEqual(output["rejected_observations"], 1)

    def test_missing_generation_is_input_error(self):
        path = pathlib.Path(self.tempdir.name) / "missing-generation.csv"
        write_csv(path, [[1, 1_700_000_000, 0, 1, 1, 0, 20, 20, 5, 2600, 10, 35]])
        result = replay(self.binary, path)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(json.loads(result.stdout)["status"], "input_error")

    def test_new_source_discards_old_cohort_before_fit(self):
        rows, now_epoch = sufficient_rows()
        last = rows[-1]
        rows.append([last[0] + 60_000, last[1] + 60, 2, 1, 1, 0, 20, 20, 5, 2600, 10, 35])
        path = pathlib.Path(self.tempdir.name) / "new-source.csv"
        write_csv(path, rows)
        result = replay(self.binary, path, now_epoch + 60)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(result.stdout)
        self.assertFalse(output["advice_ready"])
        self.assertEqual(output["cohort_changes"], 1)
        self.assertGreater(output["discarded_cohort_records"], 0)
        self.assertEqual(output["rls_accepted_samples"], 0)
        self.assertIsNone(output["u_rls"])

    def test_embedded_nul_row_is_input_error(self):
        path = pathlib.Path(self.tempdir.name) / "nul.csv"
        path.write_bytes((HEADER + "\n1,1700000000,1,1,1,0,20,20,5,2600,10,35\n").encode() + b"2,1700000060,1,1,1,0,20,20,5,2600,10,35\x00,extra\n")
        result = replay(self.binary, path)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(json.loads(result.stdout)["status"], "input_error")

    def test_generation_reuse_or_decrease_cannot_restore_an_old_cohort(self):
        for generation_column in (2, 3, 4):
            with self.subTest(generation_column=generation_column):
                rows = [
                    [1000, 1700000000, 1, 1, 1, 0, 20, 20, 5, 3000, 10, 35],
                    [61000, 1700000060, 1, 1, 1, 0, 20, 20, 5, 3000, 10, 35],
                    [121000, 1700000120, 1, 1, 1, 0, 20, 20, 5, 3000, 10, 35],
                ]
                rows[1][generation_column] = 2
                path = pathlib.Path(self.tempdir.name) / "late-context.csv"
                write_csv(path, rows)
                result = replay(self.binary, path)
                self.assertEqual(result.returncode, 2, result.stderr)
                output = json.loads(result.stdout)
                self.assertEqual(output["status"], "input_error")
                self.assertFalse(output["advice_ready"])
                self.assertFalse(output["auto_apply_allowed"])


if __name__ == "__main__":
    unittest.main()
