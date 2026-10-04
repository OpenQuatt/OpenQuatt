import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getBinaryEntityState, getSetupCompleteState, isEntityActive } = await import("../js/src/core/app-shared.js");
const initial = structuredClone(state);

test.beforeEach(() => {
  Object.assign(state, structuredClone(initial));
});

test("binary entity state accepts boolean, text and numeric representations", () => {
  for (const [expected, values] of [[true, [true, "ON", " true ", 1, "1"]], [false, [false, "OFF", " false ", 0, "0"]]]) {
    for (const value of values) {
      state.entities.cm100Active = { value };
      assert.equal(getBinaryEntityState("cm100Active"), expected);
      state.entities.cm100Active = { state: value };
      assert.equal(getBinaryEntityState("cm100Active"), expected);
    }
  }
});

test("binary entity state preserves unknown and missing data", () => {
  delete state.entities.cm100Active;
  assert.equal(getBinaryEntityState("cm100Active"), null);
  for (const value of [undefined, null, "", " ", "UNKNOWN", " unavailable ", "invalid", 2]) {
    state.entities.cm100Active = { value };
    assert.equal(getBinaryEntityState("cm100Active"), null);
  }
});

test("binary entity state prefers boolean values, then state over other values", () => {
  for (const value of [true, false]) {
    state.entities.cm100Active = { value, state: "UNKNOWN" };
    assert.equal(getBinaryEntityState("cm100Active"), value);
  }
  state.entities.cm100Active = { state: "OFF", value: "on" };
  assert.equal(getBinaryEntityState("cm100Active"), false);
  state.entities.cm100Active = { state: "UNKNOWN", value: "on" };
  assert.equal(getBinaryEntityState("cm100Active"), null);
  state.entities.cm100Active = { state: null, value: "on" };
  assert.equal(getBinaryEntityState("cm100Active"), true);
});

test("legacy boolean and setup helpers keep their existing parsing contracts", () => {
  state.entities.cm100Active = { value: " on " };
  assert.equal(isEntityActive("cm100Active"), false);
  assert.equal(getBinaryEntityState("cm100Active"), true);
  state.entities.cm100Active = { value: true, state: "UNKNOWN" };
  assert.equal(isEntityActive("cm100Active"), true);
  state.entities.setupComplete = { value: true, state: "UNKNOWN" };
  assert.equal(getSetupCompleteState(), null);
});
