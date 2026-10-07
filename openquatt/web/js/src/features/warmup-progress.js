import { hasEntity } from "../core/app-shared.js";
import { getEntityValue } from "../core/entity-store.js";
import { t } from "../i18n/index.js";

function formatWarmupDuration(seconds) {
  const minutes = Math.floor(seconds / 60);
  return `${String(Math.floor(minutes / 60)).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
}

export function getWarmupProgressText() {
  if (!hasEntity("warmupStatus") || !hasEntity("warmupElapsed")) return "";
  const integer = (key) => {
    const value = getEntityValue(key);
    return typeof value === "number" ? value :
      typeof value === "string" && /^\d+$/.test(value.trim()) ? Number(value) : NaN;
  };
  const status = integer("warmupStatus");
  const seconds = integer("warmupElapsed");
  if (!Number.isInteger(status) || status < 1 || status > 9 ||
      !Number.isInteger(seconds) || seconds < 0 || seconds > 28800) return "";
  const duration = formatWarmupDuration(seconds);
  if (status === 2) return t("warmup.elapsed", { duration });
  if (status === 3) return t("warmup.completed", { duration });
  if (status === 5) return t("warmup.timeLimit", { duration });
  return t("warmup.cancelled", { duration });
}
