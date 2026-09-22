import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import { ENTITY_DEFS, QUICK_STEPS } from "../js/src/core/config.js";

globalThis.__OQ_PREVIEW__ = false;

const {
  isPerformanceTelemetryChoiceConfirmed,
  shouldInitializeQuickStartPerformanceTelemetryChoice,
  waitForPerformanceTelemetryChoiceConfirmation,
} = await import("../js/src/core/performance-telemetry-domain.js");

test("performance telemetry is a default-off Quick Start step before confirm", async () => {
  const ids = QUICK_STEPS.map((step) => step.id);
  assert.deepEqual(ids.slice(ids.indexOf("usage-telemetry"), ids.indexOf("confirm") + 1), [
    "usage-telemetry",
    "performance-telemetry",
    "confirm",
  ]);
  const step = QUICK_STEPS.find((entry) => entry.id === "performance-telemetry");
  assert.equal(step?.titleKey, "quickStart.performanceTelemetry.title");
  assert.equal(step?.optionalEntity, "performanceTelemetryEnabled");
  const { default: nlCatalogue } = await import("../js/src/i18n/nl.js");
  assert.equal(nlCatalogue.quickStart.performanceTelemetry.title, "Prestatiemetingen");
  assert.match(nlCatalogue.quickStart.performanceTelemetry.copy || "", /standaard uit/);
  assert.deepEqual(ENTITY_DEFS.performanceTelemetryChoiceConfigured, {
    domain: "binary_sensor",
    name: "Performance model validation choice configured",
    optional: true,
  });
});

test("performance telemetry hydrates its switch and choice sensor", async () => {
  const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");

  assert.match(quickStartActionsSource, /"performanceTelemetryEnabled", "performanceTelemetryChoiceConfigured", \.\.\.HEADER_ENTITY_KEYS\]/);
  assert.match(quickStartActionsSource, /if \(stepId === "performance-telemetry"\) \{[\s\S]*?"performanceTelemetryChoiceConfigured",/);
});

test("performance telemetry choice initializes only on its own step without a choice", () => {
  assert.equal(
    shouldInitializeQuickStartPerformanceTelemetryChoice({
      stepId: "performance-telemetry",
      telemetryAvailable: true,
      choiceAvailable: true,
      choiceValue: false,
    }),
    true,
  );
  assert.equal(
    shouldInitializeQuickStartPerformanceTelemetryChoice({
      stepId: "usage-telemetry",
      telemetryAvailable: true,
      choiceAvailable: true,
      choiceValue: false,
    }),
    false,
  );
  assert.equal(
    shouldInitializeQuickStartPerformanceTelemetryChoice({
      stepId: "performance-telemetry",
      telemetryAvailable: true,
      choiceAvailable: true,
      choiceValue: true,
    }),
    false,
  );
});

test("performance telemetry choice confirms the expected switch state", () => {
  assert.equal(
    isPerformanceTelemetryChoiceConfirmed({ telemetryValue: false, choiceValue: true, expectedEnabled: false }),
    true,
  );
  assert.equal(
    isPerformanceTelemetryChoiceConfirmed({ telemetryValue: true, choiceValue: true, expectedEnabled: true }),
    true,
  );
  assert.equal(
    isPerformanceTelemetryChoiceConfirmed({ telemetryValue: true, choiceValue: true, expectedEnabled: false }),
    false,
  );
  assert.equal(
    isPerformanceTelemetryChoiceConfirmed({ telemetryValue: false, choiceValue: false, expectedEnabled: false }),
    false,
  );
});

test("performance telemetry choice confirmation polls until the deadline", async () => {
  let calls = 0;
  const confirmed = await waitForPerformanceTelemetryChoiceConfirmation({
    refresh: async () => {
      calls += 1;
      return calls < 3 ? [false, false] : [false, true];
    },
    expectedEnabled: false,
    wait: async () => {},
    now: (() => {
      let clock = 0;
      return () => (clock += 100);
    })(),
  });
  assert.equal(confirmed, true);
  assert.equal(calls, 3);

  const missing = await waitForPerformanceTelemetryChoiceConfirmation({
    refresh: async () => [false, false],
    expectedEnabled: false,
    wait: async () => {},
    now: (() => {
      let clock = 0;
      return () => (clock += 500);
    })(),
  });
  assert.equal(missing, false);
});

test("performance telemetry mirrors the usage telemetry Quick Start wiring", async () => {
  const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
  const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");
  const quickStartUiActionsSource = await readFile(new URL("../js/src/features/quickstart-ui-actions.js", import.meta.url), "utf8");
  const entityWriteSource = await readFile(new URL("../js/src/core/entity-write-actions.js", import.meta.url), "utf8");
  const consentSource = await readFile(new URL("../js/src/features/performance-telemetry.js", import.meta.url), "utf8");
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");

  assert.match(quickStartSource, /renderPerformanceTelemetryWorkspace\(\)/);
  assert.match(quickStartSource, /activeStep === "performance-telemetry"/);
  assert.match(quickStartSource, /data-oq-action="confirm-no-performance-telemetry"/);
  assert.match(quickStartSource, /data-oq-action="retry-performance-telemetry-choice"/);
  assert.match(quickStartSource, /t\("quickStart\.reviewPerfSharing"\), isEntityActive\("performanceTelemetryEnabled"\) \? t\("common\.on"\) : t\("common\.off"\)/);
  assert.match(quickStartActionsSource, /initializeQuickStartPerformanceTelemetryChoice/);
  assert.match(quickStartActionsSource, /setQuickStartSwitch\("performanceTelemetryEnabled", false\)/);
  assert.match(quickStartUiActionsSource, /preparesPerformanceTelemetry/);
  assert.match(quickStartUiActionsSource, /"retry-performance-telemetry-choice": \(\) => prepareQuickStartStep\("performance-telemetry"\)/);
  assert.match(entityWriteSource, /commitPerformanceTelemetrySwitch/);
  assert.match(entityWriteSource, /key === "performanceTelemetryEnabled"/);
  assert.match(consentSource, /t\("performance\.consentWizardCopy"\)/);
  assert.match(mockSource, /setEntity\("switch", "Performance model validation", \{ value: false, state: false \}\)/);
  assert.match(mockSource, /setEntity\("binary_sensor", "Performance model validation choice configured", \{ value: false, state: false \}\)/);
  assert.match(mockSource, /if \(name === "Performance model validation"\)/);
});
