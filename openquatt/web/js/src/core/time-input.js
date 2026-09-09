import { ENTITY_DEFS } from "./config.js";
import { getEntityValue, normalizeTimeValue } from "./entity-store.js";
import { commitTime } from "./entity-write-actions.js";
import { render } from "./render-scheduler.js";
import { state } from "./state.js";

let edit = null;

export function finishTimeInput(input) {
  const key = input?.dataset?.oqField;
  if (ENTITY_DEFS[key]?.domain !== "time" || state.savingTimeFields.has(key)
      || !Object.prototype.hasOwnProperty.call(state.inputDrafts, key)) {
    return;
  }
  const value = normalizeTimeValue(input.value);
  if (!value) {
    state.controlError = `${ENTITY_DEFS[key].name} verwacht tijd als HH:MM.`;
    render();
    return;
  }
  if (value === normalizeTimeValue(getEntityValue(key))) {
    delete state.inputDrafts[key];
    return;
  }
  // Lock this field immediately; rendering may wait for a neighbouring time edit.
  input.disabled = true;
  void commitTime(key, input.value);
}

export function handleTimeInputFocus(event) {
  const input = event?.target;
  if (input?.type !== "time" || ENTITY_DEFS[input.dataset?.oqField]?.domain !== "time") {
    return;
  }
  if (event.type === "focusin") {
    edit = { input, view: state.appView, group: state.settingsGroup, modal: state.systemModal, pending: false };
  } else if (event.type === "focusout") {
    const pending = edit?.input === input && edit.pending;
    if (edit?.input === input) edit = null;
    // Let the next field receive focus and the clicked button handle its action first.
    window.setTimeout(() => {
      finishTimeInput(input);
      if (pending) render();
    }, 0);
  }
}

export function deferTimeInputRender() {
  if (!edit || document.activeElement !== edit.input || !edit.input.isConnected
      || state.appView !== edit.view || state.settingsGroup !== edit.group
      || state.systemModal !== edit.modal || state.nativeOpen
      || state.deviceReconnectMode || state.updateModalOpen || state.quickStartModalOpen) {
    edit = null;
    return false;
  }
  // Replacing a native time input loses partial segments and its open picker.
  edit.pending = true;
  return true;
}
