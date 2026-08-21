(function () {
  const OPENQUATT_RESUME_CLEAR_VALUE = "2000-01-01 00:00:00";
  const OPENQUATT_AUTH_RECOVERY_WINDOW_MS = 600000;
  const DEBUG_RECORDING_BUFFER_BYTES = 1024 * 1024;
  const DEBUG_RECORDING_SAMPLE_BYTES = 516;
  const DEBUG_RECORDING_SAMPLE_CAPACITY = Math.floor(DEBUG_RECORDING_BUFFER_BYTES / DEBUG_RECORDING_SAMPLE_BYTES);
  const MOCK_STABLE_VERSION = "v0.0.0-demo";
  const MOCK_DEV_VERSION = "v0.0.1-demo";
  const MOCK_TEST_VERSION = "v0.0.0-demo-pr.test";
  const mockFixtures = window.__OQ_MOCK_FIXTURES__;
  const mockEntityDefs = window.__OQ_MOCK_ENTITY_DEFS__;
  const mockIncidentScenarios = window.__OQ_MOCK_INCIDENT_SCENARIOS__;
  if (!mockFixtures || !Array.isArray(mockEntityDefs) || !mockIncidentScenarios) {
    throw new Error("OpenQuatt mockmetadata ontbreekt.");
  }
  const DOMAINS = new Set(mockEntityDefs.map(([domain]) => domain));
  const entities = new Map();
  let devControlsRoot = null;
  const state = {
    scenario: "heating",
    installation: "duo",
    hardware: "heatpump_controller_q",
    connection: "wifi",
    boiler: "off",
    diagnostics: "clear",
    auxTempGateOn: false,
    auxRelayLastMode: 0,
    complete: true,
    tick: 0,
    autoAnimate: true,
    incidentSimulation: {
      scenario: "none",
      phaseIndex: 0,
      actionCsrfToken: "oq-mock-incident-token-1",
      nextActionId: 1,
      pendingAction: null,
      lastActionResults: {},
      rejectedCsrfActions: {},
    },
    compressorCyclingAlert: {
      latched: false,
      firstSeenAt: 0,
      lastSeenAt: 0,
      hp1Peak2h: 0,
      hp1Peak72h: 0,
      hp2Peak2h: 0,
      hp2Peak72h: 0,
      alternating: false,
    },
    commissioningTimers: [],
    quickFlowTestTimer: null,
    bootedAt: Date.now() - ((2 * 3600) + (13 * 60)) * 1000,
    commissioning: {
      cm100Active: false,
      task: "none",
      phase: "idle",
      globalStatus: "CM0 - Standby",
      boilerStatusText: "IDLE",
      autotuneStatusText: "IDLE",
      airPurgeStatusText: "IDLE",
      airPurgeRemaining: 0,
      airPurgePhase: 0,
      airPurgeTargetIpwm: 0,
      manualFlowStatusText: "IDLE",
      manualFlowSetpoint: 800,
      manualFlowTargetIpwm: 400,
      manualHpStatusText: "IDLE",
      manualHpGuardStatusText: "Vrijgegeven",
      manualHp1Level: 0,
      manualHp2Level: 0,
      hpWaterCalibrationStatusText: "IDLE",
      hpWaterCalibrationRemaining: 0,
      hpWaterCalibrationPhase: 0,
      hpWaterCalibrationSpread: NaN,
      hpWaterCalibrationSupplyDelta: NaN,
      hpWaterCalibrationStableProgress: 0,
      hpWaterCalibrationStableRequired: 60,
      hpWaterCalibrationResultReference: NaN,
      hpWaterCalibrationResultSpreadBefore: NaN,
      hpWaterCalibrationResultExpectedSpread: NaN,
      hpWaterCalibrationResultSupplySource: "",
      hpWaterCalibrationResultRawAverages: {
        hp1In: NaN,
        hp1Out: NaN,
        hp2In: NaN,
        hp2Out: NaN,
        supply: NaN,
      },
      hpWaterCalibrationSuggested: {
        hp1In: 0,
        hp1Out: 0,
        hp2In: 0,
        hp2Out: 0,
        supply: 0,
      },
      boilerResult: 0,
      boilerConfidence: 0,
      flowKpSuggested: 0,
      flowKiSuggested: 0,
    },
    otaTimers: [],
    auth: {
      enabled: false,
      username: "",
      password: "",
      source: "bootstrap-open",
      csrfToken: "",
      recoveryUntil: 0,
    },
    apiSecurity: {
      transportActive: false,
      keyPresent: false,
      provisioningPending: true,
      provisioningClosed: false,
    },
    mqtt: {
      enabled: true,
      connected: true,
      broker: "mqtt.local",
      port: 1883,
      username: "openquatt",
      passwordSet: true,
      source: "runtime-enabled",
      csrfToken: "",
      inputTopics: {
        cooling_dew_point: "openquatt/openquatt/input/cooling/dew_point",
        outside_temperature: "openquatt/openquatt/input/weather/outdoor_temperature",
        room_temperature: "openquatt/openquatt/input/thermostat/room_temperature",
        room_setpoint: "openquatt/openquatt/input/thermostat/room_setpoint",
        heating_enable: "openquatt/openquatt/input/thermostat/heating_enable",
        cooling_enable: "openquatt/openquatt/input/thermostat/cooling_enable",
      },
      inputEnabled: {
        cooling_dew_point: true,
        outside_temperature: true,
        room_temperature: true,
        room_setpoint: true,
        heating_enable: true,
        cooling_enable: true,
      },
      inputRetained: {
        cooling_dew_point: false,
        outside_temperature: false,
        room_temperature: false,
        room_setpoint: true,
        heating_enable: false,
        cooling_enable: true,
      },
      inputAcceptRetained: {
        cooling_dew_point: false,
        outside_temperature: false,
        room_temperature: false,
        room_setpoint: true,
        heating_enable: true,
        cooling_enable: true,
      },
      lastDewPointAt: Date.now() - 42000,
      lastOutsideTemperatureAt: Date.now() - 120000,
      lastRoomTemperatureAt: Date.now() - 36000,
      lastRoomSetpointAt: Date.now() - 180000,
      lastHeatingEnableAt: Date.now() - 28000,
      lastCoolingEnableAt: Date.now() - 54000,
    },
    trendFlashWrites: 437,
    trendFlashStoredKiB: 182.5,
    trendFlashOldestAt: Date.now() - Math.round(18.4 * 24 * 60 * 60 * 1000),
    trendFlashNewestAt: Date.now() - (2 * 60 * 1000),
    trendFlashLastFlushAt: Date.now() - (12 * 60 * 1000),
    energyHistoryRecords: [],
    energyHistoryHourRecords: [],
    energyHistoryStoredKiB: 1024,
    energyHistoryWrites: 0,
    energyHistoryLastWriteAt: Date.now() - (9 * 60 * 60 * 1000),
    energyHistoryHourRetention: "180 dagen",
    energyCountersReset: false,
    logHistoryEnabled: true,
    logHistoryEntries: [],
    debugRecording: {
      active: false,
      mode: "manual",
      frozen: false,
      startedAt: 0,
      stoppedAt: 0,
      durationS: 15 * 60,
      nextOffsetS: 0,
      fields: [],
      samples: [],
    },
    oduEepromDumps: {
      1: { active: false, ready: false, startedAt: 0, completedAt: 0, jobId: 0 },
      2: { active: false, ready: false, startedAt: 0, completedAt: 0, jobId: 0 },
    },
    oduRuntimeFrequency: {
      HP1: {
        cooling: [0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74],
        heating: [0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90],
      },
      HP2: {
        cooling: [0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74],
        heating: [0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90],
      },
    },
  };

  function isCoolingScenario(name = state.scenario) {
    return name === "cooling" || name === "cooling_startup_wait" || name === "cooling_limited" || name === "cooling_buffer_stop" || name === "cooling_stop_reasons" || name === "cooling_limiter_log";
  }

  function isSummerIdleScenario(name = state.scenario) {
    return name === "summer_idle";
  }

  function isFlowHoldScenario(name = state.scenario) {
    return name === "flow_hold";
  }

  function isCandidateBlockedScenario(name = state.scenario) {
    return name === "start_blocked";
  }

  function isHeatingEnabledScenario(name = state.scenario) {
    return !isCoolingScenario(name) && !isSummerIdleScenario(name) && name !== "idle";
  }

  const HP2_ENTITIES = mockFixtures.hp2Entities;
  const COMPRESSOR_LEVEL_OPTIONS = mockFixtures.compressorLevelOptions;
  const ODU_RUNTIME_FREQUENCY_LEVELS = Array.from({ length: 11 }, (_item, index) => index);
  const ODU_RUNTIME_FREQUENCY_MODES = ["cooling", "heating"];

  function oduRuntimePrefix(hp) {
    return `${hp} - EXPERIMENTAL`;
  }

  function oduRuntimeControlName(hp, suffix) {
    const prefix = oduRuntimePrefix(hp);
    if (suffix === "enable") return `${prefix} ODU runtime frequency write enable`;
    if (suffix === "load") return `${prefix} load ODU runtime frequency table`;
    if (suffix === "apply") return `${prefix} apply ODU runtime frequency table`;
    return `${prefix} ODU runtime frequency status`;
  }

  function oduRuntimeValueName(hp, mode, level) {
    return `${oduRuntimePrefix(hp)} ${mode} F${level} runtime Hz`;
  }

  function parseOduRuntimeButtonName(name) {
    const match = String(name || "").match(/^(HP[12]) - EXPERIMENTAL (load|apply) ODU runtime frequency table$/);
    return match ? { hp: match[1], action: match[2] } : null;
  }

  function clearOduRuntimeFrequencyEntities(hp) {
    ["enable", "load", "apply", "status"].forEach((suffix) => {
      const domain = suffix === "enable" ? "switch" : suffix === "status" ? "text_sensor" : "button";
      entities.delete(entityKey(domain, oduRuntimeControlName(hp, suffix)));
    });
    ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
      ODU_RUNTIME_FREQUENCY_LEVELS.forEach((level) => {
        entities.delete(entityKey("number", oduRuntimeValueName(hp, mode, level)));
      });
    });
  }

  function seedOduRuntimeFrequencyEntities(hp) {
    const table = state.oduRuntimeFrequency[hp];
    if (!table) {
      return;
    }
    setEntity("switch", oduRuntimeControlName(hp, "enable"), { value: false, state: false });
    setEntity("button", oduRuntimeControlName(hp, "load"), {});
    setEntity("button", oduRuntimeControlName(hp, "apply"), {});
    setEntity("text_sensor", oduRuntimeControlName(hp, "status"), {
      state: "IDLE: runtime values are mock data",
      value: "IDLE: runtime values are mock data",
    });
    ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
      ODU_RUNTIME_FREQUENCY_LEVELS.forEach((level) => {
        setEntity("number", oduRuntimeValueName(hp, mode, level), {
          value: table[mode][level],
          min_value: 0,
          max_value: 120,
          step: 1,
          uom: "Hz",
        });
      });
    });
  }

  function entityKey(domain, name) {
    return `${domain}/${name}`;
  }

  function clone(value) {
    if (typeof structuredClone === "function") {
      return structuredClone(value);
    }
    return JSON.parse(JSON.stringify(value));
  }

  function setEntity(domain, name, payload) {
    entities.set(entityKey(domain, name), {
      domain,
      name,
      state: "",
      value: "",
      ...payload,
    });
  }

  function seedEntityDefinitions() {
    mockEntityDefs.forEach(([domain, name]) => {
      const value = domain === "binary_sensor" || domain === "switch" ? false : domain === "sensor" || domain === "number" ? 0 : "";
      setEntity(domain, name, { state: value, value });
    });
  }

  function getEntity(domain, name) {
    return entities.get(entityKey(domain, name));
  }

  function setNumber(name, value, uom) {
    const entity = getEntity("number", name) || getEntity("sensor", name);
    if (!entity) {
      return;
    }
    let nextValue = Number(value);
    const hpWaterMatch = /^HP([12]) - Water (in|out) temperature$/.exec(name);
    if (hpWaterMatch) {
      const hp = `HP${hpWaterMatch[1]}`;
      const side = hpWaterMatch[2];
      const rawEntity = getEntity("sensor", `${hp} - Water ${side} temperature raw`);
      const offsetEntity = getEntity("number", `${hp} water ${side} temperature offset`);
      if (rawEntity) {
        rawEntity.value = nextValue;
        rawEntity.state = "";
        rawEntity.uom = uom || rawEntity.uom;
      }
      const offset = Number(offsetEntity?.value || 0);
      nextValue = Number.isFinite(nextValue) ? Number((nextValue + offset).toFixed(2)) : nextValue;
    }
    entity.value = nextValue;
    entity.state = "";
    if (uom) {
      entity.uom = uom;
    }
  }

  function syncUptimeEntity() {
    const uptimeHours = Math.max(0, (Date.now() - state.bootedAt) / 3600000);
    setNumber("Uptime", Number(uptimeHours.toFixed(2)), "h");
  }

  function setText(domain, name, value) {
    const entity = getEntity(domain, name);
    if (!entity) {
      return;
    }
    entity.state = String(value);
    entity.value = String(value);
  }

  function setSensorText(name, value) {
    const entity = getEntity("sensor", name);
    if (!entity) {
      return;
    }
    entity.state = String(value);
    entity.value = String(value);
  }

  function setBinary(name, value) {
    const entity = getEntity("binary_sensor", name);
    if (!entity) {
      return;
    }
    entity.value = Boolean(value);
    entity.state = Boolean(value);
  }

  function setSwitch(name, value) {
    const entity = getEntity("switch", name);
    if (!entity) {
      return;
    }
    entity.value = Boolean(value);
    entity.state = Boolean(value);
  }

  const FALLBACK_BLOCK_REASON_LABELS = [
    "no block",
    "manual override active",
    "commissioning active",
    "cooling active",
    "frost protection active",
    "no heating request",
    "boiler fallback disabled",
    "a heat pump is still available",
    "heat-pump availability incomplete",
    "fallback cause not confirmed",
    "heat-pump stop not confirmed",
    "flow unavailable",
    "flow too low",
    "supply temperature unavailable",
    "boiler safety interlock",
  ];

  function getIncidentSimulationState() {
    return mockIncidentScenarios.buildPhaseState(
      state.incidentSimulation.scenario,
      state.incidentSimulation.phaseIndex,
      state.installation,
    );
  }

  function isIncidentScenarioActive() {
    return state.incidentSimulation.scenario !== "none";
  }

  function syncIncidentScenarioUrl() {
    if (typeof window === "undefined" || !window.location || !window.history?.replaceState) {
      return;
    }
    const url = new URL(window.location.href);
    if (isIncidentScenarioActive()) {
      url.searchParams.set("incident", state.incidentSimulation.scenario);
      url.searchParams.set("incidentStep", String(state.incidentSimulation.phaseIndex));
    } else {
      url.searchParams.delete("incident");
      url.searchParams.delete("incidentStep");
    }
    window.history.replaceState(window.history.state, "", url);
  }

  function resetIncidentActionState() {
    state.incidentSimulation.pendingAction = null;
    state.incidentSimulation.lastActionResults = {};
    state.incidentSimulation.rejectedCsrfActions = {};
  }

  function configureIncidentScenario(scenarioId, phaseIndex = 0, options = {}) {
    const selected = mockIncidentScenarios.getScenario(scenarioId);
    const compatible = mockIncidentScenarios.isCompatible(selected, state.installation);
    const next = compatible ? selected : mockIncidentScenarios.getScenario("none");
    const selectedPhase = mockIncidentScenarios.getPhase(next, phaseIndex);
    state.incidentSimulation.scenario = next.id;
    state.incidentSimulation.phaseIndex = selectedPhase.index;
    resetIncidentActionState();

    if (next.id !== "none") {
      state.scenario = next.base_scenario;
      if (next.required_hardware) {
        state.hardware = next.required_hardware;
        setText("text_sensor", "OpenQuatt Hardware Profile", state.hardware);
      }
      if (next.boiler_transport) {
        setText("select", "Boiler connection", next.boiler_transport);
      }
    } else {
      state.boiler = "off";
    }

    if (options.syncUrl !== false) {
      syncIncidentScenarioUrl();
    }
  }

  function initializeIncidentScenarioFromUrl() {
    if (typeof window === "undefined" || !window.location) {
      return;
    }
    const url = new URL(window.location.href);
    const selected = mockIncidentScenarios.getScenario(url.searchParams.get("incident"));
    if (selected.id === "none") {
      return;
    }
    if (selected.topology === "single" || selected.topology === "duo") {
      state.installation = selected.topology;
    }
    if (selected.required_hardware) {
      state.hardware = selected.required_hardware;
    }
    state.incidentSimulation.scenario = selected.id;
    state.incidentSimulation.phaseIndex = mockIncidentScenarios.getPhase(
      selected,
      Number(url.searchParams.get("incidentStep") || 0),
    ).index;
    state.scenario = selected.base_scenario;
  }

  function controlModeLabel(controlMode) {
    if (controlMode === 0) return "CM0 - Standby";
    if (controlMode === 1) return "CM1 - Flow / transition";
    if (controlMode === 2) return "CM2 - Heating - Heat Pump Only";
    if (controlMode === 3) return "CM3 - Heating - Heat Pump + Boiler";
    if (controlMode === 4) return "CM4 - Boiler Only - Fault fallback";
    return `CM${controlMode}`;
  }

  function applyIncidentScenario() {
    if (!isIncidentScenarioActive()) {
      return;
    }
    const { scenario, phase } = getIncidentSimulationState();
    const systemState = phase.system;
    const boilerCommandActive = systemState.boiler_command_active === true;
    const opentherm = scenario.boiler_transport === "OpenTherm";
    state.boiler = boilerCommandActive ? "on" : "off";

    if (scenario.required_hardware) {
      state.hardware = scenario.required_hardware;
      setText("text_sensor", "OpenQuatt Hardware Profile", state.hardware);
    }
    if (scenario.boiler_transport) {
      setText("select", "Boiler connection", scenario.boiler_transport);
    }

    setText("text_sensor", "Control Mode (Label)", controlModeLabel(systemState.control_mode));
    setBinary("Boiler command valid", true);
    setBinary("Boiler command active", boilerCommandActive);
    setBinary("Boiler active", boilerCommandActive);
    setText(
      "text_sensor",
      "Boiler command source",
      systemState.control_mode === 4
        ? "Heat-pump incident fallback"
        : systemState.control_mode === 3 ? "Boiler assist" : "None",
    );
    setText(
      "text_sensor",
      "Boiler block reason",
      FALLBACK_BLOCK_REASON_LABELS[systemState.fallback_block_reason] || "unknown",
    );
    setNumber("Boiler command target temperature", boilerCommandActive ? 45 : 0, "°C");
    setNumber("Boiler command requested power", boilerCommandActive ? 1800 : 0, "W");
    setNumber("Boiler Heat Power", boilerCommandActive ? 1800 : 0, "W");

    setBinary("OTB - Boiler Link Available", opentherm);
    setSwitch("OTB - Central Heating Command", opentherm && boilerCommandActive);
    setBinary("OTB - Central Heating Active", opentherm && boilerCommandActive);
    setBinary("OTB - Flame On", opentherm && boilerCommandActive);
    setNumber("OTB - Control Setpoint Command", opentherm && boilerCommandActive ? 45 : 0, "°C");
    setNumber("OTB - Relative Modulation", opentherm && boilerCommandActive ? 42 : 0, "%");

    let runningHeatPumpCount = 0;
    let heatPumpHeatPower = 0;
    let heatPumpInputPower = 0;
    phase.heat_pumps.forEach((hp) => {
      const name = `HP${hp.index}`;
      const running = hp.run_state === "running";
      const failureLabels = hp.incidents
        .filter((item) => item.runtime?.confirmed_active || item.runtime?.latched)
        .map((item) => item.definition?.key)
        .filter(Boolean);
      setText("text_sensor", `${name} - Active Failures List`, failureLabels.join(", ") || "None");
      setText("text_sensor", `${name} - Working Mode Label`, running ? "Heating" : "Standby");
      if (running) {
        runningHeatPumpCount += 1;
        heatPumpHeatPower += 3100;
        heatPumpInputPower += 940;
        setNumber(`${name} - Power Input`, 940, "W");
        setNumber(`${name} - Heat Power`, 3100, "W");
        setNumber(`${name} - COP`, 3.3, "");
        setNumber(`${name} - Compressor frequency`, 49, "Hz");
        setNumber(`${name} - Fan speed`, 640, "rpm");
        setNumber(`${name} - Flow`, 760, "L/h");
      } else {
        heatPumpInputPower += 5.2;
        setNumber(`${name} - Power Input`, 5.2, "W");
        setNumber(`${name} - Heat Power`, 0, "W");
        setNumber(`${name} - COP`, 0, "");
        setNumber(`${name} - Compressor frequency`, 0, "Hz");
        setNumber(`${name} - Fan speed`, 0, "rpm");
        setNumber(`${name} - Flow`, 0, "L/h");
      }
    });

    const boilerHeatPower = boilerCommandActive ? 1800 : 0;
    const flow = Number.isFinite(phase.entity_patch?.flowLph)
      ? Number(phase.entity_patch.flowLph)
      : runningHeatPumpCount > 0 ? 760 : boilerCommandActive ? 700 : 0;
    setNumber("Total Power Input", heatPumpInputPower, "W");
    setNumber("Total Heat Power", heatPumpHeatPower, "W");
    setNumber("Total COP", heatPumpInputPower >= 5 ? Number((heatPumpHeatPower / heatPumpInputPower).toFixed(1)) : 0, "");
    setNumber("System Heat Power", heatPumpHeatPower + boilerHeatPower, "W");
    setNumber("Flow average (Selected)", flow, "L/h");
    setNumber("Flow average (local)", flow, "L/h");
    setNumber("Controller Flow", Math.max(0, flow - 10), "L/h");
    setBinary("Lowflow fault active", flow < 250 && phase.system.fallback_block_reason === 12);
  }

  function parseDemoLogEntry(raw, index, total) {
    const normalized = String(raw || "").trim();
    const match = normalized.match(/^\[([A-Z]+)\]\[([^\]]+)\]\s*:?\s*(?:\[([^\]]+)\]\s*:?\s*)?(.*)$/);
    const level = match ? String(match[1] || "I").slice(0, 2) : "I";
    const header = match ? String(match[2] || "") : "";
    const body = match ? String(match[4] || normalized) : normalized;
    const tag = header.includes(":") ? header.slice(0, header.indexOf(":")) : header;
    const spacingMs = 45 * 1000;
    const offset = Math.max(0, total - index - 1) * spacingMs;
    return {
      seq: index + 1,
      ts: Date.now() - offset,
      level,
      tag,
      message: body,
      raw: normalized,
    };
  }

  function seedLogHistoryEntries() {
    const source = Array.isArray(window.__OQ_DEV_WEBSERVER_LOGS__) ? window.__OQ_DEV_WEBSERVER_LOGS__ : [];
    state.logHistoryEntries = source.map((entry, index) => parseDemoLogEntry(entry, index, source.length));
  }

  function appendLogHistoryEntry(raw) {
    const entry = parseDemoLogEntry(raw, state.logHistoryEntries.length, state.logHistoryEntries.length + 1);
    entry.seq = state.logHistoryEntries.length + 1;
    entry.ts = Date.now();
    state.logHistoryEntries = [...state.logHistoryEntries, entry].slice(-250);
  }

  function isSwitchEnabled(name) {
    return Boolean(getEntity("switch", name)?.value);
  }

  function clearOtaSimulation() {
    state.otaTimers.forEach((timer) => window.clearTimeout(timer));
    state.otaTimers = [];
  }

  function clearCommissioningTimers() {
    state.commissioningTimers.forEach((timer) => window.clearTimeout(timer));
    state.commissioningTimers = [];
  }

  function clearQuickFlowTestTimer() {
    if (state.quickFlowTestTimer) {
      window.clearTimeout(state.quickFlowTestTimer);
      state.quickFlowTestTimer = null;
    }
  }

  function resetHpWaterCalibrationMock(status = "IDLE") {
    state.commissioning.hpWaterCalibrationStatusText = status;
    state.commissioning.hpWaterCalibrationRemaining = 0;
    state.commissioning.hpWaterCalibrationPhase = 0;
    state.commissioning.hpWaterCalibrationSpread = NaN;
    state.commissioning.hpWaterCalibrationSupplyDelta = NaN;
    state.commissioning.hpWaterCalibrationStableProgress = 0;
    state.commissioning.hpWaterCalibrationStableRequired = 60;
    state.commissioning.hpWaterCalibrationResultReference = NaN;
    state.commissioning.hpWaterCalibrationResultSpreadBefore = NaN;
    state.commissioning.hpWaterCalibrationResultExpectedSpread = NaN;
    state.commissioning.hpWaterCalibrationResultSupplySource = "";
    state.commissioning.hpWaterCalibrationResultRawAverages.hp1In = NaN;
    state.commissioning.hpWaterCalibrationResultRawAverages.hp1Out = NaN;
    state.commissioning.hpWaterCalibrationResultRawAverages.hp2In = NaN;
    state.commissioning.hpWaterCalibrationResultRawAverages.hp2Out = NaN;
    state.commissioning.hpWaterCalibrationResultRawAverages.supply = NaN;
    setBinary("HP water calibration active", false);
    setText("text_sensor", "HP water calibration status", status);
    setNumber("HP water calibration remaining", 0, "s");
    setNumber("HP water calibration phase", 0, "");
    setNumber("HP water calibration spread", NaN, "\u00B0C");
    setNumber("HP water calibration supply delta", NaN, "\u00B0C");
    setNumber("HP water calibration stable window progress", 0, "s");
    setNumber("HP water calibration stable window required", 60, "s");
    setNumber("HP water calibration result reference", NaN, "\u00B0C");
    setNumber("HP water calibration result spread before", NaN, "\u00B0C");
    setNumber("HP water calibration result expected spread", NaN, "\u00B0C");
    setNumber("HP water calibration result HP1 water in raw average", NaN, "\u00B0C");
    setNumber("HP water calibration result HP1 water out raw average", NaN, "\u00B0C");
    setNumber("HP water calibration result HP2 water in raw average", NaN, "\u00B0C");
    setNumber("HP water calibration result HP2 water out raw average", NaN, "\u00B0C");
    setNumber("HP water calibration result supply raw average", NaN, "\u00B0C");
    setNumber("HP water calibration result supply offset", NaN, "\u00B0C");
    setText("text_sensor", "HP water calibration result supply source", "");
  }

  function currentWaterSupplySourceLabel() {
    const source = String(getEntity("select", "Water Supply Source")?.value || "Unknown");
    if (source !== "Local") return source;
    const local = String(getEntity("select", "Local Water Supply Temp Source")?.value || "");
    return local ? `Local - ${local}` : "Local";
  }

  const MOCK_HA_CALIBRATION_IDENTITY = "8f1a2b3c";

  function currentWaterSupplyCalibrationBridgeName() {
    const source = String(getEntity("select", "Water Supply Source")?.value || "");
    if (source === "CIC") return "Water Supply CIC Calibration Offset";
    if (source === "HA input") return "Water Supply HA Input Calibration Offset";
    const local = String(getEntity("select", "Local Water Supply Temp Source")?.value || "PT1000");
    return local === "DS18B20"
      ? "Water Supply DS18B20 Calibration Offset"
      : "Water Supply PT1000 Calibration Offset";
  }

  function syncWaterSupplyCalibrationForMockSource() {
    const bridgeName = currentWaterSupplyCalibrationBridgeName();
    const rawOffset = getEntity("number", bridgeName)?.value;
    const offset = Number(rawOffset);
    const identityMatches = bridgeName !== "Water Supply HA Input Calibration Offset" ||
      getEntity("text", "Water Supply HA Input Calibration Identity")?.value === MOCK_HA_CALIBRATION_IDENTITY;
    if (rawOffset !== null && rawOffset !== undefined && Number.isFinite(offset) && identityMatches) {
      setNumber("Water Supply Temperature Calibration Offset", offset, "\u00B0C");
      setBinary("Water Supply Temperature Calibration Required", false);
      setText("text_sensor", "Water Supply Temperature Calibration Status", `Calibrated: ${currentWaterSupplySourceLabel()}`);
      return;
    }
    setBinary("Water Supply Temperature Calibration Required", true);
    setText("text_sensor", "Water Supply Temperature Calibration Status", `Recalibration required: ${currentWaterSupplySourceLabel()}`);
  }

  function scheduleCommissioningStep(delay, callback) {
    const timer = window.setTimeout(() => {
      callback();
      applyScenario(state.scenario);
      updateSummary();
      notifyMockUpdated();
    }, delay);
    state.commissioningTimers.push(timer);
    return timer;
  }

  function roundToHundred(value) {
    return Math.max(0, Math.round(Number(value || 0) / 100) * 100);
  }

  function setCommissioningPhase(task, phase, extra = {}) {
    state.commissioning.task = task;
    state.commissioning.phase = phase;
    if (Number.isFinite(extra.boilerResult)) {
      state.commissioning.boilerResult = Number(extra.boilerResult);
    }
    if (Number.isFinite(extra.boilerConfidence)) {
      state.commissioning.boilerConfidence = Number(extra.boilerConfidence);
    }
    if (Number.isFinite(extra.flowKpSuggested)) {
      state.commissioning.flowKpSuggested = Number(extra.flowKpSuggested);
    }
    if (Number.isFinite(extra.flowKiSuggested)) {
      state.commissioning.flowKiSuggested = Number(extra.flowKiSuggested);
    }
  }

  function syncCommissioningEntities(single) {
    const cm100Active = Boolean(state.commissioning.cm100Active);
    if (!cm100Active) {
      return;
    }
    const task = String(state.commissioning.task || "none");
    const phase = String(state.commissioning.phase || "idle");
    const boilerTaskActive = task === "boiler" && cm100Active && !["done", "applied", "aborted", "refused"].includes(phase.toLowerCase());
    const autotuneTaskActive = task === "autotune" && cm100Active && !["done", "applied", "aborted", "refused"].includes(phase.toLowerCase());
    const airPurgeTaskActive = task === "purge" && cm100Active && !["done", "aborted", "refused"].includes(phase.toLowerCase());
    const manualFlowTaskActive = task === "manual-flow" && cm100Active && !["done", "aborted", "refused"].includes(phase.toLowerCase());
    const manualHpTaskActive = task === "manual-hp" && cm100Active && !["done", "aborted", "refused"].includes(phase.toLowerCase());
    const hpWaterCalibrationTaskActive = task === "hp-water-calibration" && cm100Active && !["done", "applied", "aborted", "refused"].includes(phase.toLowerCase());
    const commissioningLabel = "CM100 - Commissioning";
    const commissioningStatus = String(state.commissioning.globalStatus || "CM100 READY");

    setText("text_sensor", "Control Mode (Label)", commissioningLabel);
    setText("text_sensor", "Commissioning status", commissioningStatus);
    setBinary("CM100 active", cm100Active);
    setText("text_sensor", "Flow Mode", cm100Active
      ? (boilerTaskActive
        ? "CM100 boiler test"
        : autotuneTaskActive
          ? "CM100 flow autotune"
          : airPurgeTaskActive
            ? "CM100 air purge"
            : manualFlowTaskActive
              ? "MANUAL FLOW"
              : manualHpTaskActive
                ? "MANUAL HP"
                : hpWaterCalibrationTaskActive
                  ? "HP WATER CAL"
            : "CM100 idle")
      : "Gepauzeerd");

    if (cm100Active) {
      const purgeFlow = airPurgeTaskActive
        ? (phase === "pulse_hard" ? 980 : phase === "stabilize" ? 760 : 680)
        : 0;
      setNumber("Flow average (Selected)", boilerTaskActive ? 800 : autotuneTaskActive ? 790 : manualFlowTaskActive ? state.commissioning.manualFlowSetpoint - 8 : manualHpTaskActive ? 792 : hpWaterCalibrationTaskActive ? 735 : purgeFlow, "L/h");
      setNumber("Total Heat Power", boilerTaskActive ? Number(getEntity("sensor", "Boiler Heat Power")?.value || 0) : 0, "W");
      setNumber("Total Power Input", boilerTaskActive ? (single ? 560 : 640) : airPurgeTaskActive ? (single ? 48 : 78) : hpWaterCalibrationTaskActive ? (single ? 34 : 52) : single ? 12 : 18, "W");
      setBinary("Boiler active", task === "boiler" && ["boiler settling", "measuring"].includes(phase.toLowerCase()));
    }

    setBinary("Boiler power test active", boilerTaskActive);
    setText("text_sensor", "Boiler power test status", cm100Active
      ? String(state.commissioning.boilerStatusText || "IDLE")
      : "IDLE");
    setNumber("Boiler power test result", state.commissioning.boilerResult, "W");
    setNumber("Boiler power test confidence", state.commissioning.boilerConfidence, "%");

    setText("text_sensor", "Flow Autotune status", cm100Active
      ? String(state.commissioning.autotuneStatusText || "IDLE")
      : "IDLE");
    setNumber("Flow Autotune Kp suggested", state.commissioning.flowKpSuggested, "");
    setNumber("Flow Autotune Ki suggested", state.commissioning.flowKiSuggested, "");

    setBinary("Air purge active", airPurgeTaskActive);
    setText("text_sensor", "Air purge status", cm100Active
      ? String(state.commissioning.airPurgeStatusText || "IDLE")
      : String(state.commissioning.airPurgeStatusText || "IDLE"));
    setNumber("Air purge remaining", state.commissioning.airPurgeRemaining, "s");
    setNumber("Air purge phase", state.commissioning.airPurgePhase, "");
    setNumber("Air purge target iPWM", state.commissioning.airPurgeTargetIpwm, "iPWM");
    setBinary("Manual flow active", manualFlowTaskActive);
    setText("text_sensor", "Manual flow status", String(state.commissioning.manualFlowStatusText || "IDLE"));
    setNumber("Manual flow target iPWM", state.commissioning.manualFlowTargetIpwm, "iPWM");
    setBinary("Manual HP active", manualHpTaskActive);
    setText("text_sensor", "Manual HP status", String(state.commissioning.manualHpStatusText || "IDLE"));
    setText("text_sensor", "Manual HP guard status", String(state.commissioning.manualHpGuardStatusText || "Vrijgegeven"));
    const manualHp1Mode = String(getEntity("select", "Manual HP1 service mode")?.value || "Standby");
    const manualHp2Mode = String(getEntity("select", "Manual HP2 service mode")?.value || "Standby");
    setNumber("HP1 compressor level", manualHpTaskActive && manualHp1Mode !== "Standby" ? state.commissioning.manualHp1Level : 0, "");
    setNumber("HP2 compressor level", manualHpTaskActive && manualHp2Mode !== "Standby" ? state.commissioning.manualHp2Level : 0, "");
    if (manualHpTaskActive) {
      setText("text_sensor", "HP1 - Working Mode Label", manualHp1Mode);
      setText("text_sensor", "HP2 - Working Mode Label", manualHp2Mode);
    }
    setBinary("HP water calibration active", hpWaterCalibrationTaskActive);
    setText("text_sensor", "HP water calibration status", String(state.commissioning.hpWaterCalibrationStatusText || "IDLE"));
    setNumber("HP water calibration remaining", state.commissioning.hpWaterCalibrationRemaining, "s");
    setNumber("HP water calibration phase", state.commissioning.hpWaterCalibrationPhase, "");
    setNumber("HP water calibration spread", state.commissioning.hpWaterCalibrationSpread, "\u00B0C");
    setNumber("HP water calibration supply delta", state.commissioning.hpWaterCalibrationSupplyDelta, "\u00B0C");
    setNumber("HP water calibration stable window progress", state.commissioning.hpWaterCalibrationStableProgress, "s");
    setNumber("HP water calibration stable window required", state.commissioning.hpWaterCalibrationStableRequired, "s");
    setNumber("HP water calibration result reference", state.commissioning.hpWaterCalibrationResultReference, "\u00B0C");
    setNumber("HP water calibration result spread before", state.commissioning.hpWaterCalibrationResultSpreadBefore, "\u00B0C");
    setNumber("HP water calibration result expected spread", state.commissioning.hpWaterCalibrationResultExpectedSpread, "\u00B0C");
    setNumber("HP water calibration result HP1 water in raw average", state.commissioning.hpWaterCalibrationResultRawAverages.hp1In, "\u00B0C");
    setNumber("HP water calibration result HP1 water out raw average", state.commissioning.hpWaterCalibrationResultRawAverages.hp1Out, "\u00B0C");
    setNumber("HP water calibration result HP2 water in raw average", state.commissioning.hpWaterCalibrationResultRawAverages.hp2In, "\u00B0C");
    setNumber("HP water calibration result HP2 water out raw average", state.commissioning.hpWaterCalibrationResultRawAverages.hp2Out, "\u00B0C");
    setNumber("HP water calibration result supply raw average", state.commissioning.hpWaterCalibrationResultRawAverages.supply, "\u00B0C");
    setNumber("HP water calibration result supply offset", state.commissioning.hpWaterCalibrationSuggested.supply, "\u00B0C");
    setText("text_sensor", "HP water calibration result supply source", state.commissioning.hpWaterCalibrationResultSupplySource);
    setNumber("HP calibration HP1 water in offset suggested", state.commissioning.hpWaterCalibrationSuggested.hp1In, "\u00B0C");
    setNumber("HP calibration HP1 water out offset suggested", state.commissioning.hpWaterCalibrationSuggested.hp1Out, "\u00B0C");
    setNumber("HP calibration HP2 water in offset suggested", state.commissioning.hpWaterCalibrationSuggested.hp2In, "\u00B0C");
    setNumber("HP calibration HP2 water out offset suggested", state.commissioning.hpWaterCalibrationSuggested.hp2Out, "\u00B0C");
    setNumber("HP calibration supply temperature offset suggested", state.commissioning.hpWaterCalibrationSuggested.supply, "\u00B0C");
  }

  function generateAuthToken() {
    const bytes = new Uint8Array(12);
    window.crypto.getRandomValues(bytes);
    return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
  }

  function isAuthRecoveryWindowActive() {
    return Date.now() < Number(state.auth.recoveryUntil || 0);
  }

  function refreshAuthToken() {
    state.auth.csrfToken = generateAuthToken();
  }

  function refreshMqttToken() {
    state.mqtt.csrfToken = generateAuthToken();
  }

  function getAuthStatusPayload() {
    return {
      enabled: Boolean(state.auth.enabled),
      setup_window_active: isAuthRecoveryWindowActive(),
      username: String(state.auth.username || ""),
      source: String(state.auth.source || ""),
      csrf_token: String(state.auth.csrfToken || ""),
    };
  }

  function armAuthRecoveryWindow(durationMs = OPENQUATT_AUTH_RECOVERY_WINDOW_MS) {
    state.auth.recoveryUntil = Date.now() + Math.max(0, Number(durationMs) || 0);
    refreshAuthToken();
  }

  function parseAuthFormBody(init) {
    const body = init && typeof init === "object" ? init.body : "";
    if (typeof body === "string") {
      return new URLSearchParams(body);
    }
    if (body instanceof URLSearchParams) {
      return body;
    }
    return new URLSearchParams();
  }

  function parseBulkEntityFormBody(init) {
    return parseAuthFormBody(init);
  }

  function getMqttAgeSeconds(lastAt) {
    lastAt = Number(lastAt || 0);
    if (!lastAt) {
      return NaN;
    }
    return Math.max(0, Math.round((Date.now() - lastAt) / 1000));
  }

  function getMqttDewPointAgeSeconds() {
    return getMqttAgeSeconds(state.mqtt.lastDewPointAt);
  }

  function syncMqttInputAgeEntities() {
    setNumber("MQTT Cooling Dew Point Age", getMqttDewPointAgeSeconds(), "s");
    setNumber("MQTT Outside Temperature Age", getMqttAgeSeconds(state.mqtt.lastOutsideTemperatureAt), "s");
    setNumber("MQTT Room Temperature Age", getMqttAgeSeconds(state.mqtt.lastRoomTemperatureAt), "s");
    setNumber("MQTT Room Setpoint Age", getMqttAgeSeconds(state.mqtt.lastRoomSetpointAt), "s");
    setNumber("MQTT Heating Enable Age", getMqttAgeSeconds(state.mqtt.lastHeatingEnableAt), "s");
    setNumber("MQTT Cooling Enable Age", getMqttAgeSeconds(state.mqtt.lastCoolingEnableAt), "s");
  }

  function applyCoolingDewPointSourceSelection() {
    const source = String(getEntity("select", "Cooling Dew Point Source")?.value || "Auto");
    const ha = Number(getEntity("sensor", "HA - Cooling Dew Point")?.value);
    const mqtt = Number(getEntity("sensor", "MQTT Cooling Dew Point")?.value);
    const haValid = Boolean(getEntity("binary_sensor", "HA - Cooling Dew Point Valid")?.value);
    const mqttValid = Boolean(getEntity("binary_sensor", "MQTT Cooling Dew Point Valid")?.value);
    let selected = NaN;
    let guardMode = "Geen geldig dauwpunt";

    if (source === "Home Assistant") {
      if (haValid && Number.isFinite(ha)) {
        selected = ha;
        guardMode = "Dew point (HA)";
      }
    } else if (source === "MQTT") {
      if (mqttValid && Number.isFinite(mqtt)) {
        selected = mqtt;
        guardMode = "Dew point (MQTT)";
      }
    } else if (haValid && mqttValid && Number.isFinite(ha) && Number.isFinite(mqtt)) {
      selected = Math.max(ha, mqtt);
      guardMode = selected === mqtt ? "Dew point (MQTT)" : "Dew point (HA)";
    } else if (haValid && Number.isFinite(ha)) {
      selected = ha;
      guardMode = "Dew point (HA)";
    } else if (mqttValid && Number.isFinite(mqtt)) {
      selected = mqtt;
      guardMode = "Dew point (MQTT)";
    }

    setNumber("Cooling Dew Point (Selected)", selected, "°C");
    setText("text_sensor", "Cooling Guard Mode", guardMode);
    syncMqttInputAgeEntities();
  }

  const SERVICE_STATUS_ENTITY_MAP = {
    commissioningStatus: ["text_sensor", "Commissioning status"],
    cm100Active: ["binary_sensor", "CM100 active"],
    boilerPowerTestResult: ["sensor", "Boiler power test result"],
    boilerPowerTestConfidence: ["sensor", "Boiler power test confidence"],
    boilerPowerTestActive: ["binary_sensor", "Boiler power test active"],
    boilerPowerTestStatus: ["text_sensor", "Boiler power test status"],
    flowAutotuneStatus: ["text_sensor", "Flow Autotune status"],
    flowKpSuggested: ["number", "Flow Autotune Kp suggested"],
    flowKiSuggested: ["number", "Flow Autotune Ki suggested"],
    airPurgeActive: ["binary_sensor", "Air purge active"],
    airPurgeStatus: ["text_sensor", "Air purge status"],
    airPurgeRemaining: ["sensor", "Air purge remaining"],
    airPurgePhase: ["sensor", "Air purge phase"],
    airPurgeTargetIpwm: ["sensor", "Air purge target iPWM"],
    manualFlowActive: ["binary_sensor", "Manual flow active"],
    manualFlowStatus: ["text_sensor", "Manual flow status"],
    manualFlowTargetIpwm: ["sensor", "Manual flow target iPWM"],
    manualHpActive: ["binary_sensor", "Manual HP active"],
    manualHpStatus: ["text_sensor", "Manual HP status"],
    manualHpGuardStatus: ["text_sensor", "Manual HP guard status"],
    hpWaterCalibrationActive: ["binary_sensor", "HP water calibration active"],
    hpWaterCalibrationStatus: ["text_sensor", "HP water calibration status"],
    hpWaterCalibrationRemaining: ["sensor", "HP water calibration remaining"],
    hpWaterCalibrationPhase: ["sensor", "HP water calibration phase"],
    hpWaterCalibrationSpread: ["sensor", "HP water calibration spread"],
    hpWaterCalibrationSupplyDelta: ["sensor", "HP water calibration supply delta"],
    hpWaterCalibrationStableProgress: ["sensor", "HP water calibration stable window progress"],
    hpWaterCalibrationStableRequired: ["sensor", "HP water calibration stable window required"],
    hpWaterCalibrationResultReference: ["sensor", "HP water calibration result reference"],
    hpWaterCalibrationResultSpreadBefore: ["sensor", "HP water calibration result spread before"],
    hpWaterCalibrationResultExpectedSpread: ["sensor", "HP water calibration result expected spread"],
    hpWaterCalibrationResultHp1InRawAvg: ["sensor", "HP water calibration result HP1 water in raw average"],
    hpWaterCalibrationResultHp1OutRawAvg: ["sensor", "HP water calibration result HP1 water out raw average"],
    hpWaterCalibrationResultHp2InRawAvg: ["sensor", "HP water calibration result HP2 water in raw average"],
    hpWaterCalibrationResultHp2OutRawAvg: ["sensor", "HP water calibration result HP2 water out raw average"],
    hpWaterCalibrationResultSupplyRawAvg: ["sensor", "HP water calibration result supply raw average"],
    hpWaterCalibrationResultSupplyOffset: ["sensor", "HP water calibration result supply offset"],
    hpWaterCalibrationResultSupplySource: ["text_sensor", "HP water calibration result supply source"],
  };

  function handleServiceStatus() {
    const responseEntities = {};
    Object.entries(SERVICE_STATUS_ENTITY_MAP).forEach(([key, [domain, name]]) => {
      const entity = getEntity(domain, name);
      if (entity) {
        responseEntities[key] = clone(entity);
      }
    });
    return mockResponse(200, {
      ok: true,
      entities: responseEntities,
    });
  }

  function completePendingIncidentActionIfReady() {
    const pending = state.incidentSimulation.pendingAction;
    if (!pending) {
      return;
    }
    pending.readCount += 1;
    if (pending.readCount < pending.completeAfterReads) {
      return;
    }

    const selected = mockIncidentScenarios.getScenario(state.incidentSimulation.scenario);
    const targetIndex = selected.phases.findIndex((item) => item.id === pending.targetPhase);
    if (targetIndex >= 0) {
      state.incidentSimulation.phaseIndex = targetIndex;
    }
    state.incidentSimulation.lastActionResults[pending.hp] = {
      sequence: pending.actionId,
      request_id: pending.actionId,
      action: pending.action,
      ok: pending.ok,
      result: pending.result,
      at_ms: getIncidentSimulationState().phase.elapsed_s * 1000,
    };
    state.incidentSimulation.pendingAction = null;
    applyScenario(state.scenario);
    updateSummary();
    syncIncidentScenarioUrl();
  }

  function buildIncidentSnapshotPayload() {
    completePendingIncidentActionIfReady();
    const { phase } = getIncidentSimulationState();
    const heatPumps = clone(phase.heat_pumps);
    heatPumps.forEach((hp) => {
      hp.last_action_result = clone(state.incidentSimulation.lastActionResults[hp.index] || null);
    });
    return {
      schema_version: 1,
      catalog_version: 1,
      generated_at_s: Math.floor(Date.now() / 1000),
      action_csrf_token: state.incidentSimulation.actionCsrfToken,
      system: clone(phase.system),
      heat_pumps: heatPumps,
    };
  }

  function handleIncidentSnapshot() {
    const { phase } = getIncidentSimulationState();
    if (phase.incident_http_status !== 200) {
      return mockResponse(phase.incident_http_status, {
        error: "mock_incident_endpoint_unavailable",
      });
    }
    return mockResponse(200, buildIncidentSnapshotPayload());
  }

  function handleIncidentAction(pathname, init) {
    const action = pathname.endsWith("/retry-start")
      ? "start_failure_retry"
      : pathname.endsWith("/confirm-odu-power-cycle")
        ? "confirm_odu_power_cycle"
        : "";
    const params = parseAuthFormBody(init);
    const hp = Number(params.get("hp"));
    const token = String(params.get("csrf_token") || "");
    if (!action || (hp !== 1 && hp !== 2)) {
      return mockResponse(400, { accepted: false, result: "invalid_hp" });
    }
    if (token !== state.incidentSimulation.actionCsrfToken) {
      return mockResponse(403, { accepted: false, result: "forbidden" });
    }

    const { scenario, phase } = getIncidentSimulationState();
    const actionConfig = phase.actions?.[action];
    if (!actionConfig) {
      return mockResponse(409, {
        accepted: false,
        result: action === "start_failure_retry" ? "no_start_failure" : "no_cleared_manual_reset_latch",
      });
    }

    const rejectionKey = `${scenario.id}:${phase.id}:${action}`;
    if (actionConfig.reject_csrf_once && !state.incidentSimulation.rejectedCsrfActions[rejectionKey]) {
      state.incidentSimulation.rejectedCsrfActions[rejectionKey] = true;
      state.incidentSimulation.actionCsrfToken = `oq-mock-incident-token-${Date.now()}`;
      return mockResponse(403, { accepted: false, result: "forbidden" });
    }

    const actionId = state.incidentSimulation.nextActionId++;
    state.incidentSimulation.pendingAction = {
      hp,
      action,
      actionId,
      targetPhase: actionConfig.target_phase,
      completeAfterReads: Math.max(1, Number(actionConfig.complete_after_reads) || 1),
      readCount: 0,
      ok: actionConfig.ok === true,
      result: String(actionConfig.result || ""),
    };
    return mockResponse(202, {
      accepted: true,
      hp,
      action,
      action_id: actionId,
    });
  }

  function handleDecisionLog(url = null) {
    const nowMs = Date.now();
    if (url?.searchParams?.get("meta")) {
      return mockResponse(200, {
        ok: true,
        enabled: true,
        available: true,
        stored_events: 2840,
        capacity_events: 5120,
        retention_days: 7,
        oldest_epoch_s: Math.floor((nowMs - (7 * 24 * 60 * 60 * 1000)) / 1000),
        newest_epoch_s: Math.floor((nowMs - (60 * 60 * 1000)) / 1000),
        last_flush_epoch_s: Math.floor((nowMs - (18 * 60 * 1000)) / 1000),
        storage_bytes: 131072,
        write_count: 482,
      });
    }
    const decisionLogBootedAt = nowMs - (8 * 24 * 60 * 60 * 1000);
    const bootEpochS = Math.floor(decisionLogBootedAt / 1000);
    const uptimeS = Math.max(0, Math.floor((nowMs - decisionLogBootedAt) / 1000));
    let seq = 1;
    const events = [];
    const pushEvent = (ageMinutes, eventType, subject, reason, severity, cm, from, to, valueA = 0, valueB = 0, thresholdA = 0, durationS = 0, flags = 0) => {
      const epochS = Math.max(0, Math.floor((nowMs - (ageMinutes * 60000)) / 1000));
      events.push({
        seq: seq++,
        uptime_s: Math.max(0, epochS - bootEpochS),
        epoch_s: epochS,
        event_type: eventType,
        subject,
        reason,
        severity,
        cm,
        from,
        to,
        value_a: valueA,
        value_b: valueB,
        threshold_a: thresholdA,
        duration_s: durationS,
        flags,
      });
    };

    if (!isIncidentScenarioActive()) {
    if (state.scenario !== "cooling_limiter_log" && state.scenario !== "cooling_stop_reasons" && state.scenario !== "heating_stop_reasons") {
      pushEvent(6 * 24 * 60 + 9 * 60, "sticky_pump_run", "PUMP", "sticky_protection", "normal", 98, "standby", "active", 60, 0, 0, 60);
      pushEvent(4 * 24 * 60 + 13 * 60, "cooling_limited", "COOLING", "dew_stop", "limited", 5, "active", "limited", 0, 4, 2);
      pushEvent(4 * 24 * 60 + 12 * 60 + 42, "cooling_released", "COOLING", "keep_current", "normal", 5, "limited", "active", 3, 4, 2);
      pushEvent(2 * 24 * 60 + 7 * 60 + 20, "source_start", "HP2", "runtime_lead", "normal", 1, "standby", "active");
      pushEvent(2 * 24 * 60 + 7 * 60 + 5, "topology_change", "BOTH", "better_heat", "normal", 2, "single", "duo", 2, 1, 15);
      pushEvent(2 * 24 * 60 + 5 * 60 + 55, "defrost_seen_start", "HP1", "defrost_hold", "normal", 2, "active", "limited");
      pushEvent(2 * 24 * 60 + 5 * 60 + 48, "defrost_seen_clear", "HP1", "defrost_hold", "normal", 2, "limited", "active", 0, 0, 0, 420);
    }

    if (state.scenario === "summer_idle") {
      pushEvent(3 * 60, "sticky_pump_run", "PUMP", "sticky_protection", "normal", 98, "standby", "active", 60, 0, 0, 60);
      pushEvent(24 * 60 + 3 * 60, "sticky_pump_run", "PUMP", "sticky_protection", "normal", 98, "standby", "active", 60, 0, 0, 60);
    } else if (state.scenario === "heating_stop_reasons") {
      // One reduced-demand transition, followed by a complete heating stop with a short runtime hold.
      pushEvent(150, "flow_hold_start", "SYSTEM", "flow_preflow", "normal", 1, "standby", "limited");
      pushEvent(149, "flow_hold_clear", "SYSTEM", "flow_preflow", "normal", 1, "limited", "active", 2, 0, 0, 45);
      pushEvent(148, "topology_change", "HP2", "runtime_lead", "normal", 2, "idle", "single", 0, 3);
      pushEvent(148, "source_start", "HP2", "runtime_lead", "normal", 2, "idle", "active", 3);
      pushEvent(116, "topology_change", "BOTH", "better_heat", "normal", 2, "single", "duo", 2, 1, 15);
      pushEvent(115, "source_start", "HP1", "better_heat", "normal", 2, "idle", "active", 2);
      pushEvent(76, "topology_change", "HP2", "less_power", "normal", 2, "duo", "single", 0, 3);
      pushEvent(76, "source_stop", "HP1", "less_power", "normal", 2, "active", "idle", 2);
      pushEvent(29, "flow_hold_start", "SYSTEM", "flow_postflow", "normal", 1, "active", "limited");
      pushEvent(27, "topology_change", "SYSTEM", "heating_request_cleared", "normal", 1, "single", "idle");
      pushEvent(27, "source_stop", "HP2", "heating_request_cleared", "normal", 1, "active", "idle", 3);
      pushEvent(25, "flow_hold_clear", "SYSTEM", "flow_postflow", "normal", 1, "limited", "standby", 0, 0, 0, 120);
    } else if (isCandidateBlockedScenario()) {
      pushEvent(18, "source_start", "HP1", "runtime_lead", "normal", 2, "standby", "active", 4);
      pushEvent(9, "candidate_blocked", "HP2", "candidate_in_rest", "limited", 2, "standby", "blocked", 5, 420);
      pushEvent(4, "decision_hold", "HP2", "hold_active", "limited", 2, "standby", "blocked", 5);
    } else if (isFlowHoldScenario()) {
      pushEvent(16, "flow_hold_start", "SYSTEM", "flow_preflow", "normal", 1, "standby", "limited", 2);
      pushEvent(15, "decision_blocked", "SYSTEM", "flow_too_low", "limited", 1, "limited", "blocked", 180, 0, 250);
      pushEvent(9, "flow_hold_clear", "SYSTEM", "flow_too_low", "normal", 1, "limited", "active", 2, 0, 0, 420);
      pushEvent(6, "source_start", "HP1", "runtime_lead", "normal", 2, "standby", "active", 3);
      pushEvent(2, "flow_hold_start", "SYSTEM", "flow_postflow", "limited", 1, "active", "limited", 0);
    } else if (state.scenario === "cooling_startup_wait") {
      pushEvent(5, "flow_hold_start", "SYSTEM", "flow_preflow", "normal", 1, "standby", "limited", 5);
      pushEvent(4.25, "flow_hold_clear", "SYSTEM", "flow_preflow", "normal", 1, "limited", "active", 5, 0, 0, 45);
      pushEvent(4, "startup_inhibit_start", "HP2", "startup_inhibit", "normal", 2, "standby", "blocked", 2, 210, 240);
      pushEvent(3.5, "startup_inhibit_refresh", "HP1", "startup_inhibit", "normal", 5, "blocked", "blocked", 1, 180, 240, 30);
    } else if (state.scenario === "cooling_stop_reasons") {
      // Two complete cooling runs: one is stopped by dew-point protection, the other ends normally.
      pushEvent(96, "flow_hold_start", "SYSTEM", "flow_preflow", "normal", 1, "standby", "limited");
      pushEvent(95, "flow_hold_clear", "SYSTEM", "flow_preflow", "normal", 1, "limited", "active", 4, 0, 0, 45);
      pushEvent(94, "topology_change", "HP2", "runtime_lead", "normal", 5, "idle", "single", 0, 1);
      pushEvent(94, "source_start", "HP2", "runtime_lead", "normal", 5, "idle", "active", 1);
      pushEvent(61, "cooling_limited", "COOLING", "dew_stop", "limited", 5, "active", "limited", 0, 0, 12);
      pushEvent(60, "topology_change", "SYSTEM", "dew_stop", "normal", 5, "single", "idle");
      pushEvent(60, "source_stop", "HP2", "dew_stop", "normal", 5, "active", "idle", 1);
      pushEvent(43, "topology_change", "HP1", "runtime_lead", "normal", 5, "idle", "single", 1);
      pushEvent(43, "source_start", "HP1", "runtime_lead", "normal", 5, "idle", "active", 1);
      pushEvent(18, "topology_change", "SYSTEM", "cooling_request_cleared", "normal", 1, "single", "idle");
      pushEvent(18, "source_stop", "HP1", "cooling_request_cleared", "normal", 1, "active", "idle", 1);
      pushEvent(16, "flow_hold_start", "SYSTEM", "flow_postflow", "limited", 1, "active", "limited");
      pushEvent(12, "flow_hold_clear", "SYSTEM", "flow_postflow", "normal", 1, "limited", "standby", 0, 0, 0, 180);
    } else if (state.scenario === "cooling_limiter_log") {
      const logUptimeS = 6608;
      const pushLogEvent = (eventUptimeS, ...eventArgs) => {
        pushEvent((logUptimeS - eventUptimeS) / 60, ...eventArgs);
      };
      pushLogEvent(186, "flow_hold_start", "SYSTEM", "flow_preflow", "normal", 1, "standby", "limited");
      pushLogEvent(236, "flow_hold_clear", "SYSTEM", "flow_preflow", "normal", 1, "limited", "active", 5, 0, 0, 50);
      pushLogEvent(249, "topology_change", "HP2", "runtime_lead", "normal", 5, "idle", "single", 0, 1);
      pushLogEvent(249, "source_start", "HP2", "runtime_lead", "normal", 5, "idle", "active", 1);
      pushLogEvent(891, "cooling_limited", "COOLING", "capacity_cap", "limited", 5, "active", "limited", 2, 3, 119);
      pushLogEvent(976, "cooling_limited", "COOLING", "projected_floor", "limited", 5, "active", "limited", 1, 3, 103);
      pushLogEvent(1036, "cooling_limited", "COOLING", "capacity_cap", "limited", 5, "active", "limited", 1, 3, 97);
      pushLogEvent(1351, "cooling_limited", "COOLING", "projected_floor", "limited", 5, "active", "limited", 1, 2, 53);
      pushLogEvent(1411, "cooling_limited", "COOLING", "capacity_cap", "limited", 5, "active", "limited", 1, 2, 60);
      pushLogEvent(1471, "cooling_limited", "COOLING", "projected_floor", "limited", 5, "active", "limited", 1, 1, 36);
      pushLogEvent(2056, "cooling_limited", "COOLING", "dew_stop", "limited", 5, "active", "limited", 0, 0, 12);
      pushLogEvent(2069, "topology_change", "SYSTEM", "dew_stop", "normal", 5, "single", "idle");
      pushLogEvent(2069, "source_stop", "HP2", "dew_stop", "normal", 5, "active", "idle", 1);
      pushLogEvent(2081, "cooling_limited", "COOLING", "restart_wait", "limited", 5, "active", "limited", 0, 0, 15);
      pushLogEvent(2156, "cooling_limited", "COOLING", "capacity_cap", "limited", 5, "active", "limited", 1, 3, 248);
      pushLogEvent(2169, "topology_change", "HP1", "runtime_lead", "normal", 5, "idle", "single", 1);
      pushLogEvent(2169, "source_start", "HP1", "runtime_lead", "normal", 5, "idle", "active", 1);
      pushLogEvent(2366, "cooling_released", "COOLING", "keep_current", "normal", 5, "limited", "active");
      pushLogEvent(2366, "flow_hold_start", "SYSTEM", "flow_postflow", "limited", 1, "standby", "limited");
      pushLogEvent(2494, "topology_change", "SYSTEM", "cooling_request_cleared", "normal", 1, "single", "idle");
      pushLogEvent(2494, "source_stop", "HP1", "cooling_request_cleared", "normal", 1, "active", "idle", 1);
      pushLogEvent(2506, "flow_hold_clear", "SYSTEM", "flow_postflow", "normal", 1, "limited", "standby", 0, 0, 0, 140);
    } else if (isCoolingScenario()) {
      pushEvent(160, "source_start", "HP1", "runtime_lead", "normal", 5, "standby", "active");
      const coolingPauseReason = state.scenario === "cooling_limited"
        ? "dew_stop"
        : state.scenario === "cooling_buffer_stop"
        ? "buffer_stop"
        : "cooling_limiter";
      pushEvent(112, "cooling_limited", "COOLING", coolingPauseReason, coolingPauseReason === "buffer_stop" ? "normal" : "limited", 5, "active", "limited", 0, 5, 2);
      pushEvent(82, "decision_hold", "HP1", "soft_guard", "limited", 5, "active", "active", 1, 3, 2);
      pushEvent(48, "cooling_released", "COOLING", "keep_current", "normal", 5, "limited", "active", 3, 5, 2);
      pushEvent(22, "source_stop", "HP1", "cooling_request_cleared", "normal", 5, "active", "standby");
    } else {
      pushEvent(19 * 60 + 35, "decision_hold", "HP2", "min_rest_active", "limited", 1, "standby", "blocked", 12);
      pushEvent(17 * 60 + 15, "source_start", "HP1", "runtime_lead", "normal", 1, "standby", "active");
      pushEvent(16 * 60 + 50, "topology_change", "BOTH", "better_heat", "normal", 2, "single", "duo", 2, 1, 15);
      pushEvent(14 * 60 + 35, "defrost_seen_start", "HP2", "defrost_hold", "normal", 2, "active", "limited");
      pushEvent(14 * 60 + 28, "defrost_seen_clear", "HP2", "defrost_hold", "normal", 2, "limited", "active", 0, 0, 0, 420);
      pushEvent(12 * 60 + 40, "boiler_assist_start", "CV", "boiler_assist", "normal", 3, "standby", "active", 351);
      pushEvent(12 * 60 + 5, "boiler_assist_stop", "CV", "less_power", "normal", 2, "active", "standby", 363);
      pushEvent(10 * 60 + 30, "source_stop", "HP2", "less_power", "normal", 1, "active", "standby");
      pushEvent(75, "attention_pattern", "HP1", "start_stop_rate_high", "attention", 1, "active", "active", 5, 60, 4);
    }

    if (state.boiler === "on" && !isCoolingScenario() && state.scenario !== "summer_idle" && !isFlowHoldScenario() && !isCandidateBlockedScenario()) {
      pushEvent(42, "boiler_assist_start", "CV", "boiler_assist", "normal", 3, "standby", "active", 348);
    }
    }

    if (isIncidentScenarioActive()) {
      const { phase } = getIncidentSimulationState();
      mockIncidentScenarios
        .collectEvents(state.incidentSimulation.scenario, state.incidentSimulation.phaseIndex)
        .forEach((item) => {
          const ageMinutes = Math.max(0.05, (phase.elapsed_s - item.at_s + 3) / 60);
          pushEvent(
            ageMinutes,
            item.event_type,
            item.subject,
            item.reason,
            item.severity,
            item.cm,
            item.from,
            item.to,
            item.value_a,
            item.value_b,
            item.threshold_a,
            item.duration_s,
            item.flags,
          );
        });
    }

    events.sort((left, right) => left.uptime_s - right.uptime_s);
    events.forEach((event, index) => {
      event.seq = index + 1;
    });

    const buckets = [];
    for (let ageHours = 0; ageHours < 168; ageHours += 1) {
      const hourStartUptimeS = Math.max(0, uptimeS - (ageHours * 3600));
      const bucketEvents = events.filter((event) => event.uptime_s >= hourStartUptimeS && event.uptime_s < hourStartUptimeS + 3600);
      if (!bucketEvents.length) {
        continue;
      }
      buckets.push({
        hour_start_uptime_s: hourStartUptimeS,
        hour_start_epoch_s: Math.floor((nowMs - (ageHours * 3600000)) / 3600000) * 3600,
        source: ageHours === 0 ? "ram" : "flash",
        starts_hp1: bucketEvents.filter((event) => event.event_type === "source_start" && event.subject === "HP1").length,
        starts_hp2: bucketEvents.filter((event) => event.event_type === "source_start" && event.subject === "HP2").length,
        stops_hp1: bucketEvents.filter((event) => event.event_type === "source_stop" && event.subject === "HP1").length,
        stops_hp2: bucketEvents.filter((event) => event.event_type === "source_stop" && event.subject === "HP2").length,
        topology_single_count: bucketEvents.filter((event) => event.event_type === "topology_change" && event.to === "single").length,
        topology_duo_count: bucketEvents.filter((event) => event.event_type === "topology_change" && event.to === "duo").length,
        cv_assist_start_count: bucketEvents.filter((event) => event.event_type === "boiler_assist_start").length,
        cv_assist_stop_count: bucketEvents.filter((event) => event.event_type === "boiler_assist_stop").length,
        cooling_limited_count: bucketEvents.filter((event) => event.event_type === "cooling_limited").length,
        cooling_released_count: bucketEvents.filter((event) => event.event_type === "cooling_released").length,
        dewpoint_stop_count: bucketEvents.filter((event) => event.reason === "dew_stop").length,
        sticky_run_count: bucketEvents.filter((event) => event.event_type === "sticky_pump_run").length,
        defrost_seen_count_hp1: bucketEvents.filter((event) => event.event_type === "defrost_seen_start" && event.subject === "HP1").length,
        defrost_seen_count_hp2: bucketEvents.filter((event) => event.event_type === "defrost_seen_start" && event.subject === "HP2").length,
        defrost_hold_count_hp1: bucketEvents.filter((event) => event.event_type === "decision_hold" && event.reason === "defrost_hold" && event.subject === "HP1").length,
        defrost_hold_count_hp2: bucketEvents.filter((event) => event.event_type === "decision_hold" && event.reason === "defrost_hold" && event.subject === "HP2").length,
        defrost_boost_count_hp1: 0,
        defrost_boost_count_hp2: 0,
        attention_count: bucketEvents.filter((event) => event.severity === "attention").length,
      });
    }

    return mockResponse(200, {
      ok: true,
      storage: {
        events: "psram",
        buckets: "psram",
        event_capacity: 5120,
        event_requested: 5120,
        bucket_capacity: 168,
        bucket_requested: 168,
        event_archive: "flash",
        flash_enabled: true,
      },
      meta: {
        event_record_size: 32,
        bucket_record_size: 56,
        event_count: events.length,
        dropped_count: 0,
        boot_epoch_s: bootEpochS,
        uptime_s: uptimeS,
        internal_heap_free: 87000,
        internal_heap_min: 72000,
        psram_free: 7400000,
        flash_stored_events: events.length,
        flash_oldest_epoch_s: Math.floor((nowMs - (7 * 24 * 60 * 60 * 1000)) / 1000),
        flash_newest_epoch_s: Math.floor((nowMs - (60 * 60 * 1000)) / 1000),
        flash_last_flush_epoch_s: Math.floor((nowMs - (18 * 60 * 1000)) / 1000),
        flash_storage_bytes: 131072,
        flash_write_count: 482,
      },
      events,
      buckets: buckets.sort((left, right) => left.hour_start_uptime_s - right.hour_start_uptime_s),
    });
  }

  function handleBulkEntities(init) {
    const params = parseBulkEntityFormBody(init || {});
    const lines = String(params.get("entities") || "").split(/\r?\n/);
    const responseEntities = {};
    const missing = [];

    lines.forEach((line) => {
      const [key, domain, name] = String(line || "").split("\t");
      const normalizedKey = String(key || "").trim();
      const normalizedDomain = String(domain || "").trim();
      const normalizedName = String(name || "").trim();
      if (!normalizedKey || !normalizedDomain || !normalizedName) {
        return;
      }

      const entity = getEntity(normalizedDomain, normalizedName);
      if (entity) {
        responseEntities[normalizedKey] = clone(entity);
      } else {
        missing.push(normalizedKey);
      }
    });

    return mockResponse(200, {
      entities: responseEntities,
      missing,
    });
  }

  function makeAuthResponse(status, payload) {
    return mockResponse(status, payload);
  }

  function handleAuthStatus() {
    return makeAuthResponse(200, getAuthStatusPayload());
  }

  function handleAuthChange(init) {
    const params = parseAuthFormBody(init);
    const status = getAuthStatusPayload();
    if (params.get("csrf_token") !== status.csrf_token) {
      return makeAuthResponse(403, { ok: false, error: "forbidden" });
    }

    const currentPassword = String(params.get("current_password") || "");
    const newUsername = String(params.get("new_username") || "").trim();
    const newPassword = String(params.get("new_password") || "");

    if (!state.auth.enabled && !status.setup_window_active) {
      return makeAuthResponse(403, { ok: false, error: "setup_window_required" });
    }
    if (state.auth.enabled && currentPassword !== state.auth.password) {
      return makeAuthResponse(403, { ok: false, error: "invalid_current_password" });
    }
    if (!newUsername || !newPassword) {
      return makeAuthResponse(400, { ok: false, error: "missing_fields" });
    }

    state.auth.enabled = true;
    state.auth.username = newUsername;
    state.auth.password = newPassword;
    state.auth.source = "runtime-credentials";
    state.auth.recoveryUntil = 0;
    refreshAuthToken();
    return makeAuthResponse(200, {
      ok: true,
      status: getAuthStatusPayload(),
    });
  }

  function handleAuthDisable(init) {
    const params = parseAuthFormBody(init);
    const status = getAuthStatusPayload();
    if (params.get("csrf_token") !== status.csrf_token) {
      return makeAuthResponse(403, { ok: false, error: "forbidden" });
    }
    if (!state.auth.enabled) {
      return makeAuthResponse(409, { ok: false, error: "already_disabled" });
    }
    if (String(params.get("current_password") || "") !== state.auth.password) {
      return makeAuthResponse(403, { ok: false, error: "invalid_current_password" });
    }

    state.auth.enabled = false;
    state.auth.username = "";
    state.auth.password = "";
    state.auth.source = "runtime-disabled";
    state.auth.recoveryUntil = 0;
    refreshAuthToken();
    return makeAuthResponse(200, {
      ok: true,
      status: getAuthStatusPayload(),
    });
  }

  function getApiSecurityStatusPayload() {
    return {
      transport_active: Boolean(state.apiSecurity.transportActive),
      key_present: Boolean(state.apiSecurity.keyPresent),
      provisioning_pending: Boolean(state.apiSecurity.provisioningPending),
      provisioning_closed: Boolean(state.apiSecurity.provisioningClosed),
    };
  }

  function handleApiSecurityStatus() {
    return makeAuthResponse(200, getApiSecurityStatusPayload());
  }

  function getMqttStatusPayload() {
    const broker = String(state.mqtt.broker || "").trim();
    const port = Number(state.mqtt.port || 1883);
    const inputTopics = {
      cooling_dew_point: String(state.mqtt.inputTopics?.cooling_dew_point || ""),
      outside_temperature: String(state.mqtt.inputTopics?.outside_temperature || ""),
      room_temperature: String(state.mqtt.inputTopics?.room_temperature || ""),
      room_setpoint: String(state.mqtt.inputTopics?.room_setpoint || ""),
      heating_enable: String(state.mqtt.inputTopics?.heating_enable || ""),
      cooling_enable: String(state.mqtt.inputTopics?.cooling_enable || ""),
    };
    const inputEnabled = {
      cooling_dew_point: state.mqtt.inputEnabled?.cooling_dew_point !== false,
      outside_temperature: state.mqtt.inputEnabled?.outside_temperature !== false,
      room_temperature: state.mqtt.inputEnabled?.room_temperature !== false,
      room_setpoint: state.mqtt.inputEnabled?.room_setpoint !== false,
      heating_enable: state.mqtt.inputEnabled?.heating_enable !== false,
      cooling_enable: state.mqtt.inputEnabled?.cooling_enable !== false,
    };
    const inputRetained = {
      cooling_dew_point: Boolean(state.mqtt.inputRetained?.cooling_dew_point),
      outside_temperature: Boolean(state.mqtt.inputRetained?.outside_temperature),
      room_temperature: Boolean(state.mqtt.inputRetained?.room_temperature),
      room_setpoint: Boolean(state.mqtt.inputRetained?.room_setpoint),
      heating_enable: Boolean(state.mqtt.inputRetained?.heating_enable),
      cooling_enable: Boolean(state.mqtt.inputRetained?.cooling_enable),
    };
    const inputAcceptRetained = {
      cooling_dew_point: false,
      outside_temperature: false,
      room_temperature: false,
      room_setpoint: state.mqtt.inputAcceptRetained?.room_setpoint !== false,
      heating_enable: state.mqtt.inputAcceptRetained?.heating_enable !== false,
      cooling_enable: state.mqtt.inputAcceptRetained?.cooling_enable !== false,
    };
    return {
      enabled: Boolean(state.mqtt.enabled),
      connected: Boolean(state.mqtt.enabled && state.mqtt.connected && broker),
      broker,
      port: Number.isInteger(port) && port >= 1 && port <= 65535 ? port : 1883,
      username: String(state.mqtt.username || ""),
      password_set: Boolean(state.mqtt.passwordSet),
      dew_point_topic: inputTopics.cooling_dew_point,
      input_topics: inputTopics,
      input_enabled: inputEnabled,
      input_retained: inputRetained,
      input_accept_retained: inputAcceptRetained,
      non_retained_stateful_timeout_s: 1800,
      source: String(state.mqtt.source || ""),
      csrf_token: String(state.mqtt.csrfToken || ""),
    };
  }

  function handleMqttStatus() {
    return makeAuthResponse(200, getMqttStatusPayload());
  }

  function handleMqttSave(init) {
    const params = parseAuthFormBody(init);
    const status = getMqttStatusPayload();
    if (params.get("csrf_token") !== status.csrf_token) {
      return makeAuthResponse(403, { ok: false, error: "forbidden" });
    }

    const enabled = String(params.get("enabled") || "") === "true";
    const broker = String(params.get("broker") || "").trim();
    const rawPort = Number(String(params.get("port") || "").trim());
    const username = String(params.get("username") || "").trim();
    const password = String(params.get("password") || "");
    const clearPassword = String(params.get("clear_password") || "") === "true";

    if (!Number.isInteger(rawPort) || rawPort < 1 || rawPort > 65535) {
      return makeAuthResponse(400, { ok: false, error: "invalid_port" });
    }
    if (enabled && !broker) {
      return makeAuthResponse(400, { ok: false, error: "missing_broker" });
    }

    state.mqtt.enabled = enabled;
    state.mqtt.connected = enabled && Boolean(broker);
    state.mqtt.broker = broker;
    state.mqtt.port = rawPort;
    state.mqtt.username = username;
    if (clearPassword) {
      state.mqtt.passwordSet = false;
    } else if (password) {
      state.mqtt.passwordSet = true;
    }
    state.mqtt.source = enabled ? "runtime-enabled" : "runtime-disabled";
    refreshMqttToken();

    return makeAuthResponse(200, {
      ok: true,
      status: getMqttStatusPayload(),
    });
  }

  function handleMqttInputSave(init) {
    const params = parseAuthFormBody(init);
    const status = getMqttStatusPayload();
    if (params.get("csrf_token") !== status.csrf_token) {
      return makeAuthResponse(403, { ok: false, error: "forbidden" });
    }

    const input = String(params.get("input") || "");
    if (!Object.prototype.hasOwnProperty.call(status.input_enabled, input)) {
      return makeAuthResponse(400, { ok: false, error: "invalid_input" });
    }

    state.mqtt.inputEnabled[input] = String(params.get("enabled") || "") === "true";
    state.mqtt.source = state.mqtt.enabled ? "runtime-enabled" : "runtime-disabled";
    refreshMqttToken();

    return makeAuthResponse(200, {
      ok: true,
      enabled: state.mqtt.inputEnabled[input],
      connected: Boolean(state.mqtt.enabled && state.mqtt.connected),
    });
  }

  function handleMqttInputRetainedSave(init) {
    const params = parseAuthFormBody(init);
    const status = getMqttStatusPayload();
    if (params.get("csrf_token") !== status.csrf_token) {
      return makeAuthResponse(403, { ok: false, error: "forbidden" });
    }

    const input = String(params.get("input") || "");
    if (!["room_setpoint", "heating_enable", "cooling_enable"].includes(input)) {
      return makeAuthResponse(400, { ok: false, error: "invalid_input" });
    }

    const acceptRetained = String(params.get("accept_retained") || "") === "true";
    state.mqtt.inputAcceptRetained[input] = acceptRetained;
    if (!acceptRetained) {
      state.mqtt.inputRetained[input] = false;
    }
    state.mqtt.source = state.mqtt.enabled ? "runtime-enabled" : "runtime-disabled";
    refreshMqttToken();

    return makeAuthResponse(200, {
      ok: true,
      accept_retained: acceptRetained,
      connected: Boolean(state.mqtt.enabled && state.mqtt.connected),
    });
  }

  function getMockReleaseUrl(channel) {
    return channel === "dev"
      ? "https://github.com/OpenQuatt/OpenQuatt/releases/download/dev-latest/manifest.json"
      : "https://github.com/OpenQuatt/OpenQuatt/releases/latest";
  }

  function syncOverviewTelemetry(single) {
    const hp1Outside = Number(getEntity("sensor", "HP1 - Outside temperature")?.value);
    const hp2Outside = Number(getEntity("sensor", "HP2 - Outside temperature")?.value);
    const hpOutlet = Number(getEntity("sensor", single ? "HP1 - Water out temperature" : "HP2 - Water out temperature")?.value);
    const supplyTemp = Number(getEntity("sensor", "Water Supply Temp (Selected)")?.value);
    const flowLph = Number(getEntity("sensor", "Flow average (Selected)")?.value);
    const totalHeat = Number(getEntity("sensor", "Total Heat Power")?.value);
    const totalCooling = Number(getEntity("sensor", "Total Cooling Power")?.value);
    const totalPower = Number(getEntity("sensor", "Total Power Input")?.value);
    const strategy = String(getEntity("select", "Heating Control Mode")?.value || "");
    const roomTemp = Number(getEntity("sensor", "Room Temperature (Selected)")?.value);
    const roomSetpoint = Number(getEntity("sensor", "Room Setpoint (Selected)")?.value);
    const housePower = Number(getEntity("number", "Rated maximum house power")?.value);
    const houseCold = Number(getEntity("number", "House cold temp")?.value);
    const outdoorMax = Number(getEntity("number", "Maximum heating outdoor temperature")?.value);
    const kp = Number(getEntity("number", "Power House temperature reaction")?.value);
    const selectedOutside = single || Number.isNaN(hp2Outside)
      ? hp1Outside
      : Number(((hp1Outside + hp2Outside) / 2).toFixed(1));

    let houseModel = 0;
    if (
      !Number.isNaN(selectedOutside)
      && !Number.isNaN(houseCold)
      && !Number.isNaN(outdoorMax)
      && !Number.isNaN(housePower)
      && outdoorMax > houseCold
    ) {
      const ratio = Math.max(0, Math.min(1, (outdoorMax - selectedOutside) / (outdoorMax - houseCold)));
      houseModel = Math.round(housePower * ratio);
    }

    const roomDelta = Number.isNaN(roomSetpoint) || Number.isNaN(roomTemp) ? 0 : roomSetpoint - roomTemp;
    const roomCorrection = Number.isNaN(kp) ? 0 : Math.round(Math.max(-1500, Math.min(1500, roomDelta * kp)));
    const powerHouseRequested = Math.max(0, Math.round(houseModel + roomCorrection));
    const coolingScenario = isCoolingScenario();
    const summerIdleScenario = isSummerIdleScenario();
    const strategyRequested = coolingScenario || summerIdleScenario
      ? 0
      : Math.max(0, Math.round(strategy === "Power House" ? powerHouseRequested : totalHeat || 0));

    let capacity = 0;
    if (state.scenario === "idle" || summerIdleScenario) {
      capacity = single ? 2800 : 5200;
    } else if (state.scenario === "heating") {
      capacity = single ? 3200 : 5200;
    } else if (state.scenario === "dual") {
      capacity = 5200;
    } else if (state.scenario === "defrost") {
      capacity = single ? 1800 : 3200;
    } else if (coolingScenario) {
      capacity = single ? 2600 : 4200;
    }

    setNumber("Outside Temperature (Selected)", selectedOutside, "°C");
    setNumber("Power House – P_house", coolingScenario || summerIdleScenario ? 0 : houseModel, "W");
    setNumber("Power House – P_req", coolingScenario || summerIdleScenario ? 0 : powerHouseRequested, "W");
    setNumber("Strategy requested power", strategyRequested, "W");
    setNumber("HP capacity (W)", capacity, "W");
    setNumber("HP deficit (W)", Math.max(0, strategyRequested - capacity), "W");

    const boilerAssistEnabled = isSwitchEnabled("Boiler assist enabled");
    const boilerConnection = String(getEntity("select", "Boiler connection")?.value || "R1");
    const openthermSelected = boilerConnection === "OpenTherm";
    const otbLinkAvailable = openthermSelected && state.hardware === "heatpump_controller_q";
    const boilerRequested = boilerAssistEnabled && state.boiler === "on";
    const boilerActive = boilerRequested && (!openthermSelected || otbLinkAvailable);
    const boilerDelta = Number.isNaN(supplyTemp) || Number.isNaN(hpOutlet) ? 0 : supplyTemp - hpOutlet;
    const boilerHeat = boilerActive && !Number.isNaN(flowLph)
      ? Number(Math.max(0, (flowLph / 3600) * 4186 * boilerDelta).toFixed(1))
      : 0;
    const systemHeat = Math.max(0, Number((Number(totalHeat || 0) + boilerHeat).toFixed(0)));
    const electricalDaily = state.scenario === "idle" || summerIdleScenario ? 3.1 : state.scenario === "defrost" ? 6.4 : coolingScenario ? (single ? 6.8 : 8.1) : single ? 7.2 : 8.6;
    const heatpumpDaily = state.scenario === "idle" || summerIdleScenario ? 9.4 : state.scenario === "defrost" ? 18.2 : coolingScenario ? (single ? 24.6 : 31.8) : single ? 28.4 : 36.9;
    const coolingElectricalDaily = coolingScenario ? (single ? 1.8 : 2.4) : 0.0;
    const coolingDaily = coolingScenario ? (single ? 7.1 : 9.3) : 0.0;
    const boilerDaily = boilerActive ? 2.7 : 0.0;
    const systemDaily = Number((heatpumpDaily + boilerDaily).toFixed(1));
    const heatpumpCopDaily = electricalDaily > 0 ? Number((heatpumpDaily / electricalDaily).toFixed(2)) : 0;
    const heatpumpEerDaily = coolingElectricalDaily > 0 ? Number((coolingDaily / coolingElectricalDaily).toFixed(2)) : 0;
    const electricalCumulative = state.energyCountersReset ? 0 : single ? 286.4 : 469.5;
    const heatpumpCumulative = state.energyCountersReset ? 0 : single ? 1208.7 : 2048.6;
    const coolingElectricalCumulative = state.energyCountersReset ? 0 : coolingScenario ? (single ? 28.6 : 41.9) : 0.0;
    const coolingCumulative = state.energyCountersReset ? 0 : coolingScenario ? (single ? 109.4 : 163.7) : 0.0;
    const boilerCumulative = state.energyCountersReset ? 0 : boilerActive ? 114.8 : 0.0;
    const systemCumulative = Number((heatpumpCumulative + boilerCumulative).toFixed(1));
    const heatpumpCopCumulative = electricalCumulative > 0 ? Number((heatpumpCumulative / electricalCumulative).toFixed(2)) : 0;
    const heatpumpEerCumulative = coolingElectricalCumulative > 0 ? Number((coolingCumulative / coolingElectricalCumulative).toFixed(2)) : 0;
    const heatingElectricalDaily = Math.max(0, Number((electricalDaily - coolingElectricalDaily).toFixed(1)));
    const heatingElectricalCumulative = Math.max(0, Number((electricalCumulative - coolingElectricalCumulative).toFixed(1)));
    const totalCoolingPower = coolingScenario ? Math.max(0, Number(totalCooling || 0)) : 0;
    const totalEer = (coolingScenario && coolingElectricalDaily > 0)
      ? Number((coolingDaily / coolingElectricalDaily).toFixed(2))
      : 0;

    setNumber("Boiler Heat Power", boilerHeat, "W");
    setBinary("Boiler active", boilerActive);
    setBinary("Boiler command valid", true);
    setBinary("Boiler command active", boilerActive);
    setBinary("OTB - Boiler Link Available", otbLinkAvailable);
    setBinary("OTB - Central Heating Active", otbLinkAvailable && boilerActive);
    setBinary("OTB - Domestic Hot Water Active", false);
    setBinary("OTB - Flame On", otbLinkAvailable && boilerActive);
    setNumber("Boiler command target temperature", boilerRequested ? 45 : 0, "°C");
    setNumber("Boiler command requested power", boilerRequested ? 1800 : 0, "W");
    setNumber("Boiler command age", 0.2, "s");
    setNumber("OTB - Control Setpoint Command", boilerRequested && otbLinkAvailable ? 45 : 0, "°C");
    setNumber("OTB - Relative Modulation", otbLinkAvailable && boilerActive ? 42 : 0, "%");
    setNumber("OTB - CH Water Pressure", 1.6, "bar");
    setNumber("OTB - Boiler Water Temperature", otbLinkAvailable ? (boilerActive ? supplyTemp + 4.8 : supplyTemp) : 0, "°C");
    setNumber("OTB - Return Water Temperature", otbLinkAvailable ? hpOutlet : 0, "°C");
    setNumber("OTB - Last Response Age", otbLinkAvailable ? 0.4 : 14.2, "s");
    setText("text_sensor", "Boiler command source", "Power House");
    setText(
      "text_sensor",
      "Boiler block reason",
      boilerRequested && openthermSelected && !otbLinkAvailable
        ? "selected boiler transport unavailable"
        : (boilerRequested ? "" : "no boiler heat request"),
    );
    const otbChCommand = getEntity("switch", "OTB - Central Heating Command");
    if (otbChCommand) {
      otbChCommand.value = otbLinkAvailable && boilerActive;
      otbChCommand.state = otbChCommand.value;
    }
    setNumber("System Heat Power", systemHeat, "W");
    setNumber("Heating Power Input", coolingScenario ? 0 : (Number.isNaN(totalPower) ? 0 : totalPower), "W");
    setNumber("Cooling Power Input", coolingScenario ? (Number.isNaN(totalPower) ? 0 : totalPower) : 0, "W");
    setNumber("Electrical Energy Daily", electricalDaily, "kWh");
    setNumber("Electrical Energy Cumulative", electricalCumulative, "kWh");
    setNumber("Heating Electrical Energy Daily", heatingElectricalDaily, "kWh");
    setNumber("Heating Electrical Energy Cumulative", heatingElectricalCumulative, "kWh");
    setNumber("Cooling Electrical Energy Daily", coolingElectricalDaily, "kWh");
    setNumber("Cooling Electrical Energy Cumulative", coolingElectricalCumulative, "kWh");
    setNumber("HeatPump Thermal Energy Daily", heatpumpDaily, "kWh");
    setNumber("HeatPump Thermal Energy Cumulative", heatpumpCumulative, "kWh");
    setNumber("HeatPump Cooling Energy Daily", coolingDaily, "kWh");
    setNumber("HeatPump Cooling Energy Cumulative", coolingCumulative, "kWh");
    setNumber("HeatPump COP Daily", heatpumpCopDaily, "");
    setNumber("HeatPump COP Cumulative", heatpumpCopCumulative, "");
    setNumber("HeatPump EER Daily", heatpumpEerDaily, "");
    setNumber("HeatPump EER Cumulative", heatpumpEerCumulative, "");
    setNumber("Total Cooling Power", totalCoolingPower, "W");
    setNumber("Total EER", totalEer, "");
    setNumber("Boiler Thermal Energy Daily", boilerDaily, "kWh");
    setNumber("Boiler Thermal Energy Cumulative", boilerCumulative, "kWh");
    setNumber("System Thermal Energy Daily", systemDaily, "kWh");
    setNumber("System Thermal Energy Cumulative", systemCumulative, "kWh");
    syncAuxRelayState(supplyTemp);
  }

  // Mirrors the firmware aux-relay decision (oq_aux_relay_control.yaml) for the demo.
  function syncAuxRelayState(supplyTemp) {
    const auxFunction = String(getEntity("select", "Aux Relay Function")?.value || "Disabled");
    const cmLabel = String(getEntity("text_sensor", "Control Mode (Label)")?.value || "");
    const heatingActive = cmLabel.startsWith("CM2") || cmLabel.startsWith("CM3") || cmLabel.startsWith("CM4");
    const coolingActive = cmLabel.startsWith("CM5");
    const modeCode = coolingActive ? 1 : (heatingActive ? 2 : 0);
    if (modeCode !== state.auxRelayLastMode) {
      state.auxTempGateOn = false;
      state.auxRelayLastMode = modeCode;
    }

    let relayOn = false;
    let status = "Disabled";
    if (auxFunction === "External control") {
      relayOn = isSwitchEnabled("Aux relay (R2)");
      status = "External control";
    } else if (auxFunction === "Heating demand") {
      relayOn = heatingActive;
      status = relayOn ? "Heating demand active" : "No heating demand";
    } else if (auxFunction === "Cooling demand") {
      relayOn = coolingActive;
      status = relayOn ? "Cooling demand active" : "No cooling demand";
    } else if (auxFunction === "Heating or cooling demand") {
      relayOn = heatingActive || coolingActive;
      status = heatingActive ? "Heating demand active" : coolingActive ? "Cooling demand active" : "No thermal demand";
    }

    const gateEnabled = isSwitchEnabled("Aux Relay Wait For Supply Temp");
    if (!gateEnabled || !relayOn) {
      state.auxTempGateOn = false;
    } else if (Number.isNaN(supplyTemp)) {
      state.auxTempGateOn = false;
      status = "Supply temperature unavailable";
      relayOn = false;
    } else {
      const hysteresis = Number(getEntity("number", "Aux Relay Temp Hysteresis")?.value ?? 2);
      if (modeCode === 2) {
        const startTemp = Number(getEntity("number", "Aux Relay Heating Start Temp")?.value ?? 30);
        if (supplyTemp >= startTemp) {
          state.auxTempGateOn = true;
        } else if (supplyTemp <= startTemp - hysteresis) {
          state.auxTempGateOn = false;
        }
        if (!state.auxTempGateOn) status = "Waiting for warm water";
      } else {
        const startTemp = Number(getEntity("number", "Aux Relay Cooling Start Temp")?.value ?? 18);
        if (supplyTemp <= startTemp) {
          state.auxTempGateOn = true;
        } else if (supplyTemp >= startTemp + hysteresis) {
          state.auxTempGateOn = false;
        }
        if (!state.auxTempGateOn) status = "Waiting for cold water";
      }
      relayOn = state.auxTempGateOn;
    }

    setBinary("Aux relay active", relayOn);
    setText("text_sensor", "Aux relay status", status);
  }

  function seedEntities() {
    syncDevMeta();
    seedEntityDefinitions();
    setEntity("text_sensor", "OpenQuatt Installation Topology", { state: state.installation, value: state.installation });
    setEntity("text_sensor", "OpenQuatt Hardware Profile", { state: state.hardware, value: state.hardware });
    setEntity("text_sensor", "OpenQuatt Connection", { state: state.connection, value: state.connection });
    setEntity("text_sensor", "OpenQuatt Version", { state: MOCK_STABLE_VERSION, value: MOCK_STABLE_VERSION });
    setEntity("text_sensor", "OpenQuatt Release Channel", { state: "dev", value: "dev" });
    setEntity("sensor", "Uptime", { value: 0, uom: "h" });
    syncUptimeEntity();
    setEntity("sensor", "ESP Internal Temperature", { value: 37.8, uom: "°C" });
    setEntity("sensor", "Firmware Update Progress", { value: 0, uom: "%" });
    setEntity("text_sensor", "Firmware Update Status", { state: "Idle", value: "Idle" });
    setEntity("text_sensor", "Trendhistorie beschikbaar", { state: "18,4 dagen", value: "18,4 dagen" });
    setEntity("text_sensor", "Trendhistorie oudste punt", { state: "14-04 06:00", value: "14-04 06:00" });
    setEntity("text_sensor", "Trendhistorie nieuwste punt", { state: "2 min geleden", value: "2 min geleden" });
    setEntity("text_sensor", "Trendhistorie laatste opslag", { state: "02-05 11:35", value: "02-05 11:35" });
    setEntity("sensor", "Trendhistorie grootte", { value: 182.5, uom: "kB" });
    setEntity("sensor", "Trendhistorie schrijfacties", { value: 437 });
    if (!state.energyHistoryRecords.length) {
      state.energyHistoryRecords = buildEnergyHistoryRecords();
      state.energyHistoryWrites = state.energyHistoryRecords.length;
    }
    if (!state.energyHistoryHourRecords.length) {
      state.energyHistoryHourRecords = buildEnergyHistoryHourRecords();
    }
    setEntity("select", "Uurdetail bewaren", {
      value: state.energyHistoryHourRetention,
      state: state.energyHistoryHourRetention,
      option: ["30 dagen", "90 dagen", "180 dagen", "365 dagen"],
    });
    const energyHistoryRecordCountText = `${state.energyHistoryRecords.length} records`;
    setEntity("text_sensor", "Lifetime energiehistorie beschikbaar", {
      state: energyHistoryRecordCountText,
      value: energyHistoryRecordCountText,
    });
    setEntity("sensor", "Lifetime energiehistorie grootte", { value: state.energyHistoryStoredKiB, uom: "kB" });
    setEntity("sensor", "Lifetime energiehistorie schrijfacties", { value: state.energyHistoryWrites });
    setEntity("update", "Firmware Update", {
      state: "available",
      value: "available",
      current_version: MOCK_STABLE_VERSION,
      latest_version: MOCK_DEV_VERSION,
      title: "OpenQuatt firmware",
      summary: "Nieuwe firmware met verdere UI- en regelingverbeteringen staat klaar voor deze preview.",
      release_url: getMockReleaseUrl("dev"),
    });
    setEntity("binary_sensor", "Setup Complete", { value: state.complete, state: state.complete });
    setEntity("select", "Heating Control Mode", {
      value: "Power House",
      state: "Power House",
      option: ["Power House", "Water Temperature Control (heating curve)"],
    });
    setEntity("select", "CM Override", {
      value: "Auto",
      state: "Auto",
      option: ["Auto", "Force CM0", "Force CM1", "Force CM98"],
    });
    setEntity("switch", "OpenQuatt Enabled", { value: true, state: true });
    setEntity("switch", "Boiler assist enabled", { value: true, state: true });
    setEntity("switch", "Boiler fallback on heat-pump fault", { value: false, state: false });
    setEntity("select", "Boiler connection", {
      value: "OpenTherm",
      state: "OpenTherm",
      option: ["R1", "OpenTherm"],
    });
    setEntity("switch", "Manual Cooling Enable", { value: false, state: false });
    setEntity("switch", "Cooling Room Request Required", { value: true, state: true });
    setEntity("switch", "Aux Relay Wait For Supply Temp", { value: false, state: false });
    setEntity("switch", "Aux relay (R2)", { value: false, state: false });
    setEntity("switch", "CIC - Enable polling", { value: false, state: false });
    setEntity("switch", "Status LEDs enabled", { value: true, state: true });
    setEntity("switch", "Usage statistics", { value: false, state: false });
    setEntity("binary_sensor", "Usage statistics choice configured", { value: false, state: false });
    setEntity("text_sensor", "Usage statistics installation ID", { value: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f", state: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f" });
    setEntity("text", "CIC - Feed URL", { value: "http://192.168.2.117:8080/beta/feed/data.json", state: "http://192.168.2.117:8080/beta/feed/data.json" });
    setEntity("switch", "OpenTherm Enabled", { value: false, state: false });
    setEntity("switch", "CiC Compatibility Mode", { value: false, state: false });
    setEntity("switch", "Trendopslag", { value: true, state: true });
    setEntity("switch", "Trendhistorie opslaan in flash", { value: true, state: true });
    setEntity("switch", "Lifetime energiehistorie opslaan", { value: true, state: true });
    setEntity("switch", "RAM log history", { value: true, state: true });
    updateEnergyHistoryStats();
    setEntity("select", "Debug Level", {
      value: "INFO",
      state: "INFO",
      option: ["NONE", "ERROR", "WARN", "INFO", "CONFIG", "DEBUG"],
    });
    setEntity("select", "Silent Mode Override", {
      value: "Schedule",
      state: "Schedule",
      option: ["Schedule", "On", "Off"],
    });
    setEntity("select", "Flow Control Mode", {
      value: "Flow Setpoint",
      state: "Flow Setpoint",
      option: ["Flow Setpoint", "Manual PWM"],
    });
    setEntity("text_sensor", "Commissioning status", { state: "IDLE", value: "IDLE" });
    setEntity("switch", "Air purge return to Auto", { value: true, state: true });
    setEntity("text_sensor", "Boiler power test status", { state: "IDLE", value: "IDLE" });
    setEntity("sensor", "Boiler power test result", { value: 0, uom: "W" });
    setEntity("sensor", "Boiler power test confidence", { value: 0, uom: "%" });
    setEntity("text_sensor", "Flow Autotune status", { state: "IDLE", value: "IDLE" });
    setEntity("number", "Flow Autotune Kp suggested", { value: 0, min_value: 0, max_value: 5, step: 0.01, uom: "" });
    setEntity("number", "Flow Autotune Ki suggested", { value: 0, min_value: 0, max_value: 5, step: 0.01, uom: "" });
    setEntity("text_sensor", "Air purge status", { state: "IDLE", value: "IDLE" });
    setEntity("sensor", "Air purge remaining", { value: 0, uom: "s" });
    setEntity("sensor", "Air purge phase", { value: 0, uom: "" });
    setEntity("sensor", "Air purge target iPWM", { value: 0, uom: "iPWM" });
    setEntity("text_sensor", "Manual flow status", { state: "IDLE", value: "IDLE" });
    setEntity("sensor", "Manual flow target iPWM", { value: 400, uom: "iPWM" });
    setEntity("text_sensor", "Manual HP status", { state: "IDLE", value: "IDLE" });
    setEntity("text_sensor", "Manual HP guard status", { state: "Vrijgegeven", value: "Vrijgegeven" });
    setEntity("select", "Manual HP1 service mode", { value: "Standby", state: "Standby", option: ["Standby", "Heating", "Cooling"] });
    setEntity("select", "Manual HP2 service mode", { value: "Standby", state: "Standby", option: ["Standby", "Heating", "Cooling"] });
    setEntity("text_sensor", "HP water calibration status", { state: "IDLE", value: "IDLE" });
    setEntity("sensor", "HP water calibration remaining", { value: 0, uom: "s" });
    setEntity("sensor", "HP water calibration phase", { value: 0, uom: "" });
    setEntity("sensor", "HP water calibration spread", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration supply delta", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration stable window progress", { value: 0, uom: "s" });
    setEntity("sensor", "HP water calibration stable window required", { value: 60, uom: "s" });
    setEntity("sensor", "HP water calibration result reference", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result spread before", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result expected spread", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result HP1 water in raw average", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result HP1 water out raw average", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result HP2 water in raw average", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result HP2 water out raw average", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result supply raw average", { value: NaN, uom: "\u00B0C" });
    setEntity("sensor", "HP water calibration result supply offset", { value: NaN, uom: "\u00B0C" });
    setEntity("text_sensor", "HP water calibration result supply source", { value: "", state: "" });
    setEntity("number", "HP1 water in temperature offset", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP1 water out temperature offset", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP2 water in temperature offset", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP2 water out temperature offset", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "Water Supply Temperature Calibration Offset", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "Water Supply PT1000 Calibration Offset", { value: NaN, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "Water Supply DS18B20 Calibration Offset", { value: NaN, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "Water Supply CIC Calibration Offset", { value: NaN, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("text", "Water Supply HA Input Calibration Identity", { value: "", state: "" });
    setEntity("number", "Water Supply HA Input Calibration Offset", { value: NaN, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP calibration HP1 water in offset suggested", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP calibration HP1 water out offset suggested", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP calibration HP2 water in offset suggested", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP calibration HP2 water out offset suggested", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("number", "HP calibration supply temperature offset suggested", { value: 0, min_value: -2, max_value: 2, step: 0.01, uom: "\u00B0C" });
    setEntity("select", "Quatt Hybrid version", {
      value: "V1.5",
      state: "V1.5",
      option: ["V1", "V1.5", "V2"],
    });
    setEntity("text_sensor", "Control Mode (Label)", { state: "CM98" });
    setEntity("text_sensor", "Cooling Block Reason", { state: "Ready", value: "Ready" });
    setEntity("text_sensor", "Cooling Guard Mode", { state: "Dew point", value: "Dew point" });
    setEntity("text_sensor", "Flow Mode", { state: "Adaptive" });
    setEntity("select", "Behavior", {
      value: "Balanced",
      state: "Balanced",
      option: ["Quiet", "Balanced", "Fast response"],
    });
    setEntity("select", "Power House response profile", {
      value: "Balanced",
      state: "Balanced",
      option: ["Calm", "Balanced", "Responsive", "Custom"],
    });
    setEntity("select", "Heating Curve Control Profile", {
      value: "Balanced",
      state: "Balanced",
      option: ["Comfort", "Balanced", "Stable"],
    });
    setEntity("select", "Cooling Without Dew Point", {
      value: "Dew point required",
      state: "Dew point required",
      option: [
        "Dew point required",
        "Allow without dew point, use dew point approximation",
        "Allow without dew point, user responsibility",
      ],
    });
    setEntity("select", "Cooling Dew Point Source", {
      value: "Auto",
      state: "Auto",
      option: ["Auto", "Home Assistant", "MQTT"],
    });
    setEntity("select", "Water Supply Source", {
      value: "Local",
      state: "Local",
      option: ["Local", "CIC", "HA input"],
    });
    setEntity("select", "Local Water Supply Temp Source", {
      value: "PT1000",
      state: "PT1000",
      option: ["PT1000", "DS18B20"],
    });
    setEntity("select", "Flow Source", {
      value: "Outdoor unit",
      state: "Outdoor unit",
      option: ["Outdoor unit", "CIC"],
    });
    setEntity("select", "Q Flow Source", {
      value: "Auto",
      state: "Auto",
      option: ["Auto", "Local", "Outdoor unit"],
    });
    setEntity("select", "Aux Relay Function", {
      value: "Disabled",
      state: "Disabled",
      option: ["Disabled", "Heating demand", "Cooling demand", "Heating or cooling demand", "External control"],
    });
    setEntity("select", "Outdoor Unit Flow Mode", {
      value: "Local aggregate HP1/HP2",
      state: "Local aggregate HP1/HP2",
      option: ["Flowmeter HP1", "Flowmeter HP2", "Local aggregate HP1/HP2"],
    });
    setEntity("select", "Outside Temperature Source", {
      value: "Auto",
      state: "Auto",
      option: ["Auto", "Outdoor unit", "HA input", "MQTT"],
    });
    setEntity("select", "Room Temperature Source", {
      value: "OT thermostat",
      state: "OT thermostat",
      option: ["CIC", "OT thermostat", "HA input", "MQTT"],
    });
    setEntity("select", "Room Setpoint Source", {
      value: "OT thermostat",
      state: "OT thermostat",
      option: ["CIC", "OT thermostat", "HA input", "MQTT"],
    });
    setEntity("select", "Cooling Enable Source", {
      value: "Disabled",
      state: "Disabled",
      option: ["CIC", "HA input", "MQTT", "CIC or HA input", "Disabled", "OT thermostat"],
    });
    setEntity("select", "Heating Enable Source", {
      value: "Disabled",
      state: "Disabled",
      option: ["Disabled", "OT thermostat", "CIC", "HA input", "MQTT"],
    });
    setEntity("select", "Firmware Update Channel", {
      value: "dev",
      state: "dev",
      option: ["main", "dev"],
    });
    setEntity("select", "Firmware Update Target", {
      value: "current build",
      state: "current build",
      option: ["current build", "alternate connection", "alternate topology", "alternate topology and connection"],
    });
    setEntity("select", "Preset", {
      value: "Balanced",
      state: "Balanced",
      option: ["Quiet", "Balanced", "High output", "Custom"],
    });
    setEntity("select", "HP1 - Excluded compressor level A", {
      value: "None",
      state: "None",
      option: COMPRESSOR_LEVEL_OPTIONS,
    });
    setEntity("select", "HP1 - Excluded compressor level B", {
      value: "None",
      state: "None",
      option: COMPRESSOR_LEVEL_OPTIONS,
    });

    [
      ["Flow Setpoint", 800, 0, 1500, 10, "L/h"],
      ["Cooling Flow Setpoint", 800, 0, 1500, 10, "L/h"],
      ["Manual flow service setpoint", 800, 0, 1500, 10, "L/h"],
      ["Manual HP1 compressor level", 0, 0, 10, 1, ""],
      ["Manual HP2 compressor level", 0, 0, 10, 1, ""],
      ["Manual iPWM", 400, 50, 850, 1, "iPWM"],
      ["Flow PI Kp", 0.35, 0, 5, 0.01, ""],
      ["Flow PI Ki", 0.05, 0, 5, 0.01, ""],
      ["Heating Curve PID Kp", 0.28, 0, 0.8, 0.01, ""],
      ["Heating Curve PID Ki", 0.0006, 0, 0.003, 0.0001, ""],
      ["Heating Curve PID Kd", 0.2, 0, 0.4, 0.01, ""],
      ["Cooling PID Kp", 3, 0, 10, 0.1, ""],
      ["Cooling PID Ki", 0.12, 0, 2, 0.01, ""],
      ["Cooling PID Kd", 0, 0, 2, 0.01, ""],
      ["Boiler rated heat power", 1800, 500, 10000, 100, "W"],
      ["CM3 deficit ON threshold", 1000, 0, 10000, 50, "W"],
      ["CM3 deficit OFF threshold", 400, 0, 10000, 50, "W"],
      ["Day max level", 10, 0, 10, 1, ""],
      ["Silent max level", 6, 0, 10, 1, ""],
      ["Maximum water temperature", 56, 25, 75, 1, "°C"],
      ["Minimum runtime", 300, 300, 3600, 30, "s"],
      ["Compressor starts 2h warning limit", 6, 1, 20, 1, ""],
      ["Compressor starts 72h warning limit", 40, 1, 120, 1, ""],
      ["Rated maximum house power", 4500, 500, 12000, 100, "W"],
      ["House cold temp", -10, -25, 5, 0.5, "°C"],
      ["Maximum heating outdoor temperature", 16, -10, 25, 1, "°C"],
      ["Power House temperature reaction", 3000, 0, 4000, 10, "W/K"],
      ["Power House comfort below setpoint", 0.1, 0, 2, 0.05, "°C"],
      ["Power House comfort above setpoint", 0.3, 0, 2, 0.05, "°C"],
      ["Power House demand rise time", 8, 2, 20, 1, "min"],
      ["Power House demand fall time", 3, 1, 10, 1, "min"],
      ["Cooling Minimum Supply Temp", 18, 5, 24, 0.5, "°C"],
      ["Cooling Demand Max", 4, 1, 10, 1, "step"],
      ["Cooling Restart Delta", 1.0, 0, 5, 0.1, "°C"],
      ["Cooling Request On Delta", 0.4, 0, 2, 0.1, "°C"],
      ["Cooling Request Off Delta", 0.1, 0, 2, 0.1, "°C"],
      ["Cooling Safety Margin", 2, 0, 4, 0.1, "°C"],
      ["Aux Relay Heating Start Temp", 30, 20, 60, 0.5, "°C"],
      ["Aux Relay Cooling Start Temp", 18, 8, 25, 0.5, "°C"],
      ["Aux Relay Temp Hysteresis", 2, 0.5, 5, 0.5, "°C"],
      ["Curve Tsupply @ -20°C", 48, 20, 70, 1, "°C"],
      ["Curve Tsupply @ -10°C", 43, 20, 70, 1, "°C"],
      ["Curve Tsupply @ 0°C", 38, 20, 70, 1, "°C"],
      ["Curve Tsupply @ 5°C", 34, 20, 70, 1, "°C"],
      ["Curve Tsupply @ 10°C", 30, 20, 70, 1, "°C"],
      ["Curve Tsupply @ 15°C", 27, 20, 70, 1, "°C"],
      ["Curve Fallback Tsupply (No Outside Temp)", 40, 25, 70, 0.5, "°C"],
    ].forEach(([name, value, min, max, step, uom]) => {
      setEntity("number", name, {
        value,
        min_value: min,
        max_value: max,
        step,
        uom,
      });
    });

    [
      ["Silent start time", "19:00:00"],
      ["Silent end time", "07:00:00"],
    ].forEach(([name, value]) => {
      setEntity("time", name, {
        value,
        state: value,
      });
    });
    setEntity("datetime", "OpenQuatt resume at", {
      value: OPENQUATT_RESUME_CLEAR_VALUE,
      state: OPENQUATT_RESUME_CLEAR_VALUE,
    });

    [
      ["Total Power Input", 0, "W"],
      ["Heating Power Input", 0, "W"],
      ["Cooling Power Input", 0, "W"],
      ["Total COP", 0, ""],
      ["Total EER", 0, ""],
      ["Total Heat Power", 0, "W"],
      ["Total Cooling Power", 0, "W"],
      ["Boiler Heat Power", 0, "W"],
      ["Boiler command target temperature", 0, "°C"],
      ["Boiler command requested power", 0, "W"],
      ["Boiler command age", 0.2, "s"],
      ["OTB - Relative Modulation", 0, "%"],
      ["OTB - CH Water Pressure", 1.6, "bar"],
      ["OTB - Boiler Water Temperature", 35.2, "°C"],
      ["OTB - Return Water Temperature", 29.5, "°C"],
      ["OTB - Domestic Hot Water Temperature", 48.0, "°C"],
      ["OTB - OEM Fault Code", 0, ""],
      ["OTB - OEM Diagnostic Code", 0, ""],
      ["OTB - Maximum Boiler Capacity", 24, "kW"],
      ["OTB - Minimum Modulation", 18, "%"],
      ["OTB - OpenTherm Device Version", 2.2, ""],
      ["OTB - Device Type", 1, ""],
      ["OTB - Device Product Version", 1, ""],
      ["OTB - Last Response Age", 0.4, "s"],
      ["OTB - Valid Response Count", 1842, ""],
      ["OTB - Last Response Message ID", 25, ""],
      ["System Heat Power", 0, "W"],
      ["Strategy requested power", 0, "W"],
      ["HP capacity (W)", 0, "W"],
      ["HP deficit (W)", 0, "W"],
      ["Electrical Energy Daily", 0, "kWh"],
      ["Electrical Energy Cumulative", 0, "kWh"],
      ["Heating Electrical Energy Daily", 0, "kWh"],
      ["Heating Electrical Energy Cumulative", 0, "kWh"],
      ["Cooling Electrical Energy Daily", 0, "kWh"],
      ["Cooling Electrical Energy Cumulative", 0, "kWh"],
      ["HeatPump Thermal Energy Daily", 0, "kWh"],
      ["HeatPump Thermal Energy Cumulative", 0, "kWh"],
      ["HeatPump Cooling Energy Daily", 0, "kWh"],
      ["HeatPump Cooling Energy Cumulative", 0, "kWh"],
      ["HeatPump COP Daily", 0, ""],
      ["HeatPump COP Cumulative", 0, ""],
      ["HeatPump EER Daily", 0, ""],
      ["HeatPump EER Cumulative", 0, ""],
      ["Boiler Thermal Energy Daily", 0, "kWh"],
      ["Boiler Thermal Energy Cumulative", 0, "kWh"],
      ["System Thermal Energy Daily", 0, "kWh"],
      ["System Thermal Energy Cumulative", 0, "kWh"],
      ["Flow average (Selected)", 0, "L/h"],
      ["Flow average (local)", 0, "L/h"],
      ["Controller Flow", 0, "L/h"],
      ["OT - Control Setpoint", 30.0, "\u00B0C"],
      ["OT - Room Setpoint", 20.0, "\u00B0C"],
      ["OT - Room Temperature", 20.9, "\u00B0C"],
      ["CIC - Water Supply Temp", 29.5, "\u00B0C"],
      ["CIC - Control setpoint", 30.0, "\u00B0C"],
      ["CIC - Room setpoint", 20.0, "\u00B0C"],
      ["CIC - Room temperature", 20.9, "\u00B0C"],
      ["CIC - Flowrate (filtered)", 785, "L/h"],
      ["CIC - Last success age", 12, "s"],
      ["HA - Outside Temperature", 15.5, "\u00B0C"],
      ["HA - Water Supply Temperature", 28.9, "\u00B0C"],
      ["HA - Thermostat Setpoint", 20.0, "\u00B0C"],
      ["HA - Thermostat Room Temperature", 21.2, "\u00B0C"],
      ["HA - Cooling Dew Point", 15.9, "°C"],
      ["Room Temperature (Selected)", 20.6, "°C"],
      ["Room Setpoint (Selected)", 21.0, "°C"],
      ["Water Supply Temp (Selected)", 29.5, "°C"],
      ["Outside Temperature (Selected)", 8.2, "°C"],
      ["Heating Curve Supply Target", 33.0, "°C"],
      ["Power House – P_house", 2500, "W"],
      ["Power House – P_req", 2800, "W"],
      ["MQTT Cooling Dew Point", 16.2, "°C"],
      ["MQTT Cooling Dew Point Age", getMqttDewPointAgeSeconds(), "s"],
      ["MQTT Outside Temperature", 14.9, "°C"],
      ["MQTT Outside Temperature Age", getMqttAgeSeconds(state.mqtt.lastOutsideTemperatureAt), "s"],
      ["MQTT Room Temperature", 21.0, "°C"],
      ["MQTT Room Temperature Age", getMqttAgeSeconds(state.mqtt.lastRoomTemperatureAt), "s"],
      ["MQTT Room Setpoint", 21.0, "°C"],
      ["MQTT Room Setpoint Age", getMqttAgeSeconds(state.mqtt.lastRoomSetpointAt), "s"],
      ["MQTT Heating Enable Age", getMqttAgeSeconds(state.mqtt.lastHeatingEnableAt), "s"],
      ["MQTT Cooling Enable Age", getMqttAgeSeconds(state.mqtt.lastCoolingEnableAt), "s"],
      ["Cooling Dew Point (Selected)", 16.1, "°C"],
      ["Cooling Minimum Safe Supply Temp", 18.1, "°C"],
      ["Cooling Effective Minimum Supply Temp", 18.1, "°C"],
      ["Cooling Fallback Night Minimum Outdoor Temp", 14.3, "°C"],
      ["Cooling Fallback Minimum Supply Temp", 19.0, "°C"],
      ["Cooling Supply Target", 18.0, "°C"],
      ["Cooling Supply Error", 0.9, "°C"],
      ["Cooling Demand (raw)", 2, ""],
      ["Cooling limited demand", 2, ""],
      ["Cooling limiter allowed max", 4, ""],
      ["Cooling limiter reason code", "full", ""],
      ["HP1 - Power Input", 0, "W"],
      ["HP1 - Heat Power", 0, "W"],
      ["HP1 - Cooling Power", 0, "W"],
      ["HP1 - COP", 0, ""],
      ["HP1 compressor level", 0, ""],
      ["HP1 - Compressor frequency", 0, "Hz"],
      ["HP1 - Compressor starts 2h", 3, ""],
      ["HP1 - Compressor starts 6h", 11, ""],
      ["HP1 - Compressor starts 24h", 29, ""],
      ["HP1 - Compressor starts 72h", 40, ""],
      ["HP1 - Compressor last start age", 12, "min"],
      ["HP1 - Runtime Hours", 2854, "h"],
      ["Compressor cycling alert first seen", 0, "s"],
      ["Compressor cycling alert last seen", 0, "s"],
      ["Compressor cycling alert HP1 peak 2h", 0, ""],
      ["Compressor cycling alert HP1 peak 72h", 0, ""],
      ["Compressor cycling alert HP2 peak 2h", 0, ""],
      ["Compressor cycling alert HP2 peak 72h", 0, ""],
      ["HP1 - Fan speed", 0, "rpm"],
      ["HP1 - Flow", 0, "L/h"],
      ["Water Supply Temp", 29.5, "\u00B0C"],
      ["Water Supply Temp (PT1000)", 29.5, "\u00B0C"],
      ["Water Supply Temp (DS18B20)", 29.2, "\u00B0C"],
      ["Outside Temperature (Local aggregated)", 15.8, "\u00B0C"],
      ["HP1 - Evaporator coil temperature", 0, "\u00B0C"],
      ["HP1 - Inner coil temperature", 0, "\u00B0C"],
      ["HP1 - Outside temperature", 0, "\u00B0C"],
      ["HP1 - Condenser pressure", 0, "bar"],
      ["HP1 - Gas discharge temperature", 0, "\u00B0C"],
      ["HP1 - Evaporator pressure", 0, "bar"],
      ["HP1 - Gas return temperature", 0, "\u00B0C"],
      ["HP1 - EEV steps", 0, "p"],
      ["HP1 - Water in temperature", 25.5, "°C"],
      ["HP1 - Water out temperature", 29.5, "°C"],
      ["HP1 - Water in temperature raw", 25.5, "°C"],
      ["HP1 - Water out temperature raw", 29.5, "°C"],
    ].forEach(([name, value, uom]) => {
      setEntity("sensor", name, { value, uom });
    });

    [
      ["HP1 - Working Mode Label", "Standby"],
      ["HP1 - Active Failures List", "None"],
      ["Room Temperature Effective Source", "OT thermostat"],
      ["Room Setpoint Effective Source", "OT thermostat"],
      ["Water Supply Temp Effective Source", "Local - PT1000"],
      ["Water Supply Temperature Calibration Status", "Not calibrated"],
      ["Heating Enable Effective Source", "None"],
      ["Cooling Enable Effective Source", "HA input"],
      ["Boiler command source", "Power House"],
      ["Boiler block reason", "no boiler heat request"],
      ["Runtime lead HP", "HP2"],
    ].forEach(([name, value]) => {
      setEntity("text_sensor", name, { state: value, value });
    });

    [
      ["Silent active", false],
      ["Sticky pump active", false],
      ["Cooling Enable (Selected)", false],
      ["Heating Enable (Selected)", true],
      ["Heating Enable Valid", true],
      ["Heating blocked by thermostat", false],
      ["Cooling Request Active", false],
      ["Cooling Permitted", false],
      ["Boiler active", false],
      ["Boiler command valid", true],
      ["Boiler command active", false],
      ["OTB - Boiler Link Available", true],
      ["OTB - Fault Indication", false],
      ["OTB - Central Heating Active", false],
      ["OTB - Domestic Hot Water Active", false],
      ["OTB - Flame On", false],
      ["OTB - Diagnostic Indication", false],
      ["OTB - DHW Present", true],
      ["OTB - Service Required", false],
      ["OTB - Lockout Reset", false],
      ["OTB - Low Water Pressure", false],
      ["OTB - Flame Fault", false],
      ["OTB - Air Pressure Fault", false],
      ["OTB - Water Overtemperature", false],
      ["Compressor cycling warning 2h", false],
      ["Compressor cycling warning 72h", false],
      ["Alternating compressor starts warning", false],
      ["Compressor cycling alert latched", false],
      ["Compressor cycling alert alternating", false],
      ["Lowflow fault active", false],
      ["PT1000 read problem", false],
      ["Water Supply Temp Fallback Active", false],
      ["Water Supply Temperature Calibration Required", false],
      ["Flow mismatch (HP1 vs HP2)", false],
      ["OT - Thermostat CH Enable", false],
      ["OT - Thermostat Status Valid", true],
      ["OT - Thermostat Cooling Enable", false],
      ["CIC - CH enabled", false],
      ["CIC - CH enable valid", true],
      ["CIC - Cooling enabled", false],
      ["CIC - JSON Feed OK", true],
      ["HA - Outside Temperature Valid", true],
      ["HA - Water Supply Temperature Valid", true],
      ["HA - Room Setpoint Valid", true],
      ["HA - Room Temperature Valid", true],
      ["HA - Cooling Dew Point Valid", true],
      ["HA - Heating Enable", false],
      ["HA - Heating Enable Valid", true],
      ["HA - Cooling Enable", false],
      ["HA - Cooling Enable Valid", true],
      ["MQTT Cooling Dew Point Valid", true],
      ["MQTT Outside Temperature Valid", true],
      ["MQTT Room Temperature Valid", true],
      ["MQTT Room Setpoint Valid", true],
      ["MQTT Heating Enable", false],
      ["MQTT Heating Enable Valid", true],
      ["MQTT Cooling Enable", false],
      ["MQTT Cooling Enable Valid", true],
      ["CIC - Data stale", false],
      ["OT - Link Problem", false],
      ["HP1 - Defrost", false],
      ["HP1 - 4-Way valve", false],
      ["HP1 - Bottom plate heater", false],
      ["HP1 - Crankcase heater", false],
    ].forEach(([name, value]) => {
      setEntity("binary_sensor", name, { value });
    });

    applyCoolingDewPointSourceSelection();

    seedOduRuntimeFrequencyEntities("HP1");
    seedHp2Entities();
    seedOduRuntimeFrequencyEntities("HP2");
    syncRuntimeCounterEntities();
    setEntity("button", "Reset Cumulative Energy Counters", { state: "", value: "" });

  }

  function seedHp2Entities() {
    HP2_ENTITIES.forEach(([domain, name, payload]) => {
      setEntity(domain, name, clone(payload));
    });
  }

  function syncRuntimeCounterEntities() {
    entities.delete(entityKey("button", "Reset Runtime Counters (HP1)"));
    entities.delete(entityKey("button", "Reset Runtime Counters (HP1+HP2)"));
    if (state.installation === "single") {
      entities.delete(entityKey("text_sensor", "Runtime lead HP"));
      setEntity("button", "Reset Runtime Counters (HP1)", { state: "", value: "" });
      return;
    }
    setEntity("button", "Reset Runtime Counters (HP1+HP2)", { state: "", value: "" });
    if (!getEntity("text_sensor", "Runtime lead HP")) {
      setEntity("text_sensor", "Runtime lead HP", { state: "HP2", value: "HP2" });
    }
  }

  function clearHp2Entities() {
    HP2_ENTITIES.forEach(([domain, name]) => {
      entities.delete(entityKey(domain, name));
    });
  }

  function setInstallationMode(mode) {
    state.installation = mode === "single" ? "single" : "duo";
    setText("text_sensor", "OpenQuatt Installation Topology", state.installation);
    if (state.installation === "single") {
      clearHp2Entities();
      clearOduRuntimeFrequencyEntities("HP2");
      if (state.scenario === "dual") {
        state.scenario = "heating";
      }
    } else {
      seedHp2Entities();
      seedOduRuntimeFrequencyEntities("HP2");
    }
    syncRuntimeCounterEntities();
  }

  function syncDevMeta() {
    syncUptimeEntity();
    const updateEntity = getEntity("update", "Firmware Update");
    const updateAvailable = Boolean(
      updateEntity
      && String(updateEntity.latest_version || "").trim()
      && String(updateEntity.current_version || "").trim()
      && String(updateEntity.latest_version).trim() !== String(updateEntity.current_version).trim()
    );
    window.__OQ_DEV_META = {
      installation: state.installation,
      hardwareProfile: state.hardware,
      connection: state.connection,
      ipAddress: "192.168.2.123",
      bootedAt: state.bootedAt,
      updateAvailable,
      updateLabel: updateAvailable ? "Beschikbaar" : "Actueel",
    };
    window.__OQ_DEV_WEBSERVER_LOGS__ = [
      "[I][main:41] OpenQuatt web_server gestart",
      "[I][wifi:28] Verbonden met lokaal netwerk",
      "[D][web_server:91] Event stream beschikbaar op /events",
      "[I][control:77] Regeling actief",
      "[W][heatpump:203] Defrost actief, compressor tijdelijk uit",
      "[I][automation:118] Hervatmoment gepland",
      "[D][logger:65] Debuglog opgebouwd voor preview",
    ];
    if (!state.logHistoryEntries.length) {
      seedLogHistoryEntries();
    }
  }

  function notifyMockUpdated() {
    updateTrendFlashStats();
    updateEnergyHistoryStats();
    syncDevMeta();
    window.dispatchEvent(new Event("oq-mock-updated"));
  }

  function notifyDevControlsChanged() {
    window.dispatchEvent(new Event("oq-dev-controls-changed"));
  }

  function computePreset() {
    const behavior = getEntity("select", "Behavior").value;
    const day = Number(getEntity("number", "Day max level").value);
    const silent = Number(getEntity("number", "Silent max level").value);
    const near = (a, b) => Math.abs(a - b) < 0.25;

    if (near(day, 7) && near(silent, 5) && behavior === "Quiet") {
      return "Quiet";
    }
    if (near(day, 10) && near(silent, 6) && behavior === "Balanced") {
      return "Balanced";
    }
    if (near(day, 10) && near(silent, 8) && behavior === "Fast response") {
      return "High output";
    }
    return "Custom";
  }

  function updateSummary() {
    const mode = getEntity("select", "Heating Control Mode").value.includes("Water Temperature Control")
      ? "Water Temperature Control"
      : "Power House";
    const behavior = getEntity("select", "Behavior").value || "Balanced";
    const preset = computePreset();
    const day = Number(getEntity("number", "Day max level").value);
    const silent = Number(getEntity("number", "Silent max level").value);
    const water = Number(getEntity("number", "Maximum water temperature").value);
    const text = `${mode}, ${behavior}, ${preset} preset, day ${day.toFixed(0)}, silent ${silent.toFixed(0)}, max ${water.toFixed(1)} C${state.complete ? ", setup complete" : ""}`;

    setBinary("Setup Complete", state.complete);
    setText("text_sensor", "Summary", text);
    setText("select", "Preset", preset);
    applyDiagnosticScenario();
    applyIncidentScenario();
  }

  function applyDiagnosticScenario() {
    const single = state.installation === "single";
    const coolingScenario = isCoolingScenario();
    const heatingEnabledScenario = isHeatingEnabledScenario();
    setBinary("Compressor cycling warning 2h", false);
    setBinary("Compressor cycling warning 72h", false);
    setBinary("Alternating compressor starts warning", false);
    setBinary("Lowflow fault active", false);
    setBinary("Flow mismatch (HP1 vs HP2)", false);
    setBinary("OT - Thermostat CH Enable", heatingEnabledScenario);
    setBinary("OT - Thermostat Cooling Enable", coolingScenario);
    setBinary("CIC - CH enabled", heatingEnabledScenario);
    setBinary("CIC - CH enable valid", true);
    setBinary("CIC - Cooling enabled", coolingScenario);
    setBinary("CIC - JSON Feed OK", true);
    setBinary("HA - Heating Enable", heatingEnabledScenario);
    setBinary("HA - Cooling Enable", coolingScenario);
    setBinary("MQTT Heating Enable", heatingEnabledScenario);
    setBinary("MQTT Heating Enable Valid", true);
    setBinary("MQTT Cooling Enable", coolingScenario);
    setBinary("MQTT Cooling Enable Valid", true);
    setBinary("CIC - Data stale", !isSwitchEnabled("CIC - Enable polling"));
    setBinary("OT - Link Problem", false);
    setBinary("OT - Thermostat Status Valid", true);
    const heatingEnableSource = String(getEntity("select", "Heating Enable Source")?.value || "Disabled");
    const heatingEnableValid = heatingEnableSource === "Disabled"
      || (heatingEnableSource === "OT thermostat" && Boolean(getEntity("binary_sensor", "OT - Thermostat Status Valid")?.value))
      || (heatingEnableSource === "CIC" && Boolean(getEntity("binary_sensor", "CIC - CH enable valid")?.value))
      || (heatingEnableSource === "HA input" && Boolean(getEntity("binary_sensor", "HA - Heating Enable Valid")?.value))
      || (heatingEnableSource === "MQTT" && Boolean(getEntity("binary_sensor", "MQTT Heating Enable Valid")?.value));
    const heatingEnableSelected = heatingEnableSource === "Disabled"
      || (heatingEnableValid && heatingEnableSource === "OT thermostat" && Boolean(getEntity("binary_sensor", "OT - Thermostat CH Enable")?.value))
      || (heatingEnableValid && heatingEnableSource === "CIC" && Boolean(getEntity("binary_sensor", "CIC - CH enabled")?.value))
      || (heatingEnableValid && heatingEnableSource === "HA input" && Boolean(getEntity("binary_sensor", "HA - Heating Enable")?.value))
      || (heatingEnableValid && heatingEnableSource === "MQTT" && Boolean(getEntity("binary_sensor", "MQTT Heating Enable")?.value));
    const coolingEnableSource = String(getEntity("select", "Cooling Enable Source")?.value || "Disabled");
    const manualCoolingEnabled = isSwitchEnabled("Manual Cooling Enable");
    const cicCoolingValid = Boolean(getEntity("binary_sensor", "CIC - JSON Feed OK")?.value)
      && !Boolean(getEntity("binary_sensor", "CIC - Data stale")?.value);
    const haCoolingValid = Boolean(getEntity("binary_sensor", "HA - Cooling Enable Valid")?.value);
    const cicCoolingEnabled = cicCoolingValid && Boolean(getEntity("binary_sensor", "CIC - Cooling enabled")?.value);
    const haCoolingEnabled = haCoolingValid
      && Boolean(getEntity("binary_sensor", "HA - Cooling Enable")?.value);
    const coolingEnableValid = coolingEnableSource === "Disabled"
      || (coolingEnableSource === "OT thermostat" && Boolean(getEntity("binary_sensor", "OT - Thermostat Status Valid")?.value))
      || (coolingEnableSource === "CIC" && cicCoolingValid)
      || (coolingEnableSource === "CIC or HA input" && (cicCoolingValid || haCoolingValid))
      || (coolingEnableSource === "HA input" && haCoolingValid)
      || (coolingEnableSource === "MQTT" && Boolean(getEntity("binary_sensor", "MQTT Cooling Enable Valid")?.value));
    const sourceCoolingEnabled = coolingEnableValid && (
      (coolingEnableSource === "OT thermostat" && Boolean(getEntity("binary_sensor", "OT - Thermostat Cooling Enable")?.value))
      || (coolingEnableSource === "CIC" && cicCoolingEnabled)
      || (coolingEnableSource === "CIC or HA input" && (cicCoolingEnabled || haCoolingEnabled))
      || (coolingEnableSource === "HA input" && Boolean(getEntity("binary_sensor", "HA - Cooling Enable")?.value))
      || (coolingEnableSource === "MQTT" && Boolean(getEntity("binary_sensor", "MQTT Cooling Enable")?.value))
    );
    const sourceCoolingEffective = coolingEnableSource === "CIC or HA input"
      ? (cicCoolingEnabled && haCoolingEnabled ? "CIC + HA input" : cicCoolingEnabled ? "CIC" : haCoolingEnabled ? "HA input" : "None")
      : sourceCoolingEnabled ? coolingEnableSource : "None";
    const coolingEnableSelected = manualCoolingEnabled
      || sourceCoolingEnabled;
    const coolingEnableEffectiveSource = sourceCoolingEffective !== "None" && manualCoolingEnabled
      ? `${sourceCoolingEffective} + Manual`
      : sourceCoolingEffective !== "None"
        ? sourceCoolingEffective
        : manualCoolingEnabled
          ? "Manual"
          : "None";
    setBinary("Heating Enable Valid", heatingEnableValid);
    setBinary("Heating Enable (Selected)", heatingEnableSelected);
    setBinary("Cooling Enable Valid", coolingEnableValid);
    setBinary("Cooling Enable (Selected)", coolingEnableSelected);
    setBinary("Heating blocked by thermostat", heatingEnabledScenario && !heatingEnableSelected);
    setText("text_sensor", "Room Temperature Effective Source", String(getEntity("select", "Room Temperature Source")?.value || "Unknown"));
    setText("text_sensor", "Room Setpoint Effective Source", String(getEntity("select", "Room Setpoint Source")?.value || "Unknown"));
    const waterSupplySource = String(getEntity("select", "Water Supply Source")?.value || "Unknown");
    const localWaterSupplySource = String(getEntity("select", "Local Water Supply Temp Source")?.value || "");
    setText("text_sensor", "Water Supply Temp Effective Source", waterSupplySource === "Local" && localWaterSupplySource
      ? `Local - ${localWaterSupplySource}`
      : waterSupplySource);
    setText("text_sensor", "Heating Enable Effective Source", heatingEnableSource === "Disabled" ? "None" : heatingEnableSource);
    setText("text_sensor", "Cooling Enable Effective Source", coolingEnableEffectiveSource);
    setNumber("OT - Control Setpoint", coolingScenario ? 18.0 : 30.0, "\u00B0C");
    setNumber("OT - Room Setpoint", coolingScenario ? 23.0 : 21.0, "\u00B0C");
    setNumber("OT - Room Temperature", Number(getEntity("sensor", "Room Temperature (Selected)")?.value || 20.6), "\u00B0C");
    setNumber("CIC - Control setpoint", coolingScenario ? 18.0 : 30.0, "\u00B0C");
    setNumber("CIC - Room setpoint", Number(getEntity("sensor", "Room Setpoint (Selected)")?.value || 21.0), "\u00B0C");
    setNumber("CIC - Room temperature", Number(getEntity("sensor", "Room Temperature (Selected)")?.value || 20.6), "\u00B0C");
    setNumber("CIC - Flowrate (filtered)", Number(getEntity("sensor", "Flow average (Selected)")?.value || 0), "L/h");
    {
      const selectedFlow = Number(getEntity("sensor", "Flow average (Selected)")?.value || 0);
      setNumber("Controller Flow", Math.max(0, selectedFlow - 10), "L/h");
      setNumber("Flow average (local)", selectedFlow, "L/h");
    }
    setNumber("CIC - Last success age", isSwitchEnabled("CIC - Enable polling") ? 12 : 0, "s");
    setNumber("HP1 - Compressor starts 2h", 3);
    setNumber("HP1 - Compressor starts 6h", 11);
    setNumber("HP1 - Compressor starts 24h", 29);
    setNumber("HP1 - Compressor starts 72h", 40);
    setNumber("HP1 - Compressor last start age", 12, "min");
    if (!single) {
      setNumber("HP2 - Compressor starts 2h", 3);
      setNumber("HP2 - Compressor starts 6h", 9);
      setNumber("HP2 - Compressor starts 24h", 24);
      setNumber("HP2 - Compressor starts 72h", 40);
      setNumber("HP2 - Compressor last start age", 18, "min");
    }

    if (state.diagnostics === "cycling") {
      setBinary("Compressor cycling warning 2h", true);
      setBinary("Compressor cycling warning 72h", true);
      setBinary("Alternating compressor starts warning", !single);
      setNumber("HP1 - Compressor starts 2h", 8);
      setNumber("HP1 - Compressor starts 6h", 18);
      setNumber("HP1 - Compressor starts 24h", 33);
      setNumber("HP1 - Compressor starts 72h", 48);
      setNumber("HP1 - Compressor last start age", 3, "min");
      if (!single) {
        setNumber("HP2 - Compressor starts 2h", 8);
        setNumber("HP2 - Compressor starts 6h", 16);
        setNumber("HP2 - Compressor starts 24h", 30);
        setNumber("HP2 - Compressor starts 72h", 44);
        setNumber("HP2 - Compressor last start age", 7, "min");
      }
      recordMockCompressorCyclingAlert({
        hp1Peak2h: 8,
        hp1Peak72h: 48,
        hp2Peak2h: single ? 0 : 8,
        hp2Peak72h: single ? 0 : 44,
        alternating: !single,
        ongoing: true,
      });
    } else if (state.diagnostics === "cycling-recovered") {
      recordMockCompressorCyclingAlert({
        hp1Peak2h: 8,
        hp1Peak72h: 48,
        hp2Peak2h: single ? 0 : 8,
        hp2Peak72h: single ? 0 : 44,
        alternating: !single,
      });
    } else if (state.diagnostics === "hydraulics") {
      setBinary("Lowflow fault active", true);
      setBinary("Flow mismatch (HP1 vs HP2)", !single);
    } else if (state.diagnostics === "connections") {
      setEntity("switch", "CIC - Enable polling", { value: true, state: true });
      setEntity("switch", "OpenTherm Enabled", { value: true, state: true });
      setBinary("CIC - JSON Feed OK", false);
      setBinary("CIC - Data stale", true);
      setBinary("OT - Link Problem", true);
    } else if (state.diagnostics === "hp-fault") {
      setText("text_sensor", "HP1 - Active Failures List", "Condenser pressure sensor failure");
    }
    syncMockCompressorCyclingAlertEntities();
  }

  function recordMockCompressorCyclingAlert({ hp1Peak2h, hp1Peak72h, hp2Peak2h, hp2Peak72h, alternating, ongoing = false }) {
    const alert = state.compressorCyclingAlert;
    const now = Date.now();
    if (!alert.latched) {
      alert.latched = true;
      alert.firstSeenAt = now - (52 * 60 * 1000);
      alert.lastSeenAt = ongoing ? now : now - (11 * 60 * 1000);
    } else if (ongoing) {
      alert.lastSeenAt = now;
    }
    alert.hp1Peak2h = Math.max(alert.hp1Peak2h, hp1Peak2h);
    alert.hp1Peak72h = Math.max(alert.hp1Peak72h, hp1Peak72h);
    alert.hp2Peak2h = Math.max(alert.hp2Peak2h, hp2Peak2h);
    alert.hp2Peak72h = Math.max(alert.hp2Peak72h, hp2Peak72h);
    alert.alternating = alert.alternating || alternating;
  }

  function clearMockCompressorCyclingAlert() {
    Object.assign(state.compressorCyclingAlert, {
      latched: false,
      firstSeenAt: 0,
      lastSeenAt: 0,
      hp1Peak2h: 0,
      hp1Peak72h: 0,
      hp2Peak2h: 0,
      hp2Peak72h: 0,
      alternating: false,
    });
  }

  function syncMockCompressorCyclingAlertEntities() {
    const alert = state.compressorCyclingAlert;
    setBinary("Compressor cycling alert latched", alert.latched);
    setBinary("Compressor cycling alert alternating", alert.alternating);
    setNumber("Compressor cycling alert first seen", Math.round(alert.firstSeenAt / 1000), "s");
    setNumber("Compressor cycling alert last seen", Math.round(alert.lastSeenAt / 1000), "s");
    setNumber("Compressor cycling alert HP1 peak 2h", alert.hp1Peak2h);
    setNumber("Compressor cycling alert HP1 peak 72h", alert.hp1Peak72h);
    setNumber("Compressor cycling alert HP2 peak 2h", alert.hp2Peak2h);
    setNumber("Compressor cycling alert HP2 peak 72h", alert.hp2Peak72h);
  }

  function buildTrendPreviewSamples(windowHours = 24) {
    const safeWindowHours = Number.isFinite(Number(windowHours)) && Number(windowHours) > 0 ? Number(windowHours) : 24;
    const windowMs = safeWindowHours * 60 * 60 * 1000;
    const points = Math.max(12, Math.round(safeWindowHours * 12));
    const endTime = Date.now();
    const startTime = endTime - windowMs;
    const span = Math.max(points - 1, 1);
    const samples = [];

    for (let index = 0; index < points; index += 1) {
      const fraction = index / span;
      const dayWave = Math.sin((fraction * Math.PI * 2) - 1.1);
      const detailWave = Math.sin(fraction * Math.PI * 10);
      const driftWave = Math.cos((fraction * Math.PI * 2) + 0.45);

      samples.push({
        t: startTime + Math.round(fraction * windowMs),
        outside: 8.5 + (dayWave * 3.4) + (detailWave * 0.5),
        supply: 35.5 + (Math.sin((fraction * Math.PI * 2) - 0.35) * 4.8) + (detailWave * 0.9),
        room: 20.2 + (Math.sin((fraction * Math.PI * 2) - 0.22) * 0.35) + (detailWave * 0.08),
        roomSetpoint: 20.6 + (Math.cos((fraction * Math.PI * 2) - 0.1) * 0.10),
        flow: Math.max(0, 760 + (Math.cos((fraction * Math.PI * 2) + 0.1) * 110) + (detailWave * 18)),
        input: Math.max(280, 1180 + (dayWave * 380) + (detailWave * 150) + (driftWave * 110)),
        output: Math.max(1000, 4250 + (dayWave * 860) + (detailWave * 260)),
      });
    }

    return samples;
  }

  function formatTrendFlashDate(date) {
    const day = String(date.getDate()).padStart(2, "0");
    const month = String(date.getMonth() + 1).padStart(2, "0");
    const hours = String(date.getHours()).padStart(2, "0");
    const minutes = String(date.getMinutes()).padStart(2, "0");
    return `${day}-${month} ${hours}:${minutes}`;
  }

  function formatTrendFlashAge(fromMs, toMs = Date.now()) {
    const deltaMinutes = Math.max(0, Math.round((toMs - fromMs) / 60000));
    if (deltaMinutes < 1) {
      return "Zojuist";
    }
    if (deltaMinutes < 60) {
      return `${deltaMinutes} min geleden`;
    }
    const deltaHours = Math.round(deltaMinutes / 60);
    if (deltaHours < 24) {
      return `${deltaHours} u geleden`;
    }
    return `${Math.round(deltaHours / 24)} d geleden`;
  }

  function updateTrendFlashStats() {
    if (!getEntity("text_sensor", "Trendhistorie beschikbaar")) {
      return;
    }

    if (!isSwitchEnabled("Trendhistorie opslaan in flash")) {
      setText("text_sensor", "Trendhistorie beschikbaar", "Nog leeg");
      setText("text_sensor", "Trendhistorie oudste punt", "Nog niet");
      setText("text_sensor", "Trendhistorie nieuwste punt", "Nog leeg");
      setText("text_sensor", "Trendhistorie laatste opslag", "Nog niet");
      setNumber("Trendhistorie grootte", 0, "kB");
      setNumber("Trendhistorie schrijfacties", 0, "");
      return;
    }

    setText("text_sensor", "Trendhistorie beschikbaar", "18,4 dagen");
    setText("text_sensor", "Trendhistorie oudste punt", formatTrendFlashDate(new Date(state.trendFlashOldestAt)));
    setText("text_sensor", "Trendhistorie nieuwste punt", formatTrendFlashAge(state.trendFlashNewestAt));
    setText("text_sensor", "Trendhistorie laatste opslag", formatTrendFlashDate(new Date(state.trendFlashLastFlushAt)));
    setNumber("Trendhistorie grootte", state.trendFlashStoredKiB, "kB");
    setNumber("Trendhistorie schrijfacties", state.trendFlashWrites, "");
  }

  function dateKeyFromDate(date) {
    return (date.getFullYear() * 10000) + ((date.getMonth() + 1) * 100) + date.getDate();
  }

  function formatEnergyHistoryDate(dateKey) {
    const year = Math.floor(dateKey / 10000);
    const month = Math.floor(dateKey / 100) % 100;
    const day = dateKey % 100;
    return `${String(day).padStart(2, "0")}-${String(month).padStart(2, "0")}-${year}`;
  }

  function buildEnergyHistoryRecords() {
    const records = [];
    const recordCount = 900;
    const today = new Date();
    today.setHours(12, 0, 0, 0);
    const start = new Date(today.getTime());
    start.setDate(start.getDate() - recordCount);
    for (let index = 0; index < recordCount; index += 1) {
      const date = new Date(start.getTime());
      date.setDate(start.getDate() + index);
      const month = date.getMonth();
      const winter = month <= 2 || month >= 10;
      const shoulder = month === 3 || month === 4 || month === 8 || month === 9;
      const summer = month >= 5 && month <= 7;
      const wave = 0.65 + (Math.sin(index / 8) * 0.18) + (Math.sin(index / 23) * 0.11);
      const heatingInput = winter
        ? Math.max(2600, Math.round((6600 + (month === 0 ? 1800 : 0)) * wave))
        : shoulder
          ? Math.max(600, Math.round(2600 * wave))
          : Math.round(180 * Math.max(0, Math.sin(index / 5)));
      const coolingInput = summer ? Math.max(0, Math.round((900 + (month === 6 ? 850 : 0)) * (0.55 + Math.sin(index / 6) * 0.28))) : 0;
      const boilerHeat = winter && index % 9 === 0 ? Math.round((1800 + (index % 5) * 260) * wave) : 0;
      const heatOutput = Math.round(heatingInput * (3.2 + Math.sin(index / 17) * 0.38));
      const coolingOutput = Math.round(coolingInput * (3.5 + Math.cos(index / 13) * 0.42));
      records.push({
        sequence: index,
        dateKey: dateKeyFromDate(date),
        flags: 0,
        electricalInputWh: heatingInput + coolingInput,
        heatingInputWh: heatingInput,
        coolingInputWh: coolingInput,
        heatpumpHeatOutputWh: heatingInput > 0 ? heatOutput : 0,
        heatpumpCoolingOutputWh: coolingInput > 0 ? coolingOutput : 0,
        boilerHeatOutputWh: boilerHeat,
        systemHeatOutputWh: (heatingInput > 0 ? heatOutput : 0) + boilerHeat,
      });
    }
    return records;
  }

  function buildEnergyHistoryHourRecords(days = 7) {
    const records = [];
    const now = new Date();
    now.setMinutes(30, 0, 0);
    let sequence = 0;
    for (let dayOffset = days - 1; dayOffset >= 0; dayOffset -= 1) {
      const date = new Date(now.getTime());
      date.setDate(now.getDate() - dayOffset);
      const dateKey = dateKeyFromDate(date);
      const month = date.getMonth();
      const summer = month >= 5 && month <= 7;
      for (let hour = 0; hour < 24; hour += 1) {
        if (dayOffset === 0 && hour > now.getHours()) {
          continue;
        }
        const morning = Math.exp(-Math.pow((hour - 7) / 3.2, 2));
        const evening = Math.exp(-Math.pow((hour - 19) / 4.0, 2));
        const coolingPeak = Math.exp(-Math.pow((hour - 15) / 3.6, 2));
        const dayWave = 0.78 + (Math.sin((sequence + 8) / 11) * 0.14);
        const heatingInput = summer
          ? Math.round(Math.max(0, 80 * morning * dayWave))
          : Math.round(Math.max(0, (520 * morning + 760 * evening + 120) * dayWave));
        const coolingInput = summer ? Math.round(Math.max(0, (320 * coolingPeak + 35) * dayWave)) : 0;
        const boilerHeat = !summer && hour >= 6 && hour <= 8 && sequence % 13 === 0 ? 360 : 0;
        const heatOutput = Math.round(heatingInput * (3.5 + Math.sin(sequence / 9) * 0.32));
        const coolingOutput = Math.round(coolingInput * (3.2 + Math.cos(sequence / 7) * 0.25));
        records.push({
          sequence,
          dateKey,
          hour,
          electricalInputWh: heatingInput + coolingInput,
          heatingInputWh: heatingInput,
          coolingInputWh: coolingInput,
          heatpumpHeatOutputWh: heatingInput > 0 ? heatOutput : 0,
          heatpumpCoolingOutputWh: coolingInput > 0 ? coolingOutput : 0,
          boilerHeatOutputWh: boilerHeat,
          systemHeatOutputWh: (heatingInput > 0 ? heatOutput : 0) + boilerHeat,
        });
        sequence += 1;
      }
    }
    return records;
  }

  function updateEnergyHistoryStats() {
    if (!getEntity("text_sensor", "Lifetime energiehistorie beschikbaar")) {
      return;
    }
    const records = state.energyHistoryRecords || [];
    const oldest = records[0];
    const newest = records[records.length - 1];
    setText("text_sensor", "Lifetime energiehistorie beschikbaar", records.length ? `${records.length} records` : "Geen data");
    setText("text_sensor", "Lifetime energiehistorie oudste dag", oldest ? formatEnergyHistoryDate(oldest.dateKey) : "Geen data");
    setText("text_sensor", "Lifetime energiehistorie nieuwste dag", newest ? formatEnergyHistoryDate(newest.dateKey) : "Geen data");
    setText("text_sensor", "Lifetime energiehistorie laatste opslag", records.length ? formatTrendFlashDate(new Date(state.energyHistoryLastWriteAt)) : "Geen data");
    setNumber("Lifetime energiehistorie grootte", state.energyHistoryStoredKiB, "kB");
    setNumber("Lifetime energiehistorie schrijfacties", state.energyHistoryWrites, "");
  }

  function getCurrentEnergyHistoryValues() {
    const readKwh = (name) => {
      const entity = getEntity("sensor", name);
      const value = Number(entity?.value ?? entity?.state);
      return Number.isFinite(value) && value >= 0 ? Math.round(value * 1000) : -1;
    };
    return {
      dateKey: dateKeyFromDate(new Date()),
      electricalInputWh: readKwh("Electrical Energy Daily"),
      heatingInputWh: readKwh("Heating Electrical Energy Daily"),
      coolingInputWh: readKwh("Cooling Electrical Energy Daily"),
      heatpumpHeatOutputWh: readKwh("HeatPump Thermal Energy Daily"),
      heatpumpCoolingOutputWh: readKwh("HeatPump Cooling Energy Daily"),
      boilerHeatOutputWh: readKwh("Boiler Thermal Energy Daily"),
      systemHeatOutputWh: readKwh("System Thermal Energy Daily"),
    };
  }

  function captureCurrentEnergyHistoryRecord() {
    const current = getCurrentEnergyHistoryValues();
    const records = Array.isArray(state.energyHistoryRecords) ? state.energyHistoryRecords : [];
    const existingIndex = records.findIndex((record) => record.dateKey === current.dateKey);
    const nextSequence = records.length
      ? Math.max(...records.map((record) => Number(record.sequence) || 0)) + 1
      : 0;
    const record = {
      sequence: existingIndex >= 0 ? records[existingIndex].sequence : nextSequence,
      dateKey: current.dateKey,
      flags: 0,
      electricalInputWh: current.electricalInputWh,
      heatingInputWh: current.heatingInputWh,
      coolingInputWh: current.coolingInputWh,
      heatpumpHeatOutputWh: current.heatpumpHeatOutputWh,
      heatpumpCoolingOutputWh: current.heatpumpCoolingOutputWh,
      boilerHeatOutputWh: current.boilerHeatOutputWh,
      systemHeatOutputWh: current.systemHeatOutputWh,
    };

    if (existingIndex >= 0) {
      records[existingIndex] = record;
    } else {
      records.push(record);
      records.sort((left, right) => left.dateKey - right.dateKey);
    }

    state.energyHistoryRecords = records;
    state.energyHistoryWrites += 1;
    state.energyHistoryStoredKiB = Math.max(1, Number((state.energyHistoryStoredKiB + 0.04).toFixed(2)));
    state.energyHistoryLastWriteAt = Date.now();
    updateEnergyHistoryStats();
  }

  function buildEnergyHistoryTextPayload(url = null) {
    const records = state.energyHistoryRecords || [];
    const hourRecords = state.energyHistoryHourRecords || [];
    const current = getCurrentEnergyHistoryValues();
    const fromDate = Number(url?.searchParams?.get("from")) || 0;
    const toDate = Number(url?.searchParams?.get("to")) || 0;
    const includeHours = url?.searchParams?.get("hours") !== "0";
    const metaOnly = url?.searchParams?.get("meta") === "1";
    const inRange = (dateKey) => {
      const key = Number(dateKey);
      return Number.isFinite(key) && (!fromDate || key >= fromDate) && (!toDate || key <= toDate);
    };
    const filteredRecords = metaOnly ? [] : records.filter((record) => inRange(record.dateKey));
    const filteredHours = includeHours && !metaOnly ? hourRecords.filter((record) => inRange(record.dateKey)) : [];
    const oldest = records[0]?.dateKey || 0;
    const newest = records[records.length - 1]?.dateKey || 0;
    const hourDates = [...new Set(hourRecords.map((record) => Number(record.dateKey)).filter(Number.isFinite))].sort((a, b) => a - b);
    const retentionDays = Number.parseInt(state.energyHistoryHourRetention, 10) || 180;
    const hourLastWriteTimestampS = hourDates.length ? Math.floor(Date.now() / 1000) : 0;
    const lines = [
      "@schema|3",
      `@enabled|${isSwitchEnabled("Lifetime energiehistorie opslaan") ? 1 : 0}`,
      `@now|${Date.now()}`,
      `@records|${records.length}`,
      `@hours|${hourRecords.length}|7`,
      `@range|${fromDate}|${toDate}|${includeHours ? 1 : 0}`,
      `@bounds|${records.length}|${oldest}|${newest}|${hourDates.length}|${hourDates[0] || 0}|${hourDates[hourDates.length - 1] || 0}`,
      `@hour_retention|${retentionDays}|${retentionDays}|1|${hourDates.length}|${hourDates.length}|${retentionDays}|${hourLastWriteTimestampS}`,
      ...filteredRecords.map((record) => [
        record.sequence,
        record.dateKey,
        record.flags || 0,
        record.electricalInputWh,
        record.heatingInputWh,
        record.coolingInputWh,
        record.heatpumpHeatOutputWh,
        record.heatpumpCoolingOutputWh,
        record.boilerHeatOutputWh,
        record.systemHeatOutputWh,
      ].join("|")),
      [
        "@current",
        current.dateKey,
        current.electricalInputWh,
        current.heatingInputWh,
        current.coolingInputWh,
        current.heatpumpHeatOutputWh,
        current.heatpumpCoolingOutputWh,
        current.boilerHeatOutputWh,
        current.systemHeatOutputWh,
      ].join("|"),
      ...filteredHours.map((record) => [
        "@hour",
        record.sequence,
        record.dateKey,
        record.hour,
        record.electricalInputWh,
        record.heatingInputWh,
        record.coolingInputWh,
        record.heatpumpHeatOutputWh,
        record.heatpumpCoolingOutputWh,
        record.boilerHeatOutputWh,
        record.systemHeatOutputWh,
      ].join("|")),
    ];
    return `${lines.join("\n")}\n`;
  }

  function applyPreset(value) {
    if (value === "Quiet") {
      setText("select", "Behavior", "Quiet");
      setNumber("Day max level", 7);
      setNumber("Silent max level", 5);
    } else if (value === "Balanced") {
      setText("select", "Behavior", "Balanced");
      setNumber("Day max level", 10);
      setNumber("Silent max level", 6);
    } else if (value === "High output") {
      setText("select", "Behavior", "Fast response");
      setNumber("Day max level", 10);
      setNumber("Silent max level", 8);
    }
  }

  function syncBoilerDevControlFromDom() {
    if (typeof document === "undefined") {
      return;
    }
    const boilerControl = devControlsRoot?.querySelector('[data-oq-dev-control="boiler"]')
      || document.querySelector('[data-oq-dev-control="boiler"]');
    if (!boilerControl) {
      return;
    }
    state.boiler = boilerControl.value === "on" ? "on" : "off";
  }

  function setConnectionMode(value) {
    state.connection = value === "eth" ? "eth" : "wifi";
    setText("text_sensor", "OpenQuatt Connection", state.connection);
    setText("select", "Firmware Update Target", "current build");
    syncDevMeta();
  }

  function applyScenario(name) {
    if (state.installation === "single" && name === "dual") {
      name = "heating";
    }
    state.scenario = name;
    const t = state.tick / 5;
    const wave = (base, amp, offset = 0) => Number((base + Math.sin(t + offset) * amp).toFixed(1));
    const waveInt = (base, amp, offset = 0) => Math.round(base + Math.sin(t + offset) * amp);
    const single = state.installation === "single";

    syncMqttInputAgeEntities();
    setNumber("MQTT Outside Temperature", wave(14.9, 0.2), "°C");
    setNumber("MQTT Room Temperature", wave(state.scenario === "cooling" ? 24.0 : 21.0, 0.08, 0.3), "°C");
    setNumber("MQTT Room Setpoint", state.scenario === "cooling" ? 23.0 : 21.0, "°C");
    setBinary("MQTT Outside Temperature Valid", true);
    setBinary("MQTT Room Temperature Valid", true);
    setBinary("MQTT Room Setpoint Valid", true);
    setBinary("Silent active", false);
    setBinary("Sticky pump active", false);
    setBinary("HP1 - Defrost", false);
    setBinary("HP1 - 4-Way valve", false);
    setBinary("HP1 - Bottom plate heater", false);
    setBinary("HP1 - Crankcase heater", false);
    setNumber("HP1 - EEV steps", 0, "p");
    if (!single) {
      setBinary("HP2 - Defrost", false);
      setBinary("HP2 - 4-Way valve", false);
      setBinary("HP2 - Bottom plate heater", false);
      setBinary("HP2 - Crankcase heater", false);
      setNumber("HP2 - EEV steps", 0, "p");
    }
    setText("text_sensor", "HP1 - Active Failures List", "None");
    if (!single) {
      setText("text_sensor", "HP2 - Active Failures List", "None");
    }
    setNumber("Cooling limited demand", 0, "");
    setNumber("Cooling limiter allowed max", 10, "");
    setSensorText("Cooling limiter reason code", "inactive");

    if (name === "idle") {
      setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Adaptive");
      setNumber("Total Power Input", single ? 5.2 : 10.3, "W");
      setNumber("Total Heat Power", 0, "W");
      setNumber("Total COP", 0);
      setNumber("Flow average (Selected)", 0, "L/h");
      setNumber("Room Temperature (Selected)", 20.9, "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", 26.1, "°C");
      setNumber("HP1 - Power Input", 5.2, "W");
      setNumber("HP1 - Heat Power", 0, "W");
      setNumber("HP1 - COP", 0);
      setNumber("HP1 - Compressor frequency", 0, "Hz");
      setNumber("HP1 - Fan speed", 0, "rpm");
      setNumber("HP1 - Flow", 0, "L/h");
      setNumber("HP1 - Evaporator coil temperature", 25.4, "\u00B0C");
      setNumber("HP1 - Inner coil temperature", 27.1, "\u00B0C");
      setNumber("HP1 - Outside temperature", 11.8, "\u00B0C");
      setNumber("HP1 - Condenser pressure", 7.8, "bar");
      setNumber("HP1 - Gas discharge temperature", 26.7, "\u00B0C");
      setNumber("HP1 - Evaporator pressure", 7.6, "bar");
      setNumber("HP1 - Gas return temperature", 25.8, "\u00B0C");
      setNumber("HP1 - EEV steps", 0, "p");
      setNumber("HP1 - Water in temperature", 25.6, "°C");
      setNumber("HP1 - Water out temperature", 26.0, "°C");
      setText("text_sensor", "HP1 - Working Mode Label", "Standby");
      if (!single) {
        setNumber("HP2 - Power Input", 5.1, "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - COP", 0);
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", 0, "L/h");
        setNumber("HP2 - Evaporator coil temperature", 25.1, "\u00B0C");
        setNumber("HP2 - Inner coil temperature", 26.5, "\u00B0C");
        setNumber("HP2 - Outside temperature", 11.5, "\u00B0C");
        setNumber("HP2 - Condenser pressure", 7.7, "bar");
        setNumber("HP2 - Gas discharge temperature", 26.4, "\u00B0C");
        setNumber("HP2 - Evaporator pressure", 7.5, "bar");
        setNumber("HP2 - Gas return temperature", 25.5, "\u00B0C");
        setNumber("HP2 - EEV steps", 0, "p");
        setNumber("HP2 - Water in temperature", 25.4, "°C");
        setNumber("HP2 - Water out temperature", 25.8, "°C");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        setBinary("HP2 - Bottom plate heater", true);
        setBinary("HP2 - Crankcase heater", true);
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "start_blocked") {
      setText("text_sensor", "Control Mode (Label)", "CM2 - Heating - Heat Pump Only");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Adaptive");
      setNumber("Total Power Input", single ? wave(940, 24) : wave(980, 28), "W");
      setNumber("Total Heat Power", single ? wave(3100, 80) : wave(3180, 90), "W");
      setNumber("Total COP", 3.3);
      setNumber("Flow average (Selected)", wave(760, 16), "L/h");
      setNumber("Room Temperature (Selected)", wave(20.2, 0.05), "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(34.8, 0.2), "°C");
      setNumber("HP1 - Power Input", wave(940, 24), "W");
      setNumber("HP1 - Heat Power", wave(3100, 80), "W");
      setNumber("HP1 - COP", 3.3);
      setNumber("HP1 - Compressor frequency", waveInt(49, 2), "Hz");
      setNumber("HP1 - Fan speed", waveInt(640, 16), "rpm");
      setNumber("HP1 - Flow", wave(760, 16), "L/h");
      setText("text_sensor", "HP1 - Working Mode Label", "Heating");
      if (!single) {
        setNumber("HP2 - Power Input", 5.4, "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - COP", 0);
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", 0, "L/h");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "flow_hold") {
      setText("text_sensor", "Control Mode (Label)", "CM1 - Voorloop/naloop");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Voorloop");
      setNumber("Total Power Input", single ? wave(54, 4) : wave(62, 5), "W");
      setNumber("Total Heat Power", 0, "W");
      setNumber("Total COP", 0);
      setNumber("Flow average (Selected)", wave(520, 18), "L/h");
      setNumber("Room Temperature (Selected)", wave(20.4, 0.04), "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(29.2, 0.12), "°C");
      setNumber("HP1 - Power Input", wave(48, 3), "W");
      setNumber("HP1 - Heat Power", 0, "W");
      setNumber("HP1 - COP", 0);
      setNumber("HP1 - Compressor frequency", 0, "Hz");
      setNumber("HP1 - Fan speed", 0, "rpm");
      setNumber("HP1 - Flow", wave(520, 18), "L/h");
      setText("text_sensor", "HP1 - Working Mode Label", "Standby");
      if (!single) {
        setNumber("HP2 - Power Input", 5.2, "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - COP", 0);
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", 0, "L/h");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "summer_idle") {
      setText("text_sensor", "Control Mode (Label)", "CM0 - Pompbescherming");
      setBinary("Sticky pump active", true);
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Geen koelvraag");
      setText("text_sensor", "Cooling Guard Mode", "Dew point");
      setText("text_sensor", "Flow Mode", "Pompbescherming");
      setNumber("Cooling Demand (raw)", 0, "");
      setNumber("Cooling limited demand", 0, "");
      setNumber("Cooling limiter allowed max", 10, "");
      setSensorText("Cooling limiter reason code", "inactive");
      setNumber("Total Power Input", single ? wave(38, 3) : wave(47, 4), "W");
      setNumber("Total Heat Power", 0, "W");
      setNumber("Total Cooling Power", 0, "W");
      setNumber("Total COP", 0);
      setNumber("Total EER", 0);
      setNumber("Flow average (Selected)", wave(620, 22), "L/h");
      setNumber("Room Temperature (Selected)", wave(23.1, 0.08), "°C");
      setNumber("Room Setpoint (Selected)", 22.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(25.6, 0.15), "°C");
      setNumber("HP1 - Power Input", wave(36, 3), "W");
      setNumber("HP1 - Heat Power", 0, "W");
      setNumber("HP1 - Cooling Power", 0, "W");
      setNumber("HP1 - COP", 0);
      setNumber("HP1 - Compressor frequency", 0, "Hz");
      setNumber("HP1 - Fan speed", 0, "rpm");
      setNumber("HP1 - Flow", wave(620, 22), "L/h");
      setNumber("HP1 - Evaporator coil temperature", wave(25.0, 0.2), "\u00B0C");
      setNumber("HP1 - Inner coil temperature", wave(25.7, 0.2), "\u00B0C");
      setNumber("HP1 - Outside temperature", wave(25.8, 0.25), "\u00B0C");
      setNumber("HP1 - Condenser pressure", wave(8.0, 0.1), "bar");
      setNumber("HP1 - Gas discharge temperature", wave(26.8, 0.2), "\u00B0C");
      setNumber("HP1 - Evaporator pressure", wave(7.8, 0.1), "bar");
      setNumber("HP1 - Gas return temperature", wave(25.3, 0.2), "\u00B0C");
      setNumber("HP1 - EEV steps", 0, "p");
      setNumber("HP1 - Water in temperature", wave(25.5, 0.1), "°C");
      setNumber("HP1 - Water out temperature", wave(25.7, 0.1), "°C");
      setText("text_sensor", "HP1 - Working Mode Label", "Standby");
      if (!single) {
        setNumber("HP2 - Power Input", 5.4, "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - Cooling Power", 0, "W");
        setNumber("HP2 - COP", 0);
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", 0, "L/h");
        setNumber("HP2 - Evaporator coil temperature", wave(25.0, 0.2, 0.4), "\u00B0C");
        setNumber("HP2 - Inner coil temperature", wave(25.6, 0.2, 0.2), "\u00B0C");
        setNumber("HP2 - Outside temperature", wave(25.6, 0.25, 0.2), "\u00B0C");
        setNumber("HP2 - Condenser pressure", wave(8.0, 0.1, 0.3), "bar");
        setNumber("HP2 - Gas discharge temperature", wave(26.4, 0.2, 0.3), "\u00B0C");
        setNumber("HP2 - Evaporator pressure", wave(7.8, 0.1, 0.3), "bar");
        setNumber("HP2 - Gas return temperature", wave(25.1, 0.2, 0.3), "\u00B0C");
        setNumber("HP2 - EEV steps", 0, "p");
        setNumber("HP2 - Water in temperature", wave(25.3, 0.1), "°C");
        setNumber("HP2 - Water out temperature", wave(25.4, 0.1), "°C");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "heating" || name === "heating_stop_reasons") {
      setText("text_sensor", "Control Mode (Label)", "CM2 - Heating - Heat Pump Only");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Adaptive");
      const hp1Power = wave(418, 22);
      const hp1Heat = wave(1880, 120);
      const hp1Cop = Number((4.55 + Math.sin(t) * 0.18).toFixed(2));
      setNumber("Total Power Input", single ? hp1Power : wave(560, 55), "W");
      setNumber("Total Heat Power", single ? hp1Heat : wave(2430, 190), "W");
      setNumber("Total COP", single ? hp1Cop : Number((4.4 + Math.sin(t) * 0.22).toFixed(2)));
      setNumber("Flow average (Selected)", wave(780, 40), "L/h");
      setNumber("Room Temperature (Selected)", wave(20.2, 0.12), "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(31.4, 0.8), "°C");
      setNumber("HP1 - Power Input", hp1Power, "W");
      setNumber("HP1 - Heat Power", hp1Heat, "W");
      setNumber("HP1 - COP", hp1Cop);
      setNumber("HP1 - Compressor frequency", waveInt(30, 3), "Hz");
      setNumber("HP1 - Fan speed", wave(562, 18), "rpm");
      setNumber("HP1 - Flow", wave(790, 34), "L/h");
      setNumber("HP1 - Evaporator coil temperature", wave(3.8, 0.7), "\u00B0C");
      setNumber("HP1 - Inner coil temperature", wave(7.6, 0.6, 0.1), "\u00B0C");
      setNumber("HP1 - Outside temperature", wave(4.9, 0.25, 0.12), "\u00B0C");
      setNumber("HP1 - Condenser pressure", wave(22.8, 0.7), "bar");
      setNumber("HP1 - Gas discharge temperature", wave(67.2, 1.6), "\u00B0C");
      setNumber("HP1 - Evaporator pressure", wave(7.8, 0.2), "bar");
      setNumber("HP1 - Gas return temperature", wave(8.2, 0.5), "\u00B0C");
      setNumber("HP1 - EEV steps", waveInt(286, 18), "p");
      setNumber("HP1 - Water in temperature", wave(25.0, 0.4), "°C");
      setNumber("HP1 - Water out temperature", wave(30.5, 0.5), "°C");
      setText("text_sensor", "HP1 - Working Mode Label", "Heating");
      if (!single) {
        setNumber("HP2 - Power Input", wave(110, 12, 0.7), "W");
        setNumber("HP2 - Heat Power", wave(520, 60, 0.7), "W");
        setNumber("HP2 - COP", Number((4.1 + Math.sin(t + 0.7) * 0.14).toFixed(2)));
        setNumber("HP2 - Compressor frequency", waveInt(12, 2, 0.5), "Hz");
        setNumber("HP2 - Fan speed", wave(186, 10, 0.5), "rpm");
        setNumber("HP2 - Flow", wave(180, 20, 0.5), "L/h");
        setNumber("HP2 - Evaporator coil temperature", wave(25.0, 0.4, 0.5), "\u00B0C");
        setNumber("HP2 - Inner coil temperature", wave(26.6, 0.35, 0.2), "\u00B0C");
        setNumber("HP2 - Outside temperature", wave(4.7, 0.22, 0.18), "\u00B0C");
        setNumber("HP2 - Condenser pressure", wave(8.4, 0.2, 0.4), "bar");
        setNumber("HP2 - Gas discharge temperature", wave(30.4, 0.6, 0.4), "\u00B0C");
        setNumber("HP2 - Evaporator pressure", wave(8.1, 0.2, 0.4), "bar");
        setNumber("HP2 - Gas return temperature", wave(24.6, 0.4, 0.4), "\u00B0C");
        setNumber("HP2 - EEV steps", waveInt(32, 6, 0.5), "p");
        setNumber("HP2 - Water in temperature", wave(25.3, 0.3), "°C");
        setNumber("HP2 - Water out temperature", wave(29.4, 0.4), "°C");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        setBinary("HP2 - Bottom plate heater", false);
        setBinary("HP2 - Crankcase heater", true);
      }
      if (name === "heating_stop_reasons") {
        setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
        setText("text_sensor", "Flow Mode", "Standby");
        setNumber("Total Power Input", single ? 5.2 : 10.3, "W");
        setNumber("Total Heat Power", 0, "W");
        setNumber("Total COP", 0);
        setNumber("Flow average (Selected)", 0, "L/h");
        setNumber("HP1 - Power Input", 5.2, "W");
        setNumber("HP1 - Heat Power", 0, "W");
        setNumber("HP1 - COP", 0);
        setNumber("HP1 - Compressor frequency", 0, "Hz");
        setNumber("HP1 - Fan speed", 0, "rpm");
        setNumber("HP1 - Flow", 0, "L/h");
        setText("text_sensor", "HP1 - Working Mode Label", "Standby");
        if (!single) {
          setNumber("HP2 - Power Input", 5.2, "W");
          setNumber("HP2 - Heat Power", 0, "W");
          setNumber("HP2 - COP", 0);
          setNumber("HP2 - Compressor frequency", 0, "Hz");
          setNumber("HP2 - Fan speed", 0, "rpm");
          setNumber("HP2 - Flow", 0, "L/h");
          setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        }
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "dual") {
      setText("text_sensor", "Control Mode (Label)", "CM2 - Heating - Duo");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Adaptive");
      setBinary("Silent active", false);
      setText("text_sensor", "HP2 - Active Failures List", "None");
      setNumber("Total Power Input", wave(910, 54), "W");
      setNumber("Total Heat Power", wave(3980, 190), "W");
      setNumber("Total COP", Number((4.35 + Math.sin(t) * 0.12).toFixed(2)));
      setNumber("Flow average (Selected)", wave(1220, 50), "L/h");
      setNumber("Room Temperature (Selected)", wave(19.8, 0.12), "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(29.8, 0.6), "°C");
      setNumber("HP1 - Power Input", wave(470, 18), "W");
      setNumber("HP1 - Heat Power", wave(2080, 110), "W");
      setNumber("HP1 - COP", Number((4.42 + Math.sin(t) * 0.11).toFixed(2)));
      setNumber("HP1 - Compressor frequency", waveInt(34, 2), "Hz");
      setNumber("HP1 - Fan speed", wave(629, 14), "rpm");
      setNumber("HP1 - Flow", wave(608, 22), "L/h");
      setNumber("HP1 - Evaporator coil temperature", wave(1.6, 0.6), "\u00B0C");
      setNumber("HP1 - Inner coil temperature", wave(6.2, 0.5, 0.2), "\u00B0C");
      setNumber("HP1 - Outside temperature", wave(5.2, 0.2, 0.05), "\u00B0C");
      setNumber("HP1 - Condenser pressure", wave(23.4, 0.8), "bar");
      setNumber("HP1 - Gas discharge temperature", wave(69.4, 1.8), "\u00B0C");
      setNumber("HP1 - Evaporator pressure", wave(8.1, 0.2), "bar");
      setNumber("HP1 - Gas return temperature", wave(9.1, 0.5), "\u00B0C");
      setNumber("HP1 - EEV steps", waveInt(302, 16), "p");
      setNumber("HP1 - Water in temperature", wave(25.2, 0.3), "°C");
      setNumber("HP1 - Water out temperature", wave(31.8, 0.5), "°C");
      setText("text_sensor", "HP1 - Working Mode Label", "Heating");
      setNumber("HP2 - Power Input", wave(440, 18, 0.4), "W");
      setNumber("HP2 - Heat Power", wave(1900, 120, 0.4), "W");
      setNumber("HP2 - COP", Number((4.32 + Math.sin(t + 0.4) * 0.1).toFixed(2)));
      setNumber("HP2 - Compressor frequency", waveInt(31, 2, 0.4), "Hz");
      setNumber("HP2 - Fan speed", wave(618, 12, 0.4), "rpm");
      setNumber("HP2 - Flow", wave(590, 18, 0.4), "L/h");
      setNumber("HP2 - Evaporator coil temperature", wave(1.9, 0.6, 0.4), "\u00B0C");
      setNumber("HP2 - Inner coil temperature", wave(6.4, 0.45, 0.15), "\u00B0C");
      setNumber("HP2 - Outside temperature", wave(5.0, 0.18, 0.2), "\u00B0C");
      setNumber("HP2 - Condenser pressure", wave(23.0, 0.7, 0.4), "bar");
      setNumber("HP2 - Gas discharge temperature", wave(68.1, 1.5, 0.4), "\u00B0C");
      setNumber("HP2 - Evaporator pressure", wave(7.9, 0.2, 0.4), "bar");
      setNumber("HP2 - Gas return temperature", wave(8.8, 0.4, 0.4), "\u00B0C");
      setNumber("HP2 - EEV steps", waveInt(296, 18, 0.4), "p");
      setNumber("HP2 - Water in temperature", wave(25.1, 0.4), "°C");
      setNumber("HP2 - Water out temperature", wave(31.0, 0.4), "°C");
      setText("text_sensor", "HP2 - Working Mode Label", "Heating");
      setBinary("HP2 - 4-Way valve", false);
      setBinary("HP2 - Bottom plate heater", true);
      setBinary("HP2 - Crankcase heater", true);
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (name === "defrost") {
      setText("text_sensor", "Control Mode (Label)", "CM99");
      setBinary("Cooling Enable (Selected)", false);
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setText("text_sensor", "Flow Mode", "Ontdooien herstel");
      setBinary("Sticky pump active", false);
      setBinary("HP1 - Defrost", true);
      setBinary("HP1 - 4-Way valve", true);
      const hp1Power = wave(520, 16);
      const hp1Heat = wave(160, 30);
      const hp1Cop = Number((0.31 + Math.sin(t) * 0.03).toFixed(2));
      setNumber("Total Power Input", single ? hp1Power : wave(610, 50), "W");
      setNumber("Total Heat Power", single ? hp1Heat : wave(350, 40), "W");
      setNumber("Total COP", single ? hp1Cop : Number((0.62 + Math.sin(t) * 0.08).toFixed(2)));
      setNumber("Flow average (Selected)", wave(920, 50), "L/h");
      setNumber("Room Temperature (Selected)", wave(20.0, 0.08), "°C");
      setNumber("Room Setpoint (Selected)", 21.0, "°C");
      setNumber("Water Supply Temp (Selected)", wave(27.4, 0.4), "°C");
      setNumber("HP1 - Power Input", hp1Power, "W");
      setNumber("HP1 - Heat Power", hp1Heat, "W");
      setNumber("HP1 - COP", hp1Cop);
      setNumber("HP1 - Compressor frequency", waveInt(39, 2), "Hz");
      setNumber("HP1 - Fan speed", wave(676, 12), "rpm");
      setNumber("HP1 - Flow", wave(530, 20), "L/h");
      setNumber("HP1 - Evaporator coil temperature", wave(-4.4, 0.6), "\u00B0C");
      setNumber("HP1 - Inner coil temperature", wave(22.4, 0.4, 0.25), "\u00B0C");
      setNumber("HP1 - Outside temperature", wave(2.3, 0.18, 0.15), "\u00B0C");
      setNumber("HP1 - Condenser pressure", wave(15.4, 0.5), "bar");
      setNumber("HP1 - Gas discharge temperature", wave(47.8, 1.1), "\u00B0C");
      setNumber("HP1 - Evaporator pressure", wave(4.8, 0.2), "bar");
      setNumber("HP1 - Gas return temperature", wave(-1.8, 0.4), "\u00B0C");
      setNumber("HP1 - EEV steps", waveInt(188, 14), "p");
      setNumber("HP1 - Water in temperature", wave(29.8, 0.3), "°C");
      setNumber("HP1 - Water out temperature", wave(26.5, 0.3), "°C");
      setText("text_sensor", "HP1 - Working Mode Label", "Heating");
      setBinary("HP1 - Bottom plate heater", true);
      setBinary("HP1 - Crankcase heater", true);
      if (!single) {
        setNumber("HP2 - Power Input", wave(55, 4), "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - COP", 0);
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", wave(120, 12), "L/h");
        setNumber("HP2 - Evaporator coil temperature", wave(24.8, 0.3), "\u00B0C");
        setNumber("HP2 - Inner coil temperature", wave(26.2, 0.25, 0.1), "\u00B0C");
        setNumber("HP2 - Outside temperature", wave(2.1, 0.15, 0.1), "\u00B0C");
        setNumber("HP2 - Condenser pressure", wave(8.2, 0.2), "bar");
        setNumber("HP2 - Gas discharge temperature", wave(29.1, 0.4), "\u00B0C");
        setNumber("HP2 - Evaporator pressure", wave(7.9, 0.2), "bar");
        setNumber("HP2 - Gas return temperature", wave(25.0, 0.3), "\u00B0C");
        setNumber("HP2 - EEV steps", waveInt(24, 4), "p");
        setNumber("HP2 - Water in temperature", wave(26.1, 0.2), "°C");
        setNumber("HP2 - Water out temperature", wave(26.8, 0.2), "°C");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        setBinary("HP2 - Bottom plate heater", false);
        setBinary("HP2 - Crankcase heater", true);
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }

    if (isCoolingScenario(name)) {
      const limited = name === "cooling_limited" || name === "cooling_limiter_log";
      const waterAlreadyCold = name === "cooling_buffer_stop";
      const startupWait = name === "cooling_startup_wait";
      setText("text_sensor", "Control Mode (Label)", "CM5 - Cooling");
      setText("text_sensor", "Flow Mode", "Adaptive");
      setBinary("Cooling Enable (Selected)", true);
      setBinary("Cooling Request Active", true);
      setBinary("Cooling Permitted", true);
      setText("text_sensor", "Cooling Block Reason", "Ready");
      setNumber("HA - Cooling Dew Point", limited ? wave(18.4, 0.12, 0.2) : wave(15.9, 0.12, 0.2), "°C");
      setBinary("HA - Cooling Dew Point Valid", true);
      setNumber("MQTT Cooling Dew Point", limited ? wave(18.1, 0.12) : wave(16.2, 0.12), "°C");
      setBinary("MQTT Cooling Dew Point Valid", true);
      applyCoolingDewPointSourceSelection();
      setNumber("Cooling Minimum Safe Supply Temp", limited ? wave(20.0, 0.12) : wave(18.0, 0.15), "°C");
      setNumber("Cooling Effective Minimum Supply Temp", limited ? wave(20.2, 0.12) : wave(18.0, 0.15), "°C");
      setNumber("Cooling Fallback Night Minimum Outdoor Temp", wave(15.4, 0.1), "°C");
      setNumber("Cooling Fallback Minimum Supply Temp", wave(19.0, 0.1), "°C");
      setNumber("Cooling Supply Target", limited ? wave(20.5, 0.1) : wave(18.0, 0.12), "°C");
      setNumber("Cooling Supply Error", limited ? wave(0.35, 0.1) : wave(1.0, 0.2), "°C");
      setNumber("Cooling Demand (raw)", limited ? waveInt(7, 1) : waveInt(4, 1), "");
      setNumber("Cooling limited demand", limited ? 3 : 4, "");
      setNumber("Cooling limiter allowed max", limited ? 3 : 10, "");
      setSensorText("Cooling limiter reason code", limited ? "capacity_cap" : "full");
      setNumber("Cooling Power Input", limited ? wave(310, 14) : wave(455, 18), "W");
      setNumber("Total Power Input", limited ? wave(310, 14) : wave(455, 18), "W");
      setNumber("Total Heat Power", 0, "W");
      setNumber("Total Cooling Power", limited ? wave(1040, 70) : wave(1720, 90), "W");
      setNumber("Total COP", 0);
      setNumber("Total EER", Number(((limited ? 3.35 : 3.9) + Math.sin(t) * 0.08).toFixed(2)));
      setNumber("Flow average (Selected)", limited ? wave(720, 24) : wave(845, 26), "L/h");
      setNumber("Room Temperature (Selected)", limited ? wave(24.8, 0.08) : wave(24.2, 0.08), "°C");
      setNumber("Room Setpoint (Selected)", 23.0, "°C");
      setNumber("Water Supply Temp (Selected)", limited ? wave(20.8, 0.18) : wave(19.6, 0.2), "°C");
      setNumber("HP1 - Power Input", single ? (limited ? wave(305, 14) : wave(448, 18)) : 5.4, "W");
      setNumber("HP1 - Heat Power", 0, "W");
      setNumber("HP1 - Cooling Power", single ? (limited ? wave(1020, 70) : wave(1710, 90)) : 0, "W");
      setNumber("HP1 - COP", single ? Number(((limited ? 3.3 : 3.82) + Math.sin(t + 0.3) * 0.08).toFixed(2)) : 0);
      setNumber("HP1 - Compressor frequency", single ? (limited ? waveInt(25, 2, 0.3) : waveInt(33, 2, 0.3)) : 0, "Hz");
      setNumber("HP1 - Fan speed", single ? (limited ? wave(520, 12, 0.3) : wave(602, 14, 0.3)) : 0, "rpm");
      setNumber("HP1 - Flow", single ? (limited ? wave(720, 18, 0.3) : wave(842, 18, 0.3)) : 0, "L/h");
      setNumber("HP1 - Evaporator coil temperature", single ? (limited ? wave(10.8, 0.25, 0.3) : wave(8.2, 0.3, 0.3)) : wave(24.8, 0.2), "\u00B0C");
      setNumber("HP1 - Inner coil temperature", single ? (limited ? wave(14.0, 0.25, 0.2) : wave(12.0, 0.3, 0.2)) : wave(25.7, 0.2), "\u00B0C");
      setNumber("HP1 - Outside temperature", wave(29.2, 0.25), "\u00B0C");
      setNumber("HP1 - Condenser pressure", single ? (limited ? wave(15.8, 0.3, 0.3) : wave(17.8, 0.3, 0.3)) : wave(8.0, 0.1), "bar");
      setNumber("HP1 - Gas discharge temperature", single ? (limited ? wave(42.0, 0.7, 0.3) : wave(47.0, 0.8, 0.3)) : wave(27.6, 0.2), "\u00B0C");
      setNumber("HP1 - Evaporator pressure", single ? (limited ? wave(6.6, 0.15, 0.3) : wave(6.0, 0.15, 0.3)) : wave(7.8, 0.1), "bar");
      setNumber("HP1 - Gas return temperature", single ? (limited ? wave(12.1, 0.2, 0.3) : wave(10.4, 0.2, 0.3)) : wave(25.3, 0.2), "\u00B0C");
      setNumber("HP1 - EEV steps", single ? (limited ? waveInt(214, 10, 0.3) : waveInt(268, 12, 0.3)) : 0, "p");
      setNumber("HP1 - Water in temperature", single ? (limited ? wave(21.5, 0.18, 0.3) : wave(21.0, 0.2, 0.3)) : wave(20.8, 0.2), "°C");
      setNumber("HP1 - Water out temperature", single ? (limited ? wave(20.6, 0.18, 0.3) : wave(19.3, 0.2, 0.3)) : wave(20.1, 0.2), "°C");
      setText("text_sensor", "HP1 - Working Mode Label", single ? "Cooling" : "Standby");
      if (single) {
        setBinary("HP1 - 4-Way valve", true);
      }
      if (!single) {
        setNumber("HP2 - Power Input", limited ? wave(305, 14, 0.3) : wave(448, 18, 0.3), "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - Cooling Power", limited ? wave(1020, 70, 0.3) : wave(1710, 90, 0.3), "W");
        setNumber("HP2 - COP", Number(((limited ? 3.3 : 3.82) + Math.sin(t + 0.3) * 0.08).toFixed(2)));
        setNumber("HP2 - Compressor frequency", limited ? waveInt(25, 2, 0.3) : waveInt(33, 2, 0.3), "Hz");
        setNumber("HP2 - Fan speed", limited ? wave(520, 12, 0.3) : wave(602, 14, 0.3), "rpm");
        setNumber("HP2 - Flow", limited ? wave(720, 18, 0.3) : wave(842, 18, 0.3), "L/h");
        setNumber("HP2 - Evaporator coil temperature", limited ? wave(10.8, 0.25, 0.3) : wave(8.2, 0.3, 0.3), "\u00B0C");
        setNumber("HP2 - Inner coil temperature", limited ? wave(14.0, 0.25, 0.2) : wave(12.0, 0.3, 0.2), "\u00B0C");
        setNumber("HP2 - Outside temperature", wave(29.0, 0.25, 0.2), "\u00B0C");
        setNumber("HP2 - Condenser pressure", limited ? wave(15.8, 0.3, 0.3) : wave(17.8, 0.3, 0.3), "bar");
        setNumber("HP2 - Gas discharge temperature", limited ? wave(42.0, 0.7, 0.3) : wave(47.0, 0.8, 0.3), "\u00B0C");
        setNumber("HP2 - Evaporator pressure", limited ? wave(6.6, 0.15, 0.3) : wave(6.0, 0.15, 0.3), "bar");
        setNumber("HP2 - Gas return temperature", limited ? wave(12.1, 0.2, 0.3) : wave(10.4, 0.2, 0.3), "\u00B0C");
        setNumber("HP2 - EEV steps", limited ? waveInt(214, 10, 0.3) : waveInt(268, 12, 0.3), "p");
        setNumber("HP2 - Water in temperature", limited ? wave(21.5, 0.18, 0.3) : wave(21.0, 0.2, 0.3), "°C");
        setNumber("HP2 - Water out temperature", limited ? wave(20.6, 0.18, 0.3) : wave(19.3, 0.2, 0.3), "°C");
        setText("text_sensor", "HP2 - Working Mode Label", "Cooling");
        setBinary("HP2 - 4-Way valve", true);
      }
      if (name === "cooling_stop_reasons") {
        setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
        setBinary("Cooling Request Active", false);
        setBinary("Cooling Permitted", false);
        setText("text_sensor", "Cooling Block Reason", "Geen koelvraag");
        setText("text_sensor", "Flow Mode", "Standby");
        setNumber("Total Power Input", single ? 5.2 : 10.3, "W");
        setNumber("Total Cooling Power", 0, "W");
        setNumber("Total EER", 0);
        setNumber("Flow average (Selected)", 0, "L/h");
        setNumber("HP1 - Power Input", 5.2, "W");
        setNumber("HP1 - Cooling Power", 0, "W");
        setNumber("HP1 - Compressor frequency", 0, "Hz");
        setNumber("HP1 - Fan speed", 0, "rpm");
        setNumber("HP1 - Flow", 0, "L/h");
        setText("text_sensor", "HP1 - Working Mode Label", "Standby");
        setBinary("HP1 - 4-Way valve", false);
        if (!single) {
          setNumber("HP2 - Power Input", 5.2, "W");
          setNumber("HP2 - Cooling Power", 0, "W");
          setNumber("HP2 - Compressor frequency", 0, "Hz");
          setNumber("HP2 - Fan speed", 0, "rpm");
          setNumber("HP2 - Flow", 0, "L/h");
          setText("text_sensor", "HP2 - Working Mode Label", "Standby");
          setBinary("HP2 - 4-Way valve", false);
        }
      }
      if (waterAlreadyCold) {
        setNumber("Cooling limited demand", 0, "");
        setNumber("Cooling limiter allowed max", 0, "");
        setSensorText("Cooling limiter reason code", "buffer_stop");
        setNumber("Cooling Power Input", 0, "W");
        setNumber("Total Power Input", single ? 46 : 58, "W");
        setNumber("Total Cooling Power", 0, "W");
        setNumber("Total EER", 0);
        setNumber("Flow average (Selected)", wave(610, 18), "L/h");
        setNumber("Water Supply Temp (Selected)", wave(18.1, 0.08), "°C");
        setNumber("HP1 - Power Input", 5.2, "W");
        setNumber("HP1 - Cooling Power", 0, "W");
        setNumber("HP1 - Compressor frequency", 0, "Hz");
        setNumber("HP1 - Fan speed", 0, "rpm");
        setText("text_sensor", "HP1 - Working Mode Label", "Standby");
        if (!single) {
          setNumber("HP2 - Power Input", 5.2, "W");
          setNumber("HP2 - Cooling Power", 0, "W");
          setNumber("HP2 - Compressor frequency", 0, "Hz");
          setNumber("HP2 - Fan speed", 0, "rpm");
          setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        }
      }
      if (startupWait) {
        setNumber("Cooling Power Input", 0, "W");
        setNumber("Total Power Input", single ? 46 : 58, "W");
        setNumber("Total Cooling Power", 0, "W");
        setNumber("Total EER", 0);
        setNumber("HP1 - Power Input", 5.2, "W");
        setNumber("HP1 - Cooling Power", 0, "W");
        setNumber("HP1 - Compressor frequency", 0, "Hz");
        setNumber("HP1 - Fan speed", 0, "rpm");
        setText("text_sensor", "HP1 - Working Mode Label", "Standby");
        setBinary("HP1 - 4-Way valve", false);
        if (!single) {
          setNumber("HP2 - Power Input", 5.2, "W");
          setNumber("HP2 - Cooling Power", 0, "W");
          setNumber("HP2 - Compressor frequency", 0, "Hz");
          setNumber("HP2 - Fan speed", 0, "rpm");
          setText("text_sensor", "HP2 - Working Mode Label", "Standby");
          setBinary("HP2 - 4-Way valve", false);
        }
      }
      applyRuntimeControlOverlay(single);
      syncCommissioningEntities(single);
      return;
    }
  }

  function normalizeDateTimeValue(rawValue) {
    const value = String(rawValue || "").trim();
    if (!value) {
      return "";
    }
    const match = value.match(/^(\d{4})-(\d{2})-(\d{2})(?:[T\s](\d{2}):(\d{2})(?::(\d{2}))?)$/);
    if (!match) {
      return "";
    }
    const year = Number(match[1]);
    const month = Number(match[2]);
    const day = Number(match[3]);
    const hour = Number(match[4]);
    const minute = Number(match[5]);
    const second = Number(match[6] || "0");
    if ([year, month, day, hour, minute, second].some((part) => Number.isNaN(part))) {
      return "";
    }
    return `${String(year).padStart(4, "0")}-${String(month).padStart(2, "0")}-${String(day).padStart(2, "0")} ${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}:${String(second).padStart(2, "0")}`;
  }

  function parseDateTimeValue(rawValue) {
    const normalized = normalizeDateTimeValue(rawValue);
    if (!normalized || normalized === OPENQUATT_RESUME_CLEAR_VALUE) {
      return null;
    }
    const match = normalized.match(/^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2})$/);
    if (!match) {
      return null;
    }
    const date = new Date(
      Number(match[1]),
      Number(match[2]) - 1,
      Number(match[3]),
      Number(match[4]),
      Number(match[5]),
      Number(match[6]),
      0
    );
    return Number.isNaN(date.getTime()) ? null : date;
  }

  function applyOpenQuattResumeSchedule() {
    const resumeAt = parseDateTimeValue(getEntity("datetime", "OpenQuatt resume at")?.value || "");
    if (!resumeAt || isSwitchEnabled("OpenQuatt Enabled")) {
      return;
    }
    if (Date.now() >= resumeAt.getTime()) {
      const enabledEntity = getEntity("switch", "OpenQuatt Enabled");
      if (enabledEntity) {
        enabledEntity.value = true;
        enabledEntity.state = true;
      }
      setText("datetime", "OpenQuatt resume at", OPENQUATT_RESUME_CLEAR_VALUE);
    }
  }

  function applyRuntimeControlOverlay(single) {
    applyOpenQuattResumeSchedule();
    const openquattEnabled = isSwitchEnabled("OpenQuatt Enabled");
    const manualCoolingEnabled = isSwitchEnabled("Manual Cooling Enable");
    const coolingRoomRequestRequired = isSwitchEnabled("Cooling Room Request Required");
    const silentModeOverride = String(getEntity("select", "Silent Mode Override")?.value || "Schedule");
    const commissioningActive = Boolean(state.commissioning.cm100Active);

    if (silentModeOverride === "On") {
      setBinary("Silent active", true);
    } else if (silentModeOverride === "Off") {
      setBinary("Silent active", false);
    }

    if (manualCoolingEnabled) {
      setBinary("Cooling Enable (Selected)", true);
      const currentCoolingBlockReason = String(getEntity("text_sensor", "Cooling Block Reason")?.state || "");
      const waitingForRoomRequest =
        currentCoolingBlockReason === "Wacht op kamervraag" ||
        currentCoolingBlockReason === "Koeling toegestaan, wacht op kamertemperatuur boven koel-setpoint";
      if (!coolingRoomRequestRequired) {
        setBinary("Cooling Request Active", true);
      }
      if (!currentCoolingBlockReason || currentCoolingBlockReason === "Ready" || waitingForRoomRequest) {
        setText("text_sensor", "Cooling Block Reason", isCoolingScenario() || !coolingRoomRequestRequired ? "Ready" : "Koeling toegestaan, wacht op kamertemperatuur boven koel-setpoint");
      }
    }

    if (!openquattEnabled && !commissioningActive) {
      setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
      setBinary("Cooling Request Active", false);
      setBinary("Cooling Permitted", false);
      setText("text_sensor", "Cooling Block Reason", manualCoolingEnabled ? "OpenQuatt gepauzeerd" : "Koeling uitgeschakeld");
      setText("text_sensor", "Flow Mode", "Gepauzeerd");

      setNumber("Total Power Input", single ? 5.2 : 10.3, "W");
      setNumber("Heating Power Input", 0, "W");
      setNumber("Cooling Power Input", 0, "W");
      setNumber("Total Heat Power", 0, "W");
      setNumber("Total Cooling Power", 0, "W");
      setNumber("Total COP", 0, "");
      setNumber("Total EER", 0, "");
      setNumber("Flow average (Selected)", 0, "L/h");

      setNumber("HP1 - Power Input", 5.2, "W");
      setNumber("HP1 - Heat Power", 0, "W");
      setNumber("HP1 - Cooling Power", 0, "W");
      setNumber("HP1 - COP", 0, "");
      setNumber("HP1 - Compressor frequency", 0, "Hz");
      setNumber("HP1 - Fan speed", 0, "rpm");
      setNumber("HP1 - Flow", 0, "L/h");
      setText("text_sensor", "HP1 - Working Mode Label", "Standby");
      setBinary("HP1 - Defrost", false);
      setBinary("HP1 - 4-Way valve", false);

      if (!single) {
        setNumber("HP2 - Power Input", 5.1, "W");
        setNumber("HP2 - Heat Power", 0, "W");
        setNumber("HP2 - Cooling Power", 0, "W");
        setNumber("HP2 - COP", 0, "");
        setNumber("HP2 - Compressor frequency", 0, "Hz");
        setNumber("HP2 - Fan speed", 0, "rpm");
        setNumber("HP2 - Flow", 0, "L/h");
        setText("text_sensor", "HP2 - Working Mode Label", "Standby");
        setBinary("HP2 - Defrost", false);
        setBinary("HP2 - 4-Way valve", false);
      }
    }

    const override = String(getEntity("select", "CM Override")?.value || "Auto");
    if (!commissioningActive && override !== "Auto") {
      const labels = {
        "Force CM0": "CM0 - Standby (override)",
        "Force CM1": "CM1 - Circulation (override)",
        "Force CM98": "CM98 - Frost circulation (override)",
      };
      setText("text_sensor", "Control Mode (Label)", labels[override] || override);
    }

    syncOverviewTelemetry(single);
  }

  function stepSimulation(force = false) {
    state.tick += 1;
    if (state.autoAnimate || force) {
      applyScenario(state.scenario);
      updateSummary();
      notifyMockUpdated();
    }
  }

  function handleSelectSet(name, value) {
    const previousValue = String(getEntity("select", name)?.value || "");
    if (name === "Manual HP1 service mode" || name === "Manual HP2 service mode") {
      const hp = name.includes("HP1") ? "HP1" : "HP2";
      const otherName = hp === "HP1" ? "Manual HP2 service mode" : "Manual HP1 service mode";
      const flow = Number(getEntity("sensor", "Flow average (Selected)")?.value || 0);
      const otherMode = String(getEntity("select", otherName)?.value || "Standby");
      const requestedMode = String(value || "Standby");
      if (requestedMode !== "Standby" && state.commissioning.task !== "manual-hp") {
        state.commissioning.manualHpGuardStatusText = `${hp}: start de bediening eerst`;
        syncCommissioningEntities(state.installation === "single");
        updateSummary();
        notifyMockUpdated();
        return;
      }
      if (requestedMode !== "Standby" && flow < 250) {
        state.commissioning.manualHpGuardStatusText = `${hp}: wacht op voldoende flow`;
        syncCommissioningEntities(state.installation === "single");
        updateSummary();
        notifyMockUpdated();
        return;
      }
      if (requestedMode !== "Standby" && otherMode !== "Standby" && otherMode !== requestedMode) {
        state.commissioning.manualHpGuardStatusText = `${hp}: conflicterende werkmodus met ${hp === "HP1" ? "HP2" : "HP1"}`;
        syncCommissioningEntities(state.installation === "single");
        updateSummary();
        notifyMockUpdated();
        return;
      }
      const levelName = `Manual ${hp} compressor level`;
      state.commissioning[hp === "HP1" ? "manualHp1Level" : "manualHp2Level"] = 0;
      state.commissioning.manualHpGuardStatusText = "Vrijgegeven";
      setNumber(levelName, 0, "");
    }
    setText("select", name, value);
    if (previousValue !== String(value || "") &&
        (name === "Water Supply Source" ||
         (name === "Local Water Supply Temp Source" &&
          String(getEntity("select", "Water Supply Source")?.value || "") === "Local"))) {
      syncWaterSupplyCalibrationForMockSource();
    }
    if (name === "Preset") {
      applyPreset(value);
    } else if (name === "Firmware Update Channel") {
      clearOtaSimulation();
      setText("text_sensor", "OpenQuatt Release Channel", value);
      setText("text_sensor", "Firmware Update Status", "Idle");
      setNumber("Firmware Update Progress", 0, "%");
      const updateEntity = getEntity("update", "Firmware Update");
      const currentVersion = String(getEntity("text_sensor", "OpenQuatt Version")?.value || MOCK_STABLE_VERSION);
      const latestVersion = value === "main" ? MOCK_STABLE_VERSION : MOCK_DEV_VERSION;
      if (updateEntity) {
        updateEntity.current_version = currentVersion;
        updateEntity.latest_version = latestVersion;
        updateEntity.release_url = getMockReleaseUrl(value);
        if (value === "main" || currentVersion === latestVersion) {
          updateEntity.state = "up_to_date";
          updateEntity.value = "up_to_date";
          updateEntity.summary = "Je preview gebruikt nu het stabiele kanaal. Er staat op dit moment geen nieuwere stable release klaar.";
        } else {
          updateEntity.state = "available";
          updateEntity.value = "available";
          updateEntity.summary = "Het dev-kanaal heeft een nieuwere OTA-build beschikbaar voor deze preview.";
        }
      }
    } else if (name === "Firmware Update Target") {
      clearOtaSimulation();
      setText("text_sensor", "Firmware Update Status", "Idle");
      setNumber("Firmware Update Progress", 0, "%");
      const updateEntity = getEntity("update", "Firmware Update");
      const currentVersion = String(getEntity("text_sensor", "OpenQuatt Version")?.value || MOCK_STABLE_VERSION);
      const alternateBuild = value !== "current build";
      const targetConnection = value === "alternate connection" || value === "alternate topology and connection"
        ? state.connection === "wifi" ? "eth" : "wifi"
        : state.connection;
      const targetTopology = value === "alternate topology" || value === "alternate topology and connection"
        ? state.installation === "single" ? "duo" : "single"
        : state.installation;
      const targetLabel = `Heatpump Controller Q ${targetTopology === "duo" ? "Duo" : "Single"} ${targetConnection === "eth" ? "Ethernet" : "Wi-Fi"}`;
      if (updateEntity) {
        updateEntity.current_version = currentVersion;
        updateEntity.latest_version = alternateBuild ? currentVersion : MOCK_DEV_VERSION;
        updateEntity.release_url = getMockReleaseUrl(String(getEntity("select", "Firmware Update Channel")?.value || "dev"));
        updateEntity.state = alternateBuild ? "up_to_date" : "available";
        updateEntity.value = updateEntity.state;
        updateEntity.summary = alternateBuild
          ? `${targetLabel} is als alternatieve target-build geselecteerd voor deze preview.`
          : "Normale firmware-update geselecteerd voor deze preview.";
      }
    } else if (name === "Debug Level") {
      appendLogHistoryEntry(`[I][oq_fw:376]: Runtime logger level updated to ${value}`);
    } else if (name === "Uurdetail bewaren") {
      state.energyHistoryHourRetention = value;
    } else if (name === "Power House response profile") {
      if (value === "Calm") {
        setNumber("Power House demand rise time", 12);
        setNumber("Power House demand fall time", 5);
      } else if (value === "Balanced") {
        setNumber("Power House demand rise time", 8);
        setNumber("Power House demand fall time", 3);
      } else if (value === "Responsive") {
        setNumber("Power House demand rise time", 5);
        setNumber("Power House demand fall time", 2);
      }
    }
    applyScenario(state.scenario);
    if (name === "Cooling Dew Point Source") {
      applyCoolingDewPointSourceSelection();
    }
    updateSummary();
    notifyMockUpdated();
  }

  function handleNumberSet(name, value) {
    const previousValue = getEntity("number", name)?.value;
    setNumber(name, Number(value));
    if (name === "Water Supply HA Input Calibration Offset" &&
        getEntity("text", "Water Supply HA Input Calibration Identity")?.value !== MOCK_HA_CALIBRATION_IDENTITY) {
      setNumber(name, previousValue);
      setText("text", "Water Supply HA Input Calibration Identity",
        Number.isFinite(Number(previousValue)) ? MOCK_HA_CALIBRATION_IDENTITY : "");
      syncWaterSupplyCalibrationForMockSource();
    } else if (name === currentWaterSupplyCalibrationBridgeName()) {
      setNumber("Water Supply Temperature Calibration Offset", Number(value), "\u00B0C");
      setBinary("Water Supply Temperature Calibration Required", false);
      setText("text_sensor", "Water Supply Temperature Calibration Status", `Calibrated: ${currentWaterSupplySourceLabel()}`);
    } else if (name === "Manual flow service setpoint") {
      state.commissioning.manualFlowSetpoint = Number(value);
    } else if (name === "Manual HP1 compressor level") {
      state.commissioning.manualHp1Level = Number(value);
      setNumber("HP1 compressor level", Number(value), "");
    } else if (name === "Manual HP2 compressor level") {
      state.commissioning.manualHp2Level = Number(value);
      setNumber("HP2 compressor level", Number(value), "");
    }
    syncOverviewTelemetry(state.installation === "single");
    syncCommissioningEntities(state.installation === "single");
    updateSummary();
    notifyMockUpdated();
  }

  function handleTimeSet(name, value) {
    const normalized = String(value || "").trim().length === 5 ? `${value}:00` : String(value || "");
    setText("time", name, normalized);
    updateSummary();
    notifyMockUpdated();
  }

  function handleDateTimeSet(name, value) {
    const normalized = normalizeDateTimeValue(value) || OPENQUATT_RESUME_CLEAR_VALUE;
    setText("datetime", name, normalized);
    updateSummary();
    notifyMockUpdated();
  }

  function handleTextSet(name, value) {
    setText("text", name, String(value || "").trim());
    updateSummary();
    notifyMockUpdated();
  }

  function handleSwitchSet(name, enabled) {
    const entity = getEntity("switch", name);
    if (!entity) {
      return;
    }
    entity.value = Boolean(enabled);
    entity.state = Boolean(enabled);
    if (name === "Usage statistics") {
      setEntity("binary_sensor", "Usage statistics choice configured", { value: true, state: true });
    }
    if (name === "Boiler assist enabled" && !enabled) {
      setSwitch("Boiler fallback on heat-pump fault", false);
    }
    if (name === "Quick flow test") {
      handleButtonPress(enabled ? "Quick Flow Test Start" : "Quick Flow Test Abort");
      updateSummary();
      notifyMockUpdated();
      return;
    }
    if (name === "OpenQuatt Enabled" && enabled && getEntity("datetime", "OpenQuatt resume at")) {
      setText("datetime", "OpenQuatt resume at", OPENQUATT_RESUME_CLEAR_VALUE);
    }
    if (name === "RAM log history") {
      state.logHistoryEnabled = Boolean(enabled);
    }
    applyScenario(state.scenario);
    updateSummary();
    notifyMockUpdated();
  }

  function getOduRuntimeDesiredTable(hp, mode) {
    return ODU_RUNTIME_FREQUENCY_LEVELS.map((level) => (
      Number(getEntity("number", oduRuntimeValueName(hp, mode, level))?.value)
    ));
  }

  function validateOduRuntimeTable(values) {
    let previous = -Infinity;
    for (const value of values) {
      if (!Number.isFinite(value) || value < 0 || value > 120 || value < previous) {
        return false;
      }
      previous = value;
    }
    return true;
  }

  function setOduRuntimeStatus(hp, status) {
    setText("text_sensor", oduRuntimeControlName(hp, "status"), status);
  }

  function handleOduRuntimeLoad(hp) {
    const table = state.oduRuntimeFrequency[hp];
    if (!table) {
      return;
    }
    setOduRuntimeStatus(hp, "LOAD_REQUESTED");
    window.setTimeout(() => {
      ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
        ODU_RUNTIME_FREQUENCY_LEVELS.forEach((level) => {
          setNumber(oduRuntimeValueName(hp, mode, level), table[mode][level], "Hz");
        });
      });
      setOduRuntimeStatus(hp, "LOADED: 22/22 runtime registers");
      notifyMockUpdated();
    }, 320);
  }

  function handleOduRuntimeApply(hp) {
    const enable = getEntity("switch", oduRuntimeControlName(hp, "enable"));
    if (!enable?.value) {
      setOduRuntimeStatus(hp, "BLOCKED: enable switch is off");
      return;
    }

    setOduRuntimeStatus(hp, "GUARD_READ_REQUESTED: checking ODU state");
    const mode = String(getEntity("text_sensor", `${hp} - Working Mode Label`)?.value || "").trim();
    const compressorHz = Number(getEntity("sensor", `${hp} - Compressor frequency`)?.value);
    if (!mode || /unknown|onbekend/i.test(mode)) {
      setOduRuntimeStatus(hp, "BLOCKED: ODU mode unknown");
      return;
    }
    if (!/standby|stand-by/i.test(mode)) {
      setOduRuntimeStatus(hp, "BLOCKED: ODU is not in standby");
      return;
    }
    if (!Number.isFinite(compressorHz)) {
      setOduRuntimeStatus(hp, "BLOCKED: compressor frequency unknown");
      return;
    }
    if (compressorHz > 0.5) {
      setOduRuntimeStatus(hp, "BLOCKED: compressor is running");
      return;
    }

    const cooling = getOduRuntimeDesiredTable(hp, "cooling");
    const heating = getOduRuntimeDesiredTable(hp, "heating");
    if (!validateOduRuntimeTable(cooling)) {
      setOduRuntimeStatus(hp, "BLOCKED: invalid cooling table");
      return;
    }
    if (!validateOduRuntimeTable(heating)) {
      setOduRuntimeStatus(hp, "BLOCKED: invalid heating table");
      return;
    }

    state.oduRuntimeFrequency[hp].cooling = cooling;
    state.oduRuntimeFrequency[hp].heating = heating;
    enable.value = false;
    enable.state = false;
    setOduRuntimeStatus(hp, "WRITE_QUEUED: runtime table write requested");
    window.setTimeout(() => {
      setOduRuntimeStatus(hp, "WRITE_CONFIRMED: runtime write acknowledged");
      window.setTimeout(() => {
        setOduRuntimeStatus(hp, "APPLIED: runtime table written and read back");
        notifyMockUpdated();
      }, 320);
      notifyMockUpdated();
    }, 320);
  }

  function handleButtonPress(name) {
    const oduRuntimeButton = parseOduRuntimeButtonName(name);
    if (oduRuntimeButton) {
      if (oduRuntimeButton.action === "load") {
        handleOduRuntimeLoad(oduRuntimeButton.hp);
      } else {
        handleOduRuntimeApply(oduRuntimeButton.hp);
      }
      updateSummary();
      notifyMockUpdated();
      notifyDevControlsChanged();
      return;
    }

    if (name === "CM100 Start") {
      clearQuickFlowTestTimer();
      clearCommissioningTimers();
      state.commissioning.cm100Active = true;
      state.commissioning.globalStatus = "CM100 READY";
      setCommissioningPhase("none", "idle", {
        boilerResult: state.commissioning.boilerResult || 0,
        boilerConfidence: state.commissioning.boilerConfidence || 0,
        flowKpSuggested: state.commissioning.flowKpSuggested || 0,
        flowKiSuggested: state.commissioning.flowKiSuggested || 0,
      });
      setText("text_sensor", "Control Mode (Label)", "CM100 - Commissioning");
      setText("text_sensor", "Flow Mode", "CM100 idle");
      setText("text_sensor", "Commissioning status", "CM100 READY");
      state.commissioning.boilerStatusText = "IDLE";
      state.commissioning.autotuneStatusText = "IDLE";
      state.commissioning.airPurgeStatusText = "IDLE";
      state.commissioning.airPurgeRemaining = 0;
      state.commissioning.airPurgePhase = 0;
      state.commissioning.airPurgeTargetIpwm = 0;
      state.commissioning.manualFlowStatusText = "IDLE";
      state.commissioning.manualHpStatusText = "IDLE";
      state.commissioning.manualHpGuardStatusText = "Vrijgegeven";
      state.commissioning.manualHp1Level = 0;
      state.commissioning.manualHp2Level = 0;
      resetHpWaterCalibrationMock();
      setText("text_sensor", "Boiler power test status", "IDLE");
      setText("text_sensor", "Flow Autotune status", "IDLE");
      setText("text_sensor", "Air purge status", "IDLE");
      setText("text_sensor", "Manual flow status", "IDLE");
      setText("text_sensor", "Manual HP status", "IDLE");
      setText("text_sensor", "Manual HP guard status", "Vrijgegeven");
      setNumber("Air purge remaining", 0, "s");
      setNumber("Air purge phase", 0, "");
      setNumber("Air purge target iPWM", 0, "iPWM");
      setNumber("Manual HP1 compressor level", 0, "");
      setNumber("Manual HP2 compressor level", 0, "");
      setText("select", "Manual HP1 service mode", "Standby");
      setText("select", "Manual HP2 service mode", "Standby");
      setNumber("Flow average (Selected)", 0, "L/h");
      setBinary("Boiler power test active", false);
      setBinary("Air purge active", false);
      setBinary("Manual flow active", false);
      const quickFlowTest = getEntity("switch", "Quick flow test");
      if (quickFlowTest) {
        quickFlowTest.value = false;
        quickFlowTest.state = false;
      }
      setBinary("Manual HP active", false);
      setBinary("Boiler active", false);
    } else if (name === "CM100 Stop") {
      clearQuickFlowTestTimer();
      clearCommissioningTimers();
      state.commissioning.cm100Active = false;
      state.commissioning.globalStatus = "CM100 STOPPED";
      setCommissioningPhase("none", "idle");
      setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
      setText("text_sensor", "Flow Mode", "Gepauzeerd");
      setText("text_sensor", "Commissioning status", "CM100 STOPPED");
      state.commissioning.boilerStatusText = "IDLE";
      state.commissioning.autotuneStatusText = "IDLE";
      state.commissioning.airPurgeStatusText = "IDLE";
      state.commissioning.airPurgeRemaining = 0;
      state.commissioning.airPurgePhase = 0;
      state.commissioning.airPurgeTargetIpwm = 0;
      state.commissioning.manualFlowStatusText = "IDLE";
      state.commissioning.manualHpStatusText = "IDLE";
      state.commissioning.manualHpGuardStatusText = "Vrijgegeven";
      state.commissioning.manualHp1Level = 0;
      state.commissioning.manualHp2Level = 0;
      resetHpWaterCalibrationMock();
      setText("text_sensor", "Boiler power test status", "IDLE");
      setText("text_sensor", "Flow Autotune status", "IDLE");
      setText("text_sensor", "Air purge status", "IDLE");
      setText("text_sensor", "Manual flow status", "IDLE");
      setText("text_sensor", "Manual HP status", "IDLE");
      setText("text_sensor", "Manual HP guard status", "Vrijgegeven");
      setNumber("Air purge remaining", 0, "s");
      setNumber("Air purge phase", 0, "");
      setNumber("Air purge target iPWM", 0, "iPWM");
      setNumber("Manual HP1 compressor level", 0, "");
      setNumber("Manual HP2 compressor level", 0, "");
      setText("select", "Manual HP1 service mode", "Standby");
      setText("select", "Manual HP2 service mode", "Standby");
      setBinary("CM100 active", false);
      setBinary("Boiler power test active", false);
      setBinary("Air purge active", false);
      setBinary("Manual flow active", false);
      const quickFlowTest = getEntity("switch", "Quick flow test");
      if (quickFlowTest) {
        quickFlowTest.value = false;
        quickFlowTest.state = false;
      }
      setBinary("Manual HP active", false);
      setBinary("Boiler active", false);
    } else if (name === "Boiler Power Test Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.boilerStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "Boiler power test status", "REFUSED: CM100 required");
      } else {
        clearQuickFlowTestTimer();
        clearCommissioningTimers();
        state.commissioning.globalStatus = "BOILER TEST STARTED";
        setCommissioningPhase("boiler", "requested", {
          boilerResult: state.commissioning.boilerResult || 0,
          boilerConfidence: state.commissioning.boilerConfidence || 0,
        });
        setText("text_sensor", "Control Mode (Label)", "CM100 - Commissioning");
        setText("text_sensor", "Flow Mode", "CM100 boiler test");
        setNumber("Flow average (Selected)", 800, "L/h");
        setBinary("Boiler power test active", true);
        state.commissioning.boilerStatusText = "REQUESTED";
        setText("text_sensor", "Boiler power test status", "REQUESTED");
        setText("text_sensor", "Commissioning status", "BOILER TEST STARTED");
        scheduleCommissioningStep(700, () => {
          setCommissioningPhase("boiler", "flow_settling");
          state.commissioning.boilerStatusText = "FLOW_SETTLING";
          setText("text_sensor", "Boiler power test status", "FLOW_SETTLING");
        });
        scheduleCommissioningStep(3700, () => {
          setCommissioningPhase("boiler", "boiler_settling");
          state.commissioning.boilerStatusText = "BOILER_SETTLING";
          setText("text_sensor", "Boiler power test status", "BOILER_SETTLING");
          setBinary("Boiler active", true);
          setNumber("Boiler Heat Power", 0, "W");
        });
        scheduleCommissioningStep(6700, () => {
          setCommissioningPhase("boiler", "measuring");
          state.commissioning.boilerStatusText = "MEASURING";
          setText("text_sensor", "Boiler power test status", "MEASURING");
          setBinary("Boiler active", true);
          setNumber("Boiler Heat Power", 1803, "W");
          setNumber("Flow average (Selected)", 802, "L/h");
        });
        scheduleCommissioningStep(9700, () => {
          setCommissioningPhase("boiler", "done", {
            boilerResult: 1803,
            boilerConfidence: 65,
          });
          state.commissioning.boilerStatusText = "DONE: 1803W (conf 65%)";
          setText("text_sensor", "Boiler power test status", "DONE: 1803W (conf 65%)");
          setText("text_sensor", "Commissioning status", "CM100 READY");
          setBinary("Boiler power test active", false);
          setBinary("Boiler active", false);
          setNumber("Boiler Heat Power", 0, "W");
          state.commissioning.globalStatus = "CM100 READY";
        });
      }
    } else if (name === "Boiler Power Test Abort") {
      clearCommissioningTimers();
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      setCommissioningPhase("boiler", "aborted");
      state.commissioning.boilerStatusText = "ABORTED";
      setText("text_sensor", "Boiler power test status", "ABORTED");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
      setBinary("Boiler power test active", false);
      setBinary("Boiler active", false);
      setNumber("Boiler Heat Power", 0, "W");
    } else if (name === "Boiler Power Test Apply") {
      const rounded = roundToHundred(state.commissioning.boilerResult || Number(getEntity("sensor", "Boiler power test result")?.value || 0));
      setNumber("Boiler rated heat power", rounded, "W");
      state.commissioning.boilerStatusText = `APPLIED: ${rounded}W`;
      setText("text_sensor", "Boiler power test status", `APPLIED: ${rounded}W`);
      setCommissioningPhase("boiler", "applied", {
        boilerResult: rounded,
      });
      state.commissioning.globalStatus = "CM100 READY";
    } else if (name === "Flow Autotune Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.autotuneStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "Flow Autotune status", "REFUSED: CM100 required");
      } else {
        clearCommissioningTimers();
        state.commissioning.globalStatus = "FLOW AUTOTUNE STARTED";
        setCommissioningPhase("autotune", "requested", {
          flowKpSuggested: state.commissioning.flowKpSuggested || 0,
          flowKiSuggested: state.commissioning.flowKiSuggested || 0,
        });
        setText("text_sensor", "Control Mode (Label)", "CM100 - Commissioning");
        setText("text_sensor", "Flow Mode", "CM100 flow autotune");
        setNumber("Flow average (Selected)", 790, "L/h");
        state.commissioning.autotuneStatusText = "REQUESTED";
        setText("text_sensor", "Flow Autotune status", "REQUESTED");
        setText("text_sensor", "Commissioning status", "FLOW AUTOTUNE STARTED");
        scheduleCommissioningStep(700, () => {
          setCommissioningPhase("autotune", "baseline_settling");
          state.commissioning.autotuneStatusText = "BASELINE_SETTLING";
          setText("text_sensor", "Flow Autotune status", "BASELINE_SETTLING");
        });
        scheduleCommissioningStep(1700, () => {
          setCommissioningPhase("autotune", "step_test");
          state.commissioning.autotuneStatusText = "STEP_TEST";
          setText("text_sensor", "Flow Autotune status", "STEP_TEST");
          setNumber("Flow average (Selected)", 798, "L/h");
        });
        scheduleCommissioningStep(2900, () => {
          setCommissioningPhase("autotune", "measuring", {
            flowKpSuggested: 0.42,
            flowKiSuggested: 0.08,
          });
          state.commissioning.autotuneStatusText = "MEASURING";
          setText("text_sensor", "Flow Autotune status", "MEASURING");
        });
        scheduleCommissioningStep(4200, () => {
          setCommissioningPhase("autotune", "done", {
            flowKpSuggested: 0.42,
            flowKiSuggested: 0.08,
          });
          state.commissioning.autotuneStatusText = "DONE: Kp 0.42 Ki 0.08";
          setText("text_sensor", "Flow Autotune status", "DONE: Kp 0.42 Ki 0.08");
          setText("text_sensor", "Commissioning status", "CM100 READY");
          state.commissioning.globalStatus = "CM100 READY";
        });
      }
    } else if (name === "Flow Autotune Abort") {
      clearCommissioningTimers();
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      setCommissioningPhase("autotune", "aborted");
      state.commissioning.autotuneStatusText = "ABORTED";
      setText("text_sensor", "Flow Autotune status", "ABORTED");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
    } else if (name === "Apply Flow Autotune Kp-Ki") {
      const kp = Number(state.commissioning.flowKpSuggested || getEntity("number", "Flow Autotune Kp suggested")?.value || 0);
      const ki = Number(state.commissioning.flowKiSuggested || getEntity("number", "Flow Autotune Ki suggested")?.value || 0);
      setNumber("Flow PI Kp", kp, "");
      setNumber("Flow PI Ki", ki, "");
      state.commissioning.autotuneStatusText = `APPLIED: Kp ${kp.toFixed(2)} Ki ${ki.toFixed(2)}`;
      setText("text_sensor", "Flow Autotune status", `APPLIED: Kp ${kp.toFixed(2)} Ki ${ki.toFixed(2)}`);
      setCommissioningPhase("autotune", "applied", {
        flowKpSuggested: kp,
        flowKiSuggested: ki,
      });
      state.commissioning.globalStatus = "CM100 READY";
    } else if (name === "Air Purge Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.airPurgeStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "Air purge status", "REFUSED: CM100 required");
      } else {
        clearCommissioningTimers();
        state.commissioning.globalStatus = "AIR PURGE STARTED";
        state.commissioning.airPurgeStatusText = "PHASE1_STEADY";
        state.commissioning.airPurgeRemaining = 300;
        state.commissioning.airPurgePhase = 1;
        state.commissioning.airPurgeTargetIpwm = 800;
        setCommissioningPhase("purge", "steady");
        setText("text_sensor", "Control Mode (Label)", "CM100 - Commissioning");
        setText("text_sensor", "Flow Mode", "CM100 air purge");
        setText("text_sensor", "Commissioning status", "AIR PURGE STARTED");
        setText("text_sensor", "Air purge status", "PHASE1_STEADY");
        setBinary("Air purge active", true);
        setNumber("Air purge remaining", 300, "s");
        setNumber("Air purge phase", 1, "");
        setNumber("Air purge target iPWM", 800, "iPWM");
        setNumber("Flow average (Selected)", 680, "L/h");
        scheduleCommissioningStep(1200, () => {
          setCommissioningPhase("purge", "pulse_hard");
          state.commissioning.airPurgeStatusText = "PHASE2_PULSE_HARD";
          state.commissioning.airPurgeRemaining = 220;
          state.commissioning.airPurgePhase = 2;
          state.commissioning.airPurgeTargetIpwm = 300;
          setText("text_sensor", "Air purge status", "PHASE2_PULSE_HARD");
        });
        scheduleCommissioningStep(2300, () => {
          setCommissioningPhase("purge", "pulse_rest");
          state.commissioning.airPurgeStatusText = "PHASE2_PULSE_REST";
          state.commissioning.airPurgeRemaining = 150;
          state.commissioning.airPurgePhase = 2;
          state.commissioning.airPurgeTargetIpwm = 800;
          setText("text_sensor", "Air purge status", "PHASE2_PULSE_REST");
        });
        scheduleCommissioningStep(3400, () => {
          setCommissioningPhase("purge", "stabilize");
          state.commissioning.airPurgeStatusText = "PHASE3_STABILIZE";
          state.commissioning.airPurgeRemaining = 55;
          state.commissioning.airPurgePhase = 3;
          state.commissioning.airPurgeTargetIpwm = 650;
          setText("text_sensor", "Air purge status", "PHASE3_STABILIZE");
        });
        scheduleCommissioningStep(4700, () => {
          const returnToAuto = isSwitchEnabled("Air purge return to Auto");
          setCommissioningPhase("purge", "done");
          state.commissioning.airPurgeStatusText = "DONE";
          state.commissioning.airPurgeRemaining = 0;
          state.commissioning.airPurgePhase = 0;
          state.commissioning.airPurgeTargetIpwm = 0;
          setText("text_sensor", "Air purge status", "DONE");
          setBinary("Air purge active", false);
          setNumber("Air purge remaining", 0, "s");
          setNumber("Air purge phase", 0, "");
          setNumber("Air purge target iPWM", 0, "iPWM");
          if (returnToAuto) {
            state.commissioning.cm100Active = false;
            state.commissioning.globalStatus = "IDLE";
            setBinary("CM100 active", false);
            setText("text_sensor", "Commissioning status", "IDLE");
          } else {
            state.commissioning.globalStatus = "CM100 READY";
            setText("text_sensor", "Commissioning status", "CM100 READY");
            setText("text_sensor", "Flow Mode", "CM100 idle");
          }
        });
      }
    } else if (name === "Air Purge Abort") {
      clearCommissioningTimers();
      setCommissioningPhase("purge", "aborted");
      state.commissioning.airPurgeStatusText = "ABORTED";
      state.commissioning.airPurgeRemaining = 0;
      state.commissioning.airPurgePhase = 0;
      state.commissioning.airPurgeTargetIpwm = 0;
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      setText("text_sensor", "Air purge status", "ABORTED");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
      setBinary("Air purge active", false);
      setNumber("Air purge remaining", 0, "s");
      setNumber("Air purge phase", 0, "");
      setNumber("Air purge target iPWM", 0, "iPWM");
      if (state.commissioning.cm100Active) {
        setText("text_sensor", "Flow Mode", "CM100 idle");
      }
    } else if (name === "HP Water Calibration Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.hpWaterCalibrationStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "HP water calibration status", "REFUSED: CM100 required");
      } else {
        clearCommissioningTimers();
        state.commissioning.globalStatus = "HP WATER CAL STARTED";
        state.commissioning.hpWaterCalibrationStatusText = "REQUESTED";
        state.commissioning.hpWaterCalibrationRemaining = 420;
        state.commissioning.hpWaterCalibrationPhase = 1;
        state.commissioning.hpWaterCalibrationSpread = NaN;
        state.commissioning.hpWaterCalibrationSupplyDelta = NaN;
        state.commissioning.hpWaterCalibrationStableProgress = 0;
        state.commissioning.hpWaterCalibrationStableRequired = 60;
        state.commissioning.hpWaterCalibrationResultReference = NaN;
        state.commissioning.hpWaterCalibrationResultSpreadBefore = NaN;
        state.commissioning.hpWaterCalibrationResultExpectedSpread = NaN;
        state.commissioning.hpWaterCalibrationResultSupplySource = "";
        setCommissioningPhase("hp-water-calibration", "requested");
        setText("text_sensor", "Control Mode (Label)", "CM100 - Commissioning");
        setText("text_sensor", "Flow Mode", "HP WATER CAL");
        setText("text_sensor", "Commissioning status", "HP WATER CAL STARTED");
        setText("text_sensor", "HP water calibration status", "REQUESTED");
        setBinary("HP water calibration active", true);
        setNumber("HP water calibration remaining", 420, "s");
        setNumber("HP water calibration phase", 1, "");
        setNumber("HP water calibration stable window progress", 0, "s");
        setNumber("HP water calibration stable window required", 60, "s");
        setNumber("Flow average (Selected)", 735, "L/h");
        scheduleCommissioningStep(800, () => {
          setCommissioningPhase("hp-water-calibration", "mixing");
          state.commissioning.hpWaterCalibrationStatusText = "MIXING";
          state.commissioning.hpWaterCalibrationRemaining = 300;
          state.commissioning.hpWaterCalibrationPhase = 2;
          setText("text_sensor", "HP water calibration status", "MIXING");
        });
        scheduleCommissioningStep(2200, () => {
          setCommissioningPhase("hp-water-calibration", "measuring");
          state.commissioning.hpWaterCalibrationStatusText = "MEASURING";
          state.commissioning.hpWaterCalibrationRemaining = 120;
          state.commissioning.hpWaterCalibrationPhase = 3;
          state.commissioning.hpWaterCalibrationSpread = 0.16;
          state.commissioning.hpWaterCalibrationStableProgress = 24;
          setText("text_sensor", "HP water calibration status", "MEASURING");
        });
        scheduleCommissioningStep(3900, () => {
          const single = state.installation === "single";
          const hp1In = Number(getEntity("sensor", "HP1 - Water in temperature raw")?.value || getEntity("sensor", "HP1 - Water in temperature")?.value || 25.08);
          const hp1Out = Number(getEntity("sensor", "HP1 - Water out temperature raw")?.value || getEntity("sensor", "HP1 - Water out temperature")?.value || 25.34);
          const hp2In = Number(getEntity("sensor", "HP2 - Water in temperature raw")?.value || getEntity("sensor", "HP2 - Water in temperature")?.value || hp1In + 0.07);
          const hp2Out = Number(getEntity("sensor", "HP2 - Water out temperature raw")?.value || getEntity("sensor", "HP2 - Water out temperature")?.value || hp1Out - 0.05);
          const values = single ? [hp1In, hp1Out] : [hp1In, hp1Out, hp2In, hp2Out];
          const reference = values.reduce((sum, value) => sum + value, 0) / values.length;
          const supplySelected = Number(getEntity("sensor", "Water Supply Temp (Selected)")?.value);
          const calibrationValid = !Boolean(getEntity("binary_sensor", "Water Supply Temperature Calibration Required")?.value) &&
            String(getEntity("text_sensor", "Water Supply Temperature Calibration Status")?.value || "").startsWith("Calibrated:");
          const activeSupplyOffset = calibrationValid
            ? Number(getEntity("number", "Water Supply Temperature Calibration Offset")?.value || 0)
            : 0;
          const supply = Number.isFinite(supplySelected) ? supplySelected - activeSupplyOffset : NaN;
          state.commissioning.hpWaterCalibrationSuggested.hp1In = Number((reference - hp1In).toFixed(2));
          state.commissioning.hpWaterCalibrationSuggested.hp1Out = Number((reference - hp1Out).toFixed(2));
          state.commissioning.hpWaterCalibrationSuggested.hp2In = single ? 0 : Number((reference - hp2In).toFixed(2));
          state.commissioning.hpWaterCalibrationSuggested.hp2Out = single ? 0 : Number((reference - hp2Out).toFixed(2));
          state.commissioning.hpWaterCalibrationSuggested.supply = Number.isFinite(supply) ? Number((reference - supply).toFixed(2)) : 0;
          state.commissioning.hpWaterCalibrationSpread = Number((Math.max(...values) - Math.min(...values)).toFixed(2));
          state.commissioning.hpWaterCalibrationSupplyDelta = Number.isFinite(supply) ? Number((reference - supply).toFixed(2)) : NaN;
          state.commissioning.hpWaterCalibrationStableProgress = 60;
          state.commissioning.hpWaterCalibrationResultReference = Number(reference.toFixed(2));
          state.commissioning.hpWaterCalibrationResultSpreadBefore = state.commissioning.hpWaterCalibrationSpread;
          state.commissioning.hpWaterCalibrationResultExpectedSpread = 0;
          state.commissioning.hpWaterCalibrationResultRawAverages.hp1In = Number(hp1In.toFixed(2));
          state.commissioning.hpWaterCalibrationResultRawAverages.hp1Out = Number(hp1Out.toFixed(2));
          state.commissioning.hpWaterCalibrationResultRawAverages.hp2In = single ? NaN : Number(hp2In.toFixed(2));
          state.commissioning.hpWaterCalibrationResultRawAverages.hp2Out = single ? NaN : Number(hp2Out.toFixed(2));
          state.commissioning.hpWaterCalibrationResultRawAverages.supply = Number.isFinite(supply) ? Number(supply.toFixed(2)) : NaN;
          state.commissioning.hpWaterCalibrationResultSupplySource = currentWaterSupplySourceLabel();
          state.commissioning.hpWaterCalibrationRemaining = 0;
          state.commissioning.hpWaterCalibrationPhase = 4;
          state.commissioning.hpWaterCalibrationStatusText = single ? "DONE: HP1 and supply offsets" : "DONE: 4 HP and supply offsets";
          state.commissioning.globalStatus = "CM100 READY";
          setCommissioningPhase("hp-water-calibration", "done");
          setText("text_sensor", "HP water calibration status", state.commissioning.hpWaterCalibrationStatusText);
          setText("text_sensor", "Commissioning status", "CM100 READY");
          setBinary("HP water calibration active", false);
          setText("text_sensor", "Flow Mode", "CM100 idle");
        });
      }
    } else if (name === "HP Water Calibration Abort") {
      clearCommissioningTimers();
      setCommissioningPhase("hp-water-calibration", "aborted");
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      resetHpWaterCalibrationMock("ABORTED");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
      setText("text_sensor", "Flow Mode", state.commissioning.cm100Active ? "CM100 idle" : "Gepauzeerd");
      setNumber("Flow average (Selected)", 0, "L/h");
    } else if (name === "Apply HP Water Calibration Offsets") {
      const suggested = state.commissioning.hpWaterCalibrationSuggested;
      setNumber("HP1 water in temperature offset", suggested.hp1In, "\u00B0C");
      setNumber("HP1 water out temperature offset", suggested.hp1Out, "\u00B0C");
      setNumber("HP2 water in temperature offset", suggested.hp2In, "\u00B0C");
      setNumber("HP2 water out temperature offset", suggested.hp2Out, "\u00B0C");
      setNumber("Water Supply Temperature Calibration Offset", suggested.supply, "\u00B0C");
      setNumber(currentWaterSupplyCalibrationBridgeName(), suggested.supply, "\u00B0C");
      if (currentWaterSupplyCalibrationBridgeName() === "Water Supply HA Input Calibration Offset") {
        setText("text", "Water Supply HA Input Calibration Identity", MOCK_HA_CALIBRATION_IDENTITY);
      }
      setBinary("Water Supply Temperature Calibration Required", false);
      setText("text_sensor", "Water Supply Temperature Calibration Status", `Calibrated: ${state.commissioning.hpWaterCalibrationResultSupplySource || currentWaterSupplySourceLabel()}`);
      [
        ["HP1", "in"],
        ["HP1", "out"],
        ["HP2", "in"],
        ["HP2", "out"],
      ].forEach(([hp, side]) => {
        const raw = getEntity("sensor", `${hp} - Water ${side} temperature raw`);
        if (raw) {
          setNumber(`${hp} - Water ${side} temperature`, raw.value, raw.uom || "\u00B0C");
        }
      });
      state.commissioning.hpWaterCalibrationStatusText = "APPLIED";
      state.commissioning.globalStatus = "CM100 READY";
      setCommissioningPhase("hp-water-calibration", "applied");
      setText("text_sensor", "HP water calibration status", "APPLIED");
      setText("text_sensor", "Commissioning status", "CM100 READY");
    } else if (name === "Manual Flow Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.manualFlowStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "Manual flow status", "REFUSED: CM100 required");
      } else {
        state.commissioning.globalStatus = "MANUAL FLOW ACTIVE";
        state.commissioning.manualFlowStatusText = "ACTIVE";
        state.commissioning.manualFlowSetpoint = Number(getEntity("number", "Manual flow service setpoint")?.value || 800);
        setCommissioningPhase("manual-flow", "active");
        setText("text_sensor", "Commissioning status", "MANUAL FLOW ACTIVE");
        setText("text_sensor", "Manual flow status", "ACTIVE");
        setText("text_sensor", "Flow Mode", "MANUAL FLOW");
        setBinary("Manual flow active", true);
        setNumber("Flow average (Selected)", state.commissioning.manualFlowSetpoint - 8, "L/h");
        setNumber("Manual flow target iPWM", state.commissioning.manualFlowTargetIpwm, "iPWM");
      }
    } else if (name === "Manual Flow Abort") {
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      state.commissioning.manualFlowStatusText = "STOPPED";
      setCommissioningPhase("manual-flow", "aborted");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
      setText("text_sensor", "Manual flow status", "STOPPED");
      setText("text_sensor", "Flow Mode", state.commissioning.cm100Active ? "CM100 idle" : "Gepauzeerd");
      setBinary("Manual flow active", false);
      setNumber("Flow average (Selected)", 0, "L/h");
    } else if (name === "Quick Flow Test Start") {
      if (!state.commissioning.cm100Active) {
        setText("text_sensor", "Commissioning status", "REFUSED: CM100 required");
        const quickFlowTest = getEntity("switch", "Quick flow test");
        if (quickFlowTest) {
          quickFlowTest.value = false;
          quickFlowTest.state = false;
        }
      } else {
        clearQuickFlowTestTimer();
        clearCommissioningTimers();
        state.commissioning.globalStatus = "QUICK FLOW TEST ACTIVE";
        setCommissioningPhase("manual-flow", "active");
        setText("text_sensor", "Commissioning status", "QUICK FLOW TEST ACTIVE");
        setText("text_sensor", "Flow Mode", "QUICK FLOW TEST");
        setBinary("Manual flow active", true);
        setNumber("Flow average (Selected)", 640, "L/h");
        state.quickFlowTestTimer = window.setTimeout(() => {
          state.quickFlowTestTimer = null;
          state.commissioning.cm100Active = false;
          state.commissioning.globalStatus = "IDLE";
          setCommissioningPhase("none", "idle");
          setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
          setText("text_sensor", "Commissioning status", "IDLE");
          setText("text_sensor", "Flow Mode", "Gepauzeerd");
          setBinary("CM100 active", false);
          setBinary("Manual flow active", false);
          const quickFlowTest = getEntity("switch", "Quick flow test");
          if (quickFlowTest) {
            quickFlowTest.value = false;
            quickFlowTest.state = false;
          }
          setNumber("Flow average (Selected)", 0, "L/h");
          applyScenario(state.scenario);
          updateSummary();
          notifyMockUpdated();
        }, 3000);
      }
    } else if (name === "Quick Flow Test Abort") {
      clearQuickFlowTestTimer();
      clearCommissioningTimers();
      state.commissioning.cm100Active = false;
      state.commissioning.globalStatus = "IDLE";
      setCommissioningPhase("none", "idle");
      setText("text_sensor", "Control Mode (Label)", "CM0 - Standby");
      setText("text_sensor", "Commissioning status", "IDLE");
      setText("text_sensor", "Flow Mode", "Gepauzeerd");
      setBinary("CM100 active", false);
      setBinary("Manual flow active", false);
      const quickFlowTest = getEntity("switch", "Quick flow test");
      if (quickFlowTest) {
        quickFlowTest.value = false;
        quickFlowTest.state = false;
      }
      setNumber("Flow average (Selected)", 0, "L/h");
    } else if (name === "Apply Manual Flow To Heating") {
      setNumber("Flow Setpoint", Number(getEntity("number", "Manual flow service setpoint")?.value || 0), "L/h");
      state.commissioning.manualFlowStatusText = "SAVED FOR HEATING";
      setText("text_sensor", "Manual flow status", "SAVED FOR HEATING");
    } else if (name === "Apply Manual Flow To Cooling") {
      setNumber("Cooling Flow Setpoint", Number(getEntity("number", "Manual flow service setpoint")?.value || 0), "L/h");
      state.commissioning.manualFlowStatusText = "SAVED FOR COOLING";
      setText("text_sensor", "Manual flow status", "SAVED FOR COOLING");
    } else if (name === "Manual HP Start") {
      if (!state.commissioning.cm100Active) {
        state.commissioning.manualHpStatusText = "REFUSED: CM100 required";
        setText("text_sensor", "Manual HP status", "REFUSED: CM100 required");
      } else {
        state.commissioning.globalStatus = "MANUAL HP ACTIVE";
        state.commissioning.manualHpStatusText = "ACTIVE: select mode and compressor level";
        state.commissioning.manualHpGuardStatusText = "Vrijgegeven";
        state.commissioning.manualHp1Level = 0;
        state.commissioning.manualHp2Level = 0;
        setNumber("Manual HP1 compressor level", 0, "");
        setNumber("Manual HP2 compressor level", 0, "");
        setText("select", "Manual HP1 service mode", "Standby");
        setText("select", "Manual HP2 service mode", "Standby");
        setCommissioningPhase("manual-hp", "active");
        setText("text_sensor", "Commissioning status", "MANUAL HP ACTIVE");
        setText("text_sensor", "Manual HP status", state.commissioning.manualHpStatusText);
        setText("text_sensor", "Manual HP guard status", state.commissioning.manualHpGuardStatusText);
        setText("text_sensor", "Flow Mode", "MANUAL HP");
        setBinary("Manual HP active", true);
        setNumber("Flow average (Selected)", 792, "L/h");
      }
    } else if (name === "Manual HP Abort") {
      state.commissioning.globalStatus = state.commissioning.cm100Active ? "CM100 READY" : "CM0 - Standby";
      state.commissioning.manualHpStatusText = "STOPPED";
      state.commissioning.manualHpGuardStatusText = "Vrijgegeven";
      state.commissioning.manualHp1Level = 0;
      state.commissioning.manualHp2Level = 0;
      setNumber("Manual HP1 compressor level", 0, "");
      setNumber("Manual HP2 compressor level", 0, "");
      setText("select", "Manual HP1 service mode", "Standby");
      setText("select", "Manual HP2 service mode", "Standby");
      setCommissioningPhase("manual-hp", "aborted");
      setText("text_sensor", "Commissioning status", state.commissioning.globalStatus);
      setText("text_sensor", "Manual HP status", "STOPPED");
      setText("text_sensor", "Manual HP guard status", "Vrijgegeven");
      setText("text_sensor", "Flow Mode", state.commissioning.cm100Active ? "CM100 idle" : "Gepauzeerd");
      setBinary("Manual HP active", false);
      setNumber("HP1 compressor level", 0, "");
      setNumber("HP2 compressor level", 0, "");
    } else if (name === "Reset Runtime Counters (HP1)" || name === "Reset Runtime Counters (HP1+HP2)") {
      setNumber("HP1 - Runtime Hours", 0, "h");
      if (name === "Reset Runtime Counters (HP1+HP2)") {
        setNumber("HP2 - Runtime Hours", 0, "h");
        setText("text_sensor", "Runtime lead HP", "HP1");
      }
    } else if (name === "Reset Cumulative Energy Counters") {
      state.energyCountersReset = true;
      syncOverviewTelemetry(state.installation === "single");
    } else if (name === "Complete setup") {
      state.complete = true;
    } else if (name === "Reset setup state") {
      state.complete = false;
    } else if (name === "Acknowledge compressor cycling alert") {
      if (state.diagnostics !== "cycling") {
        clearMockCompressorCyclingAlert();
        if (state.diagnostics === "cycling-recovered") {
          state.diagnostics = "clear";
        }
      }
    } else if (name === "Check Firmware Updates") {
      const channel = String(getEntity("select", "Firmware Update Channel")?.value || "dev");
      const target = String(getEntity("select", "Firmware Update Target")?.value || "current build");
      const updateEntity = getEntity("update", "Firmware Update");
      const currentVersion = String(getEntity("text_sensor", "OpenQuatt Version")?.value || MOCK_STABLE_VERSION);
      const alternateBuild = target !== "current build";
      const latestVersion = alternateBuild ? currentVersion : channel === "main" ? MOCK_STABLE_VERSION : MOCK_DEV_VERSION;
      clearOtaSimulation();
      setText("text_sensor", "Firmware Update Status", "Idle");
      setNumber("Firmware Update Progress", 0, "%");
      if (updateEntity) {
        updateEntity.current_version = currentVersion;
        updateEntity.latest_version = latestVersion;
        updateEntity.release_url = getMockReleaseUrl(channel);
        updateEntity.state = currentVersion === latestVersion ? "up_to_date" : "available";
        updateEntity.value = updateEntity.state;
      }
    } else if (name === "Install Firmware Update Target") {
      handleUpdateInstall("Firmware Update");
    } else if (name === "Install Firmware Test OTA") {
      handleUpdateInstall("Firmware Test OTA");
    } else if (name === "Trendhistorie nu opslaan") {
      state.trendFlashLastFlushAt = Date.now();
      state.trendFlashNewestAt = Date.now() - (2 * 60 * 1000);
      state.trendFlashWrites += 1;
      state.trendFlashStoredKiB = Math.min(360, Number((state.trendFlashStoredKiB + 0.5).toFixed(1)));
    } else if (name === "Lifetime energiehistorie nu opslaan") {
      captureCurrentEnergyHistoryRecord();
    } else if (name === "Lifetime energiehistorie wissen") {
      state.energyHistoryRecords = [];
      state.energyHistoryHourRecords = [];
      state.energyHistoryWrites = 0;
      state.energyHistoryLastWriteAt = Date.now();
      updateEnergyHistoryStats();
    } else if (name === "Restart") {
    }
    updateSummary();
    notifyMockUpdated();
    notifyDevControlsChanged();
  }

  function handleUpdateInstall(name) {
    if (name !== "Firmware Update" && name !== "Firmware Test OTA") {
      return;
    }
    const testFirmware = name === "Firmware Test OTA";
    const updateEntity = getEntity("update", "Firmware Update");
    if (!updateEntity) {
      return;
    }
    clearOtaSimulation();

    const updateTarget = String(getEntity("select", "Firmware Update Target")?.value || "current build");
    const changesConnection = updateTarget === "alternate connection" || updateTarget === "alternate topology and connection";
    const changesTopology = updateTarget === "alternate topology" || updateTarget === "alternate topology and connection";
    const targetConnection = changesConnection
      ? state.connection === "wifi" ? "eth" : "wifi"
      : state.connection;
    const targetTopology = changesTopology
      ? (state.installation === "single" ? "duo" : "single")
      : state.installation;
    const targetVersion = testFirmware
      ? MOCK_TEST_VERSION
      : String(updateEntity.latest_version || updateEntity.current_version || MOCK_STABLE_VERSION);
    const scheduleStep = (delay, callback) => {
      const timer = window.setTimeout(() => {
        callback();
        updateSummary();
        notifyMockUpdated();
      }, delay);
      state.otaTimers.push(timer);
    };

    updateEntity.state = "installing";
    updateEntity.value = "installing";
    updateEntity.summary = testFirmware
      ? "Testfirmware wordt voorbereid in deze preview."
      : "Firmware wordt voorbereid voor upload in deze preview.";
    setText("text_sensor", "Firmware Update Status", "Starting");
    setNumber("Firmware Update Progress", 0, "%");
    notifyMockUpdated();

    scheduleStep(700, () => {
      updateEntity.summary = testFirmware
        ? "Testfirmware wordt gedownload in deze preview."
        : "Firmware wordt geüpload in deze preview.";
      setText("text_sensor", "Firmware Update Status", "Uploading");
      setNumber("Firmware Update Progress", 18, "%");
    });

    scheduleStep(1500, () => {
      setNumber("Firmware Update Progress", 44, "%");
    });

    scheduleStep(2400, () => {
      setNumber("Firmware Update Progress", 73, "%");
    });

    scheduleStep(3300, () => {
      updateEntity.summary = "Firmware is geplaatst. Device start opnieuw op in deze preview.";
      setText("text_sensor", "Firmware Update Status", "Rebooting");
      setNumber("Firmware Update Progress", 100, "%");
    });

    scheduleStep(4800, () => {
      updateEntity.state = "up_to_date";
      updateEntity.value = "up_to_date";
      updateEntity.current_version = targetVersion;
      updateEntity.latest_version = targetVersion;
      updateEntity.summary = testFirmware
        ? "De preview draait nu op testfirmware."
        : "De preview draait nu op de nieuwste firmware.";
      setText("text_sensor", "OpenQuatt Version", targetVersion);
      setInstallationMode(targetTopology);
      state.connection = targetConnection;
      setText("text_sensor", "OpenQuatt Connection", state.connection);
      setText("select", "Firmware Update Target", "current build");
      setText("text_sensor", "Firmware Update Status", "Idle");
      setNumber("Firmware Update Progress", 0, "%");
      clearOtaSimulation();
      syncDevMeta();
    });
  }

  function getDebugRecordingElapsedS(recording = state.debugRecording) {
    const endAt = recording.active ? Date.now() : Number(recording.stoppedAt || 0);
    if (!recording.startedAt || !endAt) {
      return 0;
    }
    return Math.max(0, Math.floor((endAt - recording.startedAt) / 1000));
  }

  function makeDebugRecordingSample(offsetS) {
    const uptimeMs = Math.max(0, Math.round(Date.now() - state.bootedAt));
    const wobble = Math.round(Math.sin(offsetS / 17) * 4200);
    const systemValues = [
      uptimeMs,
      192000 - Math.round(offsetS * 7) + wobble,
      5148000 - Math.round(offsetS * 13),
      184000 - Math.round(offsetS * 5),
    ];
    return {
      offset_s: offsetS,
      values: state.debugRecording.fields.map((field, index) => {
        if (index < systemValues.length) {
          return systemValues[index];
        }
        const entity = getEntity(field.domain, field.name);
        const value = entity?.value ?? entity?.state ?? null;
        if (field.domain === "binary_sensor" || field.domain === "switch") {
          return value === true || value === "ON" || value === "on";
        }
        if (field.domain === "sensor" || field.domain === "number") {
          const numeric = Number(value);
          return Number.isFinite(numeric) ? numeric : null;
        }
        return value == null ? null : String(value);
      }),
    };
  }

  function syncDebugRecordingSamples() {
    const recording = state.debugRecording;
    if (!recording.startedAt) {
      return;
    }
    const rolling = recording.mode === "rolling";
    const elapsedS = rolling ? getDebugRecordingElapsedS(recording) : Math.min(getDebugRecordingElapsedS(recording), Number(recording.durationS || 0));
    if (!Number.isFinite(Number(recording.nextOffsetS))) {
      recording.nextOffsetS = 0;
    }
    while (Number(recording.nextOffsetS || 0) <= elapsedS) {
      recording.samples.push(makeDebugRecordingSample(Number(recording.nextOffsetS || 0)));
      recording.nextOffsetS = Number(recording.nextOffsetS || 0) + 10;
      if (recording.samples.length > DEBUG_RECORDING_SAMPLE_CAPACITY) {
        recording.samples.shift();
      }
    }
    if (!rolling && recording.active && elapsedS >= Number(recording.durationS || 0)) {
      recording.active = false;
      recording.stoppedAt = recording.startedAt + Number(recording.durationS || 0) * 1000;
    }
  }

  function getDebugRecordingStatusPayload() {
    syncDebugRecordingSamples();
    const recording = state.debugRecording;
    const rolling = recording.mode === "rolling";
    const elapsedS = rolling ? getDebugRecordingElapsedS(recording) : Math.min(getDebugRecordingElapsedS(recording), Number(recording.durationS || 0));
    const remainingS = recording.active && !rolling ? Math.max(0, Number(recording.durationS || 0) - elapsedS) : 0;
    const firstSample = recording.samples[0] || null;
    const lastSample = recording.samples[recording.samples.length - 1] || null;
    const retainedDurationS = firstSample && lastSample ? Math.max(0, lastSample.offset_s - firstSample.offset_s) : 0;
    return {
      ok: true,
      available: true,
      active: Boolean(recording.active),
      mode: rolling ? "rolling" : "manual",
      rolling,
      frozen: Boolean(recording.frozen),
      recording_id: Number(recording.startedAt || 0),
      storage: "psram",
      interval_s: 10,
      duration_s: Number(recording.durationS || 0),
      elapsed_s: elapsedS,
      remaining_s: remainingS,
      retained_duration_s: retainedDurationS,
      retention_capacity_s: (DEBUG_RECORDING_SAMPLE_CAPACITY - 1) * 10,
      sample_count: recording.samples.length,
      sample_capacity: DEBUG_RECORDING_SAMPLE_CAPACITY,
      field_count: recording.fields.length,
      entity_field_count: Math.max(0, recording.fields.length - 4),
      missing_field_count: 0,
      buffer_size: DEBUG_RECORDING_BUFFER_BYTES,
      estimated_size: 2048 + recording.samples.length * (16 + recording.fields.length * 3),
      buffer: "psram",
    };
  }

  function handleDebugRecordingConfigure(url, init) {
    const params = new URLSearchParams(String(init?.body || ""));
    if (url.searchParams.get("reset") === "1") {
      state.debugRecording.fields = [
        { key: "uptimeMs", domain: "system", name: "uptimeMs", unit: "ms" },
        { key: "freeHeap", domain: "system", name: "freeHeap", unit: "B" },
        { key: "freePsram", domain: "system", name: "freePsram", unit: "B" },
        { key: "minFreeHeap", domain: "system", name: "minFreeHeap", unit: "B" },
      ];
    }
    String(params.get("entities") || "").split("\n").forEach((line) => {
      const [key, domain, name] = line.split("\t");
      if (key && domain && name) {
        state.debugRecording.fields.push({ key, domain, name, unit: getEntity(domain, name)?.uom || "" });
      }
    });
    return mockResponse(200, getDebugRecordingStatusPayload());
  }

  function handleDebugRecordingStart(url) {
    const rolling = url.searchParams.get("rolling") === "1";
    const durationS = rolling ? 0 : Math.max(60, Math.min(3600, Number(url.searchParams.get("duration_s") || 15 * 60)));
    state.debugRecording = {
      active: true,
      mode: rolling ? "rolling" : "manual",
      frozen: false,
      startedAt: Date.now(),
      stoppedAt: 0,
      durationS,
      nextOffsetS: 0,
      fields: [...state.debugRecording.fields],
      samples: [],
    };
    syncDebugRecordingSamples();
    return mockResponse(200, getDebugRecordingStatusPayload());
  }

  function handleDebugRecordingFreeze() {
    const recording = state.debugRecording;
    syncDebugRecordingSamples();
    if (recording.active) {
      recording.active = false;
      recording.frozen = recording.mode === "rolling";
      recording.stoppedAt = Date.now();
    }
    return mockResponse(200, getDebugRecordingStatusPayload());
  }

  function handleDebugRecordingStop() {
    const recording = state.debugRecording;
    syncDebugRecordingSamples();
    if (recording.active) {
      recording.active = false;
      recording.frozen = recording.mode === "rolling";
      recording.stoppedAt = Date.now();
    }
    return mockResponse(200, getDebugRecordingStatusPayload());
  }

  function buildDebugRecordingDownloadPayload() {
    syncDebugRecordingSamples();
    const recording = state.debugRecording;
    const startedAtMs = Number(recording.startedAt || Date.now());
    const endedAtMs = recording.active ? Date.now() : Number(recording.stoppedAt || startedAtMs);
    const initial = recording.samples[0] || null;
    const samples = recording.samples.map((sample, index) => {
      const previous = index > 0 ? recording.samples[index - 1] : initial;
      const deltas = [];
      sample.values.forEach((value, valueIndex) => {
        if (previous && !Object.is(value, previous.values[valueIndex])) {
          deltas.push([valueIndex, value]);
        }
      });
      return [sample.offset_s, deltas];
    });
    return {
      format: "openquatt-debug-device-v1",
      schema_version: 1,
      kind: "openquatt_debug_recording",
      encoding: "device-psram-delta-json-v1",
      exported_at_ms: Date.now(),
      source: {
        device: "OpenQuatt",
        storage: "psram",
      },
      recording: {
        started_at_ms: startedAtMs,
        recording_id: Number(recording.startedAt || 0),
        ended_at_ms: endedAtMs,
        active: Boolean(recording.active),
        mode: recording.mode === "rolling" ? "rolling" : "manual",
        rolling: recording.mode === "rolling",
        frozen: Boolean(recording.frozen),
        duration_s: Math.max(0, Math.floor((endedAtMs - startedAtMs) / 1000)),
        retained_duration_s: initial && recording.samples.length
          ? Math.max(0, recording.samples[recording.samples.length - 1].offset_s - initial.offset_s)
          : 0,
        retention_capacity_s: (DEBUG_RECORDING_SAMPLE_CAPACITY - 1) * 10,
        interval_s: 10,
        sample_count: recording.samples.length,
        sample_capacity: DEBUG_RECORDING_SAMPLE_CAPACITY,
        buffer_size: DEBUG_RECORDING_BUFFER_BYTES,
        column_count: recording.fields.length,
        storage: "psram",
      },
      columns: recording.fields.map((field) => field.key),
      units: recording.fields.flatMap((field, index) => field.unit ? [[index, field.unit]] : []),
      initial: initial ? initial.values.flatMap((value, index) => value == null ? [] : [[index, value]]) : [],
      samples,
      events: [],
    };
  }

  function handleDebugRecordingDownload() {
    return mockResponse(200, buildDebugRecordingDownloadPayload());
  }

  function calculateMockCrc(words) {
    let crc = 0xffff;
    for (let index = 0; index < 510; index += 1) {
      crc ^= Number(words[index] || 0) & 0xff;
      for (let bit = 0; bit < 8; bit += 1) {
        crc = (crc & 1) !== 0 ? ((crc >>> 1) ^ 0xa001) : (crc >>> 1);
      }
    }
    return crc & 0xffff;
  }

  function encodeMockAsciiWords(value, count = 20) {
    const bytes = [...String(value || "")].map((character) => character.charCodeAt(0) & 0xff);
    const words = [];
    for (let index = 0; index < count; index += 1) {
      const high = bytes[index * 2] || 0;
      const low = bytes[index * 2 + 1] || 0;
      words.push((high << 8) | low);
    }
    return words;
  }

  function buildMockOduEepromWords(hp) {
    const words = Array.from({ length: 512 }, (_, index) => (index * 7 + hp * 13) & 0xff);
    const frequency = state.oduRuntimeFrequency[`HP${hp}`];
    words[0] = 255;
    frequency.cooling.forEach((value, index) => { words[index + 1] = Number(value); });
    frequency.heating.forEach((value, index) => { words[index + 12] = Number(value); });
    words[310] = hp === 2 ? 2 : 1;
    words[317] = hp === 2 ? 0x0204 : 0x0102;
    words[456] = hp === 2 ? 13 : 12;
    words[459] = hp === 2 ? 2 : 1;
    words[498] = 32;
    [38, 42, 46, 50, 54, 58].forEach((value, index) => { words[502 + index] = value + hp; });
    const crc = calculateMockCrc(words);
    words[510] = crc & 0xff;
    words[511] = (crc >>> 8) & 0xff;
    return words;
  }

  function getMockOduIdentity(hp) {
    const v2 = hp === 2;
    const pcbProgram = v2 ? 0x0204 : 0x0102;
    const eepromProgram = v2 ? 0x0032 : 0x0021;
    const officialFirmware = v2 ? 0x0206 : 0x0108;
    const model = v2 ? "QUATT ODU V2" : "QUATT ODU V1";
    const serial = v2 ? "QV2-MOCK-000002" : "QV1-MOCK-000001";
    const core = Array(14).fill(0);
    core[0] = v2 ? 2 : 1;
    core[1] = hp;
    core[7] = 0;
    core[8] = pcbProgram;
    core[9] = eepromProgram;
    core[13] = v2 ? 0x1202 : 0x1101;
    return {
      model,
      customerModel: model,
      serial,
      pcbProgram,
      pcbLabel: `V${String((pcbProgram >>> 8) & 0xff).padStart(3, "0")}_T${String(pcbProgram & 0xff).padStart(2, "0")}`,
      eepromProgram,
      officialFirmware,
      officialLabel: `${(officialFirmware >>> 8) & 0xff}.${officialFirmware & 0xff}`,
      core,
      extended: [hp, v2 ? 202 : 101, v2 ? 2 : 1, officialFirmware, 0, eepromProgram],
      modelWords: encodeMockAsciiWords(model),
      customerModelWords: encodeMockAsciiWords(model),
      serialWords: encodeMockAsciiWords(serial),
    };
  }

  function syncMockOduEepromDump(hp) {
    const dump = state.oduEepromDumps[hp];
    if (!dump.active) return;
    const elapsed = Math.max(0, Date.now() - dump.startedAt);
    if (elapsed >= 6000) {
      dump.active = false;
      dump.ready = true;
      dump.completedAt = Date.now();
    }
  }

  function getMockOduEepromStatus(hp) {
    syncMockOduEepromDump(hp);
    const dump = state.oduEepromDumps[hp];
    const elapsed = dump.active ? Math.max(0, Date.now() - dump.startedAt) : 0;
    const progress = dump.ready ? 100 : dump.active ? Math.max(2, Math.min(99, Math.round(elapsed / 60))) : 0;
    const registersRead = dump.ready ? 512 : dump.active ? Math.min(511, Math.round(Math.max(0, progress - 10) / 90 * 512)) : 0;
    const identity = getMockOduIdentity(hp);
    const words = buildMockOduEepromWords(hp);
    const crc = calculateMockCrc(words);
    return {
      ok: true,
      available: true,
      hp,
      modbus_device_address: hp,
      active: dump.active,
      dump_ready: dump.ready,
      job_id: dump.jobId,
      phase: dump.ready ? "complete" : dump.active ? progress < 10 ? "reading extended ODU identity" : progress < 98 ? "reading EEPROM shadow" : "verifying EEPROM CRC" : "idle",
      progress_percent: progress,
      registers_read: registersRead,
      register_count: 512,
      warning_flags: 0,
      error: "",
      crc: {
        calculated: `0x${crc.toString(16).toUpperCase().padStart(4, "0")}`,
        stored: `0x${crc.toString(16).toUpperCase().padStart(4, "0")}`,
        matches_stored_eeprom: dump.ready,
        retry_count: 0,
      },
      identity: {
        extended_supported: true,
        model: dump.ready ? identity.model : "",
        core_available: dump.ready,
        pcb_program_raw: dump.ready ? identity.pcbProgram : 0,
        pcb_program: dump.ready ? identity.pcbLabel : "",
        eeprom_program_raw: dump.ready ? identity.eepromProgram : 0,
      },
    };
  }

  function handleMockOduEepromStart(hp) {
    const dump = state.oduEepromDumps[hp];
    syncMockOduEepromDump(hp);
    if (dump.active) return mockResponse(409, { ok: false, error: "dump_busy" });
    dump.active = true;
    dump.ready = false;
    dump.startedAt = Date.now();
    dump.completedAt = 0;
    dump.jobId += 1;
    return mockResponse(200, getMockOduEepromStatus(hp));
  }

  function buildMockOduEepromDownload(hp) {
    const dump = state.oduEepromDumps[hp];
    const identity = getMockOduIdentity(hp);
    const words = buildMockOduEepromWords(hp);
    const crc = calculateMockCrc(words);
    const crcHex = `0x${crc.toString(16).toUpperCase().padStart(4, "0")}`;
    return {
      format: "openquatt-odu-eeprom-v1",
      schema_version: 1,
      captured_at_epoch: Math.floor((dump.completedAt || Date.now()) / 1000),
      source: { device: "OpenQuatt", hp, modbus_device_address: hp, snapshot: "runtime_eeprom_shadow" },
      job: { id: dump.jobId, duration_ms: Math.max(0, (dump.completedAt || Date.now()) - dump.startedAt), warning_flags: 0, warnings: [] },
      identity: {
        core_available: true,
        compressor_code: identity.core[0],
        odu_dip_switch: identity.core[1],
        failures_raw: 0,
        eeprom_failure: false,
        pcb_program: { raw: identity.pcbProgram, hex: `0x${identity.pcbProgram.toString(16).toUpperCase().padStart(4, "0")}`, main: identity.pcbProgram >>> 8, sub: identity.pcbProgram & 0xff, label: identity.pcbLabel },
        eeprom_program: { raw: identity.eepromProgram, hex: `0x${identity.eepromProgram.toString(16).toUpperCase().padStart(4, "0")}` },
        control_board_item: { raw: identity.core[13], hex: `0x${identity.core[13].toString(16).toUpperCase().padStart(4, "0")}` },
        extended_supported: true,
        odu_address: identity.extended[0],
        project_code: identity.extended[1],
        hardware_version: identity.extended[2],
        official_firmware: { raw: identity.officialFirmware, label: identity.officialLabel },
        beta_version: 0,
        extended_eeprom_version: identity.eepromProgram,
        model: identity.model,
        customer_model: identity.customerModel,
        serial: identity.serial,
        raw_blocks: {
          core: { modbus_start: 2114, values: identity.core },
          extended: { modbus_start: 11004, values: identity.extended },
          model: { modbus_start: 11120, values: identity.modelWords },
          customer_model: { modbus_start: 11160, values: identity.customerModelWords },
          serial: { modbus_start: 11219, values: identity.serialWords },
        },
      },
      eeprom: {
        complete: true,
        sheet_start: 3000,
        modbus_start: 2999,
        register_count: 512,
        crc: { algorithm: "CRC16/Modbus", data: "low byte of sheet 3000..3509", init: 0xffff, polynomial: 0xa001, calculated: crcHex, stored: crcHex, matches_stored_eeprom: true, retry_count: 0 },
        fingerprints: { fan_count: words[310], model_main_pcb_address: words[317], minimum_flow: words[456], flow_sensor_type: words[459], refrigerant: words[498], pump_fan_power_words: words.slice(502, 508) },
        registers: words.map((word, index) => ({ sheet_address: 3000 + index, modbus_address: 2999 + index, word, hex: `0x${word.toString(16).toUpperCase().padStart(4, "0")}`, high_byte: (word >>> 8) & 0xff, low_byte: word & 0xff })),
      },
    };
  }

  function handleMockOduEepromRequest(url, method) {
    const match = url.pathname.match(/\/openquatt\/odu-eeprom\/hp([12])\/(status|start|download)$/);
    if (!match) return null;
    const hp = Number(match[1]);
    const action = match[2];
    if (hp === 2 && state.installation !== "duo") return mockResponse(404, { ok: false, error: "not_found" });
    if (action === "status" && method === "GET") return mockResponse(200, getMockOduEepromStatus(hp));
    if (action === "start" && method === "POST") return handleMockOduEepromStart(hp);
    if (action === "download" && method === "GET") {
      syncMockOduEepromDump(hp);
      if (!state.oduEepromDumps[hp].ready) return mockResponse(409, { ok: false, error: "dump_not_ready" });
      return mockResponse(200, buildMockOduEepromDownload(hp));
    }
    return mockResponse(405, { ok: false, error: "method_not_allowed" });
  }

  function parseMockRequest(input) {
    const url = new URL(String(typeof input === "string" ? input : input.url), window.location.href);
    const parts = url.pathname.split("/").filter(Boolean);
    const maybeAction = parts.at(-1);
    const action = ["set", "press", "install", "turn_on", "turn_off"].includes(maybeAction) ? parts.pop() : "";
    const name = decodeURIComponent(parts.pop() || "");
    const domain = parts.pop() || "";
    if (!DOMAINS.has(domain)) {
      return null;
    }
    return { url, domain, name, action };
  }

  function mockResponse(status, payload) {
    return Promise.resolve({
      ok: status >= 200 && status < 300,
      status,
      json: async () => clone(payload),
      text: async () => JSON.stringify(payload),
    });
  }

  function mockTextResponse(status, text) {
    return Promise.resolve({
      ok: status >= 200 && status < 300,
      status,
      json: async () => ({ text }),
      text: async () => String(text || ""),
    });
  }

  function installFetchMock() {
    const realFetch = window.fetch ? window.fetch.bind(window) : null;
    window.fetch = async function fetchMock(input, init) {
      const url = new URL(String(typeof input === "string" ? input : input.url), window.location.href);
      const method = String(init?.method || "GET").toUpperCase();
      if (url.pathname === "/auth/status" && (!init || !init.method || String(init.method).toUpperCase() === "GET")) {
        return handleAuthStatus();
      }
      if (url.pathname === "/auth/change" && String(init?.method || "GET").toUpperCase() === "POST") {
        return handleAuthChange(init || {});
      }
      if (url.pathname === "/auth/disable" && String(init?.method || "GET").toUpperCase() === "POST") {
        return handleAuthDisable(init || {});
      }
      if (url.pathname === "/api-security/status" && (!init || !init.method || String(init.method).toUpperCase() === "GET")) {
        return handleApiSecurityStatus();
      }
      if (url.pathname === "/mqtt/status" && method === "GET") {
        return handleMqttStatus();
      }
      if (url.pathname === "/mqtt/save" && method === "POST") {
        return handleMqttSave(init || {});
      }
      if (url.pathname === "/mqtt/input/save" && method === "POST") {
        return handleMqttInputSave(init || {});
      }
      if (url.pathname === "/mqtt/input/retained/save" && method === "POST") {
        return handleMqttInputRetainedSave(init || {});
      }
      if (url.pathname.endsWith("/openquatt/logs/recent") && String(init?.method || "GET").toUpperCase() === "GET") {
        return mockResponse(200, {
          enabled: Boolean(state.logHistoryEnabled),
          entries: clone(state.logHistoryEntries),
        });
      }
      if (url.pathname.endsWith("/energy/history") && method === "GET") {
        return mockTextResponse(200, buildEnergyHistoryTextPayload(url));
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/status") && method === "GET") {
        return mockResponse(200, getDebugRecordingStatusPayload());
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/configure") && method === "POST") {
        return handleDebugRecordingConfigure(url, init || {});
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/start") && method === "POST") {
        return handleDebugRecordingStart(url);
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/freeze") && method === "POST") {
        return handleDebugRecordingFreeze();
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/stop") && method === "POST") {
        return handleDebugRecordingStop();
      }
      if (url.pathname.endsWith("/openquatt/debug-recording/download") && method === "GET") {
        return handleDebugRecordingDownload();
      }
      const oduEepromResponse = handleMockOduEepromRequest(url, method);
      if (oduEepromResponse) {
        return oduEepromResponse;
      }
      if (url.pathname.endsWith("/openquatt/incidents") && method === "GET") {
        return handleIncidentSnapshot();
      }
      if (
        (url.pathname.endsWith("/openquatt/incidents/retry-start")
          || url.pathname.endsWith("/openquatt/incidents/confirm-odu-power-cycle"))
        && method === "POST"
      ) {
        return handleIncidentAction(url.pathname, init || {});
      }
      if (url.pathname.endsWith("/openquatt/service/status") && method === "GET") {
        return handleServiceStatus();
      }
      if (url.pathname.endsWith("/openquatt/decision-log") && method === "GET") {
        return handleDecisionLog(url);
      }
      if (url.pathname.endsWith("/openquatt/entities") && String(init?.method || "GET").toUpperCase() === "POST") {
        return handleBulkEntities(init || {});
      }
      if (url.pathname === "/update" && String(init?.method || "GET").toUpperCase() === "POST") {
        handleUpdateInstall("Firmware Update");
        return mockResponse(200, { ok: true });
      }

      const request = parseMockRequest(input);
      if (!request) {
        if (realFetch) {
          return realFetch(input, init);
        }
        throw new Error("No real fetch available");
      }

      const entity = getEntity(request.domain, request.name);
      if (!entity) {
        return mockResponse(404, { error: "Not found" });
      }

      if (request.action === "set") {
        const rawValue = request.url.searchParams.get("value");
        const optionValue = request.url.searchParams.get("option");
        if (request.domain === "select") {
          handleSelectSet(request.name, optionValue || rawValue || "");
        } else if (request.domain === "number") {
          handleNumberSet(request.name, rawValue || "0");
        } else if (request.domain === "time") {
          handleTimeSet(request.name, rawValue || "");
        } else if (request.domain === "datetime") {
          handleDateTimeSet(request.name, rawValue || "");
        } else if (request.domain === "text") {
          handleTextSet(request.name, rawValue || "");
        }
        return mockResponse(200, entity);
      }

      if (request.action === "turn_on" || request.action === "turn_off") {
        if (request.domain === "switch") {
          handleSwitchSet(request.name, request.action === "turn_on");
        }
        return mockResponse(200, { ok: true });
      }

      if (request.action === "press") {
        handleButtonPress(request.name);
        return mockResponse(200, { ok: true });
      }

      if (request.action === "install" && request.domain === "update") {
        handleUpdateInstall(request.name);
        return mockResponse(200, { ok: true });
      }

      return mockResponse(200, entity);
    };
  }

  function renderDevControlOptions(controlKey) {
    const options = mockFixtures.devControlOptions[controlKey] || [];
    return options
      .filter((option) => !option.duoOnly || state.installation === "duo")
      .map((option) => `<option value="${option.value}">${option.label}</option>`)
      .join("");
  }

  function renderIncidentScenarioOptions() {
    const groups = new Map();
    mockIncidentScenarios.scenarios
      .filter((item) => mockIncidentScenarios.isCompatible(item, state.installation))
      .forEach((item) => {
        if (!groups.has(item.group)) {
          groups.set(item.group, []);
        }
        groups.get(item.group).push(item);
      });
    return [...groups.entries()]
      .map(([group, items]) => `
        <optgroup label="${group}">
          ${items.map((item) => `<option value="${item.id}">${item.label}</option>`).join("")}
        </optgroup>
      `)
      .join("");
  }

  function renderIncidentPhaseOptions() {
    const selected = mockIncidentScenarios.getScenario(state.incidentSimulation.scenario);
    return selected.phases
      .map((item, index) => `<option value="${index}">${index + 1}. ${item.label}</option>`)
      .join("");
  }

  function renderIncidentSimulationMeta() {
    const { scenario, phase, phaseIndex } = getIncidentSimulationState();
    const hpBadges = phase.heat_pumps.map((hp) => (
      `<span class="oq-helper-hub-dev-badge">HP${hp.index}: ${hp.link_state} · ${hp.availability}</span>`
    )).join("");
    const continuity = phase.system.boiler_output_continuous
      ? '<span class="oq-helper-hub-dev-badge is-positive">Ketelopdracht continu</span>'
      : "";
    return `
      <p class="oq-helper-hub-dev-copy">${phase.description}</p>
      <div class="oq-helper-hub-dev-actions">
        <button
          class="oq-helper-hub-dev-button"
          type="button"
          data-oq-dev-incident-action="previous"
          ${phaseIndex === 0 ? "disabled" : ""}
        >Vorige</button>
        <button
          class="oq-helper-hub-dev-button"
          type="button"
          data-oq-dev-incident-action="next"
          ${phaseIndex >= scenario.phases.length - 1 ? "disabled" : ""}
        >Volgende</button>
        <button
          class="oq-helper-hub-dev-button"
          type="button"
          data-oq-dev-incident-action="reset"
        >Reset storing</button>
      </div>
      <div class="oq-helper-hub-dev-meta">
        <span class="oq-helper-hub-dev-badge">Stap ${phaseIndex + 1}/${scenario.phases.length}</span>
        <span class="oq-helper-hub-dev-badge">t = ${phase.elapsed_s}s</span>
        <span class="oq-helper-hub-dev-badge">CM${phase.system.control_mode}</span>
        ${hpBadges}
        ${continuity}
      </div>
    `;
  }

  function renderDevControls() {
    return `
      <section class="oq-helper-hub-block oq-helper-hub-dev" data-oq-dev-controls>
        <p class="oq-helper-hub-kicker">Preview en test</p>
        <div class="oq-helper-hub-dev-grid">
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Installatie</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="installation">
              ${renderDevControlOptions("installation")}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Hardware</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="hardware">
              ${renderDevControlOptions("hardware")}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Verbinding</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="connection">
              ${renderDevControlOptions("connection")}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Scenario</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="scenario">
              ${renderDevControlOptions("scenario")}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">CV-ketel</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="boiler">
              ${renderDevControlOptions("boiler")}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Diagnose</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="diagnostics">
              ${renderDevControlOptions("diagnostics")}
            </select>
          </label>
        </div>
        <div class="oq-helper-hub-dev-divider" role="presentation"></div>
        <p class="oq-helper-hub-kicker">Warmtepompstoringssimulator</p>
        <div class="oq-helper-hub-dev-grid">
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Storingsscenario</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="incident-scenario">
              ${renderIncidentScenarioOptions()}
            </select>
          </label>
          <label class="oq-helper-hub-dev-row">
            <span class="oq-helper-hub-dev-label">Fase</span>
            <select class="oq-helper-hub-dev-select" data-oq-dev-control="incident-phase">
              ${renderIncidentPhaseOptions()}
            </select>
          </label>
        </div>
        ${renderIncidentSimulationMeta()}
      </section>
    `;
  }

  function applyIncidentSimulationControlChange() {
    applyScenario(state.scenario);
    updateSummary();
    syncIncidentScenarioUrl();
    notifyMockUpdated();
    notifyDevControlsChanged();
  }

  function setIncidentSimulationPhase(phaseIndex) {
    const selected = mockIncidentScenarios.getScenario(state.incidentSimulation.scenario);
    state.incidentSimulation.phaseIndex = mockIncidentScenarios.getPhase(selected, phaseIndex).index;
    resetIncidentActionState();
    applyIncidentSimulationControlChange();
  }

  function bindDevControls(root) {
    const controlsRoot = root.querySelector("[data-oq-dev-controls]");
    if (!controlsRoot) {
      return;
    }
    devControlsRoot = controlsRoot;

    const installation = controlsRoot.querySelector('[data-oq-dev-control="installation"]');
    if (installation) {
      installation.value = state.installation;
      installation.onchange = () => {
        setInstallationMode(installation.value);
        const selectedIncident = mockIncidentScenarios.getScenario(state.incidentSimulation.scenario);
        if (!mockIncidentScenarios.isCompatible(selectedIncident, state.installation)) {
          configureIncidentScenario("none");
        }
        applyScenario(state.scenario);
        updateSummary();
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
    }

    const hardware = controlsRoot.querySelector('[data-oq-dev-control="hardware"]');
    if (hardware) {
      hardware.value = state.hardware;
      hardware.onchange = () => {
        state.hardware = hardware.value;
        setEntity("text_sensor", "OpenQuatt Hardware Profile", { state: state.hardware, value: state.hardware });
        const selectedIncident = mockIncidentScenarios.getScenario(state.incidentSimulation.scenario);
        if (selectedIncident.required_hardware && selectedIncident.required_hardware !== state.hardware) {
          configureIncidentScenario("none");
        }
        syncDevMeta();
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
    }

    const connection = controlsRoot.querySelector('[data-oq-dev-control="connection"]');
    if (connection) {
      connection.value = state.connection;
      connection.onchange = () => {
        setConnectionMode(connection.value);
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
    }

    const scenario = controlsRoot.querySelector('[data-oq-dev-control="scenario"]');
    if (scenario) {
      scenario.value = state.scenario;
      scenario.onchange = () => {
        state.scenario = scenario.value;
        applyScenario(state.scenario);
        updateSummary();
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
    }

    const boiler = controlsRoot.querySelector('[data-oq-dev-control="boiler"]');
    if (boiler) {
      const handleBoilerChange = () => {
        state.boiler = boiler.value === "on" ? "on" : "off";
        applyScenario(state.scenario);
        updateSummary();
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
      boiler.value = state.boiler;
      boiler.onchange = handleBoilerChange;
      boiler.oninput = handleBoilerChange;
    }

    const diagnostics = controlsRoot.querySelector('[data-oq-dev-control="diagnostics"]');
    if (diagnostics) {
      diagnostics.value = state.diagnostics;
      diagnostics.onchange = () => {
        state.diagnostics = diagnostics.value;
        applyScenario(state.scenario);
        updateSummary();
        notifyMockUpdated();
        notifyDevControlsChanged();
      };
    }

    const incidentScenario = controlsRoot.querySelector('[data-oq-dev-control="incident-scenario"]');
    if (incidentScenario) {
      incidentScenario.value = state.incidentSimulation.scenario;
      incidentScenario.onchange = () => {
        configureIncidentScenario(incidentScenario.value);
        applyIncidentSimulationControlChange();
      };
    }

    const incidentPhase = controlsRoot.querySelector('[data-oq-dev-control="incident-phase"]');
    if (incidentPhase) {
      incidentPhase.value = String(state.incidentSimulation.phaseIndex);
      incidentPhase.onchange = () => {
        setIncidentSimulationPhase(Number(incidentPhase.value));
      };
    }

    controlsRoot.querySelectorAll("[data-oq-dev-incident-action]").forEach((button) => {
      button.onclick = () => {
        const action = button.dataset.oqDevIncidentAction;
        if (action === "previous") {
          setIncidentSimulationPhase(state.incidentSimulation.phaseIndex - 1);
        } else if (action === "next") {
          setIncidentSimulationPhase(state.incidentSimulation.phaseIndex + 1);
        } else if (action === "reset") {
          configureIncidentScenario("none");
          applyIncidentSimulationControlChange();
        }
      };
    });

  }

  window.__OQ_DEV_CONTROLS__ = {
    render: renderDevControls,
    bind: bindDevControls,
  };
  window.__OQ_DEV_TREND_MOCKS__ = {
    buildTrendPreviewSamples,
  };
  window.__OQ_SET_MOCK_BOILER__ = applyBoilerDevMode;

  function getDevControlFromEvent(event) {
    const path = typeof event.composedPath === "function" ? event.composedPath() : [];
    return path.find((item) => item?.dataset?.oqDevControl) || event.target?.closest?.("[data-oq-dev-control]");
  }

  function applyBoilerDevMode(value) {
    state.boiler = value === "on" ? "on" : "off";
    const boilerControl = devControlsRoot?.querySelector('[data-oq-dev-control="boiler"]');
    if (boilerControl) {
      boilerControl.value = state.boiler;
    }
    applyScenario(state.scenario);
    updateSummary();
    notifyMockUpdated();
    notifyDevControlsChanged();
  }

  window.addEventListener("click", (event) => {
    const control = getDevControlFromEvent(event);
    const action = control?.dataset?.oqDevControl;
    if (action === "boiler-on") {
      applyBoilerDevMode("on");
    } else if (action === "boiler-off") {
      applyBoilerDevMode("off");
    }
  });

  window.addEventListener("change", (event) => {
    const control = getDevControlFromEvent(event);
    if (control?.dataset?.oqDevControl === "boiler") {
      applyBoilerDevMode(control.value);
    }
  });

  seedEntities();
  initializeIncidentScenarioFromUrl();
  refreshAuthToken();
  refreshMqttToken();
  setInstallationMode(state.installation);
  applyScenario(state.scenario);
  updateSummary();
  installFetchMock();
  window.setInterval(() => {
    stepSimulation();
  }, 1600);
}());
