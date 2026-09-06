import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { runInNewContext } from "node:vm";
import test from "node:test";

const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
const start = mockSource.indexOf("  function validateOduRuntimeTable(");
const end = mockSource.indexOf("  function handleButtonPress(", start);
assert.ok(start >= 0 && end > start, "ODU mock handlers must be present");

function createOduMock(mode, frequency) {
  const timers = [];
  const table = () => ({ cooling: [0, ...Array(10).fill(30)], heating: [0, ...Array(10).fill(35)] });
  const settings = () => ({
    loaded: true, busy: false, profileAvailable: false, autoReapply: false, status: "LOADED",
    actual: { mode: 1, startTemperatureC: 4, stopDeltaC: 3 },
  });
  const runtime = () => ({ loaded: true, armed: true, busy: false, extendedLayout: false, status: "LOADED" });
  const state = {
    installation: "duo",
    oduGenerations: { 1: "V1.5", 2: "V1.5" },
    oduSettingsService: { 1: settings(), 2: settings() },
    oduRuntimeFrequencyService: { 1: runtime(), 2: runtime() },
    oduRuntimeFrequency: { HP1: table(), HP2: table() },
  };
  const telemetry = { mode, frequency };
  const context = {
    state, URLSearchParams,
    window: { setTimeout: (callback) => timers.push(callback) },
    mockFixtures: { oduProfiles: { "V1.5": { variant: "V1.5" } } },
    getEntity: (_type, name) => ({ value: name.endsWith("Working Mode Label") ? telemetry.mode : telemetry.frequency }),
    mockResponse: (status, payload) => ({ status, payload: JSON.parse(JSON.stringify(payload)) }),
    notifyMockUpdated: () => {},
  };
  runInNewContext(mockSource.slice(start, end), context);
  return {
    state, telemetry,
    request(service, hp, action, values) {
      const url = new URL(`http://localhost/openquatt/${service}/hp${hp}/${action}`);
      const handler = service === "odu-settings" ? context.handleMockOduSettingsRequest : context.handleMockOduRuntimeRequest;
      return handler(url, action === "status" ? "GET" : "POST", { body: new URLSearchParams(values).toString() });
    },
    finish() { timers.splice(0).forEach((callback) => callback()); },
  };
}

test("bodemplaatmock past alle drie modi bij beide HP's toe tijdens bedrijf", () => {
  for (const [operation, frequency] of [["Standby", 0], ["Heating", 30], ["Cooling", 35], ["Defrost", 25]]) {
    for (const hp of [1, 2]) {
      for (const mode of [1, 2, 3]) {
        const mock = createOduMock(operation, frequency);
        const params = { csrf_token: "oq-mock-odu-settings", mode, start_temperature_c: 2, stop_delta_c: 5, auto_reapply: mode === 3 };
        const accepted = mock.request("odu-settings", hp, "save", params);
        assert.equal(accepted.status, 200);
        assert.equal(accepted.payload.busy, true);
        assert.equal(accepted.payload.actual.start_temperature_c, 4);
        assert.equal(mock.request("odu-settings", hp, "save", { ...params, start_temperature_c: 8 }).status, 409);
        mock.finish();
        const completed = mock.request("odu-settings", hp, "status").payload;
        assert.equal(completed.busy, false);
        assert.equal(completed.status, "IN_SYNC");
        assert.deepEqual(completed.actual, { mode, start_temperature_c: 2, stop_delta_c: 5 });
        assert.equal(completed.profile_available, true);
        assert.equal(completed.auto_reapply, mode === 3);
        assert.deepEqual(mock.telemetry, { mode: operation, frequency });
      }
    }
  }
});

test("frequentietabelmock blijft geblokkeerd tot standby met stilstaande compressor", () => {
  for (const [mode, frequency] of [["Heating", 30], ["Cooling", 35], ["Defrost", 25], ["Standby", 30]]) {
    const mock = createOduMock(mode, frequency);
    const values = { csrf_token: "oq-mock-odu-runtime", cooling: [0, ...Array(10).fill(40)].join(","), heating: [0, ...Array(10).fill(45)].join(",") };
    const initialTable = mock.state.oduRuntimeFrequency.HP1;
    assert.equal(mock.request("odu-runtime", 1, "apply", values).status, 200);
    mock.finish();
    assert.match(mock.request("odu-runtime", 1, "status").payload.status, /^BLOCKED:/);
    assert.equal(mock.state.oduRuntimeFrequency.HP1, initialTable);
    mock.telemetry.mode = "Standby";
    mock.telemetry.frequency = 0;
    assert.equal(mock.request("odu-runtime", 1, "apply", values).status, 200);
    mock.finish();
    assert.match(mock.request("odu-runtime", 1, "status").payload.status, /^APPLIED:/);
  }
});

test("bodemplaatmock behoudt CSRF- en Single-installatiecontrole", () => {
  const mock = createOduMock("Heating", 30);
  const result = mock.request("odu-settings", 1, "save", { csrf_token: "invalid", mode: 2 });
  assert.equal(result.status, 409);
  assert.equal(result.payload.error, "forbidden");
  assert.equal(mock.state.oduSettingsService[1].profileAvailable, false);
  mock.state.installation = "single";
  assert.equal(mock.request("odu-settings", 2, "save", { csrf_token: "oq-mock-odu-settings", mode: 2 }).status, 404);
});
