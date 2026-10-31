import { getEntityValue } from "./entity-store.js";
import { getBasePath } from "./url-path.js";

  export function buildEntityPath(domain, name, action = "") {
    const suffix = action ? `/${action}` : "";
    return `${getBasePath()}/${domain}/${encodeURIComponent(name)}${suffix}`;
  }

  export function isCurveMode(value = getEntityValue("strategy")) {
    return String(value || "").includes("Water Temperature Control");
  }

  export function isManualFlowMode(value = getEntityValue("flowControlMode")) {
    return String(value || "").toLowerCase().includes("manual");
  }

  // Delta T flow control, the third Flow Control Mode option.
  export function isDeltaTFlowMode(value = getEntityValue("flowControlMode")) {
    return String(value || "").trim() === "Delta T";
  }

