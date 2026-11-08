import assert from "node:assert/strict";
import test, { mock } from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
  setTimeout: (...args) => globalThis.setTimeout(...args),
  clearTimeout: (...args) => globalThis.clearTimeout(...args),
};

const { getStoredFeedbackPosition, state } = await import("../js/src/core/state.js");
const { dismissSettingsFeedback, finishFeedbackUndo, renderSettingsFeedback, syncSettingsFeedback, takeFeedbackUndo } =
  await import("../js/src/features/settings-feedback.js");

const NOTICE = "Minimale koel-aanvoer bijgewerkt.";
const undoFor = (key, notice = NOTICE, from = "18.0 °C", to = "20.0 °C") => ({ key, value: 18, label: key, from, to, notice });

function createRoot({ modal = false } = {}) {
  return { querySelector: (selector) => (selector === "[data-oq-modal]" && modal ? {} : null) };
}

// Mirrors the shell: render the markup, then sync the presentation.
function renderAndSync(root = createRoot()) {
  const markup = renderSettingsFeedback();
  syncSettingsFeedback(root);
  return markup;
}

// The toasts in a rendered stack, newest first.
function toastsIn(markup) {
  return [...markup.matchAll(/<div class="oq-settings-feedback(?: oq-settings-feedback--error)?" data-toast-id="(\d+)"[^>]*><div class="oq-settings-feedback-inner">.*?<p>([^<]*)/g)]
    .map(([, id, text]) => ({ id, text }));
}

function reset() {
  dismissSettingsFeedback();
  Object.assign(state, { appView: "settings", controlError: "", controlNotice: "", settingsUndo: null, busyAction: "", feedbackPosition: "top" });
  syncSettingsFeedback(createRoot());
}

