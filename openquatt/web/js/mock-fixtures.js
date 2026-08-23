(function () {
  const scenarios = window.__OQ_MOCK_SCENARIOS__;
  if (!Array.isArray(scenarios)) {
    throw new Error("OpenQuatt mock scenarios ontbreken.");
  }

  const compressorLevelOptions = [
    "None",
    "L1 (H30/C30)",
    "L2 (H39/C36)",
    "L3 (H49/C42)",
    "L4 (H55/C47)",
    "L5 (H61/C52)",
    "L6 (H67/C56)",
    "L7 (H72/C61)",
    "L8 (H79/C66)",
    "L9 (H85/C71)",
    "L10 (H90/C74)",
  ];


  const hp2Entities = [
    ["select", "HP2 - Excluded compressor level A", { value: "None", state: "None", option: compressorLevelOptions }],
    ["select", "HP2 - Excluded compressor level B", { value: "None", state: "None", option: compressorLevelOptions }],
    ["sensor", "HP2 - Power Input", { value: 0, uom: "W" }],
    ["sensor", "HP2 - Heat Power", { value: 0, uom: "W" }],
    ["sensor", "HP2 - Cooling Power", { value: 0, uom: "W" }],
    ["sensor", "HP2 - COP", { value: 0, uom: "" }],
    ["sensor", "HP2 compressor level", { value: 0, uom: "" }],
    ["sensor", "HP2 - Compressor frequency", { value: 0, uom: "Hz" }],
    ["sensor", "HP2 - Compressor starts 2h", { value: 3 }],
    ["sensor", "HP2 - Compressor starts 6h", { value: 9 }],
    ["sensor", "HP2 - Compressor starts 24h", { value: 24 }],
    ["sensor", "HP2 - Compressor starts 72h", { value: 40 }],
    ["sensor", "HP2 - Compressor last start age", { value: 18, uom: "min" }],
    ["sensor", "HP2 - Runtime Hours", { value: 2761, uom: "h" }],
    ["sensor", "HP2 - Fan speed", { value: 0, uom: "rpm" }],
    ["sensor", "HP2 - Flow", { value: 0, uom: "L/h" }],
    ["sensor", "HP2 - Evaporator coil temperature", { value: 0, uom: "\u00B0C" }],
    ["sensor", "HP2 - Inner coil temperature", { value: 0, uom: "\u00B0C" }],
    ["sensor", "HP2 - Outside temperature", { value: 0, uom: "\u00B0C" }],
    ["sensor", "HP2 - Condenser pressure", { value: 0, uom: "bar" }],
    ["sensor", "HP2 - Gas discharge temperature", { value: 0, uom: "\u00B0C" }],
    ["sensor", "HP2 - Evaporator pressure", { value: 0, uom: "bar" }],
    ["sensor", "HP2 - Gas return temperature", { value: 0, uom: "\u00B0C" }],
    ["sensor", "HP2 - EEV steps", { value: 0, uom: "p" }],
    ["sensor", "HP2 - Water in temperature", { value: 25.4, uom: "\u00B0C" }],
    ["sensor", "HP2 - Water out temperature", { value: 29.1, uom: "\u00B0C" }],
    ["sensor", "HP2 - Water in temperature raw", { value: 25.4, uom: "\u00B0C" }],
    ["sensor", "HP2 - Water out temperature raw", { value: 29.1, uom: "\u00B0C" }],
    ["text_sensor", "HP2 - Working Mode Label", { state: "Standby", value: "Standby" }],
    ["text_sensor", "HP2 - Active Failures List", { state: "None", value: "None" }],
    ["binary_sensor", "HP2 - Defrost", { value: false }],
    ["binary_sensor", "HP2 - 4-Way valve", { value: false }],
    ["binary_sensor", "HP2 - Bottom plate heater", { value: true }],
    ["binary_sensor", "HP2 - Crankcase heater", { value: true }],
  ];

  const devControlOptions = {
    installation: [
      { value: "single", label: "Quatt Single" },
      { value: "duo", label: "Quatt Duo" },
    ],
    hardware: [
      { value: "heatpump_controller_q", label: "Q-edition" },
      { value: "heatpump_listener", label: "Listener" },
      { value: "waveshare", label: "Waveshare" },
    ],
    connection: [
      { value: "wifi", label: "Wi-Fi" },
      { value: "eth", label: "Ethernet" },
    ],
    scenario: scenarios,
    boiler: [
      { value: "off", label: "Uit" },
      { value: "on", label: "Aan" },
    ],
    diagnostics: [
      { value: "clear", label: "Geen bijzonderheden" },
      { value: "cycling", label: "Pendelen actief" },
      { value: "cycling-recovered", label: "Pendelen hersteld, melding open" },
      { value: "hydraulics", label: "Hydrauliek" },
      { value: "connections", label: "Verbindingen" },
      { value: "hp-fault", label: "Warmtepompstoring" },
    ],
  };

  window.__OQ_MOCK_FIXTURES__ = Object.freeze({
    compressorLevelOptions,
    hp2Entities,
    devControlOptions,
  });
})();
