import { formatDate, t } from "../i18n/index.js";

export const ENERGY_HISTORY_VALUE_KEYS = [
  "electricalInputWh",
  "heatingInputWh",
  "coolingInputWh",
  "heatpumpHeatOutputWh",
  "heatpumpCoolingOutputWh",
  "boilerHeatOutputWh",
  "systemHeatOutputWh",
];

// Weekdagnamen volgen de actieve locale (nl: Zo..Za, en: Sun..Sat).
// Referentie: zondag 7 januari 2024.
const WEEKDAY_REFERENCE_SUNDAY = new Date(2024, 0, 7, 12, 0, 0);

export function getEnergyHistoryWeekdayLabels() {
  return Array.from({ length: 7 }, (_item, index) => {
    const date = new Date(WEEKDAY_REFERENCE_SUNDAY.getTime());
    date.setDate(date.getDate() + index);
    const label = formatDate(date, { weekday: "short" }).replace(/\./g, "");
    return label.charAt(0).toUpperCase() + label.slice(1);
  });
}

export function parseEnergyHistoryMetadata(rawValue) {
  const metadata = {
    storedDayCount: 0,
    oldestDateKey: null,
    newestDateKey: null,
    hourStoredDayCount: 0,
    hourOldestDateKey: null,
    hourNewestDateKey: null,
    hourRequestedRetentionDays: 0,
    hourSlotCount: 0,
    hourPartitionAvailable: false,
    hourRecordCount: 0,
    hourWriteCount: 0,
    hourStorageKb: 0,
    hourLastWriteTimestampS: 0,
    dayPartitionAvailable: false,
    dayStorageKb: 0,
    dayWriteCount: 0,
    dayLastWriteTimestampS: 0,
  };
  String(rawValue || "").split(/\r?\n/).forEach((line) => {
    const parts = line.split("|");
    if (line.startsWith("@bounds|")) {
      metadata.storedDayCount = Number(parts[1]) || 0;
      metadata.oldestDateKey = Number(parts[2]) || null;
      metadata.newestDateKey = Number(parts[3]) || null;
      metadata.hourStoredDayCount = Number(parts[4]) || 0;
      metadata.hourOldestDateKey = Number(parts[5]) || null;
      metadata.hourNewestDateKey = Number(parts[6]) || null;
    } else if (line.startsWith("@day_retention|")) {
      metadata.dayPartitionAvailable = Number(parts[1]) === 1;
      metadata.dayStorageKb = Number(parts[2]) || 0;
      metadata.dayWriteCount = Number(parts[3]) || 0;
      metadata.dayLastWriteTimestampS = Number(parts[4]) || 0;
    } else if (line.startsWith("@hour_retention|")) {
      metadata.hourRequestedRetentionDays = Number(parts[1]) || 0;
      metadata.hourSlotCount = Number(parts[2]) || 0;
      metadata.hourPartitionAvailable = Number(parts[3]) === 1;
      metadata.hourRecordCount = Number(parts[4]) || 0;
      metadata.hourWriteCount = Number(parts[5]) || 0;
      metadata.hourStorageKb = Number(parts[6]) || 0;
      metadata.hourLastWriteTimestampS = Number(parts[7]) || 0;
    }
  });
  return metadata;
}

export function getEnergyHistoryDateKeyFromDate(date) {
  return (date.getFullYear() * 10000) + ((date.getMonth() + 1) * 100) + date.getDate();
}

export function getEnergyHistoryDateFromParts(year, month, day) {
  return new Date(year, month - 1, day, 12, 0, 0);
}

export function getEnergyHistoryDaysInMonth(year, month) {
  return new Date(year, month, 0).getDate();
}

export function padEnergyHistoryDatePart(value) {
  return String(value).padStart(2, "0");
}

export function parseEnergyHistoryDateKey(dateKey) {
  const key = Number(dateKey);
  if (!Number.isFinite(key) || key <= 0) {
    return null;
  }
  const year = Math.floor(key / 10000);
  const month = Math.floor(key / 100) % 100;
  const day = key % 100;
  if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31) {
    return null;
  }
  const date = new Date(year, month - 1, day, 12, 0, 0);
  if (date.getFullYear() !== year || date.getMonth() + 1 !== month || date.getDate() !== day) {
    return null;
  }
  return { key, year, month, day, date };
}

