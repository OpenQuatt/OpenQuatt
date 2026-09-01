import assert from "node:assert/strict";
import { afterEach, test } from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { isDeviceTimeValid, parseDeviceClockMinutes } = await import("../js/src/core/entity-store.js");
const { state } = await import("../js/src/core/state.js");
const { formatDeviceClock, formatDiagnosticsDateTime } = await import("../js/src/features/device-context.js");
const { formatOverviewTrendPointTime } = await import("../js/src/views/overview.js");

const originalEntity = state.entities.timeNowHhmm;
const originalDraft = state.drafts.timeNowHhmm;

afterEach(() => {
  if (originalEntity === undefined) {
    delete state.entities.timeNowHhmm;
  } else {
    state.entities.timeNowHhmm = originalEntity;
  }
  if (originalDraft === undefined) {
    delete state.drafts.timeNowHhmm;
  } else {
    state.drafts.timeNowHhmm = originalDraft;
  }
});

test("device time validity accepts only a possible HH:MM value", () => {
  assert.equal(parseDeviceClockMinutes("00:00"), 0);
  assert.equal(parseDeviceClockMinutes("23:59"), 1439);
  for (const value of ["invalid", "", "24:00", "12:60", "unknown", "unavailable"]) {
    assert.equal(isDeviceTimeValid(value), false);
  }
});

test("device clock consumers fail closed on the firmware invalid sentinel", () => {
  state.entities.timeNowHhmm = { state: "invalid" };
  delete state.drafts.timeNowHhmm;

  assert.equal(formatDeviceClock(), "Geen tijdsync");
  assert.equal(formatDiagnosticsDateTime(), "Geen tijdsync");
  assert.deepEqual(formatOverviewTrendPointTime(0, 60000), {
    value: "1m geleden",
    note: "Geen tijdsync",
  });
});

test("device clock consumers use the published HH:MM state", () => {
  state.entities.timeNowHhmm = { state: "12:34" };
  delete state.drafts.timeNowHhmm;

  assert.equal(formatDeviceClock(), "12:34");
  assert.match(formatDiagnosticsDateTime(), / · 12:34$/);
  assert.deepEqual(formatOverviewTrendPointTime(0, 60000), {
    value: "12:33",
    note: "1m geleden",
  });
});
