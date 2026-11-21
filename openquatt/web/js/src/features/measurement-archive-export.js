import { fetchWithTimeout } from "../core/browser-utils.js";
import { getBasePath } from "../core/url-path.js";

const secondsPerDay = 86400;
const dayNumber = (epoch) => Math.floor(epoch / secondsPerDay);
export const archiveDate = (epoch) => new Date(epoch * 1000).toISOString().slice(0, 10);
export const archiveDownloadPath = () => `${getBasePath()}/openquatt/measurement-archive/download`;

async function fetchDay(date, signal) {
  for (let attempt = 0; attempt < 3; attempt++) {
    if (signal.aborted) throw signal.reason;
    const result = await fetchWithTimeout(`${archiveDownloadPath()}?date=${date}`, {
      cache: "no-store", redirect: "error", signal,
    }, 15000, "", (response) => response.ok ? response.json() : response.status);
    if (typeof result !== "number") return result;
    if (result !== 503 || attempt === 2) throw new Error(`HTTP ${result}`);
    await new Promise((resolve) => window.setTimeout(resolve, 200));
  }
}

export async function assembleMeasurementArchive({ first, last, count }, signal, onProgress) {
  const firstDay = dayNumber(first);
  const lastDay = dayNumber(last);
  const total = lastDay - firstDay + 1;
  const metadata = JSON.stringify({ schema: 1, first_epoch_s: first, last_epoch_s: last, measurement_count: count });
  const parts = [metadata.slice(0, -1), ',"days":['];
  let read = 0;
  let previous = first - 1;
  onProgress(0, total);
  for (let day = firstDay; day <= lastDay; day++) {
    const date = archiveDate(day * secondsPerDay);
    const payload = await fetchDay(date, signal);
    const fields = payload?.measurement_fields;
    const column = Array.isArray(fields) ? fields.indexOf("end_epoch_s") : -1;
    if (payload?.schema !== 1 || payload.date_utc !== date || !(column >= 0)
        || !Array.isArray(payload.measurements) || !Array.isArray(payload.heat_contexts)
        || !Array.isArray(payload.weather_fields) || !Array.isArray(payload.weather_hour_fields)
        || !Array.isArray(payload.weather)) throw new Error("Invalid archive day");
    payload.measurements = payload.measurements.filter((row) => {
      const epoch = row?.[column];
      if (!Number.isInteger(epoch) || dayNumber(epoch) !== day) throw new Error("Invalid measurement time");
      // Recording can continue while the browser fetches the last day.
      if (epoch < first || epoch > last) return false;
      if ((!read && epoch !== first) || epoch <= previous) throw new Error("Archive measurement changed");
      previous = epoch;
      read++;
      return true;
    });
    if (day !== firstDay) parts.push(",");
    parts.push(JSON.stringify(payload));
    onProgress(day - firstDay + 1, total);
  }
  if (signal.aborted) throw signal.reason;
  if (read !== count || previous !== last) throw new Error("Archive changed during download");
  parts.push("]}");
  return new Blob(parts, { type: "application/json" });
}
