import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { CURVE_SETTING_KEYS, ENTITY_DEFS, FAST_VIEW_ENTITY_REFRESH_CONCURRENCY, FIRMWARE_MODAL_KEYS, FLOW_SETTING_KEYS, FLOW_TUNING_KEYS, HEADER_ENTITY_KEYS, POWER_HOUSE_KEYS, QUICK_START_FLOW_SOURCE_KEYS, QUICK_START_THERMOSTAT_SOURCE_KEYS, SILENT_SETTING_KEYS, TOPOLOGY_HINT_KEYS } from "../core/config.js";
import { buildEntityPath } from "../core/domain-helpers.js";
import { setEntityBackupValue } from "../core/entity-backup.js";
import { getEntityValue } from "../core/entity-store.js";
import { refreshEntities } from "../core/entity-sync.js";
import { ODU_GENERATION_DETECT_KEYS, ODU_GENERATION_KEYS } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { shouldInitializeQuickStartUsageTelemetryChoice, waitForUsageTelemetryChoiceConfirmation } from "../core/usage-telemetry-domain.js";
import { getQuickStartFlowSourceModel, getQuickStartThermostatSourceModel } from "./quickstart.js";
import { render } from "../core/render-scheduler.js";

  export function getQuickStartStepHydrationKeys(stepId = state.currentStep) {
    const base = ["setupComplete", "strategy", "usageTelemetryEnabled", "usageTelemetryChoiceConfigured", ...HEADER_ENTITY_KEYS];
    if (stepId === "setup") {
      return [...new Set([...base, ...FIRMWARE_MODAL_KEYS])];
    }
    if (stepId === "generation") {
      return [...new Set([
        ...base,
        "installationTopology",
        ...TOPOLOGY_HINT_KEYS,
        "hpGeneration",
        ...ODU_GENERATION_KEYS,
        ...ODU_GENERATION_DETECT_KEYS,
      ])];
    }
    if (stepId === "flow-source") {
      return [...new Set([...base, "hpGeneration", ...QUICK_START_FLOW_SOURCE_KEYS])];
    }
    if (stepId === "thermostat-source") {
      return [...new Set([...base, ...QUICK_START_THERMOSTAT_SOURCE_KEYS])];
    }
    if (stepId === "boiler") {
      return [...new Set([
        ...base,
        "boilerCvAssistEnabled",
        "boilerFaultFallbackEnabled",
        "boilerConnection",
        "boilerRatedHeatPower",
        "otbLinkAvailable",
      ])];
    }
    if (stepId === "strategy") {
      return [...new Set([...base, "strategy"])];
    }
    if (stepId === "heating") {
      return [...new Set([...base, ...POWER_HOUSE_KEYS, ...CURVE_SETTING_KEYS, "dayMax", "silentMax"])];
    }
    if (stepId === "flow") {
      return [...new Set([...base, ...FLOW_SETTING_KEYS, ...FLOW_TUNING_KEYS])];
    }
    if (stepId === "water") {
      return [...new Set([...base, "maxWater"])];
    }
    if (stepId === "silent") {
      return [...new Set([...base, ...SILENT_SETTING_KEYS])];
    }
    if (stepId === "usage-telemetry") {
      return [...new Set([...base, "usageTelemetryEnabled", "usageTelemetryChoiceConfigured"])];
    }
    if (stepId === "confirm") {
      return [...new Set([
        ...base,
        "installationTopology",
        "hpGeneration",
        ...ODU_GENERATION_KEYS,
        ...ODU_GENERATION_DETECT_KEYS,
        "boilerCvAssistEnabled",
        "boilerFaultFallbackEnabled",
        "boilerConnection",
        "boilerRatedHeatPower",
        ...QUICK_START_FLOW_SOURCE_KEYS,
        ...QUICK_START_THERMOSTAT_SOURCE_KEYS,
        ...FLOW_SETTING_KEYS,
        ...FLOW_TUNING_KEYS,
        ...POWER_HOUSE_KEYS,
        ...CURVE_SETTING_KEYS,
        "maxWater",
        ...SILENT_SETTING_KEYS,
      ])];
    }
    return base;
  }

  export async function refreshQuickStartStepHydration(stepId = state.currentStep) {
    const keys = getQuickStartStepHydrationKeys(stepId);
    try {
      await refreshEntities(keys, "all", { concurrency: FAST_VIEW_ENTITY_REFRESH_CONCURRENCY });
      if (state.quickStartModalOpen && state.currentStep === stepId && !state.nativeOpen) {
        render();
      }
    } catch (_error) {
      // A normal poll will retry; keep step navigation responsive.
    }
  }

  export async function initializeQuickStartUsageTelemetryChoice() {
    if (!shouldInitializeQuickStartUsageTelemetryChoice({
      stepId: state.currentStep,
      telemetryAvailable: hasEntity("usageTelemetryEnabled"),
      choiceAvailable: hasEntity("usageTelemetryChoiceConfigured"),
      choiceValue: getEntityValue("usageTelemetryChoiceConfigured"),
    })) {
      return;
    }

    state.busyAction = "switch-usageTelemetryEnabled";
    state.controlNotice = "";
    state.controlError = "";
    render();

    const confirmChoice = (expectedEnabled) => waitForUsageTelemetryChoiceConfirmation({
      refresh: async () => {
        await refreshEntities(["usageTelemetryEnabled", "usageTelemetryChoiceConfigured"], "all");
        return [getEntityValue("usageTelemetryEnabled"), getEntityValue("usageTelemetryChoiceConfigured")];
      },
      expectedEnabled,
    });

    try {
      await setQuickStartSwitch("usageTelemetryEnabled", true);
      if (!await confirmChoice(true)) {
        throw new Error("De controller heeft de keuze niet bevestigd.");
      }
      state.controlError = "";
    } catch (error) {
      let disabledConfirmed = false;
      try {
        await setQuickStartSwitch("usageTelemetryEnabled", false);
        disabledConfirmed = await confirmChoice(false);
      } catch (_disableError) {
        // Keep navigation blocked when the privacy-safe fallback cannot be confirmed.
      }
      if (disabledConfirmed) {
        state.controlError = "";
        state.controlNotice = "De standaardkeuze kon niet worden ingeschakeld. Delen is bevestigd uitgeschakeld; je kunt doorgaan of het opnieuw inschakelen.";
      } else {
        state.controlError = `De keuze kon niet veilig worden bevestigd. Controleer de verbinding en probeer opnieuw. ${error.message}`;
      }
    } finally {
      state.busyAction = "";
      render();
    }
  }

  export async function applyQuickStartFlowSourceConfiguration() {
    const model = getQuickStartFlowSourceModel();
    if (!model.canApply) {
      state.controlError = model.requiresCic
        ? "Vul eerst een geldig CiC-adres of een geldige feed-URL in."
        : "De vereiste flowbroninstelling is niet beschikbaar in deze firmware.";
      render();
      return;
    }

    state.busyAction = "quickstart-flow-source";
    state.controlNotice = "";
    state.controlError = "";
    render();

    const applyValue = async (key, value) => {
      if (!hasEntity(key)) {
        return;
      }
      const current = getEntityValue(key);
      if ((typeof value === "boolean" && isEntityActive(key) === value)
        || (typeof value !== "boolean" && String(current) === String(value))) {
        return;
      }
      const applied = await setEntityBackupValue(key, value);
      state.entities[key] = {
        ...(state.entities[key] || {}),
        value: applied,
        state: applied,
      };
    };

    try {
      if (model.requiresCic) {
        await applyValue("cicFeedUrl", model.normalizedDraftUrl);
        await applyValue("cicPollingEnabled", true);
        await applyValue("flowSource", "CIC");
        state.quickStartCicFeedUrlDraft = null;
        state.controlNotice = "CiC-flowmeting ingesteld. OpenQuatt controleert nu de JSON-feed.";
      } else {
        if (model.qFlowTarget) {
          await applyValue("qFlowSource", model.qFlowTarget);
        }
        await applyValue("flowSource", "Outdoor unit");
        state.controlNotice = model.qFlowTarget === "Local"
          ? "De lokale flowmeter op de Q-edition controller is ingesteld."
          : "De flowmeter in de buitenunit is ingesteld als Modbus-bron.";
      }
      await refreshEntities(QUICK_START_FLOW_SOURCE_KEYS, "all");
    } catch (error) {
      state.controlError = `Flowconfiguratie kon niet volledig worden toegepast. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  }

  export async function refreshQuickStartFlowSignal() {
    state.busyAction = "quickstart-flow-refresh";
    state.controlNotice = "";
    state.controlError = "";
    render();

    try {
      await refreshEntities(QUICK_START_FLOW_SOURCE_KEYS, "all");
      const model = getQuickStartFlowSourceModel();
      state.controlNotice = !model.flowAvailable
        ? "Nog geen actuele flowwaarde ontvangen."
        : model.flowValue > 0
          ? `Flowsignaal bijgewerkt: ${Math.round(model.flowValue)} L/h.`
          : "Het flowsignaal is beschikbaar; momenteel is er geen circulatie.";
    } catch (error) {
      state.controlError = `Flowsignaal controleren mislukt. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  }

  export async function setQuickStartSwitch(key, enabled) {
    const entity = ENTITY_DEFS[key];
    if (!entity || !hasEntity(key)) {
      throw new Error("Deze firmware bevat de vereiste testbediening niet.");
    }
    const response = await fetch(buildEntityPath(entity.domain, entity.name, enabled ? "turn_on" : "turn_off"), {
      method: "POST",
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
  }

  export async function refreshQuickStartFlowTestControls() {
    const keys = ["commissioningCm100Start", "commissioningCm100Stop", "quickFlowTest"];
    keys.forEach((key) => {
      if (state.optionalMissingEntities) {
        delete state.optionalMissingEntities[key];
      }
      delete state.entities[key];
    });
    await refreshEntities(keys, "all");
    const missingKeys = keys.filter((key) => !state.entities[key]);
    if (missingKeys.length) {
      const missingNames = missingKeys.map((key) => ENTITY_DEFS[key]?.name || key).join(", ");
      throw new Error(`Interne waterpomptestbediening ontbreekt: ${missingNames}.`);
    }
  }

  export async function monitorQuickStartFlowTest() {
    for (let attempt = 0; attempt < 40; attempt += 1) {
      await new Promise((resolve) => window.setTimeout(resolve, 1000));
      try {
        await refreshEntities(QUICK_START_FLOW_SOURCE_KEYS, "state");
      } catch {
        return;
      }
      if (isEntityActive("quickFlowTest")) {
        continue;
      }
      if (state.busyAction !== "quickstart-flow-test-abort") {
        state.controlNotice = "Waterpomptest afgerond. OpenQuatt is teruggekeerd naar de normale regeling.";
      }
      render();
      return;
    }
  }

  export async function startQuickStartFlowTest() {
    const model = getQuickStartFlowSourceModel();
    if (!model.canRunFlowTest) {
      state.controlError = "Activeer eerst de flowconfiguratie of installeer firmware met de waterpomptest.";
      render();
      return;
    }

    state.busyAction = "quickstart-flow-test-start";
    state.controlNotice = "";
    state.controlError = "";
    render();

    let openedCm100 = false;
    try {
      await refreshQuickStartFlowTestControls();
      if (!isEntityActive("cm100Active")) {
        const cm100 = ENTITY_DEFS.commissioningCm100Start;
        const response = await fetch(buildEntityPath(cm100.domain, cm100.name, "press"), { method: "POST" });
        if (!response.ok) {
          throw new Error(`CM100 starten gaf HTTP ${response.status}`);
        }
        openedCm100 = true;
      }

      let ready = isEntityActive("cm100Active")
        && String(getEntityValue("commissioningStatus") || "").trim() === "CM100 READY";
      for (let attempt = 0; !ready && attempt < 20; attempt += 1) {
        await new Promise((resolve) => window.setTimeout(resolve, 500));
        await refreshEntities(["commissioningStatus", "cm100Active"], "state");
        ready = isEntityActive("cm100Active")
          && String(getEntityValue("commissioningStatus") || "").trim() === "CM100 READY";
      }
      if (!ready) {
        const status = String(getEntityValue("commissioningStatus") || "").trim();
        if (status) {
          throw new Error(`Service-stand werd niet gereed: ${status}.`);
        }
        throw new Error("Service-stand CM100 werd niet op tijd gereed.");
      }

      await setQuickStartSwitch("quickFlowTest", true);
      await refreshEntities(QUICK_START_FLOW_SOURCE_KEYS, "all");
      const status = String(getEntityValue("commissioningStatus") || "").trim();
      if (!isEntityActive("quickFlowTest")) {
        throw new Error(status || "De waterpomptest kon niet worden gestart.");
      }
      state.controlNotice = "Waterpomptest gestart: alleen de pomp draait 30 seconden op 400 iPWM.";
      void monitorQuickStartFlowTest();
    } catch (error) {
      if (openedCm100 && !isEntityActive("quickFlowTest")) {
        try {
          const cm100Stop = ENTITY_DEFS.commissioningCm100Stop;
          await fetch(buildEntityPath(cm100Stop.domain, cm100Stop.name, "press"), { method: "POST" });
        } catch {
          // Firmware safety behavior remains the final fallback.
        }
      }
      state.controlError = `Waterpomptest starten mislukt. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  }

  export async function abortQuickStartFlowTest() {
    state.busyAction = "quickstart-flow-test-abort";
    state.controlNotice = "";
    state.controlError = "";
    render();

    try {
      await setQuickStartSwitch("quickFlowTest", false);
      await refreshEntities(QUICK_START_FLOW_SOURCE_KEYS, "all");
      state.controlNotice = "Waterpomptest gestopt. OpenQuatt keert terug naar de normale regeling.";
    } catch (error) {
      state.controlError = `Waterpomptest stoppen mislukt. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  }

  export async function applyQuickStartThermostatSourceConfiguration() {
    const model = getQuickStartThermostatSourceModel();
    if (!model.canApply) {
      state.controlError = model.selectedSource === "CIC"
        ? "Vul eerst een geldig CiC-adres of een geldige feed-URL in."
        : "De vereiste thermostaatbroninstelling is niet beschikbaar in deze firmware.";
      render();
      return;
    }

    state.busyAction = "quickstart-thermostat-source";
    state.controlNotice = "";
    state.controlError = "";
    render();

    const applyValue = async (key, value) => {
      if (!hasEntity(key)) {
        return;
      }
      const current = getEntityValue(key);
      if ((typeof value === "boolean" && isEntityActive(key) === value)
        || (typeof value !== "boolean" && String(current) === String(value))) {
        return;
      }
      const applied = await setEntityBackupValue(key, value);
      state.entities[key] = {
        ...(state.entities[key] || {}),
        value: applied,
        state: applied,
      };
    };

    try {
      if (model.selectedSource === "OT thermostat") {
        await applyValue("otEnabled", true);
      } else if (model.selectedSource === "CIC") {
        await applyValue("cicFeedUrl", model.normalizedDraftUrl);
        await applyValue("cicPollingEnabled", true);
        state.quickStartCicFeedUrlDraft = null;
      }
      await applyValue("roomTempSource", model.selectedSource);
      await applyValue("roomSetpointSource", model.selectedSource);
      state.controlNotice = model.selectedSource === "OT thermostat"
        ? "Kamertemperatuur en setpoint zijn gekoppeld aan OpenTherm."
        : model.selectedSource === "CIC"
          ? "Kamertemperatuur en setpoint zijn gekoppeld aan de CiC JSON-feed."
          : "Kamertemperatuur en setpoint zijn gekoppeld aan Home Assistant.";
      await refreshEntities(QUICK_START_THERMOSTAT_SOURCE_KEYS, "all");
    } catch (error) {
      state.controlError = `Thermostaatconfiguratie kon niet volledig worden toegepast. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  }
