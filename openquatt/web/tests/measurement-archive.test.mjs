import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, setTimeout, clearTimeout };

const { state } = await import("../js/src/core/state.js");
const { renderLearningView } = await import("../js/src/views/learning.js");
const { commitSwitch } = await import("../js/src/core/entity-write-actions.js");
const { assembleMeasurementArchive } = await import("../js/src/features/measurement-archive-export.js");
const { setLocale } = await import("../js/src/i18n/index.js");

test.beforeEach(() => {
  setLocale("nl", { persist: false });
  state.appView = "learning";
  state.learningPanel = "diagnostics";
  state.busyAction = state.controlNotice = state.controlError = "";
  state.loadingEntities = false;
  state.entities = { weatherEnabled: { value: false }, weatherLocation: { value: "" } };
});

test("reguliere Q-firmware toont geen opnamebediening of download", () => {
  assert.doesNotMatch(renderLearningView(), /data-oq-measurement-archive|measurement-archive\/download/);
});

test("opt-in blijft zichtbaar als opname uit staat en oudere blokken zijn te downloaden", () => {
  state.entities.measurementArchiveEnabled = { value: false };
  state.entities.measurementArchiveStatus = { value: "off|1794873600|1794960000|42" };
  const markup = renderLearningView();
  assert.match(markup, /Meetarchief/);
  assert.match(markup, /Opname uit/);
  assert.match(markup, /42 meetblokken/);
  assert.match(markup, /action="\/openquatt\/measurement-archive\/download"/);
  assert.doesNotMatch(markup, /type="submit" disabled/);
  assert.match(markup, /data-oq-action="download-measurement-archive" >Hele archief downloaden/);
  assert.match(markup, /<details class="oq-archive-day">/);
  state.learningPanel = "forecast";
  assert.doesNotMatch(renderLearningView(), /data-oq-measurement-archive/);
});

test("nieuwe opname toont wachtstatus en download is nog uitgeschakeld", () => {
  state.entities.measurementArchiveEnabled = { value: true };
  state.entities.measurementArchiveStatus = { value: "waiting_clock|0|0|0" };
  const markup = renderLearningView();
  assert.match(markup, /Wacht op een geldige klok/);
  assert.match(markup, /data-oq-archive-data hidden/);
  assert.match(markup, /type="submit" disabled/);
});

test("afgesloten archief biedt geen schakelaaractie of download", () => {
  state.entities.measurementArchiveEnabled = { value: false };
  state.entities.measurementArchiveStatus = { value: "archive_unlinked|0|0|0" };
  const markup = renderLearningView();
  assert.match(markup, /Oude meetopname afgesloten/);
  assert.match(markup, /data-control-key="measurementArchiveEnabled"[\s\S]+?disabled/);
  assert.match(markup, /data-oq-archive-data hidden/);
  assert.match(markup, /type="submit" disabled/);
});

test("de webapp meldt een opslagfout wanneer uitschakelen alleen in RAM lukte", async (t) => {
  state.entities.measurementArchiveEnabled = { value: true, state: "ON" };
  state.entities.measurementArchiveStatus = { value: "recording|0|0|0" };
  t.mock.method(globalThis, "fetch", async (url) => ({
    ok: true,
    json: async () => {
      if (url !== "/openquatt/entities") return {};
      return { missing: [], entities: {
        measurementArchiveEnabled: { value: true, state: "ON" },
        measurementArchiveStatus: { value: "preference_error|0|0|0" },
      } };
    },
  }));
  await commitSwitch("measurementArchiveEnabled", false);
  assert.equal(state.controlNotice, "");
  assert.match(state.controlError, /kon niet worden opgeslagen/);
});

const start = Date.UTC(2026, 4, 22) / 1000;
const daySeconds = 86400;
const isoDate = (epoch) => new Date(epoch * 1000).toISOString().slice(0, 10);

