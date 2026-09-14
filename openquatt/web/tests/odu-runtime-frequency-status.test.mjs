import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" },
  setTimeout,
  clearTimeout,
  localStorage: { getItem: () => null },
};

const { getStatusPresentation } = await import("../js/src/features/odu-runtime-frequency.js");

const CASES = [
  // [firmware status, expected Dutch label, expected tone]
  ["Ready: load the current compressor frequency table from the ODU", "Laad eerst de actuele waarden", ""],
  ["Reading compressor frequency table from ODU", "Bezig met controleren", ""],
  ["Compressor frequency table loaded (11 levels)", "Waarden uit de buitenunit geladen", "success"],
  ["Compressor frequency table loaded (21 levels)", "Waarden uit de buitenunit geladen", "success"],
  ["Compressor frequency table writes enabled", "Wijzigingen vrijgegeven", ""],
  ["Compressor frequency table writes disabled", "Wijzigingen vergrendeld", ""],
  ["Checking whether ODU is safe to modify", "Bezig met controleren", ""],
  ["Write blocked: ODU is not in standby", "Wacht tot de buitenunit stilstaat", "warning"],
  ["Write blocked: compressor is running", "Wacht tot de buitenunit stilstaat", "warning"],
  ["Write blocked: ODU operating mode unknown", "Veilige toestand van de buitenunit kon niet worden vastgesteld", "warning"],
  ["Write blocked: compressor frequency unknown", "Veilige toestand van de buitenunit kon niet worden vastgesteld", "warning"],
  ["Write blocked: ODU safety check timed out", "Veilige toestand van de buitenunit kon niet worden vastgesteld", "warning"],
  ["Writing compressor frequency table to ODU", "Bezig met controleren", ""],
  ["Frequency table written; verifying readback", "Bezig met controleren", ""],
  ["Frequency table written and verified successfully", "De gekozen waarden zijn actief", "success"],
  ["Loading failed: ODU did not respond in time", "Toepassen kon niet worden bevestigd", "warning"],
  ["Loading failed: incomplete compressor frequency table received", "Toepassen kon niet worden bevestigd", "warning"],
  ["Loading failed: incomplete compressor frequency table extension received", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: ODU did not acknowledge the write in time", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: internal register mapping error", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: incomplete readback from ODU", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: incomplete extended readback from ODU", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: ODU values differ from requested table", "Toepassen kon niet worden bevestigd", "warning"],
  ["Verification failed: ODU disconnected during write", "Toepassen kon niet worden bevestigd", "warning"],
  ["", "Laad eerst de actuele waarden", ""],
];

test("elke odu-runtime-firmwarestatus krijgt de bedoelde Nederlandse uitleg", () => {
  for (const [status, label, tone] of CASES) {
    assert.deepEqual(getStatusPresentation(status), [label, tone], `status: ${status || "<leeg>"}`);
  }
});
