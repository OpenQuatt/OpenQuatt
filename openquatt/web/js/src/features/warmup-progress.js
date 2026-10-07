import { hasEntity } from "../core/app-shared.js";
import { getEntityValue } from "../core/entity-store.js";
import { t } from "../i18n/index.js";

function formatWarmupDuration(seconds) {
  const minutes = Math.floor(seconds / 60);
  if (minutes === 0) return t("warmup.durationLessThanMinute");
  const hours = Math.floor(minutes / 60);
  const remainder = minutes % 60;
  const hourText = hours === 1 ? t("warmup.durationHour") : t("warmup.durationHours", { count: hours });
  const minuteText = remainder === 1 ? t("warmup.durationMinute") : t("warmup.durationMinutes", { count: remainder });
  if (hours === 0) return minuteText;
  if (remainder === 0) return hourText;
  return t("warmup.durationHoursMinutes", { hours: hourText, minutes: minuteText });
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
