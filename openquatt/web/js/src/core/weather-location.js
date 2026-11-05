import { fetchWithTimeout } from "./browser-utils.js";
import { buildEntityPath } from "./domain-helpers.js";
import { t } from "../i18n/index.js";

export function normalizeWeatherLocation(value) {
  if (typeof value !== "string") throw new Error(t("weather.invalidLocation"));
  const normalized = value.trim();
  if (new TextEncoder().encode(normalized).length > 64 || /[\u0000-\u001f\u007f]/u.test(normalized)
    || normalized.includes("://") || normalized.length === 1) throw new Error(t("weather.invalidLocation"));
  if (/^[+\-\d.]/u.test(normalized) && normalized.includes(",")) {
    const parts = normalized.split(",");
    const latitude = Number(parts[0]);
    const longitude = Number(parts[1]);
    if (parts.length !== 2 || !parts[0].trim() || !parts[1].trim() || !Number.isFinite(latitude) || !Number.isFinite(longitude)
      || latitude < 50.5 || latitude > 53.8 || longitude < 3 || longitude > 7.5) throw new Error(t("weather.invalidLocation"));
  }
  return normalized;
}

// A lost POST acknowledgement does not prove failure, and HTTP 200 does not
// prove durable storage. Correlate an atomic value/token/result snapshot; an old
// equal value can survive a partially successful earlier NVS transaction.
export async function confirmWeatherLocationWrite(value, { post, read, requestToken, wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms)), pollDelays = [0, 150, 300, 500, 750, 1000, 1500, 2000], deadline = Infinity, now = Date.now }) {
  const normalized = normalizeWeatherLocation(value);
  const token = requestToken ?? Array.from(crypto.getRandomValues(new Uint8Array(16)), (byte) => byte.toString(16).padStart(2, "0")).join("");
  if (!/^[0-9a-f]{32}$/u.test(token)) throw new Error(t("weather.saveUnconfirmed"));
  let postError;
  try { await post(normalized, token); } catch (error) { if (error?.definitive) throw error; postError = error; }
  for (const delay of pollDelays) {
    if (now() >= deadline) break;
    if (delay) await wait(Math.min(delay, deadline - now()));
    if (now() >= deadline) break;
    try {
      const payload = await read();
      const confirmed = payload?.value ?? payload?.state;
      if (typeof confirmed === "string" && confirmed === normalized && payload.save_request === token && payload.save_status === "stored") return { normalized, payload };
    } catch (_error) { /* Keep reconciling this one write; never issue it again. */ }
  }
  throw new Error(t(postError ? "weather.saveUncertain" : "weather.saveUnconfirmed"));
}

export async function saveWeatherLocation(value, options = {}) {
  const entityPath = buildEntityPath("text", "Weather Location");
  const deadline = Date.now() + 10000;
  const request = async (url, init, consume) => fetchWithTimeout(url, init,
    Math.max(1, Math.min(init.method === "POST" ? 5000 : 2000, deadline - Date.now())), t("weather.requestTimeout"), async (response) => {
      if (!response.ok) {
        const error = new Error(`HTTP ${response.status}`);
        error.definitive = response.status >= 400 && response.status < 500;
        throw error;
      }
      return consume ? response.json() : null;
    });
  return confirmWeatherLocationWrite(value, {
    post: (normalized, token) => request(`${entityPath}/set?value=${encodeURIComponent(normalized)}&save_request=${token}`, { method: "POST" }, false),
    read: () => request(entityPath, { cache: "no-store", headers: { "Cache-Control": "no-store" } }, true),
    deadline,
    ...options,
  });
}