function dayPayload(epoch, measurements) {
  return {
    schema: 1,
    date_utc: isoDate(epoch),
    measurement_fields: ["end_epoch_s", "heat_context_id"],
    measurements,
    heat_contexts: [{ id: 1, route: "heat_pumps" }],
    weather_fields: ["fetched_epoch_s"],
    weather_hour_fields: ["outside_centi_c"],
    weather: [[epoch]],
  };
}

test("180 days become one file with every saved block and its heat route", async (t) => {
  const count = 180 * 288;
  const last = start + (count - 1) * 300;
  const requested = [];
  let active = 0;
  let maxActive = 0;
  t.mock.method(globalThis, "fetch", async (path) => {
    const date = new URL(path, "http://openquatt.test").searchParams.get("date");
    requested.push(date);
    const epoch = Date.parse(date) / 1000;
    active++;
    maxActive = Math.max(active, maxActive);
    return {
      ok: true,
      json: async () => {
        await Promise.resolve();
        active--;
        return dayPayload(epoch, Array.from({ length: 288 }, (_, i) => [epoch + i * 300, 1]));
      },
    };
  });
  const progress = [];
  const blob = await assembleMeasurementArchive({ first: start, last, count },
    new AbortController().signal, (done, total) => progress.push([done, total]));
  const file = JSON.parse(await blob.text());
  assert.equal(maxActive, 1);
  assert.equal(new Set(requested).size, 180);
  assert.equal(file.days.length, 180);
  assert.equal(file.days.flatMap((day) => day.measurements).length, 51840);
  assert.equal(file.days[179].measurements[287][0], last);
  assert.deepEqual(file.days[0].heat_contexts, [{ id: 1, route: "heat_pumps" }]);
  assert.deepEqual(progress.at(0), [0, 180]);
  assert.deepEqual(progress.at(-1), [180, 180]);
});

test("a gap does not invalidate a download, and new blocks stay outside the snapshot", async (t) => {
  const last = start + 2 * daySeconds;
  const days = [
    dayPayload(start, [[start, 1]]),
    dayPayload(start + daySeconds, []),
    dayPayload(last, [[last, 1], [last + 300, 1]]),
  ];
  t.mock.method(globalThis, "fetch", async () => ({ ok: true, json: async () => days.shift() }));
  const blob = await assembleMeasurementArchive({ first: start, last, count: 2 },
    new AbortController().signal, () => {});
  const file = JSON.parse(await blob.text());
  assert.deepEqual(file.days.map((day) => day.measurements), [[[start, 1]], [], [[last, 1]]]);
});

test("a missing block prevents saving a misleading complete download", async (t) => {
  t.mock.method(globalThis, "fetch", async () => ({
    ok: true,
    json: async () => dayPayload(start, [[start + 300, 1]]),
  }));
  await assert.rejects(assembleMeasurementArchive({ first: start, last: start + 300, count: 2 },
    new AbortController().signal, () => {}), /Archive measurement changed/);
});

test("cancel stops further requests and returns no file", async (t) => {
  const controller = new AbortController();
  const fetch = t.mock.method(globalThis, "fetch", async () => ({
    ok: true,
    json: async () => dayPayload(start, [[start, 1]]),
  }));
  await assert.rejects(assembleMeasurementArchive({ first: start, last: start + daySeconds, count: 2 },
    controller.signal, (done) => { if (done === 1) controller.abort(); }), { name: "AbortError" });
  assert.equal(fetch.mock.callCount(), 1);
});

test("archive busy is retried after a transient 503", async (t) => {
  let calls = 0;
  t.mock.method(globalThis, "fetch", async () => ++calls === 1 ? { ok: false, status: 503 } : {
    ok: true,
    json: async () => dayPayload(start, [[start, 1]]),
  });
  await assembleMeasurementArchive({ first: start, last: start, count: 1 }, new AbortController().signal, () => {});
  assert.equal(calls, 2);
});
