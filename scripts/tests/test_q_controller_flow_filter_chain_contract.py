from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
Q_PROFILE = (
    ROOT / "openquatt" / "profiles" / "heatpump_controller_q.yaml"
).read_text()


def controller_block(source: str) -> str:
    start = source.index("id: flow_rate_controller")
    end = source.find("\ninterval:", start)
    return source[start:end] if end >= 0 else source[start:]


def effective_value_after_timeout(
    published: list[tuple[float, float]],
    timeout_s: float,
    fallback: float,
    until_s: float,
) -> float:
    """Modelleer ESPHome `timeout` ná een upstream-filter.

    `published` zijn (tijdstip, waarde)-paren die de upstream-filter
    (hier `throttle_average`) heeft gepubliceerd. Bij stilte langer dan
    `timeout_s` publiceert het timeout-filter eenmalig `fallback` en
    blijft die waarde daarna staan tot er weer upstream-input komt.
    """
    last_t, last_v = published[-1]
    if until_s - last_t > timeout_s:
        return fallback
    return last_v


class QControllerFlowFilterChainContractTest(unittest.TestCase):
    def test_pulse_meter_keeps_its_own_short_timeout(self) -> None:
        block = controller_block(Q_PROFILE)
        self.assertIn("timeout: 5s", block)

    def test_averaged_output_fails_to_zero_when_quiet(self) -> None:
        block = controller_block(Q_PROFILE)
        self.assertIn("- throttle_average: 10s", block)
        self.assertIn("- timeout:", block)
        self.assertIn("timeout: 15s", block)
        # Fail-waarde is expliciet nul (statisch of via lambda).
        self.assertTrue(
            "return 0" in block or "value: 0" in block,
            "timeout-filter na throttle_average moet naar 0 L/h gaan",
        )
        # Het timeout-filter moet ná throttle_average komen, dus op de
        # gemiddelde output werken en niet op de ruwe pulsen.
        self.assertLess(
            block.index("- throttle_average: 10s"),
            block.index("- timeout:"),
        )

    def test_stale_average_kan_niet_blijven_staan(self) -> None:
        # Reproduceert #648: steady flow, daarna pulse-timeout-nul die met
        # oudere waarden wordt gemiddeld tot bv. 480 L/h, daarna stilte.
        published = [(0.0, 800.0), (10.0, 480.0)]

        # Zonder trailing timeout blijft de verouderde niet-nulwaarde staan.
        self.assertEqual(published[-1][1], 480.0)

        # Met trailing timeout (15 s) gaat de effectieve flow begrensd naar 0.
        self.assertEqual(
            effective_value_after_timeout(published, 15.0, 0.0, 24.0), 480.0
        )
        self.assertEqual(
            effective_value_after_timeout(published, 15.0, 0.0, 25.1), 0.0
        )
        self.assertEqual(
            effective_value_after_timeout(published, 15.0, 0.0, 100.0), 0.0
        )


if __name__ == "__main__":
    unittest.main()
