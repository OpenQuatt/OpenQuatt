import { t } from "../i18n/index.js";

export const NON_WARNING_FAILURE_NAMES = new Set([
  "compressor oil return",
]);

export function formatFailures(value) {
  const raw = String(value || "").trim();
  if (!raw || raw === "None") {
    return t("failures.none");
  }
  return raw;
}

export function getWarningFailureItems(value) {
  const formatted = formatFailures(value);
  if (formatted === t("failures.none")) {
    return [];
  }
  return formatted
    .split(",")
    .map((item) => item.trim())
    .filter((item) => item && !NON_WARNING_FAILURE_NAMES.has(item.toLowerCase()));
}

export function formatWarningFailures(value) {
  const warningItems = getWarningFailureItems(value);
  return warningItems.length > 0 ? warningItems.join(", ") : t("failures.none");
}