test("errors take precedence over notices, stay until dismissed and clear their state", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    Object.assign(state, { controlError: "Opslaan mislukt", controlNotice: "Opgeslagen" });
    const markup = renderAndSync();
    assert.match(markup, /role="alert"/);
    assert.doesNotMatch(markup, /Opgeslagen/);
    const [toast] = toastsIn(markup);
    assert.match(markup, new RegExp(`data-oq-action="dismiss-settings-feedback" data-toast-id="${toast.id}"`));
    mock.timers.tick(60_000);
    assert.equal(toastsIn(renderAndSync()).length, 1, "an error does not leave on its own");
    assert.equal(dismissSettingsFeedback(toast.id), true);
    assert.equal(state.controlError, "");
    assert.equal(state.controlNotice, "");
    assert.deepEqual(toastsIn(renderAndSync()), []);
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("an error toast leaves once the error is cleared elsewhere", () => {
  reset();
  state.controlError = "Opslaan mislukt";
  assert.equal(toastsIn(renderAndSync()).length, 1);
  state.controlError = "";
  assert.deepEqual(toastsIn(renderAndSync()), []);
  reset();
});

test("a confirmation leaves after four seconds and does not come back on later renders", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    state.controlNotice = NOTICE;
    assert.equal(toastsIn(renderAndSync()).length, 1);
    assert.equal(toastsIn(renderAndSync()).length, 1, "a later render keeps the same toast");
    mock.timers.tick(3999);
    assert.equal(toastsIn(renderSettingsFeedback()).length, 1);
    mock.timers.tick(1);
    assert.deepEqual(toastsIn(renderAndSync()), []);
    assert.equal(state.controlNotice, NOTICE, "other screens keep reading the notice");
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("new messages stack newest first, and the fourth pushes the oldest out", () => {
  reset();
  for (const text of ["Eén bijgewerkt.", "Twee bijgewerkt.", "Drie bijgewerkt.", "Vier bijgewerkt."]) {
    state.controlNotice = text;
    renderAndSync();
  }
  assert.deepEqual(toastsIn(renderSettingsFeedback()).map((toast) => toast.text), ["Vier bijgewerkt.", "Drie bijgewerkt.", "Twee bijgewerkt."]);
  reset();
});

test("changing the same setting again adds a toast, and only the newest can undo that setting", () => {
  reset();
  state.controlNotice = NOTICE;
  state.settingsUndo = undoFor("coolingMinSupply");
  renderAndSync();
  state.settingsUndo = undoFor("coolingMinSupply", NOTICE, "20.0 °C", "21.0 °C");
  renderAndSync();
  let markup = renderSettingsFeedback();
  assert.equal(toastsIn(markup).length, 2);
  assert.equal((markup.match(/data-oq-action="undo-settings-change"/g) || []).length, 1);
  const [newest, older] = toastsIn(markup);
  assert.match(markup, new RegExp(`data-oq-action="undo-settings-change" data-toast-id="${newest.id}"`));
  assert.equal(takeFeedbackUndo(older.id), null, "the older toast's undo is superseded");
  state.controlNotice = "Koelvraag bijgewerkt.";
  state.settingsUndo = undoFor("coolingDemand", "Koelvraag bijgewerkt.");
  renderAndSync();
  markup = renderSettingsFeedback();
  assert.equal((markup.match(/data-oq-action="undo-settings-change"/g) || []).length, 2, "another setting has its own undo");
  reset();
});

test("an undo turns its own toast into the outcome instead of adding one", () => {
  reset();
  state.controlNotice = NOTICE;
  const undo = undoFor("coolingMinSupply");
  state.settingsUndo = undo;
  renderAndSync();
  state.controlNotice = "Koelvraag bijgewerkt.";
  state.settingsUndo = undoFor("coolingDemand", "Koelvraag bijgewerkt.");
  renderAndSync();
  const older = toastsIn(renderSettingsFeedback())[1];
  assert.equal(takeFeedbackUndo(older.id), undo);
  assert.equal(takeFeedbackUndo(older.id), null, "one undo per toast");
  // The commit path sets its own notices while the undo runs.
  state.controlNotice = "Minimale koel-aanvoer teruggezet naar 18.0 °C.";
  state.settingsUndo = null;
  renderAndSync();
  finishFeedbackUndo();
  const markup = renderSettingsFeedback();
  assert.deepEqual(toastsIn(markup).map((toast) => toast.text), ["Koelvraag bijgewerkt.", "Minimale koel-aanvoer teruggezet naar 18.0 °C."]);
  assert.equal((markup.match(/data-oq-action="undo-settings-change"/g) || []).length, 1);
  assert.equal((markup.match(/Bekijk in logboek/g) || []).length, 2, "the undone toast keeps its log link");
  reset();
});

test("leaving the settings page finishes the messages, and a new change shows again", () => {
  reset();
  state.controlNotice = NOTICE;
  renderAndSync();
  state.appView = "overview";
  syncSettingsFeedback(createRoot());
  state.appView = "settings";
  assert.deepEqual(toastsIn(renderAndSync()), []);
  state.controlNotice = "";
  renderAndSync();
  state.controlNotice = NOTICE;
  assert.equal(toastsIn(renderAndSync()).length, 1);
  reset();
});

test("the timer waits while a modal is visible", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    state.controlNotice = NOTICE;
    renderAndSync(createRoot({ modal: true }));
    mock.timers.tick(5000);
    assert.equal(toastsIn(renderSettingsFeedback()).length, 1);
    renderAndSync(createRoot());
    mock.timers.tick(1000);
    assert.deepEqual(toastsIn(renderSettingsFeedback()), []);
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("the stack sits at the top unless the interface menu moves it to the bottom", () => {
  reset();
  assert.equal(getStoredFeedbackPosition(), "top");
  state.controlNotice = NOTICE;
  assert.doesNotMatch(renderSettingsFeedback(), /oq-settings-feedback-stack--bottom/);
  state.feedbackPosition = "bottom";
  assert.match(renderSettingsFeedback(), /oq-settings-feedback-stack--bottom/);
  reset();
});

test("a plain change offers undo with its old and new value and stays eight seconds", () => {
  mock.timers.enable({ apis: ["setTimeout"] });
  try {
    reset();
    state.controlNotice = NOTICE;
    state.settingsUndo = undoFor("coolingMinSupply");
    const markup = renderAndSync();
    assert.match(markup, /data-oq-action="undo-settings-change"/);
    assert.match(markup, /18\.0 °C → 20\.0 °C/);
    assert.match(markup, /class="oq-settings-feedback-footer"><button[^>]*data-oq-action="select-settings-group" data-group-id="changes">Bekijk in logboek<\/button><button[^>]*data-oq-action="undo-settings-change"/);
    const group = state.settingsGroup;
    state.settingsGroup = "changes";
    assert.doesNotMatch(renderSettingsFeedback(), /Bekijk in logboek/, "the log link is left out on the log itself");
    state.settingsGroup = group;
    mock.timers.tick(4000);
    assert.equal(toastsIn(renderSettingsFeedback()).length, 1, "undo keeps the toast longer than a plain confirmation");
    mock.timers.tick(4000);
    assert.deepEqual(toastsIn(renderSettingsFeedback()), []);
  } finally {
    mock.timers.reset();
    reset();
  }
});

test("undo is not shown for errors or for another notice, and dismissing everything drops it", () => {
  reset();
  state.settingsUndo = undoFor("coolingMinSupply");
  state.controlNotice = "Iets anders bijgewerkt.";
  assert.doesNotMatch(renderSettingsFeedback(), /undo-settings-change/);
  state.controlNotice = NOTICE;
  state.controlError = "Opslaan mislukt";
  assert.doesNotMatch(renderSettingsFeedback(), /undo-settings-change/);
  dismissSettingsFeedback();
  assert.equal(state.settingsUndo, null);
  reset();
});