export function getEnergyHistoryDateInputValue(dateKey) {
  const parsed = parseEnergyHistoryDateKey(dateKey);
  if (!parsed) {
    return "";
  }
  return `${parsed.year}-${padEnergyHistoryDatePart(parsed.month)}-${padEnergyHistoryDatePart(parsed.day)}`;
}

export function parseEnergyHistoryDateInputValue(value) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(value || "").trim());
  if (!match) {
    return null;
  }
  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const date = getEnergyHistoryDateFromParts(year, month, day);
  if (date.getFullYear() !== year || date.getMonth() + 1 !== month || date.getDate() !== day) {
    return null;
  }
  return parseEnergyHistoryDateKey(getEnergyHistoryDateKeyFromDate(date));
}

export function getEnergyHistoryMonthKey(year, month) {
  return (Number(year) * 100) + Number(month);
}

export function getEnergyHistoryMonthKeyFromDate(date) {
  return getEnergyHistoryMonthKey(date.getFullYear(), date.getMonth() + 1);
}

export function parseEnergyHistoryMonthKey(monthKey) {
  const key = Number(monthKey);
  if (!Number.isFinite(key) || key <= 0) {
    return null;
  }
  const year = Math.floor(key / 100);
  const month = key % 100;
  if (year < 2020 || month < 1 || month > 12) {
    return null;
  }
  return { key, year, month, date: new Date(year, month - 1, 1, 12, 0, 0) };
}

export function parseEnergyHistoryMonthInputValue(value) {
  const raw = String(value || "").trim();
  const match = /^(\d{4})-(\d{2})$/.exec(raw);
  if (match) {
    return parseEnergyHistoryMonthKey(getEnergyHistoryMonthKey(Number(match[1]), Number(match[2])));
  }
  return parseEnergyHistoryMonthKey(raw);
}

export function addEnergyHistoryMonths(monthKey, offset) {
  const parsed = parseEnergyHistoryMonthKey(monthKey);
  if (!parsed) {
    return "";
  }
  const date = new Date(parsed.year, parsed.month - 1 + Number(offset || 0), 1, 12, 0, 0);
  return String(getEnergyHistoryMonthKeyFromDate(date));
}

export function getEnergyHistoryWeekStart(date) {
  const start = new Date(date.getTime());
  const weekday = start.getDay();
  const diff = weekday === 0 ? -6 : 1 - weekday;
  start.setDate(start.getDate() + diff);
  start.setHours(12, 0, 0, 0);
  return start;
}

export function addEnergyHistoryDays(date, days) {
  const next = new Date(date.getTime());
  next.setDate(next.getDate() + days);
  next.setHours(12, 0, 0, 0);
  return next;
}

export function formatEnergyHistoryDayMonth(date) {
  return formatDate(date, { day: "numeric", month: "short" }).replace(/\./g, "");
}

export function getEnergyHistoryIsoWeekInfo(date) {
  const target = new Date(Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()));
  const dayNumber = (target.getUTCDay() + 6) % 7;
  target.setUTCDate(target.getUTCDate() - dayNumber + 3);
  const weekYear = target.getUTCFullYear();
  const firstThursday = new Date(Date.UTC(weekYear, 0, 4));
  const firstDayNumber = (firstThursday.getUTCDay() + 6) % 7;
  firstThursday.setUTCDate(firstThursday.getUTCDate() - firstDayNumber + 3);
  const week = 1 + Math.round((target - firstThursday) / (7 * 24 * 60 * 60 * 1000));
  return { week, year: weekYear };
}

export function getEnergyHistoryWeekStartKeyFromDate(date) {
  return getEnergyHistoryDateKeyFromDate(getEnergyHistoryWeekStart(date));
}

export function parseEnergyHistoryWeekValue(value) {
  const parsed = parseEnergyHistoryDateInputValue(value) || parseEnergyHistoryDateKey(value);
  if (!parsed) {
    return null;
  }
  return parseEnergyHistoryDateKey(getEnergyHistoryDateKeyFromDate(getEnergyHistoryWeekStart(parsed.date)));
}

