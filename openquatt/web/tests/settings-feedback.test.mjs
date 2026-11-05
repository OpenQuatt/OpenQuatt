import assert from "node:assert/strict";
import test, { mock } from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
  setTimeout: (...args) => globalThis.setTimeout(...args),
  clearTimeout: (...args) => globalThis.clearTimeout(...args),
};

const { getStoredFeedbackPosition, state } = await import("../js/src/core/state.js");
const { dismissSettingsFeedback, renderSettingsFeedback, syncSettingsFeedback } = await import("../js/src/features/settings-feedback.js");

const NOTICE = "Minimale koel-aanvoer bijgewerkt.";

function createRoot({ modal = false } = {}) {
  const toast = { classes: [], removed: false, classList: { add(name) { toast.classes.push(name); } }, remove() { toast.removed = true; } };
  return {
    toast,
    querySelector(selector) {
      if (selector === "[data-oq-modal]") return modal ? {} : null;
      return selector === ".oq-settings-feedback" ? toast : null;
    },
  };
}

// Mirrors the shell: render the markup, then sync the presentation.
function renderAndSync(root) {
  const markup = renderSettingsFeedback();
  syncSettingsFeedback(root);
  return markup;
}

function reset() {
  Object.assign(state, { appView: "settings", controlError: "", controlNotice: "" });
  syncSettingsFeedback(createRoot());
}

test("errors take precedence over notices and can be dismissed", () => {
  reset();
  Object.assign(state, { controlError: "Opslaan mislukt", controlNotice: "Opgeslagen" });
  const markup = renderAndSync(createRoot());
  assert.match(markup, /role="alert"/);
  assert.match(markup, /data-oq-action="dismiss-settings-feedback"/);
  assert.doesNotMatch(markup, /Opgeslagen/);
  dismissSettingsFeedback();
  assert.equal(state.controlError, "");
  assert.equal(state.controlNotice, "");
  assert.equal(renderAndSync(createRoot()), "");
});

test("a confirmation enters once, settles on later renders and leaves after four seconds", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    const root = createRoot();
    state.controlNotice = NOTICE;
    assert.doesNotMatch(renderAndSync(root), /is-settled/);
    assert.match(renderAndSync(root), /is-settled/);
    assert.match(renderAndSync(root), /is-settled/);
    mock.timers.tick(3999);
    assert.match(renderSettingsFeedback(), /is-settled/);
    mock.timers.tick(1);
    assert.deepEqual(root.toast.classes, ["is-leaving"]);
    mock.timers.tick(200);
    assert.equal(root.toast.removed, true);
    assert.equal(renderAndSync(root), "");
    assert.equal(state.controlNotice, NOTICE, "other screens keep reading the notice");
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("leaving the settings page finishes the message, and a new change shows again", () => {
  reset();
  const root = createRoot();
  state.controlNotice = NOTICE;
  renderAndSync(root);
  state.appView = "overview";
  renderAndSync(root);
  state.appView = "settings";
  assert.equal(renderAndSync(root), "");
  state.controlNotice = "";
  renderAndSync(root);
  state.controlNotice = NOTICE;
  assert.match(renderAndSync(root), /role="status"/);
  reset();
});

test("the timer waits while a modal is visible", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    state.controlNotice = NOTICE;
    renderAndSync(createRoot({ modal: true }));
    mock.timers.tick(5000);
    assert.match(renderSettingsFeedback(), /role="status"/);
    const root = createRoot();
    renderAndSync(root);
    mock.timers.tick(4000);
    assert.deepEqual(root.toast.classes, ["is-leaving"]);
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("the toast sits at the top unless the interface menu moves it to the bottom", () => {
  reset();
  assert.equal(getStoredFeedbackPosition(), "top");
  state.controlNotice = NOTICE;
  state.feedbackPosition = "top";
  assert.doesNotMatch(renderSettingsFeedback(), /oq-settings-feedback--bottom/);
  state.feedbackPosition = "bottom";
  assert.match(renderSettingsFeedback(), /oq-settings-feedback--bottom/);
  state.feedbackPosition = "top";
  reset();
});
