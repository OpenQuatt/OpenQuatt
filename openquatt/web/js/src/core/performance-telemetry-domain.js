export function shouldInitializeQuickStartPerformanceTelemetryChoice({
  stepId,
  telemetryAvailable,
  choiceAvailable,
  choiceValue,
}) {
  const choiceExplicitlyMissing = choiceValue === false
    || ["off", "false", "0"].includes(String(choiceValue).trim().toLowerCase());
  return stepId === "performance-telemetry"
    && telemetryAvailable
    && choiceAvailable
    && choiceExplicitlyMissing;
}

export function parsePerformanceTelemetryActiveValue(value) {
  if (value === true || ["on", "true", "1"].includes(String(value).trim().toLowerCase())) {
    return true;
  }
  if (value === false || ["off", "false", "0"].includes(String(value).trim().toLowerCase())) {
    return false;
  }
  return null;
}

export function isPerformanceTelemetryChoiceConfirmed({
  telemetryValue,
  choiceValue,
  expectedEnabled,
}) {
  const telemetryEnabled = parsePerformanceTelemetryActiveValue(telemetryValue);
  return parsePerformanceTelemetryActiveValue(choiceValue) === true
    && telemetryEnabled !== null
    && telemetryEnabled === expectedEnabled;
}

export async function waitForPerformanceTelemetryChoiceConfirmation({
  refresh,
  expectedEnabled,
  wait = (ms) => new Promise((done) => setTimeout(done, ms)),
  now = Date.now,
}) {
  const deadline = now() + 2000;

  for (let attempt = 0; attempt < 10 && now() < deadline; attempt += 1) {
    await wait(Math.min(200, deadline - now()));
    let values;
    try {
      values = await refresh();
    } catch {
      continue;
    }
    if (isPerformanceTelemetryChoiceConfirmed({
      telemetryValue: values[0],
      choiceValue: values[1],
      expectedEnabled,
    })) {
      return true;
    }
  }
  return false;
}

export function shouldOfferPerformanceTelemetryPrompt({ setupComplete, enabled, handled, deferred, blocked }) {
  return setupComplete === true
    && parsePerformanceTelemetryActiveValue(enabled) === false
    && parsePerformanceTelemetryActiveValue(handled) === false
    && !deferred
    && !blocked;
}

export function isPerformanceTelemetryPromptConfirmed({ enabled, choice, handled, expectedEnabled }) {
  return isPerformanceTelemetryChoiceConfirmed({ telemetryValue: enabled, choiceValue: choice, expectedEnabled })
    && parsePerformanceTelemetryActiveValue(handled) === true;
}

// Reconcile even a lost POST acknowledgement through fresh, uncached reads.
// Never repeat the write or convert an uncertain opt-in into a saved refusal.
export async function savePerformanceTelemetryPromptChoice({ write, read, expectedEnabled, wait = (ms) => new Promise((done) => setTimeout(done, ms)) }) {
  try {
    await write(expectedEnabled);
  } catch {
    // The controller may have persisted the choice before the connection failed.
  }
  for (let attempt = 0; attempt < 4; attempt += 1) {
    try {
      if (isPerformanceTelemetryPromptConfirmed({ ...await read(), expectedEnabled })) return true;
    } catch {
      // Missing or stale controller status does not prove consent or persistence.
    }
    if (attempt < 3) await wait(200);
  }
  return false;
}
