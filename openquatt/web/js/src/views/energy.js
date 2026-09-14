import { formatOverviewStatValue, getDerivedEfficiencyValue, getEntityNumericValue, getEntityStateText, hasEntity, isEfficiencyKey } from "../core/app-shared.js";
import { OVERVIEW_ENERGY_COLUMN_CONFIGS } from "../core/config.js";
import { setEnergyHistoryRequestQueryProvider } from "../core/energy-history-query.js";
import { ENERGY_HISTORY_VALUE_KEYS, ENERGY_HISTORY_WEEKDAY_LABELS, addEnergyHistoryDays, addEnergyHistoryMonths, formatEnergyHistoryDateLabel, formatEnergyHistoryWeekLabel, getEnergyHistoryDateFromParts, getEnergyHistoryDateInputValue, getEnergyHistoryDateKeyFromDate, getEnergyHistoryDaysInMonth, getEnergyHistoryIsoWeekInfo, getEnergyHistoryMonthKeyFromDate, getEnergyHistoryRecordWh, getEnergyHistoryWeekStart, getEnergyHistoryWeekStartKeyFromDate, parseEnergyHistoryCurrentLine, parseEnergyHistoryDateInputValue, parseEnergyHistoryDateKey, parseEnergyHistoryHourLine, parseEnergyHistoryLine, parseEnergyHistoryMetadata, parseEnergyHistoryMonthInputValue, parseEnergyHistoryMonthKey, parseEnergyHistoryWeekValue } from "../core/energy-history-domain.js";
export * from "../core/energy-history-domain.js";
import { getRenderSignature } from "../core/render-signatures.js";
import { state } from "../core/state.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { refreshEnergyHistoryData } from "../features/storage-history.js";
import { updateEnergyHistoryState } from "../core/feature-state.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";
import { replaceOuterHtmlIfSignatureChanged } from "./view-utils.js";
import { renderStatCard } from "./stat-card.js";

  export function renderOverviewEnergyRow([label, key]) {
    const derived = getDerivedEfficiencyValue(key);
    if (!hasEntity(key) && Number.isNaN(derived)) {
      return "";
    }
    const value = isEfficiencyKey(key) ? formatOverviewStatValue(key) : getEntityStateText(key);
    return `
      <div class="oq-overview-energy-row">
        <span>${escapeHtml(label)}</span>
        <strong>${escapeHtml(value)}</strong>
      </div>
    `;
  }

  export function renderOverviewEnergyGroup(group) {
    const filledRows = group.rows.map(renderOverviewEnergyRow).filter(Boolean).join("");
    if (!filledRows) {
      return "";
    }
    return `
      <section class="oq-overview-energy-group">
        <h5>${escapeHtml(group.title)}</h5>
        <div class="oq-overview-energy-rows">
          ${filledRows}
        </div>
      </section>
    `;
  }

  export function renderOverviewEnergyCategory(category) {
    const filledGroups = category.groups.map(renderOverviewEnergyGroup).filter(Boolean).join("");
    if (!filledGroups) {
      return "";
    }
    return `
      <section class="oq-overview-energy-category oq-overview-energy-category--${escapeHtml(category.tone)}">
        <div class="oq-overview-energy-category-head">
          <span>${escapeHtml(category.title)}</span>
        </div>
        <div class="oq-overview-energy-category-groups">
          ${filledGroups}
        </div>
      </section>
    `;
  }

  export function renderOverviewEnergyColumn(column) {
    const filledGroups = column.categories.map(renderOverviewEnergyCategory).filter(Boolean).join("");
    if (!filledGroups) {
      return "";
    }
    const counterResetKey = String(column.counterResetKey || "");
    const counterResetMarkup = counterResetKey && hasEntity(counterResetKey)
      ? `
        <button class="oq-overview-energy-reset" type="button" data-oq-action="open-energy-counter-reset-confirm" aria-label="Cumulatieve energietellers resetten" ${state.busyAction === counterResetKey ? "disabled" : ""}>
          Tellers resetten
        </button>
      `
      : "";
    return `
      <article class="oq-overview-energy-column">
        <div class="oq-overview-energy-column-copy">
          <h4>${escapeHtml(column.label)}</h4>
          ${counterResetMarkup}
        </div>
        <div class="oq-overview-energy-groups">
          ${filledGroups}
        </div>
      </article>
    `;
  }

  export function getEnergySectionModel() {
    const renderedColumns = OVERVIEW_ENERGY_COLUMN_CONFIGS.map(renderOverviewEnergyColumn).filter(Boolean);
    const gridClassName = [
      "oq-overview-energy-grid",
      renderedColumns.length === 1 ? "oq-overview-energy-grid--single" : "",
      renderedColumns.length === 2 ? "oq-overview-energy-grid--two" : "",
    ].filter(Boolean).join(" ");

    return { renderedColumns, gridClassName };
  }

  export function getEnergySectionRenderSignature(model = getEnergySectionModel()) {
    return getRenderSignature(model);
  }

  export function renderEnergySection(model = getEnergySectionModel()) {
    return `
      <section class="oq-overview-energy oq-overview-energy--solo" data-render-signature="${escapeHtml(getEnergySectionRenderSignature(model))}">
        <div class="${escapeHtml(model.gridClassName)}">
          ${model.renderedColumns.join("")}
        </div>
      </section>
    `;
  }

  export const ENERGY_HISTORY_VIEW_OPTIONS = [
    { id: "day", label: "Dag" },
    { id: "week", label: "Week" },
    { id: "month", label: "Maand" },
    { id: "year", label: "Jaar" },
    { id: "all", label: "Alles" },
  ];

  export const ENERGY_HISTORY_PERIOD_VIEW_IDS = new Set(["day", "week", "month", "year"]);

  export function normalizeEnergyHistoryView(view) {
    const value = String(view || "").trim();
    return ENERGY_HISTORY_VIEW_OPTIONS.some((option) => option.id === value) ? value : "day";
  }

  export function setEnergyHistoryView(view) {
    const nextView = normalizeEnergyHistoryView(view);
    if (state.energyHistoryView === nextView) {
      return;
    }
    updateEnergyHistoryState({ energyHistoryView: nextView, energyHistoryLastFetchAt: 0 });
    render();
    requestEnergyHistoryDataRefresh();
  }

  export function requestEnergyHistoryDataRefresh() {
    if (typeof refreshEnergyHistoryData !== "function") {
      return;
    }
    void refreshEnergyHistoryData({ force: true }).then((changed) => {
      if (changed) {
        render();
      }
    });
  }

  export function isEnergyHistoryPeriodView(view) {
    return ENERGY_HISTORY_PERIOD_VIEW_IDS.has(normalizeEnergyHistoryView(view));
  }

  export function getEnergyHistoryTodayKey() {
    const now = new Date();
    return (now.getFullYear() * 10000) + ((now.getMonth() + 1) * 100) + now.getDate();
  }

  export function getEnergyHistoryMetadataFromRaw() {
    return parseEnergyHistoryMetadata(state.energyHistoryRaw);
  }

  export function getEnergyHistoryCurrentDateKeyFromRaw() {
    const raw = String(state.energyHistoryRaw || "");
    let currentKey = null;
    raw.split(/\r?\n/).forEach((line) => {
      const record = parseEnergyHistoryCurrentLine(line);
      if (record) {
        currentKey = record.dateKey;
      }
    });
    return currentKey;
  }

  export function getEnergyHistoryReferenceDateKey(records = [], includeHours = true) {
    const currentKey = getEnergyHistoryCurrentDateKeyFromRaw();
    const metadata = getEnergyHistoryMetadataFromRaw();
    const dateKeys = (Array.isArray(records) ? records : [])
      .map((record) => Number(record?.dateKey))
      .filter(Number.isFinite);

    if (Number.isFinite(Number(metadata.newestDateKey))) {
      dateKeys.push(Number(metadata.newestDateKey));
    }

    if (Number.isFinite(Number(currentKey))) {
      dateKeys.push(Number(currentKey));
    }

    if (includeHours) {
      getEnergyHistoryHourRecords().forEach((record) => {
        const dateKey = Number(record?.dateKey);
        if (Number.isFinite(dateKey)) {
          dateKeys.push(dateKey);
        }
      });
    }

    return dateKeys.length ? Math.max(...dateKeys) : getEnergyHistoryTodayKey();
  }

  export function getEntityKwhAsWh(key) {
    const value = getEntityNumericValue(key);
    if (!Number.isFinite(value) || value < 0) {
      return null;
    }
    return Math.round(value * 1000);
  }

  export function getEnergyHistoryTodayRecord() {
    const dateKey = getEnergyHistoryCurrentDateKeyFromRaw() || getEnergyHistoryTodayKey();
    const parsed = parseEnergyHistoryDateKey(dateKey);
    if (!parsed) {
      return null;
    }
    const record = {
      sequence: Number.MAX_SAFE_INTEGER - 1,
      dateKey,
      year: parsed.year,
      month: parsed.month,
      day: parsed.day,
      partial: true,
      source: "sensors",
      electricalInputWh: getEntityKwhAsWh("electricalEnergyDaily"),
      heatingInputWh: getEntityKwhAsWh("heatingElectricalEnergyDaily"),
      coolingInputWh: getEntityKwhAsWh("coolingElectricalEnergyDaily"),
      heatpumpHeatOutputWh: getEntityKwhAsWh("heatpumpThermalEnergyDaily"),
      heatpumpCoolingOutputWh: getEntityKwhAsWh("heatpumpCoolingEnergyDaily"),
      boilerHeatOutputWh: getEntityKwhAsWh("boilerThermalEnergyDaily"),
      systemHeatOutputWh: getEntityKwhAsWh("systemThermalEnergyDaily"),
    };
    return ENERGY_HISTORY_VALUE_KEYS.some((key) => Number.isFinite(record[key])) ? record : null;
  }

  export function getEnergyHistoryRecords() {
    const byDate = new Map();
    const raw = String(state.energyHistoryRaw || "");
    raw.split(/\r?\n/).forEach((line) => {
      const record = parseEnergyHistoryLine(line) || parseEnergyHistoryCurrentLine(line);
      if (!record) {
        return;
      }
      const existing = byDate.get(record.dateKey);
      if (!existing || record.sequence >= existing.sequence) {
        byDate.set(record.dateKey, record);
      }
    });

    const todayRecord = getEnergyHistoryTodayRecord();
    if (todayRecord && !byDate.has(todayRecord.dateKey)) {
      byDate.set(todayRecord.dateKey, todayRecord);
    }

    const datesWithDayRecords = new Set(byDate.keys());
    const hourSummaries = new Map();
    getEnergyHistoryHourRecords().forEach((record) => {
      if (datesWithDayRecords.has(record.dateKey)) {
        return;
      }
      let bucket = hourSummaries.get(record.dateKey);
      if (!bucket) {
        const parsed = parseEnergyHistoryDateKey(record.dateKey);
        if (!parsed) {
          return;
        }
        bucket = createEnergyHistoryBucket({
          dateKey: parsed.key,
          year: parsed.year,
          month: parsed.month,
          day: parsed.day,
          label: formatEnergyHistoryDateLabel(parsed.key),
          sortKey: parsed.key,
          source: "hour-summary",
        });
        bucket.tooltipLabel = `${formatEnergyHistoryDateLabel(record.dateKey)} · uurdata sinds herstart`;
        hourSummaries.set(record.dateKey, bucket);
      }
      mergeEnergyHistoryRecordIntoBucket(bucket, record);
    });
    hourSummaries.forEach((bucket, dateKey) => {
      byDate.set(dateKey, bucket);
    });

    return [...byDate.values()].sort((a, b) => a.dateKey - b.dateKey);
  }

  export function getEnergyHistoryHourRecords() {
    const byHour = new Map();
    const raw = String(state.energyHistoryRaw || "");
    raw.split(/\r?\n/).forEach((line) => {
      const record = parseEnergyHistoryHourLine(line);
      if (!record) {
        return;
      }
      const key = `${record.dateKey}:${record.hour}`;
      const existing = byHour.get(key);
      if (!existing || record.sequence >= existing.sequence) {
        byHour.set(key, record);
      }
    });
    return [...byHour.values()].sort((a, b) => a.sortKey - b.sortKey);
  }

  export function getEnergyHistoryHourRecordsForDate(dateKey) {
    return getEnergyHistoryHourRecords().filter((record) => record.dateKey === Number(dateKey));
  }

  export function sumEnergyHistoryWh(records, key) {
    return records.reduce((sum, record) => {
      return sum + getEnergyHistoryRecordWh(record, key);
    }, 0);
  }

  export function getEnergyHistoryOutputWh(record) {
    return ["heatpumpHeatOutputWh", "heatpumpCoolingOutputWh", "boilerHeatOutputWh"].reduce((sum, key) => {
      return sum + getEnergyHistoryRecordWh(record, key);
    }, 0);
  }

  export function getEnergyHistoryStackWh(record) {
    return getEnergyHistoryRecordWh(record, "electricalInputWh") + getEnergyHistoryOutputWh(record);
  }

  export function formatEnergyRatio(numeratorWh, denominatorWh) {
    const numerator = Number(numeratorWh);
    const denominator = Number(denominatorWh);
    if (!Number.isFinite(numerator) || !Number.isFinite(denominator) || denominator <= 0) {
      return "—";
    }
    return (numerator / denominator).toFixed(2);
  }

  export function formatEnergyAdaptiveWh(wh, decimals = 1) {
    const value = Number(wh);
    if (!Number.isFinite(value)) {
      return "—";
    }
    if (Math.abs(value) >= 999500) {
      return `${(value / 1000000).toFixed(2)} MWh`;
    }
    if (Math.abs(value) < 1000) {
      return `${Math.round(value)} Wh`;
    }
    return `${(value / 1000).toFixed(decimals)} kWh`;
  }

  export function createEnergyHistoryBucket({ dateKey, year, month, day, hour = null, label, tooltipLabel = "", sortKey, source = "bucket" }) {
    return {
      sequence: 0,
      dateKey,
      year,
      month,
      day,
      hour,
      label,
      tooltipLabel,
      sortKey: sortKey ?? dateKey,
      partial: false,
      source,
      electricalInputWh: 0,
      heatingInputWh: 0,
      coolingInputWh: 0,
      heatpumpHeatOutputWh: 0,
      heatpumpCoolingOutputWh: 0,
      boilerHeatOutputWh: 0,
      systemHeatOutputWh: 0,
    };
  }

  export function mergeEnergyHistoryRecordIntoBucket(bucket, record) {
    ENERGY_HISTORY_VALUE_KEYS.forEach((key) => {
      bucket[key] += getEnergyHistoryRecordWh(record, key);
    });
    bucket.partial = bucket.partial || Boolean(record?.partial);
    bucket.sequence = Math.max(Number(bucket.sequence || 0), Number(record?.sequence || 0));
    return bucket;
  }

  export function getEnergyHistoryRecordsByDate(records) {
    const byDate = new Map();
    records.forEach((record) => {
      byDate.set(record.dateKey, record);
    });
    return byDate;
  }

  export function normalizeEnergyHistoryPeriodValue(view, value) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (normalizedView === "day") {
      const parsed = parseEnergyHistoryDateInputValue(value) || parseEnergyHistoryDateKey(value);
      return parsed ? String(parsed.key) : "";
    }
    if (normalizedView === "week") {
      const parsed = parseEnergyHistoryWeekValue(value);
      return parsed ? String(parsed.key) : "";
    }
    if (normalizedView === "month") {
      const parsed = parseEnergyHistoryMonthInputValue(value);
      return parsed ? String(parsed.key) : "";
    }
    if (normalizedView === "year") {
      const year = Number(value);
      return Number.isInteger(year) && year >= 2020 && year <= 2200 ? String(year) : "";
    }
    return "";
  }

  export function getEnergyHistoryPeriodBounds(records, view) {
    const normalizedView = normalizeEnergyHistoryView(view);
    const reference = parseEnergyHistoryDateKey(getEnergyHistoryReferenceDateKey(records, true));
    const metadata = getEnergyHistoryMetadataFromRaw();
    const hourRecords = getEnergyHistoryHourRecords();
    const dateKeys = [
      ...records.map((record) => record.dateKey),
      ...hourRecords.map((record) => record.dateKey),
    ].filter((key) => Number.isFinite(Number(key)));
    if (Number.isFinite(Number(metadata.oldestDateKey))) {
      dateKeys.push(Number(metadata.oldestDateKey));
    }
    if (Number.isFinite(Number(metadata.newestDateKey))) {
      dateKeys.push(Number(metadata.newestDateKey));
    }
    const oldestKey = dateKeys.length ? Math.min(...dateKeys.map(Number)) : reference?.key;
    const oldest = oldestKey ? parseEnergyHistoryDateKey(oldestKey) : reference;
    const oldestDate = oldest?.date || reference?.date || new Date();
    const referenceDate = reference?.date || new Date();
    let min = reference?.key || getEnergyHistoryTodayKey();
    let max = min;

    if (normalizedView === "week") {
      min = getEnergyHistoryWeekStartKeyFromDate(oldestDate);
      max = getEnergyHistoryWeekStartKeyFromDate(referenceDate);
    } else if (normalizedView === "month") {
      min = getEnergyHistoryMonthKeyFromDate(oldestDate);
      max = getEnergyHistoryMonthKeyFromDate(referenceDate);
    } else if (normalizedView === "year") {
      min = oldestDate.getFullYear();
      max = referenceDate.getFullYear();
    } else {
      min = getEnergyHistoryDateKeyFromDate(oldestDate);
      max = getEnergyHistoryDateKeyFromDate(referenceDate);
    }

    if (Number(min) > Number(max)) {
      min = max;
    }
    return { min: String(min), max: String(max) };
  }

  export function clampEnergyHistoryPeriodValue(value, bounds) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      return String(bounds.max);
    }
    if (numeric < Number(bounds.min)) {
      return String(bounds.min);
    }
    if (numeric > Number(bounds.max)) {
      return String(bounds.max);
    }
    return String(value);
  }

  export function getEnergyHistorySelectedPeriodValue(records, view, bounds = getEnergyHistoryPeriodBounds(records, view)) {
    const normalizedView = normalizeEnergyHistoryView(view);
    const stored = state.energyHistoryPeriodSelection?.[normalizedView];
    const normalizedStored = normalizeEnergyHistoryPeriodValue(normalizedView, stored);
    return clampEnergyHistoryPeriodValue(normalizedStored || bounds.max, bounds);
  }

  export function getEnergyHistoryPeriodOptions(view, bounds) {
    const normalizedView = normalizeEnergyHistoryView(view);
    const options = [];
    let guard = 0;

    if (normalizedView === "week") {
      let cursor = String(bounds.max);
      while (Number(cursor) >= Number(bounds.min) && guard < 6000) {
        const parsed = parseEnergyHistoryDateKey(cursor);
        options.push({
          value: cursor,
          label: formatEnergyHistoryWeekLabel(cursor),
          group: parsed ? String(getEnergyHistoryIsoWeekInfo(parsed.date).year) : "",
        });
        cursor = parsed ? String(getEnergyHistoryDateKeyFromDate(addEnergyHistoryDays(parsed.date, -7))) : "";
        guard += 1;
      }
      return options;
    }

    if (normalizedView === "month") {
      let cursor = String(bounds.max);
      while (Number(cursor) >= Number(bounds.min) && guard < 1200) {
        const parsed = parseEnergyHistoryMonthKey(cursor);
        if (!parsed) {
          break;
        }
        options.push({
          value: cursor,
          label: parsed.date.toLocaleDateString("nl-NL", { month: "long", year: "numeric" }),
          group: String(parsed.year),
        });
        cursor = addEnergyHistoryMonths(cursor, -1);
        guard += 1;
      }
      return options;
    }

    if (normalizedView === "year") {
      for (let year = Number(bounds.max); year >= Number(bounds.min); year -= 1) {
        options.push({ value: String(year), label: String(year) });
      }
    }

    return options;
  }

  export function getEnergyHistoryPeriodControlModel(records, view) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (!isEnergyHistoryPeriodView(normalizedView)) {
      return {
        view: normalizedView,
        selectedValue: "",
        minValue: "",
        maxValue: "",
        canPrevious: false,
        canNext: false,
        isNow: true,
        options: [],
      };
    }

    const bounds = getEnergyHistoryPeriodBounds(records, normalizedView);
    const selectedValue = getEnergyHistorySelectedPeriodValue(records, normalizedView, bounds);
    return {
      view: normalizedView,
      selectedValue,
      minValue: bounds.min,
      maxValue: bounds.max,
      canPrevious: Number(selectedValue) > Number(bounds.min),
      canNext: Number(selectedValue) < Number(bounds.max),
      isNow: Number(selectedValue) === Number(bounds.max),
      options: getEnergyHistoryPeriodOptions(normalizedView, bounds),
    };
  }

  export function getEnergyHistoryRequestRange(records, view) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (!isEnergyHistoryPeriodView(normalizedView)) {
      return { from: "", to: "", hours: "0" };
    }

    const period = getEnergyHistoryPeriodControlModel(records, normalizedView);
    if (normalizedView === "day") {
      return { from: period.selectedValue, to: period.selectedValue, hours: "1" };
    }
    if (normalizedView === "week") {
      const selected = parseEnergyHistoryDateKey(period.selectedValue);
      if (!selected) {
        return { from: "", to: "", hours: "0" };
      }
      const start = getEnergyHistoryWeekStart(selected.date);
      const end = addEnergyHistoryDays(start, 6);
      return {
        from: String(getEnergyHistoryDateKeyFromDate(start)),
        to: String(getEnergyHistoryDateKeyFromDate(end)),
        hours: "0",
      };
    }
    if (normalizedView === "month") {
      const selected = parseEnergyHistoryMonthKey(period.selectedValue);
      if (!selected) {
        return { from: "", to: "", hours: "0" };
      }
      return {
        from: String((selected.year * 10000) + (selected.month * 100) + 1),
        to: String((selected.year * 10000) + (selected.month * 100) + getEnergyHistoryDaysInMonth(selected.year, selected.month)),
        hours: "0",
      };
    }
    if (normalizedView === "year") {
      const year = Number(period.selectedValue);
      if (!Number.isInteger(year)) {
        return { from: "", to: "", hours: "0" };
      }
      return { from: `${year}0101`, to: `${year}1231`, hours: "0" };
    }
    return { from: "", to: "", hours: "0" };
  }

  export function getEnergyHistoryRequestQuery() {
    if (!String(state.energyHistoryRaw || "").trim()) {
      return "?meta=1";
    }
    const records = getEnergyHistoryRecords();
    const range = getEnergyHistoryRequestRange(records, state.energyHistoryView || "day");
    const params = new URLSearchParams();
    if (range.from) {
      params.set("from", range.from);
    }
    if (range.to) {
      params.set("to", range.to);
    }
    params.set("hours", range.hours);
    const query = params.toString();
    return query ? `?${query}` : "";
  }

  setEnergyHistoryRequestQueryProvider(getEnergyHistoryRequestQuery);

  export function setEnergyHistoryPeriodValue(view, value) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (!isEnergyHistoryPeriodView(normalizedView)) {
      return;
    }
    const records = getEnergyHistoryRecords();
    const bounds = getEnergyHistoryPeriodBounds(records, normalizedView);
    const normalized = normalizeEnergyHistoryPeriodValue(normalizedView, value);
    const nextValue = clampEnergyHistoryPeriodValue(normalized || bounds.max, bounds);
    updateEnergyHistoryState({
      energyHistoryPeriodSelection: {
        ...state.energyHistoryPeriodSelection,
        [normalizedView]: nextValue,
      },
      energyHistoryLastFetchAt: 0,
    });
    render();
    requestEnergyHistoryDataRefresh();
  }

  export function shiftEnergyHistoryPeriod(view, direction) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (!isEnergyHistoryPeriodView(normalizedView)) {
      return;
    }
    const records = getEnergyHistoryRecords();
    const period = getEnergyHistoryPeriodControlModel(records, normalizedView);
    const step = Number(direction) < 0 ? -1 : 1;
    let nextValue = period.selectedValue;

    if (normalizedView === "day") {
      const parsed = parseEnergyHistoryDateKey(period.selectedValue);
      nextValue = parsed ? String(getEnergyHistoryDateKeyFromDate(addEnergyHistoryDays(parsed.date, step))) : nextValue;
    } else if (normalizedView === "week") {
      const parsed = parseEnergyHistoryDateKey(period.selectedValue);
      nextValue = parsed ? String(getEnergyHistoryDateKeyFromDate(addEnergyHistoryDays(parsed.date, step * 7))) : nextValue;
    } else if (normalizedView === "month") {
      nextValue = addEnergyHistoryMonths(period.selectedValue, step);
    } else if (normalizedView === "year") {
      nextValue = String(Number(period.selectedValue) + step);
    }

    setEnergyHistoryPeriodValue(normalizedView, nextValue);
  }

  export function setEnergyHistoryPeriodToNow(view) {
    const normalizedView = normalizeEnergyHistoryView(view);
    if (!isEnergyHistoryPeriodView(normalizedView)) {
      return;
    }
    const records = getEnergyHistoryRecords();
    const bounds = getEnergyHistoryPeriodBounds(records, normalizedView);
    setEnergyHistoryPeriodValue(normalizedView, bounds.max);
  }

  export function getEnergyHistoryCalendarBuckets(records, view, periodModel = getEnergyHistoryPeriodControlModel(records, view)) {
    const normalizedView = normalizeEnergyHistoryView(view);
    const byDate = getEnergyHistoryRecordsByDate(records);
    if (!records.length && normalizedView === "all") {
      return { buckets: [], title: "Geen data", detail: "Lifetime energiehistorie" };
    }

    if (normalizedView === "day") {
      const selected = parseEnergyHistoryDateKey(periodModel.selectedValue);
      if (!selected) {
        return { buckets: [], title: "Geen data", detail: "Lifetime energiehistorie" };
      }
      const hourRecords = getEnergyHistoryHourRecordsForDate(selected.key);
      if (hourRecords.length) {
        const byHour = new Map(hourRecords.map((record) => [record.hour, record]));
        const buckets = [];
        for (let hour = 0; hour < 24; hour += 1) {
          const hourLabel = String(hour);
          const tooltipLabel = `${selected.date.toLocaleDateString("nl-NL", { day: "numeric", month: "long" })} · ${String(hour).padStart(2, "0")}:00 - ${String((hour + 1) % 24).padStart(2, "0")}:00`;
          const bucket = createEnergyHistoryBucket({
            dateKey: selected.key,
            year: selected.year,
            month: selected.month,
            day: selected.day,
            hour,
            label: hourLabel,
            tooltipLabel,
            sortKey: hour,
            source: "hour",
          });
          const record = byHour.get(hour);
          if (record) {
            mergeEnergyHistoryRecordIntoBucket(bucket, record);
          }
          buckets.push(bucket);
        }
        return {
          buckets,
          title: "Dag",
          detail: `${selected.date.toLocaleDateString("nl-NL", { weekday: "long", day: "numeric", month: "long", year: "numeric" })} · uurdata sinds herstart`,
        };
      }
      const record = byDate.get(selected.key);
      const currentDateKey = getEnergyHistoryCurrentDateKeyFromRaw() || getEnergyHistoryTodayKey();
      const label = selected.key === currentDateKey ? "Vandaag" : formatEnergyHistoryDateLabel(selected.key);
      const bucket = createEnergyHistoryBucket({
        dateKey: selected.key,
        year: selected.year,
        month: selected.month,
        day: selected.day,
        label,
        sortKey: selected.key,
        source: "day",
      });
      if (record) {
        mergeEnergyHistoryRecordIntoBucket(bucket, record);
      }
      return {
        buckets: [bucket],
        title: "Dag",
        detail: `${selected.date.toLocaleDateString("nl-NL", { weekday: "long", day: "numeric", month: "long", year: "numeric" })} · dagtotaal`,
      };
    }

    if (normalizedView === "week") {
      const selected = parseEnergyHistoryDateKey(periodModel.selectedValue);
      if (!selected) {
        return { buckets: [], title: "Geen data", detail: "Lifetime energiehistorie" };
      }
      const start = getEnergyHistoryWeekStart(selected.date);
      const buckets = [];
      for (let index = 0; index < 7; index += 1) {
        const date = addEnergyHistoryDays(start, index);
        const dateKey = getEnergyHistoryDateKeyFromDate(date);
        const parsed = parseEnergyHistoryDateKey(dateKey);
        const bucket = createEnergyHistoryBucket({
          dateKey,
          year: parsed.year,
          month: parsed.month,
          day: parsed.day,
          label: formatEnergyHistoryDateLabel(dateKey, "weekday"),
          sortKey: dateKey,
        });
        const record = byDate.get(dateKey);
        if (record) {
          mergeEnergyHistoryRecordIntoBucket(bucket, record);
        }
        buckets.push(bucket);
      }
      return {
        buckets,
        title: "Week",
        detail: formatEnergyHistoryWeekLabel(periodModel.selectedValue),
      };
    }

    if (normalizedView === "month") {
      const selected = parseEnergyHistoryMonthKey(periodModel.selectedValue);
      if (!selected) {
        return { buckets: [], title: "Geen data", detail: "Lifetime energiehistorie" };
      }
      const days = getEnergyHistoryDaysInMonth(selected.year, selected.month);
      const buckets = [];
      for (let day = 1; day <= days; day += 1) {
        const date = getEnergyHistoryDateFromParts(selected.year, selected.month, day);
        const dateKey = getEnergyHistoryDateKeyFromDate(date);
        const bucket = createEnergyHistoryBucket({
          dateKey,
          year: selected.year,
          month: selected.month,
          day,
          label: String(day),
          sortKey: dateKey,
        });
        const record = byDate.get(dateKey);
        if (record) {
          mergeEnergyHistoryRecordIntoBucket(bucket, record);
        }
        buckets.push(bucket);
      }
      return {
        buckets,
        title: "Maand",
        detail: selected.date.toLocaleDateString("nl-NL", { month: "long", year: "numeric" }),
      };
    }

    if (normalizedView === "year") {
      const selectedYear = Number(periodModel.selectedValue);
      if (!Number.isInteger(selectedYear)) {
        return { buckets: [], title: "Geen data", detail: "Lifetime energiehistorie" };
      }
      const buckets = [];
      for (let month = 1; month <= 12; month += 1) {
        const dateKey = (selectedYear * 10000) + (month * 100) + 1;
        const bucket = createEnergyHistoryBucket({
          dateKey,
          year: selectedYear,
          month,
          day: 1,
          label: formatEnergyHistoryDateLabel(dateKey, "month"),
          sortKey: month,
          source: "month",
        });
        records
          .filter((record) => record.year === selectedYear && record.month === month)
          .forEach((record) => mergeEnergyHistoryRecordIntoBucket(bucket, record));
        buckets.push(bucket);
      }
      return {
        buckets,
        title: "Jaar",
        detail: String(selectedYear),
      };
    }

    const years = new Map();
    records.forEach((record) => {
      if (!years.has(record.year)) {
        years.set(record.year, createEnergyHistoryBucket({
          dateKey: (record.year * 10000) + 101,
          year: record.year,
          month: 1,
          day: 1,
          label: String(record.year),
          sortKey: record.year,
          source: "year",
        }));
      }
      mergeEnergyHistoryRecordIntoBucket(years.get(record.year), record);
    });
    const buckets = [...years.values()].sort((a, b) => a.sortKey - b.sortKey);
    return {
      buckets,
      title: "Alles",
      detail: buckets.length ? `${buckets[0].label} - ${buckets[buckets.length - 1].label}` : "Geen data",
    };
  }

  export function getEnergyHistorySummary(records) {
    const heatOutputWh = sumEnergyHistoryWh(records, "heatpumpHeatOutputWh");
    const coolingOutputWh = sumEnergyHistoryWh(records, "heatpumpCoolingOutputWh");
    const boilerOutputWh = sumEnergyHistoryWh(records, "boilerHeatOutputWh");
    return {
      electricalInputWh: sumEnergyHistoryWh(records, "electricalInputWh"),
      heatingInputWh: sumEnergyHistoryWh(records, "heatingInputWh"),
      coolingInputWh: sumEnergyHistoryWh(records, "coolingInputWh"),
      heatOutputWh,
      coolingOutputWh,
      boilerOutputWh,
      outputWh: heatOutputWh + coolingOutputWh + boilerOutputWh,
    };
  }

  export function getEnergyHistoryHeatpumpShare(summary) {
    const heatpumpWh = Number(summary.heatOutputWh || 0) + Number(summary.coolingOutputWh || 0);
    const boilerWh = Number(summary.boilerOutputWh || 0);
    const total = heatpumpWh + boilerWh;
    if (!Number.isFinite(total) || total <= 0) {
      return Number.NaN;
    }
    return (heatpumpWh / total) * 100;
  }

  export function getEnergyHistoryEfficiencyStat(summary) {
    const cop = formatEnergyRatio(summary.heatOutputWh, summary.heatingInputWh);
    const eer = formatEnergyRatio(summary.coolingOutputWh, summary.coolingInputWh);
    const hasCop = Number(summary.heatOutputWh || 0) > 0 && cop !== "—";
    const hasEer = Number(summary.coolingOutputWh || 0) > 0 && eer !== "—";

    if (hasCop && hasEer) {
      return { label: "COP / EER", value: `${cop} / ${eer}` };
    }
    if (hasEer) {
      return { label: "Gemiddelde EER", value: eer };
    }
    return { label: "Gemiddelde COP", value: cop };
  }

  export function renderEnergyHistoryStat(label, value, note = "") {
    return renderStatCard({ label, value, note });
  }

  export function renderEnergyHistoryPeriodSelect(periodModel, label, options) {
    const groupedOptions = [];
    options.forEach((option) => {
      const groupLabel = String(option.group || "");
      let group = groupedOptions[groupedOptions.length - 1];
      if (!group || group.label !== groupLabel) {
        group = { label: groupLabel, options: [] };
        groupedOptions.push(group);
      }
      group.options.push(option);
    });
    const optionMarkup = groupedOptions.some((group) => group.label)
      ? groupedOptions.map((group) => group.label
        ? `
          <optgroup label="${escapeHtml(group.label)}">
            ${group.options.map((option) => `
              <option value="${escapeHtml(option.value)}" ${String(option.value) === String(periodModel.selectedValue) ? "selected" : ""}>
                ${escapeHtml(option.label)}
              </option>
            `).join("")}
          </optgroup>
        `
        : group.options.map((option) => `
          <option value="${escapeHtml(option.value)}" ${String(option.value) === String(periodModel.selectedValue) ? "selected" : ""}>
            ${escapeHtml(option.label)}
          </option>
        `).join("")).join("")
      : options.map((option) => `
        <option value="${escapeHtml(option.value)}" ${String(option.value) === String(periodModel.selectedValue) ? "selected" : ""}>
          ${escapeHtml(option.label)}
        </option>
      `).join("");
    return `
      <label class="oq-energy-history-period-field">
        <span>${escapeHtml(label)}</span>
        <select
          class="oq-energy-history-period-input"
          data-oq-energy-history-period-input="${escapeHtml(periodModel.view)}"
        >
          ${optionMarkup}
        </select>
      </label>
    `;
  }

  export function renderEnergyHistoryPeriodInput(periodModel) {
    if (periodModel.view === "day") {
      return `
        <label class="oq-energy-history-period-field">
          <span>Datum</span>
          <input
            class="oq-energy-history-period-input"
            type="date"
            value="${escapeHtml(getEnergyHistoryDateInputValue(periodModel.selectedValue))}"
            min="${escapeHtml(getEnergyHistoryDateInputValue(periodModel.minValue))}"
            max="${escapeHtml(getEnergyHistoryDateInputValue(periodModel.maxValue))}"
            data-oq-energy-history-period-input="day"
          >
        </label>
      `;
    }
    if (periodModel.view === "week") {
      return renderEnergyHistoryPeriodSelect(periodModel, "Week", periodModel.options);
    }
    if (periodModel.view === "month") {
      return renderEnergyHistoryPeriodSelect(periodModel, "Maand", periodModel.options);
    }
    if (periodModel.view === "year") {
      return renderEnergyHistoryPeriodSelect(periodModel, "Jaar", periodModel.options);
    }
    return `
      <div class="oq-energy-history-period-field oq-energy-history-period-field--static">
        <span>Periode</span>
        <strong>Volledig bereik</strong>
      </div>
    `;
  }

  export function renderEnergyHistoryPeriodControl(periodModel) {
    if (!isEnergyHistoryPeriodView(periodModel.view)) {
      return `
        <div class="oq-energy-history-period oq-energy-history-period--${escapeHtml(periodModel.view)}">
          ${renderEnergyHistoryPeriodInput(periodModel)}
        </div>
      `;
    }

    return `
      <div class="oq-energy-history-period oq-energy-history-period--${escapeHtml(periodModel.view)}">
        ${renderEnergyHistoryPeriodInput(periodModel)}
        <div class="oq-energy-history-period-nav" aria-label="Periode navigatie">
          <button
            type="button"
            class="oq-energy-history-period-button"
            data-oq-action="shift-energy-history-period"
            data-energy-history-direction="-1"
            ${periodModel.canPrevious ? "" : "disabled"}
          >&lt; Vorige</button>
          <button
            type="button"
            class="oq-energy-history-period-button oq-energy-history-period-button--now"
            data-oq-action="select-energy-history-now"
            ${periodModel.isNow ? "disabled" : ""}
          >Nu</button>
          <button
            type="button"
            class="oq-energy-history-period-button"
            data-oq-action="shift-energy-history-period"
            data-energy-history-direction="1"
            ${periodModel.canNext ? "" : "disabled"}
          >Volgende &gt;</button>
        </div>
      </div>
    `;
  }

  export function isEnergyHistoryPeriodControlFocused() {
    const active = document.activeElement;
    return Boolean(active && active.closest && active.closest(".oq-energy-history-period"));
  }

  export function renderEnergyHistoryViewButtons(activeView) {
    return `
      <div class="oq-energy-history-view-tabs" role="tablist" aria-label="Energiehistorie weergave">
        ${ENERGY_HISTORY_VIEW_OPTIONS.map((option) => {
          const active = option.id === activeView;
          return `
            <button
              type="button"
              class="oq-energy-history-view-tab ${active ? "is-active" : ""}"
              data-oq-action="select-energy-history-view"
              data-energy-history-view="${escapeHtml(option.id)}"
              aria-selected="${active ? "true" : "false"}"
            >${escapeHtml(option.label)}</button>
          `;
        }).join("")}
      </div>
    `;
  }

  export function renderEnergyHistoryBalance(summary) {
    const inputWh = Number(summary.electricalInputWh || 0);
    const heatWh = Number(summary.heatOutputWh || 0);
    const coolingWh = Number(summary.coolingOutputWh || 0);
    const boilerWh = Number(summary.boilerOutputWh || 0);
    const boilerTone = boilerWh > 0 ? "boiler" : "boiler-zero";
    const visibleTotal = Math.max(1, inputWh + heatWh + coolingWh + boilerWh);
    const widthOf = (value) => `${Math.max(0, (Number(value || 0) / visibleTotal) * 100).toFixed(2)}%`;
    const share = getEnergyHistoryHeatpumpShare(summary);
    return `
      <div class="oq-energy-history-balance">
        <div class="oq-energy-history-balance-bar" aria-label="Energiebalans">
          <span class="oq-energy-history-balance-part oq-energy-history-balance-part--input" style="width: ${widthOf(inputWh)}"></span>
          <span class="oq-energy-history-balance-part oq-energy-history-balance-part--heat" style="width: ${widthOf(heatWh)}"></span>
          <span class="oq-energy-history-balance-part oq-energy-history-balance-part--cooling" style="width: ${widthOf(coolingWh)}"></span>
          <span class="oq-energy-history-balance-part oq-energy-history-balance-part--${escapeHtml(boilerTone)}" style="width: ${widthOf(boilerWh)}"></span>
          <strong>${Number.isFinite(share) ? `${Math.round(share)}%` : "—"}</strong>
        </div>
        <div class="oq-energy-history-balance-list">
          <span><i class="oq-energy-history-legend-dot oq-energy-history-legend-dot--heat"></i>${escapeHtml(formatEnergyAdaptiveWh(heatWh, 1))} warmte door warmtepomp</span>
          <span><i class="oq-energy-history-legend-dot oq-energy-history-legend-dot--input"></i>${escapeHtml(formatEnergyAdaptiveWh(inputWh, 1))} verbruikte elektriciteit</span>
          <span><i class="oq-energy-history-legend-dot oq-energy-history-legend-dot--cooling"></i>${escapeHtml(formatEnergyAdaptiveWh(coolingWh, 1))} koeling</span>
          <span><i class="oq-energy-history-legend-dot oq-energy-history-legend-dot--${escapeHtml(boilerTone)}"></i>${escapeHtml(formatEnergyAdaptiveWh(boilerWh, 1))} cv-ketel</span>
        </div>
      </div>
    `;
  }

  export function getEnergyHistoryNiceAxisMax(maxWh) {
    const maxKwh = Math.max(1, Number(maxWh || 0) / 1000);
    const magnitude = Math.pow(10, Math.floor(Math.log10(maxKwh)));
    const normalized = maxKwh / magnitude;
    const nice = normalized <= 1.5 ? 1.5 : normalized <= 3 ? 3 : normalized <= 6 ? 6 : 10;
    return nice * magnitude * 1000;
  }

  export function formatEnergyHistoryAxisValue(wh) {
    const value = Number(wh);
    if (!Number.isFinite(value)) {
      return "";
    }
    if (value >= 999500) {
      return `${Number((value / 1000000).toFixed(1))}`;
    }
    return `${Number((value / 1000).toFixed(1))}`;
  }

  export function getEnergyHistoryAxisUnit(axisMaxWh) {
    return axisMaxWh >= 999500 ? "MWh" : "kWh";
  }

  export function getEnergyHistoryChartModel(records) {
    const width = 1280;
    const height = 260;
    const left = 44;
    const right = 18;
    const top = 26;
    const bottom = 38;
    const plotWidth = width - left - right;
    const plotHeight = height - top - bottom;
    const maxWh = Math.max(1000, ...records.map(getEnergyHistoryStackWh));
    const axisMax = getEnergyHistoryNiceAxisMax(maxWh);
    const barSlot = records.length ? plotWidth / records.length : plotWidth;
    const barWidth = Math.max(6, Math.min(38, barSlot * 0.68));
    const yOf = (wh) => top + ((1 - Math.min(1, Math.max(0, Number(wh || 0) / axisMax))) * plotHeight);

    return { width, height, left, right, top, bottom, plotWidth, plotHeight, axisMax, barSlot, barWidth, yOf };
  }

  export function getEnergyHistoryBucketTooltip(record) {
    const heatCop = formatEnergyRatio(record.heatpumpHeatOutputWh, record.heatingInputWh);
    const coolingEer = formatEnergyRatio(record.heatpumpCoolingOutputWh, record.coolingInputWh);
    return [
      record.tooltipLabel || record.label || formatEnergyHistoryDateLabel(record.dateKey),
      `Elektrisch totaal: ${formatEnergyAdaptiveWh(record.electricalInputWh, 1)}`,
      `Elektrisch verwarmen: ${formatEnergyAdaptiveWh(record.heatingInputWh, 1)}`,
      `Elektrisch koelen: ${formatEnergyAdaptiveWh(record.coolingInputWh, 1)}`,
      `Warmtepomp warmte: ${formatEnergyAdaptiveWh(record.heatpumpHeatOutputWh, 1)}`,
      `Warmtepomp koeling: ${formatEnergyAdaptiveWh(record.heatpumpCoolingOutputWh, 1)}`,
      `Cv-ketel warmte: ${formatEnergyAdaptiveWh(record.boilerHeatOutputWh, 1)}`,
      `COP verwarmen: ${heatCop}`,
      `EER koelen: ${coolingEer}`,
    ].join("\n");
  }

  export function renderEnergyHistoryChart(records, activeView = "") {
    if (!records.length) {
      return `
        <div class="oq-energy-history-empty">
          <strong>Geen opgeslagen dagrecords</strong>
          <span>Zet lifetime energiehistorie aan om langere grafieken op te bouwen.</span>
        </div>
      `;
    }

    const model = getEnergyHistoryChartModel(records);
    const axisUnit = getEnergyHistoryAxisUnit(model.axisMax);
    const gridValues = [0, 0.25, 0.5, 0.75, 1].map((fraction) => model.axisMax * fraction);
    const bars = records.map((record, index) => {
      const center = model.left + (model.barSlot * index) + (model.barSlot / 2);
      const stackParts = [
        { key: "electricalInputWh", className: "input", label: "Verbruikte elektriciteit" },
        { key: "heatpumpHeatOutputWh", className: "heat", label: "Warmte door warmtepomp" },
        { key: "heatpumpCoolingOutputWh", className: "cooling", label: "Koeling warmtepomp" },
        { key: "boilerHeatOutputWh", className: "boiler", label: "Cv-ketel" },
      ];
      let stackCursor = model.height - model.bottom;
      const stack = stackParts.map((part) => {
        const wh = getEnergyHistoryRecordWh(record, part.key);
        if (wh <= 0) {
          return "";
        }
        const partHeight = ((wh / model.axisMax) * model.plotHeight);
        stackCursor -= partHeight;
        return `
          <rect
            x="${(center - model.barWidth / 2).toFixed(1)}"
            y="${stackCursor.toFixed(1)}"
            width="${model.barWidth.toFixed(1)}"
            height="${Math.max(1.4, partHeight).toFixed(1)}"
            class="oq-energy-history-bar oq-energy-history-bar--${part.className}"
          >
            <title>${escapeHtml(`${record.label} · ${part.label}: ${formatEnergyAdaptiveWh(wh, 1)}`)}</title>
          </rect>
        `;
      }).join("");
      const showLabel = records.length <= 12 || index === 0 || index === records.length - 1 || index % 3 === 0;
      const label = showLabel
        ? `<text x="${center.toFixed(1)}" y="${model.height - 18}" text-anchor="middle" class="oq-energy-history-axis-label">${escapeHtml(record.label || formatEnergyHistoryDateLabel(record.dateKey))}</text>`
        : "";
      const tooltip = getEnergyHistoryBucketTooltip(record);
      return `
        <g class="oq-energy-history-bar-group" data-oq-energy-history-tip="${escapeHtml(tooltip)}" tabindex="0">
          <title>${escapeHtml(tooltip)}</title>
          <rect
            x="${(center - model.barWidth / 2 - 4).toFixed(1)}"
            y="${model.top.toFixed(1)}"
            width="${(model.barWidth + 8).toFixed(1)}"
            height="${model.plotHeight.toFixed(1)}"
            class="oq-energy-history-hit"
          ></rect>
          ${stack}
        </g>
        ${label}
      `;
    }).join("");

    return `
      <svg class="oq-energy-history-chart oq-energy-history-chart--${escapeHtml(normalizeEnergyHistoryView(activeView))}" viewBox="0 0 ${model.width} ${model.height}" role="img" aria-label="Energiehistorie">
        <rect x="0" y="0" width="${model.width}" height="${model.height}" rx="18" class="oq-energy-history-chart-bg"></rect>
        <text x="${model.left}" y="18" class="oq-energy-history-axis-unit">${escapeHtml(axisUnit)}</text>
        ${gridValues.map((value) => {
          const y = model.yOf(value);
          return `
            <line x1="${model.left}" y1="${y.toFixed(1)}" x2="${model.width - model.right}" y2="${y.toFixed(1)}" class="oq-energy-history-grid-line"></line>
            <text x="${model.left - 10}" y="${y.toFixed(1)}" text-anchor="end" dominant-baseline="middle" class="oq-energy-history-axis-label">${escapeHtml(formatEnergyHistoryAxisValue(value))}</text>
          `;
        }).join("")}
        ${bars}
      </svg>
    `;
  }

  export function renderEnergyHistoryLegend(summary = null) {
    const boilerTone = Number(summary?.boilerOutputWh || 0) > 0 ? "boiler" : "boiler-zero";
    const items = [
      ["input", "Elektrisch"],
      ["heat", "Warmte"],
      ["cooling", "Koeling"],
      [boilerTone, "Ketel"],
    ];
    return `
      <div class="oq-energy-history-legend">
        ${items.map(([tone, label]) => `
          <span><i class="oq-energy-history-legend-dot oq-energy-history-legend-dot--${escapeHtml(tone)}"></i>${escapeHtml(label)}</span>
        `).join("")}
      </div>
    `;
  }

  export function getEnergyHistorySummaryRecords(records, buckets, activeView, selectedValue) {
    const selectedDay = normalizeEnergyHistoryView(activeView) === "day" ? Number(selectedValue) : Number.NaN;
    const selectedDayRecord = Number.isFinite(selectedDay)
      ? records.find((record) => record.dateKey === selectedDay)
      : null;
    return selectedDayRecord ? [selectedDayRecord] : buckets;
  }

  export function getEnergyHistoryPanelModel() {
    const records = getEnergyHistoryRecords();
    const activeView = normalizeEnergyHistoryView(state.energyHistoryView);
    const periodControl = getEnergyHistoryPeriodControlModel(records, activeView);
    const viewModel = getEnergyHistoryCalendarBuckets(records, activeView, periodControl);
    // Hour buckets provide chart detail; day cards must keep using the authoritative day total.
    const summaryRecords = getEnergyHistorySummaryRecords(
      records,
      viewModel.buckets,
      activeView,
      periodControl.selectedValue,
    );
    const summary = getEnergyHistorySummary(summaryRecords);
    return { records, buckets: viewModel.buckets, viewModel, periodControl, summary, activeView };
  }

  export function getEnergyHistoryRenderSignature(model = getEnergyHistoryPanelModel()) {
    return getRenderSignature({
      energyHistorySignature: state.energyHistorySignature || "",
      energyHistoryError: state.energyHistoryError || "",
      activeView: model.activeView,
      periodView: model.periodControl.view,
      periodValue: model.periodControl.selectedValue,
      periodMin: model.periodControl.minValue,
      periodMax: model.periodControl.maxValue,
      recordCount: model.records.length,
      bucketCount: model.buckets.length,
      latestDate: model.records[model.records.length - 1]?.dateKey || 0,
      summary: model.summary,
    });
  }

  export function renderEnergyHistoryPanel(model = getEnergyHistoryPanelModel()) {
    const summary = model.summary;
    const efficiencyStat = getEnergyHistoryEfficiencyStat(summary);
    const oldest = model.buckets[0]?.dateKey ? formatEnergyHistoryDateLabel(model.buckets[0].dateKey) : "—";
    const newest = model.buckets[model.buckets.length - 1]?.dateKey ? formatEnergyHistoryDateLabel(model.buckets[model.buckets.length - 1].dateKey) : "—";
    return `
      <section class="oq-energy-history" data-render-signature="${escapeHtml(getEnergyHistoryRenderSignature(model))}">
        <div class="oq-energy-history-head">
          <div>
            <p class="oq-helper-label">Historie</p>
            <h3>Energiehistorie</h3>
            <p>${escapeHtml(model.viewModel.title)} · ${escapeHtml(model.viewModel.detail)}</p>
          </div>
        </div>
        <div class="oq-energy-history-controls">
          ${renderEnergyHistoryViewButtons(model.activeView)}
          ${renderEnergyHistoryPeriodControl(model.periodControl)}
        </div>
        ${state.energyHistoryError ? `<p class="oq-energy-history-error">${escapeHtml(state.energyHistoryError)}</p>` : ""}
        <div class="oq-energy-history-stats">
          ${renderEnergyHistoryStat(efficiencyStat.label, efficiencyStat.value, `${escapeHtml(oldest)} - ${escapeHtml(newest)}`)}
          ${renderEnergyHistoryStat("Elektrisch", formatEnergyAdaptiveWh(summary.electricalInputWh, 1), "verbruikt")}
          ${renderEnergyHistoryStat("Warmtepomp", formatEnergyAdaptiveWh(summary.heatOutputWh + summary.coolingOutputWh, 1), "warmte en koeling")}
          ${renderEnergyHistoryStat("Cv-ketel", formatEnergyAdaptiveWh(summary.boilerOutputWh, 1), "thermisch")}
        </div>
        ${renderEnergyHistoryBalance(summary)}
        <div class="oq-energy-history-chart-head">
          <h4>${escapeHtml(model.viewModel.title)}</h4>
          <span>${escapeHtml(model.viewModel.detail)}</span>
        </div>
        <div class="oq-energy-history-chart-wrap">
          ${renderEnergyHistoryChart(model.buckets, model.activeView)}
          <div class="oq-energy-history-tooltip" aria-hidden="true"></div>
        </div>
        ${renderEnergyHistoryLegend(summary)}
      </section>
    `;
  }

  export function handleEnergyHistoryPointerMove(event) {
    if (state.appView !== "results" || !state.root) {
      return;
    }
    const target = event.target.closest?.("[data-oq-energy-history-tip]");
    const panel = target?.closest?.(".oq-energy-history-chart-wrap") || state.root.querySelector(".oq-energy-history-chart-wrap");
    const tooltip = panel?.querySelector(".oq-energy-history-tooltip");
    if (!target || !panel || !tooltip) {
      if (tooltip) {
        tooltip.classList.remove("is-visible");
      }
      return;
    }

    const lines = String(target.dataset.oqEnergyHistoryTip || "").split(/\n/).filter(Boolean);
    if (!lines.length) {
      tooltip.classList.remove("is-visible");
      return;
    }
    tooltip.innerHTML = `
      <strong>${escapeHtml(lines[0])}</strong>
      ${lines.slice(1).map((line) => `<span>${escapeHtml(line)}</span>`).join("")}
    `;
    const rect = panel.getBoundingClientRect();
    tooltip.classList.add("is-visible");
    const tooltipRect = tooltip.getBoundingClientRect();
    const left = Math.min(Math.max(8, event.clientX - rect.left + 14), Math.max(8, rect.width - tooltipRect.width - 8));
    const top = Math.min(Math.max(8, event.clientY - rect.top - tooltipRect.height - 12), Math.max(8, rect.height - tooltipRect.height - 8));
    tooltip.style.transform = `translate(${left.toFixed(0)}px, ${top.toFixed(0)}px)`;
  }

  export function renderEnergyView() {
    return `
      <section class="oq-helper-panel oq-helper-panel--flush">
        <div class="oq-overview-board oq-overview-board--${escapeHtml(state.overviewTheme)}">
          <div class="oq-overview-head">
          <div>
            <p class="oq-helper-label">Energie</p>
            <h2 class="oq-helper-section-title">Actuele energiestromen</h2>
            <p class="oq-helper-section-copy">Bekijk actuele energiestromen, dagtotalen en cumulatieve tellers.</p>
          </div>
          </div>
          ${renderEnergySection()}
        </div>
      </section>
    `;
  }

  export function renderResultsView() {
    return `
      <section class="oq-helper-panel oq-helper-panel--flush">
        <div class="oq-overview-board oq-overview-board--${escapeHtml(state.overviewTheme)}">
          <div class="oq-overview-head">
            <div>
              <p class="oq-helper-label">Resultaten</p>
              <h2 class="oq-helper-section-title">Historische resultaten</h2>
              <p class="oq-helper-section-copy">Vergelijk opbrengst, verbruik, rendement en COP/EER per periode.</p>
            </div>
          </div>
          ${renderEnergyHistoryPanel()}
        </div>
      </section>
    `;
  }

  export function patchEnergyDom() {
    if (!state.root || state.appView !== "energy") {
      return false;
    }

    const board = state.root.querySelector(".oq-overview-board");
    const energy = board ? board.querySelector(".oq-overview-energy") : null;
    if (!board || !energy) {
      return false;
    }

    const nextBoardClass = `oq-overview-board oq-overview-board--${state.overviewTheme}`;
    if (board.className !== nextBoardClass) {
      board.className = nextBoardClass;
    }

    const model = getEnergySectionModel();
    replaceOuterHtmlIfSignatureChanged(
      energy,
      getEnergySectionRenderSignature(model),
      () => renderEnergySection(model),
    );
    return true;
  }

  export function patchResultsDom() {
    if (!state.root || state.appView !== "results") {
      return false;
    }

    const board = state.root.querySelector(".oq-overview-board");
    const history = board ? board.querySelector(".oq-energy-history") : null;
    if (!board || !history) {
      return false;
    }

    const nextBoardClass = `oq-overview-board oq-overview-board--${state.overviewTheme}`;
    if (board.className !== nextBoardClass) {
      board.className = nextBoardClass;
    }

    const historyModel = getEnergyHistoryPanelModel();
    const periodControlFocused = isEnergyHistoryPeriodControlFocused();
    if (!periodControlFocused) {
      replaceOuterHtmlIfSignatureChanged(
        history,
        getEnergyHistoryRenderSignature(historyModel),
        () => renderEnergyHistoryPanel(historyModel),
      );
    }
    return true;
  }

  setViewPatchControls({ patchEnergyDom, patchResultsDom });
