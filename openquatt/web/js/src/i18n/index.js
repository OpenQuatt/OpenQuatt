// Kleine, onderhoudbare i18n-laag voor de OpenQuatt web-app.
//
// - Nederlands (nl / nl-NL) is de standaard- en fallback-taal.
// - Engels (en / en-GB) is de tweede taal.
// - Firmware/API/entity-wire values worden NOOIT vertaald; alleen de
//   presentatie wordt gelokaliseerd (zie optionLabel/translateFirmwareText).
// - Deze module importeert bewust niets uit core/: zo blijft hij
//   importeerbaar vanuit core/ (config/state/runtime) zonder cycli, en
//   blijft core/config.js vrij van imports (build-assets.mjs leest die
//   via een data:-URL).
import en from "./en.js";
import nl from "./nl.js";

export const DEFAULT_LOCALE = "nl";
export const SUPPORTED_LOCALES = ["nl", "en"];
export const LOCALE_STORAGE_KEY = "oq-locale";

const INTL_LOCALES = {
  nl: "nl-NL",
  en: "en-GB",
};

const CATALOGUES = { nl, en };

let currentLocale = DEFAULT_LOCALE;
let localeInitialized = false;
const localeListeners = new Set();

function normalizeLocale(value) {
  const token = String(value || "").trim().toLowerCase();
  if (token === "nl" || token === "nl-nl" || token === "nld" || token.startsWith("nl-")) {
    return "nl";
  }
  if (token === "en" || token === "en-gb" || token === "en-us" || token === "eng" || token.startsWith("en-")) {
    return "en";
  }
  return "";
}

function readStoredLocale() {
  try {
    if (typeof window !== "undefined" && window.localStorage) {
      return normalizeLocale(window.localStorage.getItem(LOCALE_STORAGE_KEY));
    }
  } catch (_error) {
    // Opslag kan ontbreken in embedded browsers/tests.
  }
  try {
    if (typeof globalThis !== "undefined" && globalThis.localStorage) {
      return normalizeLocale(globalThis.localStorage.getItem(LOCALE_STORAGE_KEY));
    }
  } catch (_error) {
    // Geen bruikbare opslag beschikbaar.
  }
  return "";
}

function writeStoredLocale(locale) {
  const stores = [];
  try {
    if (typeof window !== "undefined" && window.localStorage) {
      stores.push(window.localStorage);
    }
  } catch (_error) {
    // Negeer ontbrekende opslag.
  }
  try {
    if (typeof globalThis !== "undefined" && globalThis.localStorage && !stores.includes(globalThis.localStorage)) {
      stores.push(globalThis.localStorage);
    }
  } catch (_error) {
    // Negeer ontbrekende opslag.
  }
  for (const store of stores) {
    try {
      if (typeof store.setItem === "function") {
        store.setItem(LOCALE_STORAGE_KEY, locale);
      }
    } catch (_error) {
      // Opslagfouten mogen de UI nooit breken.
    }
  }
}

// Bestaande installaties zonder voorkeur blijven Nederlands; een expliciete
// keuze wordt persistent bewaard. Browsertaal schakelt nooit ongevraagd om.
export function getLocale() {
  if (!localeInitialized) {
    currentLocale = readStoredLocale() || DEFAULT_LOCALE;
    localeInitialized = true;
  }
  return currentLocale;
}

export function getIntlLocale() {
  return INTL_LOCALES[getLocale()] || INTL_LOCALES[DEFAULT_LOCALE];
}

export function isSupportedLocale(value) {
  return SUPPORTED_LOCALES.includes(normalizeLocale(value));
}

export function onLocaleChange(listener) {
  if (typeof listener !== "function") {
    return () => {};
  }
  localeListeners.add(listener);
  return () => {
    localeListeners.delete(listener);
  };
}

export function applyLocaleToDocument() {
  const intlLocale = getIntlLocale();
  try {
    if (typeof document !== "undefined" && document.documentElement) {
      document.documentElement.lang = intlLocale;
    }
  } catch (_error) {
    // DOM is niet altijd beschikbaar (tests).
  }
  return intlLocale;
}

