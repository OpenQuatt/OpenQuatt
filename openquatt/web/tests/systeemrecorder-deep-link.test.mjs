import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { href: "http://openquatt.local/", pathname: "/" },
  history: {},
  localStorage: { getItem: () => null, setItem: () => {} },
};

const {
  SYSTEM_RECORDER_MODAL_ID,
  getUrlSystemModal,
  normalizeSystemModalToken,
  syncUrlAppView,
} = await import("../js/src/core/navigation.js");
const { state } = await import("../js/src/core/state.js");

function setHref(href) {
  window.location.href = href;
  try {
    const url = new URL(href);
    window.location.pathname = url.pathname;
    window.location.search = url.search;
    window.location.hash = url.hash;
  } catch (_error) {
    // Keep raw href for error-path coverage.
  }
}

test("systeemrecorder-tokens normaliseren naar de recorder-modal", () => {
  assert.equal(normalizeSystemModalToken("systeemrecorder"), SYSTEM_RECORDER_MODAL_ID);
  assert.equal(normalizeSystemModalToken("SysteemRecorder"), SYSTEM_RECORDER_MODAL_ID);
  assert.equal(normalizeSystemModalToken("debug-recording"), SYSTEM_RECORDER_MODAL_ID);
  assert.equal(normalizeSystemModalToken("system-recorder"), SYSTEM_RECORDER_MODAL_ID);
  assert.equal(normalizeSystemModalToken(""), "");
  assert.equal(normalizeSystemModalToken("settings"), "");
});

test("deep link via query herkent de recorder-modal", () => {
  setHref("http://openquatt.local/?view=settings&section=system&modal=systeemrecorder");
  assert.equal(getUrlSystemModal(), SYSTEM_RECORDER_MODAL_ID);

  setHref("http://openquatt.local/?modal=debug-recording");
  assert.equal(getUrlSystemModal(), SYSTEM_RECORDER_MODAL_ID);

  setHref("http://openquatt.local/?systeemrecorder");
  assert.equal(getUrlSystemModal(), SYSTEM_RECORDER_MODAL_ID);

  setHref("http://openquatt.local/?view=overview");
  assert.equal(getUrlSystemModal(), "");
});

test("deep link via hash herkent de recorder-modal", () => {
  setHref("http://openquatt.local/#systeemrecorder");
  assert.equal(getUrlSystemModal(), SYSTEM_RECORDER_MODAL_ID);

  setHref("http://openquatt.local/#debug-recording");
  assert.equal(getUrlSystemModal(), SYSTEM_RECORDER_MODAL_ID);

  setHref("http://openquatt.local/#overview");
  assert.equal(getUrlSystemModal(), "");
});

test("open recorder-modal zet een deelbare modal-param, sluiten ruimt op", () => {
  let replaced = "";
  window.history.replaceState = (_data, _title, url) => { replaced = String(url); };
  window.history.pushState = (_data, _title, url) => { replaced = String(url); };

  setHref("http://openquatt.local/?view=settings&section=system");
  state.appView = "settings";
  state.settingsGroup = "system";
  state.systemModal = SYSTEM_RECORDER_MODAL_ID;
  syncUrlAppView("replace");
  assert.match(replaced, /modal=systeemrecorder/);
  assert.match(replaced, /view=settings/);
  assert.match(replaced, /section=system/);

  setHref(replaced);
  state.systemModal = "";
  syncUrlAppView("replace");
  assert.doesNotMatch(replaced, /modal=systeemrecorder/);
  assert.doesNotMatch(replaced, /systeemrecorder/);
});