export function formatEnergyHistoryWeekLabel(weekStartKey) {
  const parsed = parseEnergyHistoryDateKey(weekStartKey);
  if (!parsed) {
    return t("energy.weekLabel");
  }
  const start = getEnergyHistoryWeekStart(parsed.date);
  const end = addEnergyHistoryDays(start, 6);
  return t("energy.weekRange", {
    week: getEnergyHistoryIsoWeekInfo(start).week,
    start: formatEnergyHistoryDayMonth(start),
    end: formatEnergyHistoryDayMonth(end),
  });
}

export function formatEnergyHistoryDateLabel(dateKey, mode = "day") {
  const parsed = parseEnergyHistoryDateKey(dateKey);
  if (!parsed) {
    return t("common.notAvailable");
  }
  if (mode === "weekday") return getEnergyHistoryWeekdayLabels()[parsed.date.getDay()] || "";
  if (mode === "month") return formatDate(parsed.date, { month: "short" });
  if (mode === "year") return String(parsed.year);
  return formatDate(parsed.date, { day: "2-digit", month: "short" });
}

export function normalizeEnergyHistoryWh(rawValue) {
  const value = Number(rawValue);
  return Number.isFinite(value) && value >= 0 ? value : null;
}

export function getEnergyHistoryRecordValuesFromParts(parts, offset = 0) {
  return Object.fromEntries(ENERGY_HISTORY_VALUE_KEYS.map((key, index) => [
    key,
    normalizeEnergyHistoryWh(parts[offset + index]),
  ]));
}

export function parseEnergyHistoryLine(line) {
  const value = String(line || "").trim();
  if (!value || value.startsWith("@")) return null;
  const parts = value.split("|");
  if (parts.length < 10) return null;
  const sequence = Number(parts[0]);
  const dateKey = Number(parts[1]);
  const flags = Number(parts[2]);
  const parsed = parseEnergyHistoryDateKey(dateKey);
  if (!Number.isFinite(sequence) || !parsed) return null;
  return {
    sequence,
    dateKey,
    year: parsed.year,
    month: parsed.month,
    day: parsed.day,
    partial: Boolean(flags & 1),
    source: "flash",
    ...getEnergyHistoryRecordValuesFromParts(parts, 3),
  };
}

export function parseEnergyHistoryCurrentLine(line) {
  const value = String(line || "").trim();
  if (!value.startsWith("@current|")) return null;
  const parts = value.split("|");
  if (parts.length < 9) return null;
  const dateKey = Number(parts[1]);
  const parsed = parseEnergyHistoryDateKey(dateKey);
  if (!parsed) return null;
  return {
    sequence: Number.MAX_SAFE_INTEGER,
    dateKey,
    year: parsed.year,
    month: parsed.month,
    day: parsed.day,
    partial: true,
    source: "current",
    ...getEnergyHistoryRecordValuesFromParts(parts, 2),
  };
}

export function parseEnergyHistoryHourLine(line) {
  const value = String(line || "").trim();
  if (!value.startsWith("@hour|")) return null;
  const parts = value.split("|");
  if (parts.length < 11) return null;
  const sequence = Number(parts[1]);
  const dateKey = Number(parts[2]);
  const hour = Number(parts[3]);
  const parsed = parseEnergyHistoryDateKey(dateKey);
  if (!Number.isFinite(sequence) || !parsed || !Number.isInteger(hour) || hour < 0 || hour > 23) return null;
  return {
    sequence,
    dateKey,
    year: parsed.year,
    month: parsed.month,
    day: parsed.day,
    hour,
    partial: true,
    source: "hour",
    label: String(hour),
    tooltipLabel: `${String(hour).padStart(2, "0")}:00 - ${String((hour + 1) % 24).padStart(2, "0")}:00`,
    sortKey: (dateKey * 100) + hour,
    ...getEnergyHistoryRecordValuesFromParts(parts, 4),
  };
}

export function getEnergyHistoryRecordWh(record, key) {
  const value = Number(record?.[key]);
  return Number.isFinite(value) && value >= 0 ? value : 0;
}