export function setLocale(nextLocale, options = {}) {
  const normalized = normalizeLocale(nextLocale) || DEFAULT_LOCALE;
  const changed = getLocale() !== normalized;
  currentLocale = normalized;
  localeInitialized = true;
  if (options.persist !== false) {
    writeStoredLocale(normalized);
  }
  if (options.applyDocument !== false) {
    applyLocaleToDocument();
  }
  if (changed && options.notify !== false) {
    for (const listener of Array.from(localeListeners)) {
      try {
        listener(normalized);
      } catch (_error) {
        // Een falende listener mag de taalwissel niet breken.
      }
    }
  }
  return normalized;
}

// Alleen voor tests: zet de module terug naar ongeïnitialiseerde staat.
export function resetLocaleForTests() {
  currentLocale = DEFAULT_LOCALE;
  localeInitialized = false;
}

function lookupKey(catalogue, key) {
  const parts = String(key || "").split(".");
  let node = catalogue;
  for (const part of parts) {
    if (!node || typeof node !== "object" || !Object.prototype.hasOwnProperty.call(node, part)) {
      return undefined;
    }
    node = node[part];
  }
  return typeof node === "string" ? node : undefined;
}

function interpolate(template, vars) {
  if (!vars || typeof vars !== "object") {
    return template;
  }
  return template.replace(/\{([a-zA-Z0-9_]+)\}/g, (match, name) => (
    Object.prototype.hasOwnProperty.call(vars, name) && vars[name] !== undefined && vars[name] !== null
      ? String(vars[name])
      : match
  ));
}

// Actieve taal -> Nederlandse fallback -> sleutel als diagnostische fallback.
export function t(key, vars) {
  const locale = getLocale();
  const primary = lookupKey(CATALOGUES[locale], key);
  if (primary !== undefined) {
    return interpolate(primary, vars);
  }
  if (locale !== DEFAULT_LOCALE) {
    const fallback = lookupKey(CATALOGUES[DEFAULT_LOCALE], key);
    if (fallback !== undefined) {
      return interpolate(fallback, vars);
    }
  }
  return String(key || "");
}

export function hasTranslation(key, locale = getLocale()) {
  return lookupKey(CATALOGUES[locale], key) !== undefined;
}

// Presentatielabel voor een firmware/API-wire value.
// De wire value zelf (option value, regellogica) blijft ongewijzigd.
export function optionLabel(value) {
  const raw = String(value ?? "");
  if (!raw.trim()) {
    return "";
  }
  const locale = getLocale();
  const primary = CATALOGUES[locale]?.options?.[raw];
  if (typeof primary === "string") {
    return primary;
  }
  const fallback = CATALOGUES[DEFAULT_LOCALE]?.options?.[raw];
  if (typeof fallback === "string") {
    return fallback;
  }
  return raw;
}

// Bekende firmware-teksten (bijv. thermal-actuator guards en handmatige
// HP-bewaking) naar gelokaliseerde presentatie. Onbekende/ruwe diagnostische
// tekst wordt ongewijzigd teruggegeven.
const FIRMWARE_EXACT_KEYS = {
  Vrijgegeven: "firmwareStatus.released",
  "stopverzoek wordt veilig afgerond": "firmwareStatus.stopFinishing",
  "maximale watertemperatuur bereikt": "firmwareStatus.maxWaterTemp",
  "low-flow-beveiliging actief": "firmwareStatus.lowFlowProtection",
  "onvoldoende flow voor compressorstart": "firmwareStatus.insufficientFlow",
  "kies eerst verwarmen of koelen": "firmwareStatus.chooseModeFirst",
  "conflicterende werkmodus tussen HP1 en HP2": "firmwareStatus.conflictingMode",
  "incidentbewaking vereist compressorstop": "firmwareStatus.incidentStopRequired",
  "incidentbewaking blokkeert een nieuwe start": "firmwareStatus.incidentStartBlocked",
  "wachten op bevestigde koelstop": "firmwareStatus.waitingCoolStop",
  "geen geldige werkmodus voor compressorstart": "firmwareStatus.noValidMode",
  "frequentietabel bevat geen bruikbare compressorstand": "firmwareStatus.noUsableLevel",
  "wachten op technische startvrijgave": "firmwareStatus.waitingTechRelease",
  "technische bewaking houdt verzoek tegen": "firmwareStatus.techGuardHolding",
  "start de bediening eerst": "firmwareStatus.hpStartManualFirst",
  "wacht op voldoende flow": "firmwareStatus.hpWaitFlow",
  "conflicterende werkmodus met HP2": "firmwareStatus.hpConflictingMode",
  "conflicterende werkmodus met HP1": "firmwareStatus.hpConflictingModeHp1",
  "zet eerst op Standby en wacht op stand 0": "firmwareStatus.hpStandbyFirst",
  "niet beschikbaar in single-opstelling": "firmwareStatus.hpUnavailableSingle",
  Standby: "firmwareStatus.standby",
};

