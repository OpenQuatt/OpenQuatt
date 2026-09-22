import { ENTITY_DEFS } from "./config.js";
import { buildEntityPath } from "./domain-helpers.js";
import { normalizeDateTimeValue, normalizeNumber, normalizeTimeValue } from "./entity-store.js";
import { t } from "../i18n/index.js";

export async function setEntityBackupValue(key, value) {
  const entity = ENTITY_DEFS[key];
  if (!entity) {
    throw new Error(t("entityBackup.unknownField", { key }));
  }

  if (entity.domain === "select") {
    const option = String(value || "").trim();
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?option=${encodeURIComponent(option)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return option;
  }

  if (entity.domain === "number") {
    const normalized = normalizeNumber(key, value);
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return normalized;
  }

  if (entity.domain === "time") {
    const normalized = normalizeTimeValue(value);
    if (!normalized) {
      throw new Error(t("entityBackup.timeExpected", { name: entity.name }));
    }
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return normalized;
  }

  if (entity.domain === "datetime") {
    const normalized = normalizeDateTimeValue(value);
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return normalized;
  }

  if (entity.domain === "text") {
    const normalized = String(value || "").trim();
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return normalized;
  }

  if (entity.domain === "switch" || entity.domain === "binary_sensor") {
    const enabled = Boolean(value);
    const action = enabled ? "turn_on" : "turn_off";
    const response = await fetch(buildEntityPath(entity.domain, entity.name, action), { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return enabled;
  }

  throw new Error(t("entityBackup.cannotRestore", { name: entity.name }));
}

export function getEntityBackupSwitchState(payload) {
  if (typeof payload?.value === "boolean") {
    return payload.value;
  }
  const raw = String(payload?.state ?? payload?.value ?? "").trim().toLowerCase();
  if (["on", "true", "1"].includes(raw)) {
    return true;
  }
  if (["off", "false", "0"].includes(raw)) {
    return false;
  }
  return null;
}

export async function verifyEntityBackupSelectState(key, expected, normalize = (value) => String(value ?? "").trim()) {
  const entity = ENTITY_DEFS[key];
  if (!entity || (entity.domain !== "select" && entity.domain !== "time")) {
    throw new Error(t("entityBackup.unknownSelectOrTime", { key }));
  }
  const normalizedExpected = normalize(expected);

  const response = await fetch(buildEntityPath(entity.domain, entity.name), {
    cache: "no-store",
    headers: { "Cache-Control": "no-store" },
  });
  if (!response.ok) {
    throw new Error(t("entityBackup.verifyHttp", { status: response.status }));
  }
  const payload = await response.json();
  const actual = normalize(payload?.value ?? payload?.state);
  if (!actual || !normalizedExpected) {
    throw new Error(t("entityBackup.invalidStatus", { name: entity.name }));
  }
  return actual === normalizedExpected;
}

export async function verifyEntityBackupSwitchState(key, expected) {
  const entity = ENTITY_DEFS[key];
  if (!entity || entity.domain !== "switch") {
    throw new Error(t("entityBackup.unknownSwitch", { key }));
  }

  const response = await fetch(buildEntityPath(entity.domain, entity.name), {
    cache: "no-store",
    headers: { "Cache-Control": "no-store" },
  });
  if (!response.ok) {
    throw new Error(t("entityBackup.verifyHttp", { status: response.status }));
  }
  const actual = getEntityBackupSwitchState(await response.json());
  if (actual === null) {
    throw new Error(t("entityBackup.invalidStatus", { name: entity.name }));
  }
  return actual === Boolean(expected);
}
