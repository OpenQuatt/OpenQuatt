export const ODU_GENERATION_KEYS = ["hp1Generation", "hp2Generation"];
export const ODU_FIRMWARE_KEYS = ["hp1OduFirmware", "hp2OduFirmware"];
export const ODU_GENERATION_DETECT_KEYS = ["hp1GenerationDetect", "hp2GenerationDetect"];
export const ODU_GENERATION_VARIANT_KEYS = ["hp1GenerationVariant", "hp2GenerationVariant"];
export const ODU_CUSTOMER_MODEL_CODE_KEYS = ["hp1CustomerModelCode", "hp2CustomerModelCode"];

const KNOWN_ODU_GENERATIONS = new Set(["V1", "V1.5", "V2"]);

export function normalizeDetectedOduGeneration(value) {
  const normalized = String(value ?? "").trim();
  return KNOWN_ODU_GENERATIONS.has(normalized) ? normalized : "Unknown";
}

export function normalizeOduFirmwareVersion(value) {
  const match = /^(\d{1,3})\.(\d{1,3})$/.exec(String(value ?? "").trim());
  if (!match) return null;
  const major = Number(match[1]);
  const minor = Number(match[2]);
  if (major > 255 || minor > 255 || (major === 0 && minor === 0) || (major === 255 && minor === 255)) return null;
  return `${major}.${String(minor).padStart(2, "0")}`;
}

function createHeatPumpDetection(index, available, value, detectAvailable, firmware) {
  const generation = normalizeDetectedOduGeneration(value);
  return {
    index,
    key: `hp${index}Generation`,
    detectKey: `hp${index}GenerationDetect`,
    available: Boolean(available),
    detectAvailable: Boolean(detectAvailable),
    generation,
    firmwareVersion: normalizeOduFirmwareVersion(firmware),
    known: Boolean(available) && generation !== "Unknown",
  };
}

export function createOduGenerationDetectionModel({
  topology = "",
  configuredGeneration = "",
  hp1Available = false,
  hp1Generation = "",
  hp1DetectAvailable = false,
  hp1Firmware = "",
  hp2Available = false,
  hp2Generation = "",
  hp2DetectAvailable = false,
  hp2Firmware = "",
} = {}) {
  const normalizedTopology = String(topology || "").trim().toLowerCase();
  const topologyKnown = normalizedTopology === "single" || normalizedTopology === "duo";
  const duo = normalizedTopology === "duo";
  const includeHp2 = duo || (!topologyKnown && hp2Available);
  const heatPumps = [
    createHeatPumpDetection(1, hp1Available, hp1Generation, hp1DetectAvailable, hp1Firmware),
    ...(includeHp2 ? [createHeatPumpDetection(2, hp2Available, hp2Generation, hp2DetectAvailable, hp2Firmware)] : []),
  ];
  const available = heatPumps.some((heatPump) => heatPump.available);
  const complete = topologyKnown && available && heatPumps.every((heatPump) => heatPump.known);
  const detectedGenerations = complete
    ? [...new Set(heatPumps.map((heatPump) => heatPump.generation))]
    : [];
  const uniformGeneration = detectedGenerations.length === 1 ? detectedGenerations[0] : "";
  const mixed = detectedGenerations.length > 1;
  const v1MixedDuo = duo
    && detectedGenerations.length === 2
    && detectedGenerations.includes("V1")
    && detectedGenerations.includes("V1.5");
  // The existing manual V1 option explicitly covers V1 + V1.5 Duo systems.
  // Other mixed combinations remain visible but deliberately receive no advice.
  const recommendation = uniformGeneration || (v1MixedDuo ? "V1" : "");
  const configured = normalizeDetectedOduGeneration(configuredGeneration);
  const configuredKnown = configured !== "Unknown";

  let status = "unavailable";
  if (available && !complete) {
    status = "unknown";
  } else if (complete && !recommendation) {
    status = "mixed";
  } else if (recommendation && configuredKnown) {
    status = configured === recommendation ? "match" : "mismatch";
  } else if (recommendation) {
    status = "detected";
  }

  return {
    topology: topologyKnown ? normalizedTopology : "unknown",
    heatPumps,
    available,
    complete,
    mixed,
    uniformGeneration,
    configuredGeneration: configuredKnown ? configured : "",
    recommendation,
    status,
  };
}