const FIRMWARE_SECONDS_PREFIXES = [
  { prefix: "opstartblokkering na reboot: nog ", key: "firmwareStatus.startupBlockAfterReboot" },
  { prefix: "minimale uit-tijd: nog ", key: "firmwareStatus.minOffTime" },
  { prefix: "minimale koel-uit-tijd: nog ", key: "firmwareStatus.minCoolOffTime" },
];

const FIRMWARE_REST_PREFIXES = [
  { prefix: "ontdooihold op modelstand ", key: "firmwareStatus.defrostHold" },
  { prefix: "minimale draaitijd: tijdelijk stand ", key: "firmwareStatus.minRuntime" },
];

function translateFirmwareSeconds(text) {
  for (const { prefix, key } of FIRMWARE_SECONDS_PREFIXES) {
    if (text.startsWith(prefix) && text.endsWith(" s")) {
      const seconds = text.slice(prefix.length, -2).trim();
      if (seconds) {
        return t(key, { seconds });
      }
    }
  }
  return null;
}

function translateFirmwareRest(text) {
  for (const { prefix, key } of FIRMWARE_REST_PREFIXES) {
    if (text.startsWith(prefix)) {
      const rest = text.slice(prefix.length).trim();
      if (rest) {
        return t(key, { rest });
      }
    }
  }
  return null;
}

export function translateFirmwareText(raw) {
  const text = String(raw ?? "").trim();
  if (!text) {
    return "";
  }
  const exactKey = FIRMWARE_EXACT_KEYS[text];
  if (exactKey) {
    return t(exactKey);
  }
  const seconds = translateFirmwareSeconds(text);
  if (seconds !== null) {
    return seconds;
  }
  const rest = translateFirmwareRest(text);
  if (rest !== null) {
    return rest;
  }
  const hpMatch = text.match(/^(HP[12]):\s*(.+)$/);
  if (hpMatch) {
    const hp = hpMatch[1];
    const remainder = hpMatch[2].trim();
    const remainderKey = FIRMWARE_EXACT_KEYS[remainder];
    if (remainderKey) {
      return t(remainderKey, { hp });
    }
    const remainderSeconds = translateFirmwareSeconds(remainder);
    if (remainderSeconds !== null) {
      return `${hp}: ${remainderSeconds}`;
    }
    const remainderRest = translateFirmwareRest(remainder);
    if (remainderRest !== null) {
      return `${hp}: ${remainderRest}`;
    }
  }
  return text;
}

// Locale-bewuste formattering voor presentatie. API/entity-waarden blijven
// taal-onafhankelijk; alleen de weergave gebruikt deze helpers.
function toValidDate(value) {
  const date = value instanceof Date ? value : new Date(value);
  return Number.isNaN(date.getTime()) ? null : date;
}

export function formatNumber(value, options = {}) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return t("common.notAvailable");
  }
  return new Intl.NumberFormat(getIntlLocale(), options).format(numeric);
}

export function formatDate(value, options = {}) {
  const date = toValidDate(value);
  if (!date) {
    return t("common.notAvailable");
  }
  return new Intl.DateTimeFormat(getIntlLocale(), options).format(date);
}

export function formatTime(value, options = {}) {
  const date = toValidDate(value);
  if (!date) {
    return t("common.notAvailable");
  }
  return new Intl.DateTimeFormat(getIntlLocale(), options).format(date);
}

export function formatDateTime(value, options = {}) {
  const date = toValidDate(value);
  if (!date) {
    return t("common.notAvailable");
  }
  return new Intl.DateTimeFormat(getIntlLocale(), options).format(date);
}

export function getTranslatorNamespaces() {
  return Object.keys(CATALOGUES[DEFAULT_LOCALE] || {});
}

export function getCatalogueKeys(locale = DEFAULT_LOCALE) {
  const catalogue = CATALOGUES[locale] || {};
  const keys = [];
  const walk = (node, prefix) => {
    for (const name of Object.keys(node)) {
      const path = prefix ? `${prefix}.${name}` : name;
      const child = node[name];
      if (child && typeof child === "object") {
        walk(child, path);
      } else {
        keys.push(path);
      }
    }
  };
  walk(catalogue, "");
  return keys.sort();
}
