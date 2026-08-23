import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import { ENTITY_DEFS, FAST_OVERVIEW_KEYS, OVERVIEW_KEYS, QUICK_STEPS, SETTINGS_KEYS } from "../js/src/core/config.js";

globalThis.__OQ_PREVIEW__ = false;

const { getPrimeBaseKeys } = await import("../js/src/core/entity-sync.js");
const {
  isUsageTelemetryChoiceConfirmed,
  shouldInitializeQuickStartUsageTelemetryChoice,
  waitForUsageTelemetryChoiceConfirmation,
} = await import("../js/src/core/usage-telemetry-domain.js");

test("usage telemetry is hydrated before Quick Start filters optional steps", () => {
  const usageStep = QUICK_STEPS.find((step) => step.id === "usage-telemetry");

  assert.equal(usageStep?.optionalEntity, "usageTelemetryEnabled");
  assert.ok(getPrimeBaseKeys().includes("usageTelemetryEnabled"));
  assert.ok(getPrimeBaseKeys().includes("usageTelemetryChoiceConfigured"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("usageTelemetryEnabled"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("usageTelemetryChoiceConfigured"));
  assert.ok(OVERVIEW_KEYS.includes("usageTelemetryEnabled"));
  assert.ok(OVERVIEW_KEYS.includes("usageTelemetryChoiceConfigured"));
});

test("telemetry choice confirmation requires persisted state and the expected value", () => {
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "ON",
    choiceValue: "ON",
    expectedEnabled: true,
  }), true);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "OFF",
    choiceValue: "ON",
    expectedEnabled: false,
  }), true);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "OFF",
    choiceValue: "OFF",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "ON",
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: undefined,
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "unknown",
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
});

test("telemetry choice confirmation waits for a deferred controller write", async () => {
  let telemetryValue = "OFF";
  let choiceValue = "OFF";
  let refreshCount = 0;
  let waitCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      if (refreshCount === 3) {
        telemetryValue = "ON";
        choiceValue = "ON";
      }
      return [telemetryValue, choiceValue];
    },
    expectedEnabled: true,
    wait: async (delayMs) => {
      assert.equal(delayMs, 200);
      waitCount += 1;
    },
  });

  assert.equal(confirmed, true);
  assert.equal(refreshCount, 3);
  assert.equal(waitCount, 3);
});

test("telemetry choice confirmation retries a transient refresh failure", async () => {
  let telemetryValue = "OFF";
  let choiceValue = "OFF";
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      if (refreshCount === 1) {
        throw new Error("temporary read failure");
      }
      telemetryValue = "ON";
      choiceValue = "ON";
      return [telemetryValue, choiceValue];
    },
    expectedEnabled: true,
    wait: async () => {},
  });

  assert.equal(confirmed, true);
  assert.equal(refreshCount, 2);
});

test("telemetry choice confirmation stops after its bounded retry window", async () => {
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      return ["OFF", "ON"];
    },
    expectedEnabled: true,
    wait: async () => {},
  });

  assert.equal(confirmed, false);
  assert.equal(refreshCount, 10);
});

test("telemetry choice confirmation does not start more reads after its deadline", async () => {
  let nowMs = 0;
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      nowMs = 3000;
      return ["OFF", "ON"];
    },
    expectedEnabled: true,
    wait: async () => {},
    now: () => nowMs,
  });

  assert.equal(confirmed, false);
  assert.equal(refreshCount, 1);
});

test("Quick Start applies default-on only while no telemetry choice is recorded", () => {
  const base = {
    stepId: "usage-telemetry",
    telemetryAvailable: true,
    choiceAvailable: true,
  };
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "OFF" }), true);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "ON" }), false);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "unknown" }), false);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, stepId: "confirm", choiceValue: "OFF" }), false);
});

test("Quick Start locks telemetry consent before hydration starts", async () => {
  const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
  const quickStartUiActionsSource = await readFile(new URL("../js/src/features/quickstart-ui-actions.js", import.meta.url), "utf8");
  const preparationLockIndex = quickStartUiActionsSource.indexOf("state.busyAction = USAGE_TELEMETRY_PREPARATION_ACTION");
  const hydrationIndex = quickStartUiActionsSource.indexOf("await refreshQuickStartStepHydration(stepId)");

  assert.match(quickStartSource, /state\.loadingEntities \|\| Boolean\(state\.busyAction\)/);
  assert.ok(preparationLockIndex >= 0 && preparationLockIndex < hydrationIndex);
  assert.match(quickStartUiActionsSource, /preparationId !== quickStartPreparationId/);
});

