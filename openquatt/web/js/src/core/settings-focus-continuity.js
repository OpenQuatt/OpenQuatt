export function captureSettingsFocusContinuity(root, appView, documentRef = document) {
  const active = documentRef.activeElement;
  const field = active?.dataset?.oqField || "";
  const focusKey = active?.dataset?.oqFocusKey || "";
  if (appView !== "settings" || !root?.contains(active) || (!field && !focusKey)) {
    return null;
  }
  return {
    field,
    focusKey,
    modalId: active.closest?.("[data-oq-modal]")?.dataset.oqModal || "",
    selectionStart: active.selectionStart,
    selectionEnd: active.selectionEnd,
  };
}

export function restoreSettingsFocusContinuity(root, focusState, documentRef = document) {
  if (!focusState || !root) {
    return;
  }
  const modal = documentRef.activeElement?.closest?.("[data-oq-modal]");
  if ((modal?.dataset.oqModal || "") !== focusState.modalId) {
    return;
  }
  const selector = focusState.field
    ? `[data-oq-field="${focusState.field}"]`
    : `[data-oq-focus-key="${focusState.focusKey}"]`;
  const control = (modal || root).querySelector(selector);
  if (!control || control.disabled) {
    return;
  }
  control.focus({ preventScroll: true });
  if (typeof focusState.selectionStart === "number" && typeof control.setSelectionRange === "function") {
    control.setSelectionRange(focusState.selectionStart, focusState.selectionEnd);
  }
}
