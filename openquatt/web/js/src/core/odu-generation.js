export const ODU_GENERATION_KEYS = ["hp1Generation", "hp2Generation"];
export const ODU_GENERATION_DETECT_KEYS = ["hp1GenerationDetect", "hp2GenerationDetect"];

const KNOWN_ODU_GENERATIONS = new Set(["V1", "V1.5", "V2"]);

export function normalizeDetectedOduGeneration(value) {
  const normalized = String(value ?? "").trim();
  return KNOWN_ODU_GENERATIONS.has(normalized) ? normalized : "Unknown";
}

function createHeatPumpDetection(index, available, value, detectAvailable) {
  const generation = normalizeDetectedOduGeneration(value);
  return {
    index,
    key: `hp${index}Generation`,
    detectKey: `hp${index}GenerationDetect`,
    available: Boolean(available),
    detectAvailable: Boolean(detectAvailable),
    generation,
    known: Boolean(available) && generation !== "Unknown",
  };
}

export function createOduGenerationDetectionModel({
  topology = "",
  configuredGeneration = "",
  hp1Available = false,
  hp1Generation = "",
  hp1DetectAvailable = false,
  hp2Available = false,
  hp2Generation = "",
  hp2DetectAvailable = false,
} = {}) {
  const normalizedTopology = String(topology || "").trim().toLowerCase();
  const topologyKnown = normalizedTopology === "single" || normalizedTopology === "duo";
  const duo = normalizedTopology === "duo";
  const includeHp2 = duo || (!topologyKnown && hp2Available);
  const heatPumps = [
    createHeatPumpDetection(1, hp1Available, hp1Generation, hp1DetectAvailable),
    ...(includeHp2 ? [createHeatPumpDetection(2, hp2Available, hp2Generation, hp2DetectAvailable)] : []),
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