test("usage telemetry disclosure matches the hourly and crash payload scope", async () => {
  const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
  const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");
  const entityWriteSource = await readFile(new URL("../js/src/core/entity-write-actions.js", import.meta.url), "utf8");
  const settingsSource = await readFile(new URL("../js/src/settings/privacy.js", import.meta.url), "utf8");
  const disclosureSource = await readFile(new URL("../js/src/features/usage-telemetry.js", import.meta.url), "utf8");
  const telemetryYaml = await readFile(new URL("../../oq_usage_telemetry.yaml", import.meta.url), "utf8");
  const telemetryCpp = await readFile(new URL("../../../components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp", import.meta.url), "utf8");

  assert.match(quickStartSource, /renderUsageTelemetryDisclosure\(\)/);
  assert.match(quickStartSource, /data-oq-action="confirm-no-usage-telemetry"/);
  assert.match(entityWriteSource, /commitUsageTelemetrySwitch/);
  assert.match(entityWriteSource, /waitForUsageTelemetryChoiceConfirmation/);
  assert.match(quickStartActionsSource, /waitForUsageTelemetryChoiceConfirmation/);
  assert.match(entityWriteSource, /"turn_off"/);
  assert.match(settingsSource, /renderUsageTelemetryDisclosure\(\{ collapsible: true/);
  assert.match(settingsSource, /usageTelemetryDetailsOpen/);
  assert.deepEqual(ENTITY_DEFS.usageTelemetryInstallationId, {
    domain: "text_sensor",
    name: "Usage statistics installation ID",
    optional: true,
  });
  assert.ok(SETTINGS_KEYS.includes("usageTelemetryInstallationId"));
  assert.match(telemetryYaml, /name: "Usage statistics installation ID"[\s\S]*internal: true/);
  assert.match(disclosureSource, /settings && enabled && hasEntity\("usageTelemetryInstallationId"\)/);
  assert.match(disclosureSource, /oq-usage-consent-installation-id/);
  assert.match(disclosureSource, /vrijwel direct, daarna ongeveer elk uur en na een echte firmwarecrash/);
  assert.match(disclosureSource, /OpenQuatt-loggingserver/);
  assert.match(disclosureSource, /wifi-signaal/);
  assert.match(disclosureSource, /Quatt Hybrid-versie, verwarmingsstrategie, flowbron en regelbronnen/);
  assert.match(disclosureSource, /Aan\/uit-status van CiC, OpenTherm-thermostaat, ketelondersteuning, MQTT-inputs en lokale historie/);
  assert.match(disclosureSource, /ketelaansluiting \(aan\/uit of OpenTherm\)/);
  assert.match(disclosureSource, /Geen gemeten of ingestelde temperaturen, grenzen, MQTT-topics of reguliere logs/);
  assert.match(disclosureSource, /Nooit een wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of inloggegevens/);
  assert.match(disclosureSource, /Voorbeeld van het verzonden bericht \(JSON\)/);
  assert.match(disclosureSource, /schema_version/);
  assert.match(disclosureSource, /timestamp_s/);
  assert.match(disclosureSource, /Voorbeeld van een crashbericht \(JSON\)/);
  assert.match(disclosureSource, /Crashreden, oorzaakcode, processorcore, foutadres indien bruikbaar, ruwe backtrace per core/);
  assert.match(disclosureSource, /current_build_id/);
  assert.match(disclosureSource, /captured_build_id/);
  assert.match(disclosureSource, /captured_source_repository/);
  assert.match(disclosureSource, /captured_source_commit/);
  assert.match(disclosureSource, /captured_build_epoch/);
  assert.match(disclosureSource, /captured_firmware_version/);
  assert.match(disclosureSource, /captured_release_channel/);
  assert.match(disclosureSource, /captured_by_current_build/);
  assert.match(disclosureSource, /other_core_backtrace/);
  assert.match(disclosureSource, /backtrace_truncated/);
  const configFields = [
    "quatt_hybrid_generation_config",
    "flow_source_config",
    "heating_strategy",
    "room_temperature_source",
    "room_setpoint_source",
    "outside_temperature_source",
    "heating_enable_source",
    "cooling_enable_source",
    "cooling_dew_point_source",
  ];
  for (const field of configFields) {
    assert.match(disclosureSource, new RegExp(field));
    assert.match(telemetryCpp, new RegExp(`"${field}"`));
  }
  assert.match(disclosureSource, /oq-usage-disclosure--collapsible/);
  assert.match(disclosureSource, /data-oq-action="toggle-usage-telemetry-details"/);
  assert.match(disclosureSource, /technisch wel het bron-IP-adres zien/);
  assert.match(disclosureSource, /OpenQuatt slaat dit IP-adres niet op/);
  assert.doesNotMatch(disclosureSource, /poort 1883|brokercredential|MQTT-broker|startvertraging|jitter/);
});
