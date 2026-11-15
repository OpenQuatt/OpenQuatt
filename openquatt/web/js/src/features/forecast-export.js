import { downloadBlobFile, fetchWithTimeout } from "../core/browser-utils.js";
import { getEntityValue } from "../core/entity-store.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";

const recordingEndpoint = () => `${getBasePath()}/openquatt/weather/recording`;
let statusPending = false;
let statusCheckedAt = -Infinity;
let statusEndpoint = "";

function showsRecording() {
  return !document.hidden && !state.nativeOpen && state.appView === "learning" && state.learningPanel === "diagnostics"
    && getEntityValue("houseLearningEnabled") === true && getEntityValue("weatherEnabled") === true;
}

export async function refreshForecastRecordingStatus() {
  const endpoint = recordingEndpoint();
  if (!showsRecording() || statusPending || (state.forecastRecordingStatus && endpoint === statusEndpoint
    && performance.now() - statusCheckedAt < 30000)) return;
  statusPending = true;
  try {
    const next = await fetchWithTimeout(`${endpoint}/status`, { cache: "no-store" }, 3000,
      "forecast-recording-status-timeout", (response) => response.ok ? response.json() : null);
    if (endpoint !== recordingEndpoint() || !showsRecording()) return;
    const status = Number.isInteger(next?.count) && next.count >= 0 && next.count <= 4608
      && Number.isInteger(next.duration_s) && next.duration_s >= 0 && next.duration_s <= 43200
      ? { count: next.count, duration_s: next.duration_s } : { unavailable: true };
    const previous = state.forecastRecordingStatus;
    state.forecastRecordingStatus = status;
    statusEndpoint = endpoint;
    statusCheckedAt = performance.now();
    if (status.count !== previous?.count || status.duration_s !== previous?.duration_s
      || status.unavailable !== previous?.unavailable) render();
  } catch {
    if (endpoint === recordingEndpoint() && showsRecording()) {
      state.forecastRecordingStatus = { unavailable: true };
      statusEndpoint = endpoint;
      statusCheckedAt = performance.now();
      render();
    }
  } finally {
    statusPending = false;
  }
}

export async function downloadForecastRecording() {
  if (state.busyAction) return false;
  state.busyAction = "forecastRecordingExport";
  state.forecastRecordingError = "";
  render();
  try {
    const blob = await fetchWithTimeout(recordingEndpoint(), { cache: "no-store" },
      20000, "forecast-recording-timeout", async (response) => {
        if (response.status === 410) return null;
        if (!response.ok || !response.headers.get("Content-Type")?.startsWith("application/json")) {
          throw new Error("forecast-recording-unavailable");
        }
        return response.blob();
      });
    if (!blob?.size) {
      state.forecastRecordingStatus = { count: 0, duration_s: 0 };
      return false;
    }
    downloadBlobFile(blob, "openquatt-forecast-measurements.json");
    return true;
  } catch {
    state.forecastRecordingError = "unavailable";
    return false;
  } finally {
    state.busyAction = "";
    render();
  }
}

export function handleForecastExportAction(action) {
  if (action !== "download-forecast-measurements") return false;
  void downloadForecastRecording();
  return true;
}
