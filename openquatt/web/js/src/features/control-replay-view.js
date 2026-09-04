import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { getRenderSignature } from "../core/render-signatures.js";
import { state } from "../core/state.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { getInstallationTopology } from "./device-context.js";
import { getControlReplayIncidentDisplaySeverity, getControlReplayIncidentEventCopy, getControlReplayIncidentModeAfterEvent, getControlReplayIncidentModeTransition, getControlReplayIncidentReasonMeta } from "./control-replay-incidents.js";
import { formatWorkingMode, getHeatPumpPanels } from "../views/heatpump.js";
import { isCoolingOverviewActive } from "../views/overview.js";
import { replaceOuterHtmlIfSignatureChanged } from "../views/view-utils.js";

  function clampControlReplayPercent(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      return 0;
    }
    return Math.max(0, Math.min(100, numeric));
  }

  function formatControlReplayInteger(key, fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const numeric = getEntityNumericValue(key);
    if (!Number.isFinite(numeric)) {
      return fallback;
    }
    return String(Math.round(numeric));
  }

  function formatControlReplayNumber(key, decimals = 1, unit = "", fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const numeric = getEntityNumericValue(key);
    if (!Number.isFinite(numeric)) {
      return fallback;
    }
    return `${numeric.toFixed(decimals)}${unit ? ` ${unit}` : ""}`;
  }

  function formatControlReplayRuntimeHours(key, fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const hours = getEntityNumericValue(key);
    if (!Number.isFinite(hours)) {
      return fallback;
    }
    return `${Math.round(hours)} u`;
  }

  function isControlReplayHpRunning(panel) {
    if (!panel || !panel.keys) {
      return false;
    }
    const mode = formatWorkingMode(getEntityStateText(panel.keys.mode, "Unknown"));
    const compressorLevel = getEntityNumericValue(panel.keys.freq);
    return mode === "Verwarmen"
      || mode === "Koelen"
      || isEntityActive(panel.keys.defrost)
      || (mode === "Onbekend" && Number.isFinite(compressorLevel) && compressorLevel > 0);
  }

  const CONTROL_WORKING_COOLING_LIMITER_REASONS = Object.freeze({
    0: "inactive",
    1: "full",
    2: "projected_floor",
    3: "simmer",
    4: "falling_gap",
    5: "buffer_stop",
    6: "dew_stop",
    7: "fallback_floor",
    8: "restart_wait",
    9: "room_cap",
    10: "fallback_cap1",
    11: "level1_hold",
    12: "oil_return_hold",
    13: "oil_return_recovery",
    14: "capacity_cap",
  });

  function normalizeControlWorkingCoolingReason(reasonCode) {
    const normalized = String(reasonCode || "").trim().toLowerCase();
    if (!normalized) {
      return "";
    }
    const numericCode = Number(normalized);
    if (Number.isInteger(numericCode)) {
      return CONTROL_WORKING_COOLING_LIMITER_REASONS[numericCode] || "unknown";
    }
    return normalized;
  }

  function isControlWorkingCoolingReasonInactive(reasonCode) {
    return ["", "full", "inactive", "none", "unknown", "unavailable"].includes(normalizeControlWorkingCoolingReason(reasonCode));
  }

  function isControlWorkingCoolingProtectionReason(reasonCode) {
    return [
      "dew_stop",
      "falling_gap",
      "projected_floor",
      "restart_wait",
      "sensor_fallback",
      "oil_return_recovery",
      "level1_hold",
    ].includes(normalizeControlWorkingCoolingReason(reasonCode));
  }

  function getControlReplayModeModel(heatPumpPanels) {
    const coolingRequest = isEntityActive("coolingRequestActive");
    const limiterReason = getEntityStateText("coolingLimiterReasonCode", "");
    const normalizedLimiterReason = normalizeControlWorkingCoolingReason(limiterReason);
    const coolingLimitedByLimiter = coolingRequest
      && normalizedLimiterReason
      && !isControlWorkingCoolingReasonInactive(normalizedLimiterReason);
    const coolingBlocked = coolingRequest && hasEntity("coolingPermitted") && !isEntityActive("coolingPermitted");
    const coolingProtection = coolingBlocked || (coolingLimitedByLimiter && isControlWorkingCoolingProtectionReason(normalizedLimiterReason));
    const coolingCapped = coolingLimitedByLimiter && !coolingProtection;
    const coolingMode = isCoolingOverviewActive() || coolingRequest;
    const hpRunningCount = heatPumpPanels.filter(isControlReplayHpRunning).length;
    const hp2Available = heatPumpPanels.some((panel) => panel.title === "HP2");
    const defrostActive = heatPumpPanels.some((panel) => isEntityActive(panel.keys.defrost));
    const boilerActive = hasEntity("boilerActive") && isEntityActive("boilerActive");
    return {
      title: "Control mode",
      copy: "De tab toont dezelfde eventlogica voor elke control mode.",
      hpRunningCount,
      hp2Available,
      defrostActive,
      boilerActive,
      coolingMode,
      coolingRequest,
      coolingBlocked,
      coolingLimited: coolingProtection || coolingCapped,
      coolingProtection,
      coolingCapped,
      coolingLimiterReason: normalizedLimiterReason || "inactive",
    };
  }

  function normalizeControlReplayModeId(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized.includes("cm100")) return "cm100";
    if (normalized.includes("cm98")) return "cm98";
    if (normalized.includes("cm5")) return "cm5";
    if (normalized.includes("cm4")) return "cm4";
    if (normalized.includes("cm3")) return "cm3";
    if (normalized.includes("cm2")) return "cm2";
    if (normalized.includes("cm1")) return "cm1";
    if (normalized.includes("cm0")) return "cm0";
    return "";
  }

  function formatControlReplayStrategyLabel() {
    const code = Math.round(getEntityNumericValue("strategyActiveCode"));
    if (code === 1) return "Koeling";
    if (code === 2) return "Stooklijn";
    if (code === 3) return "Power House";
    return getEntityStateText("strategy", "—");
  }

  function getControlReplayCounterValue(key, fallback = "—") {
    const value = formatControlReplayInteger(key, fallback);
    return value === "—" ? fallback : value;
  }

  const CONTROL_WORKING_TABS = Object.freeze([
    ["status", "Actueel", "shield"],
    ["timeline", "Tijdlijn", "activity"],
    ["graphs", "Grafieken", "bar-chart"],
  ].map(([id, label, icon]) => Object.freeze({ id, label, icon })));

  const CONTROL_WORKING_WINDOW_OPTIONS = Object.freeze([
    ["last1", "Laatste 1 uur", "1 uur", "Laatste 1 uur", "Recente beslismomenten in het afgelopen uur.", "De gekozen tijd verbindt grafiek en uitleg over het laatste uur.", { durationMinutes: 60 }],
    ["last2", "Laatste 2 uur", "2 uur", "Laatste 2 uur", "Recente beslismomenten in de afgelopen twee uur.", "De gekozen tijd verbindt grafiek en uitleg over de laatste twee uur.", { durationMinutes: 120 }],
    ["last4", "Laatste 4 uur", "4 uur", "Laatste 4 uur", "Recente momenten en periodes voor een gerichte diagnose.", "De gekozen tijd verbindt grafiek en uitleg over de laatste vier uur.", { durationMinutes: 240, quick: true }],
    ["last8", "Laatste 8 uur", "8 uur", "Laatste 8 uur", "Een compacte terugblik op de laatste acht uur.", "De gekozen tijd verbindt grafiek en uitleg over de laatste acht uur.", { durationMinutes: 480 }],
    ["last12", "Laatste 12 uur", "12 uur", "Laatste 12 uur", "Een dagdeel met alle belangrijke beslismomenten.", "De gekozen tijd verbindt grafiek en uitleg over de laatste twaalf uur.", { durationMinutes: 720 }],
    ["last24", "Afgelopen 24 uur", "24 uur", "Afgelopen 24 uur", "Gebeurtenissen die verklaren hoe het systeem in de huidige situatie kwam.", "De gekozen tijd verbindt grafiek en uitleg over de laatste 24 uur.", { durationMinutes: 1440, quick: true }],
    ["last48", "Afgelopen 48 uur", "48 uur", "Afgelopen 48 uur", "Twee dagen met belangrijke momenten en perioden.", "De gekozen tijd verbindt grafiek en uitleg over de laatste 48 uur.", { durationMinutes: 2880 }],
    ["last3d", "Afgelopen 3 dagen", "3 dagen", "Afgelopen 3 dagen", "Een terugblik op patronen over drie dagen.", "De gekozen tijd verbindt grafiek en uitleg over de laatste drie dagen.", { durationMinutes: 4320 }],
    ["today", "Vandaag", "Vandaag", "Vandaag", "Belangrijke momenten en periodes sinds middernacht.", "De gekozen tijd verbindt grafiek en uitleg voor vandaag.", { calendarDay: "today", quick: true }],
    ["yesterday", "Gisteren", "Gisteren", "Gisteren", "Terugkijken naar een volledige kalenderdag.", "De gekozen tijd verbindt grafiek en uitleg voor gisteren.", { calendarDay: "yesterday", quick: true }],
    ["week", "7 dagen", "7 dagen", "Afgelopen 7 dagen", "Patronen zoals defrosts, starts/stops en bescherming over meerdere dagen.", "De gekozen tijd verbindt grafiek en uitleg binnen de weekselectie.", { durationMinutes: 7 * 24 * 60, quick: true }],
    ["custom", "Eigen periode", "Eigen periode", "Eigen periode", "Een zelfgekozen begin- en eindmoment.", "De gekozen tijd verbindt grafiek en uitleg over de gekozen periode.", { custom: true }],
  ].map(([id, label, shortLabel, eyebrow, copy, graphCopy, options]) => Object.freeze({
    id,
    label,
    shortLabel,
    eyebrow,
    title: "Tijdlijn",
    copy,
    graphCopy,
    ...options,
  })));

  function getControlWorkingTabs() {
    return CONTROL_WORKING_TABS;
  }

  function getControlWorkingWindowOptions() {
    return CONTROL_WORKING_WINDOW_OPTIONS;
  }

  function getControlWorkingQuickWindowOptions() {
    return getControlWorkingWindowOptions().filter((option) => option.quick);
  }

  function getControlWorkingCustomEpoch(value) {
    const epochMs = new Date(String(value || "")).getTime();
    return Number.isFinite(epochMs) ? epochMs : Number.NaN;
  }

  function getControlWorkingCustomWindowBounds() {
    const start = getControlWorkingCustomEpoch(state.controlReplayCustomStart);
    const end = getControlWorkingCustomEpoch(state.controlReplayCustomEnd);
    if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) {
      return null;
    }
    return { start, end };
  }

  function formatControlWorkingDateTimeInput(epochMs) {
    const date = new Date(epochMs);
    date.setMinutes(0, 0, 0);
    const pad = (value) => String(value).padStart(2, "0");
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}`;
  }

  function formatControlWorkingDateInput(epochMs) {
    return formatControlWorkingDateTimeInput(epochMs).slice(0, 10);
  }

  function getControlWorkingCustomDateTimeParts(value) {
    const normalized = String(value || "");
    const match = normalized.match(/^(\d{4}-\d{2}-\d{2})T(\d{2}):00$/);
    return {
      date: match?.[1] || "",
      hour: match?.[2] || "00",
    };
  }

  function renderControlWorkingHourOptions(selectedHour) {
    return Array.from({ length: 24 }, (_value, hour) => {
      const value = String(hour).padStart(2, "0");
      return `<option value="${value}"${value === selectedHour ? " selected" : ""}>${value} uur</option>`;
    }).join("");
  }

  function getControlWorkingCustomDraft() {
    const nowMs = Date.now();
    return {
      start: state.controlReplayCustomStart || formatControlWorkingDateTimeInput(nowMs - (24 * 60 * 60 * 1000)),
      end: state.controlReplayCustomEnd || formatControlWorkingDateTimeInput(nowMs),
    };
  }

  function getControlWorkingCustomInputBounds(draft, nowMs = Date.now()) {
    const maxRangeMs = 7 * 24 * 60 * 60 * 1000;
    const latestMs = new Date(nowMs).setMinutes(0, 0, 0);
    const earliestMs = Math.ceil((nowMs - maxRangeMs) / (60 * 60 * 1000)) * 60 * 60 * 1000;
    const draftStartMs = getControlWorkingCustomEpoch(draft.start);
    const startMs = Number.isFinite(draftStartMs)
      ? Math.max(earliestMs, Math.min(latestMs, draftStartMs))
      : latestMs - (24 * 60 * 60 * 1000);
    const draftEndMs = getControlWorkingCustomEpoch(draft.end);
    const endMs = Number.isFinite(draftEndMs)
      ? Math.max(startMs, Math.min(latestMs, draftEndMs))
      : latestMs;
    return {
      earliestDate: formatControlWorkingDateInput(earliestMs),
      latestDate: formatControlWorkingDateInput(latestMs),
      startMaxDate: formatControlWorkingDateInput(Math.min(latestMs, endMs)),
      endMinDate: formatControlWorkingDateInput(startMs),
      endMaxDate: formatControlWorkingDateInput(Math.min(latestMs, startMs + maxRangeMs)),
    };
  }

  function getControlWorkingWindowBounds(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow)
      || getControlWorkingWindowOptions().find((candidate) => candidate.id === "last24");
    if (option?.calendarDay) {
      const start = new Date(nowMs);
      start.setHours(0, 0, 0, 0);
      if (option.calendarDay === "yesterday") {
        start.setDate(start.getDate() - 1);
      }
      return { start: start.getTime(), end: start.getTime() + (24 * 60 * 60 * 1000) };
    }
    if (option?.custom) {
      return getControlWorkingCustomWindowBounds() || {
        start: nowMs - (24 * 60 * 60 * 1000),
        end: nowMs,
      };
    }
    const durationMinutes = Number(option?.durationMinutes) || 1440;
    return {
      start: nowMs - (durationMinutes * 60 * 1000),
      end: nowMs,
    };
  }

  function getControlWorkingWindowDurationMinutes(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    return Math.max(1, (bounds.end - bounds.start) / (60 * 1000));
  }

  function formatControlWorkingAxisTime(epochMs, includeDay = false) {
    const date = new Date(epochMs);
    const time = date.toLocaleTimeString("nl-NL", { hour: "2-digit", minute: "2-digit" });
    if (!includeDay) {
      return time;
    }
    const day = date.toLocaleDateString("nl-NL", { weekday: "short" }).replace(".", "");
    return `${day} ${time}`;
  }

  function getControlWorkingWindowAxis(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    if (selectedWindow === "today" || selectedWindow === "yesterday") {
      return ["00:00", "06:00", "12:00", "18:00", "24:00"];
    }
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const durationMinutes = getControlWorkingWindowDurationMinutes(selectedWindow, nowMs);
    const includeDay = durationMinutes > 24 * 60 || selectedWindow === "custom";
    return [0, 0.25, 0.5, 0.75, 1].map((fraction, index) => {
      if (index === 4 && selectedWindow !== "custom") {
        return "Nu";
      }
      return formatControlWorkingAxisTime(bounds.start + ((bounds.end - bounds.start) * fraction), includeDay);
    });
  }

  function getControlWorkingSelectedTab() {
    return getControlWorkingTabs().some((tab) => tab.id === state.controlReplayTab)
      ? state.controlReplayTab
      : "status";
  }

  function getControlWorkingSelectedWindow() {
    const selected = getControlWorkingWindowOptions().find((option) => option.id === state.controlReplayWindow);
    if (selected?.custom && !getControlWorkingCustomWindowBounds()) {
      return "last24";
    }
    return selected
      ? state.controlReplayWindow
      : "last24";
  }

  function getControlWorkingWindowModel() {
    const selectedWindow = getControlWorkingSelectedWindow();
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow)
      || getControlWorkingWindowOptions().find((candidate) => candidate.id === "last24");
    return {
      ...option,
      axis: getControlWorkingWindowAxis(selectedWindow),
    };
  }

  const CONTROL_WORKING_SEVERITY_METAS = Object.freeze({
    normal: { label: "Normaal", tone: "normal" },
    limited: { label: "Bescherming actief", tone: "limited" },
    attention: { label: "Aandacht", tone: "attention" },
    fault: { label: "Storing", tone: "fault" },
  });

  function getControlWorkingSeverityMeta(severity = "normal") {
    return CONTROL_WORKING_SEVERITY_METAS[severity] || CONTROL_WORKING_SEVERITY_METAS.normal;
  }

  function createControlWorkingReasonMetas(definitions) {
    return Object.freeze(Object.fromEntries(definitions.map(([code, label, summary, ...checks]) => [
      code,
      { label, summary, checks },
    ])));
  }

  const CONTROL_WORKING_REASON_METAS = createControlWorkingReasonMetas([
    ["keep_current", "Huidige keuze blijft logisch", "De huidige stand past bij de vraag in huis. Wisselen zou nu weinig voordeel geven.", "Vraag blijft binnen de band", "Geen betere keuze nodig", "Rustig door laten lopen"],
    ["hold_active", "Wissel bewust uitgesteld", "Het systeem wacht bewust even, zodat warmtepompen niet onnodig vaak starten en stoppen.", "Vraag is nog niet duidelijk anders", "Minimale looptijd telt mee", "Actieve bron werkt nog goed"],
    ["defrost_hold", "Ontdooien rustig laten verlopen", "Een warmtepomp ontdooit kort. Dat is normaal wintergedrag en herstelt vanzelf.", "Ontdooien actief of net klaar", "Warmte kan kort lager zijn", "Herstart gebeurt automatisch"],
    ["better_heat", "Twee pompen passen beter", "De warmtevraag blijft hoog. Twee warmtepompen kunnen die vraag rustiger leveren dan één pomp op hoge belasting.", "Warmtevraag blijft hoog", "Beide warmtepompen beschikbaar", "Samen leveren ze rustiger vermogen"],
    ["soft_guard", "Veilige marge bewaakt", "Het systeem begrenst zichzelf om veilig binnen de temperatuur- en flowgrenzen te blijven.", "Veiligheidsmarge bewaakt", "Geen storing", "Begrenzing verdwijnt vanzelf"],
    ["less_power", "Minder vermogen nodig", "De vraag neemt af. Eén warmtepomp kan de resterende vraag weer rustig dragen.", "Vraag neemt af", "Eén warmtepomp is genoeg", "Minder elektrisch vermogen nodig"],
    ["cooling_request_cleared", "Geen koelvraag meer", "De koelvraag is weggevallen. De warmtepomp mag stoppen en de pomp kan nog kort nalopen.", "Koelvraag weg", "Warmtepomp stopt", "Naloop kan normaal zijn"],
    ["heating_request_cleared", "Geen warmtevraag meer", "De warmtevraag is weggevallen. De warmtepomp mag stoppen en de pomp kan nog kort nalopen.", "Warmtevraag weg", "Warmtepomp stopt", "Naloop kan normaal zijn"],
    ["no_candidate", "Nog geen veilige start", "Er is vraag, maar een start is nu nog niet verstandig door wachttijd of bescherming.", "Beschikbaarheid gecontroleerd", "Bescherming of wachttijd actief", "Straks opnieuw beoordelen"],
    ["candidate_in_rest", "Rusttijd loopt nog", "De warmtepomp is kort geleden gestopt en wacht nog even om korte cycli te voorkomen.", "Vorige stop is recent", "Start wordt uitgesteld", "Bij blijvende vraag opnieuw beoordelen"],
    ["candidate_in_defrost", "Warmtepomp ontdooit", "Deze warmtepomp kan nu niet starten of wisselen omdat ontdooien eerst rustig moet afronden.", "Ontdooien actief", "Niet onnodig wisselen", "Automatisch opnieuw beoordelen"],
    ["candidate_unavailable", "Warmtepomp niet beschikbaar", "De warmtepomp is nu geen geschikte kandidaat door beschikbaarheid of technische begrenzing.", "Kandidaat gecontroleerd", "Voorwaarde niet vrij", "Andere keuze blijft mogelijk"],
    ["defrost_boost", "Ontdooien opgevangen", "Een andere bron kan tijdelijk helpen terwijl een warmtepomp ontdooit.", "Ontdooien verlaagt kort vermogen", "Andere bron beschikbaar", "Comfort blijft beschermd"],
    ["boiler_assist", "CV ondersteunt tijdelijk", "De CV-ketel helpt alleen wanneer de warmtevraag tijdelijk meer vermogen vraagt dan de warmtepompen rustig kunnen leveren.", "Warmtevraag blijft hoog", "Warmtepompen leveren maximaal rustig vermogen", "CV stopt zodra ondersteuning niet meer nodig is"],
    ["runtime_lead", "Draaiurenbalans", "De warmtepompen zijn gelijkwaardig. Het systeem kiest de pomp die het beste past bij draaiuren, beschikbaarheid en wachttijd.", "Draaiuren vergeleken", "Warmtepomp beschikbaar", "Wachttijd vrij"],
    ["oil_return_hold", "Compressor beschermen", "De warmtepomp blijft kort doorlopen om de compressor netjes te beschermen.", "Minimale looptijd actief", "Stop wordt uitgesteld", "Korte cyclus voorkomen"],
    ["single_topology", "Eén warmtepomp aanwezig", "Er is maar één warmtepomp beschikbaar. Keuzes met twee warmtepompen zijn dan niet van toepassing.", "Opstelling gecontroleerd", "Geen tweede warmtepomp", "Keuze blijft beperkt"],
    ["demand_decreased", "Warmtevraag nam af", "De vraag zakte terug. Minder vermogen is genoeg om de woning op temperatuur te houden.", "Vraag is lager", "Stopvertraging verlopen", "Andere warmtepomp blijft actief"],
    ["min_rest_active", "Minimum rusttijd actief", "De warmtepomp wacht nog even om korte starts en onnodige belasting te voorkomen.", "Vorige stop is recent", "Rusttijd loopt", "Start volgt als vraag blijft"],
    ["start_stop_rate_high", "Veel starts/stops", "De warmtepomp start vaker dan wenselijk. Dat is niet direct een storing, maar wel nuttig om te bekijken.", "Startteller hoog", "Geen acute storing", "Nuttig voor support"],
    ["sticky_protection", "Pompbescherming", "De pomp draait kort zodat hij na lange stilstand niet vast gaat zitten. Dit is geen verwarmings- of koelvraag.", "Geen comfortvraag", "Dagelijkse bescherming actief", "Alleen korte pomprun"],
    ["frost_protection", "Vorstbescherming", "Het systeem laat water circuleren om bevriezing van het watercircuit te voorkomen.", "Geen comfortvraag nodig", "Vorstrisico bewaakt", "Water blijft circuleren"],
    ["flow_preflow", "Voorloop actief", "De pomp bouwt eerst waterflow op voordat de warmtepomp mag starten.", "Waterflow opbouwen", "Warmtepomp nog niet vrij", "Start volgt automatisch"],
    ["flow_postflow", "Naloop actief", "De pomp blijft kort nadraaien zodat warmte netjes uit het systeem wordt afgevoerd.", "Warmtepomp stopt", "Pomp draait kort door", "Daarna standby"],
    ["flow_too_low", "Waterflow blijft te laag", "De normale voorlooptijd is verstreken, maar de waterflow is nog niet voldoende voor een veilige start.", "Voorlooptijd verstreken", "Start blijft geblokkeerd", "Flow wordt opnieuw beoordeeld"],
    ["startup_inhibit", "Wachttijd na herstart", "Na een herstart blijft de compressor kort uit om een te snelle herstart te voorkomen.", "Comfortvraag is aanwezig", "Compressor wacht nog", "Start volgt automatisch"],
    ["capacity_cap", "Ingesteld koelmaximum", "Er is koelvraag. Het systeem blijft binnen het maximale koelniveau dat in de software is ingesteld.", "Koelvraag actief", "Softwaremaximum actief", "Dauwpunt blijft bewaakt"],
    ["falling_gap", "Dauwpuntmarge daalt", "De marge tot het dauwpunt wordt kleiner. Het systeem grijpt vroeg in om condens te voorkomen.", "Marge daalt", "Aanvoer blijft veilig", "Koeling blijft voorzichtig actief"],
    ["projected_floor", "Aanvoer nadert veilige ondergrens", "De aanvoer dreigt te koud te worden. Het systeem verlaagt de koeling preventief.", "Aanvoer voorspeld", "Veilige grens leidend", "Geen storing"],
    ["simmer", "Koeling rustig bijgesteld", "De koeling blijft op een laag niveau zodat de temperatuur rustig richting setpoint kan bewegen.", "Lage koelvraag", "Geen abrupte stop", "Rustige regeling"],
    ["buffer_stop", "Water al koud genoeg", "Er is koelvraag, maar het water is al koud genoeg. De warmtepomp hoeft daarom nu niet te starten.", "Koelvraag blijft actief", "Water is al koud genoeg", "Start volgt automatisch"],
    ["dew_stop", "Dauwpuntstop", "De warmtepomp stopt kort omdat verder koelen te dicht bij het dauwpunt zou komen.", "Condensrisico voorkomen", "Koelvraag blijft bestaan", "Herstart na veilige marge"],
    ["cooling_limiter", "Softwaremaximum actief", "Er is koelvraag. Het systeem koelt binnen het actuele softwaremaximum en blijft de veiligheidsmarges bewaken.", "Koelvraag actief", "Softwaremaximum actief", "Marge blijft bewaakt"],
    ["sensor_fallback", "Sensorwaarde onzeker", "Een meting is tijdelijk minder zeker. Het systeem kiest daarom voorzichtig gedrag.", "Metingen gecontroleerd", "Veilige keuze voorrang", "Herstel zodra data stabiel is"],
    ["restart_wait", "Koeling wacht op veilige herstart", "De koelvraag is nog aanwezig. Na de koelstop wacht het systeem tot de veilige marge voldoende is hersteld.", "Herstart wacht bewust", "Marge moet stabiel blijven", "Daarna opnieuw beoordelen"],
    ["level1_hold", "Voorzichtig blijven koelen", "De koeling blijft nog even laag totdat duidelijk is dat de veilige marge terug is.", "Even wachten met opschalen", "Geen snelle sprong omhoog", "Comfortvraag blijft bewaakt"],
    ["room_cap", "Kamervraag begrenst", "De kamer vraagt koeling, maar niet genoeg om harder te gaan koelen.", "Kamer koelt richting setpoint", "Vraag blijft beperkt", "Rustige regeling"],
    ["oil_return_recovery", "Compressorherstel", "Het systeem geeft compressorherstel tijdelijk voorrang en blijft de veiligheid bewaken.", "Compressorprotectie actief", "Gecontroleerd herstel", "Veiligheid blijft bewaakt"],
  ]);

  const CONTROL_WORKING_REASON_FALLBACK = Object.freeze({
    label: "Keuze van het systeem",
    summary: "Keuze van het systeem",
    checks: [],
  });

  function getControlWorkingReasonMeta(reasonCode) {
    return CONTROL_WORKING_REASON_METAS[reasonCode]
      || getControlReplayIncidentReasonMeta(reasonCode)
      || CONTROL_WORKING_REASON_FALLBACK;
  }

  function getControlWorkingReasonLabel(reasonCode) {
    return getControlWorkingReasonMeta(reasonCode).label;
  }

  function formatControlWorkingModeCode(cm, allowZero = false) {
    const normalized = Number(cm);
    return Number.isFinite(normalized) && (normalized > 0 || (allowZero && normalized === 0)) ? `CM${normalized}` : "";
  }

  function formatControlWorkingModeTransition(fromCm, toCm) {
    const fromLabel = formatControlWorkingModeCode(fromCm);
    const toLabel = formatControlWorkingModeCode(toCm, true);
    return fromLabel && toLabel && fromLabel !== toLabel ? `${fromLabel} → ${toLabel}` : "";
  }

  function deriveControlWorkingModeTransition(event, previousCm) {
    const eventType = String(event?.event_type || "");
    const cm = Number(event?.cm) || 0;
    const valueA = Number(event?.value_a);
    if (eventType === "boiler_assist_start") {
      return formatControlWorkingModeTransition(previousCm || 2, cm === 3 ? 3 : cm);
    }
    if (eventType === "boiler_assist_stop") {
      return formatControlWorkingModeTransition(previousCm === 3 ? 3 : previousCm, cm > 0 ? cm : 2);
    }
    if (eventType === "flow_hold_start" && cm === 1) {
      return formatControlWorkingModeTransition(previousCm, 1);
    }
    if (eventType === "flow_hold_clear" && cm === 1 && Number.isFinite(valueA)) {
      return formatControlWorkingModeTransition(1, valueA);
    }
    const incidentTransition = getControlReplayIncidentModeTransition(event, previousCm);
    if (incidentTransition) {
      return formatControlWorkingModeTransition(incidentTransition.from, incidentTransition.to);
    }
    return "";
  }

  function getControlWorkingModeAfterEvent(event) {
    const eventType = String(event?.event_type || "");
    const cm = Number(event?.cm) || 0;
    const valueA = Number(event?.value_a);
    if (eventType === "flow_hold_clear" && cm === 1 && Number.isFinite(valueA)) {
      return valueA;
    }
    if (eventType === "frost_protection_clear") {
      return 0;
    }
    const incidentMode = getControlReplayIncidentModeAfterEvent(event);
    if (incidentMode !== null) {
      return incidentMode;
    }
    return cm;
  }

  function getControlWorkingModeMetaLabel(item) {
    const transitionLabel = String(item?.modeTransitionLabel || "").trim();
    if (transitionLabel) {
      return transitionLabel;
    }
    const modeLabel = String(item?.modeLabel || "").trim();
    return modeLabel.includes("→") ? modeLabel : "";
  }

  function getControlWorkingCoolingContext() {
    const reasonCode = normalizeControlWorkingCoolingReason(getEntityStateText("coolingLimiterReasonCode", ""));
    return {
      requestActive: isEntityActive("coolingRequestActive"),
      permitted: hasEntity("coolingPermitted") ? isEntityActive("coolingPermitted") : true,
      reasonCode: reasonCode || "inactive",
      rawDemand: formatControlReplayNumber("coolingDemandRaw", 0, "", "—"),
      limitedDemand: formatControlReplayNumber("coolingLimitedDemand", 0, "", "—"),
      allowedMax: formatControlReplayNumber("coolingLimiterAllowedMax", 0, "", "—"),
      dewPoint: formatControlReplayNumber("coolingDewPointSelected", 1, "°C", "—"),
      safeSupply: formatControlReplayNumber("coolingEffectiveMinSupplyTemp", 1, "°C", "—"),
      guardMode: getEntityStateText("coolingGuardMode", "Dauwpuntbewaking"),
      blockReason: getEntityStateText("coolingBlockReason", "Ready"),
    };
  }

  function getControlWorkingKindLabel(kind) {
    const labels = {
      event: "Moment",
      span: "Periode",
      aggregate: "Samenvatting",
    };
    return labels[kind] || "Record";
  }

  function renderControlWorkingPill(label, tone = "neutral", icon = "") {
    const iconMarkup = icon ? renderOqIcon(icon, "oq-working-pill-icon") : "";
    return `<span class="oq-working-pill oq-working-pill--${escapeHtml(tone)}">${iconMarkup}<span>${escapeHtml(label)}</span></span>`;
  }

  function shouldShowControlWorkingModeBadge(item) {
    const reasonCode = item?.reasonCode || item?.primaryReason;
    return normalizeControlReplayModeId(item?.modeLabel) === "cm98" && reasonCode === "frost_protection";
  }

  function renderControlWorkingModeBadge(item) {
    if (!shouldShowControlWorkingModeBadge(item)) {
      return "";
    }
    return `<span class="oq-working-mode-badge" aria-label="Technische mode CM98">CM98</span>`;
  }

  function getControlWorkingOptimizerModel(target) {
    const reasonCode = target?.reasonCode || target?.primaryReason || "keep_current";
    const source = target?.source || "HP1 + HP2";
    if (reasonCode === "better_heat") {
      return {
        title: "Keuze van het systeem",
        verdict: "Twee warmtepompen actief",
        summary: "Omdat de warmtevraag hoog blijft, leveren twee warmtepompen rustiger vermogen dan één warmtepomp op hoge belasting.",
        rows: [
          { option: "Eén warmtepomp", result: "Te weinig reserve", code: "better_heat", detail: "De vraag bleef langer hoog dan één warmtepomp rustig kan dragen.", tone: "muted" },
          { option: "Andere losse pomp", result: "Geen voordeel", code: "hold_active", detail: "Wisselen naar de andere pomp zou geen rustiger gedrag geven.", tone: "muted" },
          { option: "Twee warmtepompen", result: "Gekozen", code: "better_heat", detail: "Samen leveren ze meer reserve en minder belasting per pomp.", tone: "selected" },
        ],
      };
    }
    if (reasonCode === "demand_decreased" || reasonCode === "less_power") {
      return {
        title: "Keuze van het systeem",
        verdict: "Eén warmtepomp is genoeg",
        summary: "De warmtevraag is gezakt. Eén warmtepomp kan de resterende warmte rustiger en zuiniger leveren.",
        rows: [
          { option: "Twee warmtepompen", result: "Niet meer nodig", code: "less_power", detail: "Samen leveren ze meer vermogen dan nu nodig is.", tone: "muted" },
          { option: source, result: "Blijft actief", code: "less_power", detail: "Eén warmtepomp dekt de lagere vraag rustiger.", tone: "selected" },
        ],
      };
    }
    if (reasonCode === "runtime_lead") {
      return {
        title: "Keuze van het systeem",
        verdict: `${source} gestart`,
        summary: "De warmtepompen zijn gelijkwaardig. De keuze volgt uit draaiuren, beschikbaarheid en wachttijden.",
        rows: [
          { option: "HP1", result: source === "HP1" ? "Gekozen" : "Niet nu", code: "runtime_lead", detail: "Past het beste bij de actuele draaiurenbalans.", tone: source === "HP1" ? "selected" : "muted" },
          { option: "HP2", result: source === "HP2" ? "Gekozen" : "Niet nu", code: "runtime_lead", detail: "Gelijkwaardige pomp, maar nu minder gunstig in balans of wachttijd.", tone: source === "HP2" ? "selected" : "muted" },
        ],
      };
    }
    if (["min_rest_active", "no_candidate", "candidate_in_rest", "candidate_in_defrost", "candidate_unavailable"].includes(reasonCode)) {
      return {
        title: "Startcontrole",
        verdict: "Start uitgesteld",
        summary: getControlWorkingReasonMeta(reasonCode).summary,
        rows: [
          { option: source, result: "Wacht nog", code: reasonCode, detail: getControlWorkingReasonMeta(reasonCode).summary, tone: "limited" },
          { option: "Opnieuw beoordelen", result: "Straks", code: "hold_active", detail: "Het systeem probeert opnieuw zodra starten verstandig is.", tone: "muted" },
        ],
      };
    }
    if (["flow_preflow", "flow_postflow", "flow_too_low"].includes(reasonCode)) {
      const eventType = target?.realEventType || target?.rawDecisionEvent?.event_type || "";
      const flowCleared = eventType === "flow_hold_clear";
      const postflow = reasonCode === "flow_postflow";
      if (flowCleared) {
        return {
          title: postflow ? "Waterflow afronden" : "Waterflow bevestigd",
          verdict: postflow ? "Naloop klaar" : "Start vrijgegeven",
          summary: postflow
            ? "De pompnaloop is afgerond. Het systeem kan terug naar standby."
            : "De waterflow is voldoende. De regelaar kan doorgaan met de volgende stap.",
          rows: [
            { option: "Waterflow", result: "Voldoende", code: reasonCode, detail: "De gemeten circulatie is vrijgegeven voor de volgende stap.", tone: "selected" },
            { option: "Warmtepomp", result: postflow ? "Gestopt" : "Vrijgegeven", code: reasonCode, detail: postflow ? "De warmtepomp is gestopt; de naloop is nu ook klaar." : "De compressor mag nu volgens de normale regeling starten.", tone: "selected" },
            { option: "Regelaar", result: "Gaat verder", code: "keep_current", detail: "De controller vervolgt automatisch de normale regeling.", tone: "muted" },
          ],
        };
      }
      const lowFlowFault = reasonCode === "flow_too_low";
      return {
        title: "Waterflow eerst",
        verdict: postflow ? "Naloop actief" : lowFlowFault ? "Start geblokkeerd" : "Voorloop actief",
        summary: getControlWorkingReasonMeta(reasonCode).summary,
        rows: [
          { option: "Waterflow", result: lowFlowFault ? "Blijft te laag" : postflow ? "Wordt afgerond" : "Wordt opgebouwd", code: reasonCode, detail: "De pomp zorgt voor circulatie voordat de volgende stap vrij is.", tone: lowFlowFault ? "limited" : "selected" },
          { option: "Warmtepomp", result: postflow ? "Gestopt" : lowFlowFault ? "Start geblokkeerd" : "Wacht op voorloop", code: reasonCode, detail: "De compressor start pas als de flowconditie veilig is.", tone: lowFlowFault ? "limited" : "muted" },
          { option: "Regelaar", result: lowFlowFault ? "Blijft controleren" : "Controleert automatisch", code: "keep_current", detail: "De controller beoordeelt de waterflow automatisch opnieuw.", tone: "muted" },
        ],
      };
    }
    if (reasonCode === "defrost_hold" || reasonCode === "defrost_boost") {
      return {
        title: "Bescherming",
        verdict: "Ontdooien krijgt voorrang",
        summary: "Tijdens ontdooien houdt het systeem de regeling rustig, zodat de warmtepomp vanzelf kan herstellen.",
        rows: [
          { option: "Actieve warmtepomp", result: "Rustig laten herstellen", code: "defrost_hold", detail: "Niet wisselen zolang ontdooien of herstel actief is.", tone: "selected" },
          { option: "Extra bron", result: reasonCode === "defrost_boost" ? "Helpt mee" : "Stand-by", code: reasonCode, detail: "Alleen inzetten als comfort of vermogen daarom vraagt.", tone: reasonCode === "defrost_boost" ? "selected" : "muted" },
        ],
      };
    }
    if (reasonCode === "boiler_assist") {
      return {
        title: "Bronkeuze",
        verdict: "CV ondersteunt tijdelijk",
        summary: "De warmtepompen blijven de basis leveren. CV vult alleen aan zolang extra vermogen nodig is.",
        rows: [
          { option: "Alleen warmtepompen", result: "Te weinig reserve", code: "better_heat", detail: "De vraag bleef hoger dan de warmtepompen rustig konden leveren.", tone: "muted" },
          { option: "CV-ketel", result: "Tijdelijk bij", code: "boiler_assist", detail: "CV levert extra vermogen en stopt zodra de vraag zakt.", tone: "selected" },
          { option: "Na piek", result: "Terug naar HP", code: "less_power", detail: "De warmtepompen nemen het weer over als ondersteuning niet meer nodig is.", tone: "muted" },
        ],
      };
    }
    if (reasonCode === "sticky_protection") {
      return {
        title: "Pompbescherming",
        verdict: "Korte pomprun",
        summary: "Alleen de pomp draait kort. De warmtepompen blijven uit omdat er geen verwarmings- of koelvraag is.",
        rows: [
          { option: "Verwarmen", result: "Niet nodig", code: "keep_current", detail: "Geen warmtevraag vanuit kamer of regeling.", tone: "muted" },
          { option: "Koelen", result: "Niet nodig", code: "keep_current", detail: "Geen koelvraag vanuit de kamer.", tone: "muted" },
          { option: "Pomp", result: "Kort aan", code: "sticky_protection", detail: "De dagelijkse bescherming laat de pomp ongeveer 1 minuut draaien.", tone: "selected" },
        ],
      };
    }
    if (["capacity_cap", "room_cap", "cooling_limiter"].includes(reasonCode)) {
      const cooling = getControlWorkingCoolingContext();
      return {
        title: "Koelregeling",
        verdict: `Maximaal ingesteld niveau ${cooling.allowedMax}`,
        summary: "De koelvraag wordt uitgevoerd binnen het ingestelde maximum. Dit is normale regeling, geen aandachtspunt.",
        rows: [
          { option: "Gevraagd koelniveau", result: cooling.rawDemand, code: "coolingDemandRaw", detail: "Wat de kamer vraagt voordat het ingestelde maximum meetelt.", tone: "muted" },
          { option: "Ingesteld maximum", result: cooling.allowedMax, code: reasonCode, detail: "Het hoogste niveau dat de software nu toestaat.", tone: "selected" },
          { option: "Uitgestuurd niveau", result: cooling.limitedDemand, code: "coolingLimitedDemand", detail: "Het niveau dat de warmtepomp op dit moment krijgt.", tone: "normal" },
        ],
      };
    }
    if (reasonCode === "buffer_stop") {
      return {
        title: "Koelregeling",
        verdict: "Water al koud genoeg",
        summary: "Er is koelvraag, maar de actuele watertemperatuur vraagt nu geen extra koeling.",
        rows: [
          { option: "Koelvraag", result: "Blijft actief", code: "coolingDemandRaw", detail: "De kamer blijft om koeling vragen.", tone: "muted" },
          { option: "Watertemperatuur", result: "Koud genoeg", code: "buffer_stop", detail: "De aanvoer is al koud genoeg voor dit moment.", tone: "selected" },
          { option: "Warmtepomp", result: "Wacht", code: "keep_current", detail: "De warmtepomp start automatisch zodra opnieuw actieve koeling nodig is.", tone: "muted" },
        ],
      };
    }
    if (["falling_gap", "projected_floor", "dew_stop", "restart_wait", "level1_hold", "oil_return_recovery", "sensor_fallback"].includes(reasonCode)) {
      const cooling = getControlWorkingCoolingContext();
      return {
        title: "Koelbewaking",
        verdict: cooling.permitted ? `Maximaal koelniveau ${cooling.allowedMax}` : "Koeling tijdelijk gepauzeerd",
        summary: "De koelvraag blijft actief, maar dauwpunt, aanvoer of compressorconditie vraagt tijdelijk voorzichtig gedrag.",
        rows: [
          { option: "Gevraagd koelniveau", result: cooling.rawDemand, code: "coolingDemandRaw", detail: "Wat de kamer vraagt voordat bewaking meetelt.", tone: "muted" },
          { option: "Maximaal veilig", result: cooling.allowedMax, code: reasonCode, detail: "Het hoogste niveau dat nu veilig is met de huidige dauwpuntmarge.", tone: "selected" },
          { option: "Uitgestuurd niveau", result: cooling.limitedDemand, code: "coolingLimitedDemand", detail: "Het niveau dat de warmtepomp op dit moment krijgt.", tone: "limited" },
        ],
      };
    }
    return null;
  }

  function renderControlWorkingOptimizer(model) {
    if (!model) {
      return "";
    }
    return `
      <div class="oq-working-optimizer">
        <div class="oq-working-optimizer-head">
          <span class="oq-working-eyebrow">${escapeHtml(model.title)}</span>
          <strong>${escapeHtml(model.verdict)}</strong>
          <p>${escapeHtml(model.summary)}</p>
        </div>
        <div class="oq-working-optimizer-options">
          ${model.rows.map((row) => `
            <div class="oq-working-optimizer-option oq-working-optimizer-option--${escapeHtml(row.tone || "muted")}">
              <span>${escapeHtml(row.option)}</span>
              <strong>${escapeHtml(row.result)}</strong>
              <p>${escapeHtml(row.detail)}</p>
            </div>
          `).join("")}
        </div>
      </div>
    `;
  }

  function getControlWorkingActiveStartupInhibit(nowMs = Date.now()) {
    const events = getDecisionLogEvents()
      .filter((event) => ["startup_inhibit_start", "startup_inhibit_refresh", "startup_inhibit_clear"].includes(String(event?.event_type || "")))
      .sort(compareDecisionEvents);
    const latest = events[events.length - 1];
    if (!latest || !["startup_inhibit_start", "startup_inhibit_refresh"].includes(String(latest.event_type))) {
      return null;
    }
    const startedEpochMs = getDecisionEventEpochMs(latest);
    const initialRemainingS = Math.max(0, Number(latest?.value_b) || 0);
    const elapsedS = Number.isFinite(startedEpochMs) ? Math.max(0, (nowMs - startedEpochMs) / 1000) : 0;
    const remainingS = Math.max(0, Math.ceil(initialRemainingS - elapsedS));
    if (initialRemainingS > 0 && remainingS <= 0) {
      return null;
    }
    return {
      event: latest,
      subject: String(latest?.subject || "SYSTEM").toUpperCase(),
      targetMode: Number(latest?.value_a) || 0,
      remainingS,
      remainingLabel: remainingS > 0 ? `Nog ${Math.max(1, Math.ceil(remainingS / 60))} min` : "Wachttijd actief",
    };
  }

  function getControlWorkingCurrent(heatPumpPanels) {
    const modeModel = getControlReplayModeModel(heatPumpPanels);
    const rawControlModeLabel = getEntityStateText("controlModeLabel", "—");
    const currentModeId = normalizeControlReplayModeId(rawControlModeLabel);
    const currentModeLabel = currentModeId ? currentModeId.toUpperCase() : rawControlModeLabel;
    const hp1Panel = heatPumpPanels.find((panel) => panel.title === "HP1") || heatPumpPanels[0];
    const hp2Panel = heatPumpPanels.find((panel) => panel.title === "HP2");
    const hp1Running = isControlReplayHpRunning(hp1Panel);
    const hp2Running = hp2Panel ? isControlReplayHpRunning(hp2Panel) : false;
    const duoActive = hp1Running && hp2Running;
    const defrostActive = modeModel.defrostActive;
    const coolingContext = getControlWorkingCoolingContext();
    const coolingProtection = modeModel.coolingProtection;
    const coolingCapped = modeModel.coolingCapped;
    const coolingActive = modeModel.coolingMode || modeModel.coolingRequest;
    const stickyActive = hasEntity("stickyActive") && isEntityActive("stickyActive");
    const boilerActive = modeModel.boilerActive;
    const startupInhibit = getControlWorkingActiveStartupInhibit();

    let title = "Eén warmtepomp actief";
    let copy = "De actuele vraag past binnen één warmtepomp. De andere warmtepomp blijft beschikbaar als extra capaciteit nodig is.";
    let expectation = "Een extra warmtepomp schakelt bij zodra de vraag lang genoeg hoog blijft en alle wachttijden vrij zijn.";
    let severity = "normal";
    let primaryReason = "keep_current";
    let sinceLabel = "Live";

    if (currentModeId === "cm98") {
      title = "Vorstbescherming actief";
      copy = "Het systeem laat water circuleren om bevriezing van het watercircuit te voorkomen.";
      expectation = "Vorstbescherming stopt zodra het risico weg is of de normale regeling weer voorrang krijgt.";
      severity = "limited";
      primaryReason = "frost_protection";
      sinceLabel = "Bescherming actief";
    } else if (stickyActive) {
      title = "Pompbescherming actief";
      copy = "Er is geen warmte- of koelvraag. De pomp draait kort om vastzitten na lange stilstand te voorkomen.";
      expectation = "Na ongeveer 1 minuut stopt de pomp en blijft het systeem standby tot er comfortvraag of bescherming nodig is.";
      primaryReason = "sticky_protection";
      sinceLabel = "Dagelijkse run";
    } else if (startupInhibit) {
      const coolingWait = startupInhibit.targetMode === 1;
      title = coolingWait ? "Koeling wacht na herstart" : "Verwarming wacht na herstart";
      copy = coolingWait
        ? "Er is koelvraag, maar de compressor blijft na de herstart nog kort uit om een te snelle herstart te voorkomen."
        : "Er is warmtevraag, maar de compressor blijft na de herstart nog kort uit om een te snelle herstart te voorkomen.";
      expectation = coolingWait
        ? "De warmtepomp start automatisch met koelen zodra de wachttijd voorbij is."
        : "De warmtepomp start automatisch met verwarmen zodra de wachttijd voorbij is.";
      primaryReason = "startup_inhibit";
      sinceLabel = startupInhibit.remainingLabel || "Wachttijd actief";
    } else if (coolingContext.reasonCode === "buffer_stop") {
      title = "Koeling wacht: water al koud genoeg";
      copy = "Er is koelvraag, maar het water is al koud genoeg. De warmtepomp hoeft daarom nu niet te starten.";
      expectation = "De warmtepomp start automatisch zodra opnieuw actieve koeling nodig is.";
      primaryReason = "buffer_stop";
      sinceLabel = "Koelvraag actief";
    } else if (coolingProtection) {
      const limiterReason = coolingContext.reasonCode && coolingContext.reasonCode !== "inactive" ? coolingContext.reasonCode : "soft_guard";
      const waitingForRestart = limiterReason === "restart_wait";
      title = waitingForRestart
        ? "Koeling wacht op veilige herstart"
        : coolingContext.permitted ? "Koeling tijdelijk beperkt" : "Koeling tijdelijk gepauzeerd";
      copy = waitingForRestart
        ? "De koelvraag is nog aanwezig. Na de koelstop wacht het systeem tot de veilige marge voldoende is hersteld."
        : `Er is koelvraag, maar het systeem koelt nu maximaal op niveau ${coolingContext.allowedMax} om condens te voorkomen.`;
      expectation = waitingForRestart
        ? "De warmtepomp start automatisch opnieuw zodra de veilige marge voldoende en stabiel is."
        : "Koeling neemt stap voor stap toe zodra de dauwpuntmarge veilig en stabiel is.";
      severity = "limited";
      primaryReason = limiterReason;
      sinceLabel = "Koelvraag actief";
    } else if (coolingCapped) {
      const coolingMaxLabel = coolingContext.allowedMax && coolingContext.allowedMax !== "—"
        ? `niveau ${coolingContext.allowedMax}`
        : "het ingestelde maximum";
      const cappedReason = ["capacity_cap", "room_cap", "cooling_limiter"].includes(coolingContext.reasonCode)
        ? coolingContext.reasonCode
        : "capacity_cap";
      title = "Koeling actief op ingesteld maximum";
      copy = `Er is koelvraag. Het systeem koelt maximaal op ${coolingMaxLabel}, zoals ingesteld in de software.`;
      expectation = "Koeling blijft binnen dit maximum. Dauwpunt, aanvoer en waterflow worden op de achtergrond bewaakt.";
      primaryReason = cappedReason;
      sinceLabel = "Koelvraag actief";
    } else if (coolingActive) {
      title = "Koeling actief";
      copy = "Er is koelvraag en dauwpuntbewaking geeft koeling vrij. Het systeem blijft marge en waterflow bewaken.";
      expectation = "Koeling blijft actief tot de kamertemperatuur richting setpoint zakt of bescherming ingrijpt.";
      primaryReason = "keep_current";
      sinceLabel = "Koelen";
    } else if (currentModeId === "cm4") {
      title = boilerActive ? "Ketelfallback actief" : "Ketelfallbackrol niet actief";
      copy = boilerActive
        ? "Geen warmtepomp is inzetbaar; de CV-ketel krijgt in CM4 de verwarmingsopdracht."
        : "CM4 is als regelrol gekozen, maar de keteluitvoer is op dit moment niet actief.";
      expectation = boilerActive
        ? "OpenQuatt blijft warmtepompherstel en alle veiligheidsvoorwaarden bewaken."
        : "De uitvoer blijft uit totdat de benodigde veiligheidsvoorwaarden geldig zijn.";
      severity = "fault";
      primaryReason = boilerActive ? "boiler_fallback" : "fallback_blocked";
      sinceLabel = boilerActive ? "Fallback actief" : "Uitvoer geblokkeerd";
    } else if (boilerActive) {
      title = "CV-ketel ondersteunt";
      copy = "De CV-ketel helpt tijdelijk omdat de warmtevraag meer vermogen vraagt dan de warmtepompen nu leveren.";
      expectation = "De CV-ketel stopt zodra de warmtepompen de vraag weer zelf kunnen dragen.";
      severity = "limited";
      primaryReason = "boiler_assist";
      sinceLabel = "Ondersteuning actief";
    } else if (defrostActive) {
      title = "Ontdooien actief";
      copy = "Een warmtepomp ontdooit tijdelijk. Het systeem houdt de keuze rustig zodat het ontdooien vanzelf kan afronden.";
      expectation = "De warmtepomp hervat automatisch zodra het ontdooien klaar is.";
      severity = "limited";
      primaryReason = "defrost_hold";
      sinceLabel = "Tijdelijk";
    } else if (duoActive) {
      title = "Duo-bedrijf actief";
      copy = "Beide warmtepompen draaien omdat de warmtevraag hoog blijft. Dit is normaal winterbedrijf.";
      expectation = "Eén warmtepomp stopt zodra de warmtevraag voldoende afneemt of single-bedrijf weer efficiënter is.";
      primaryReason = "better_heat";
      sinceLabel = "Actief";
    } else if (!hp1Running && !hp2Running) {
      title = "Geen warmtepomp actief";
      copy = "Er is nu geen warmtepompactie nodig, of het systeem wacht door bescherming of rusttijd.";
      expectation = "Bij nieuwe vraag kiest het systeem opnieuw de best passende warmtepomp.";
      primaryReason = "keep_current";
      sinceLabel = "Stand-by";
    }

    const hp1Waiting = startupInhibit && ["HP1", "BOTH"].includes(startupInhibit.subject);
    const hp2Waiting = startupInhibit && ["HP2", "BOTH"].includes(startupInhibit.subject);
    return {
      title,
      copy,
      expectation,
      severity,
      primaryReason,
      sinceLabel,
      modeLabel: currentModeLabel,
      strategyLabel: formatControlReplayStrategyLabel(),
      reasonLabel: getControlWorkingReasonLabel(primaryReason),
      hp1Running,
      hp2Running,
      hp2Available: Boolean(hp2Panel),
      hp1Status: hp1Running ? "Actief" : hp1Waiting ? "Wacht" : "Beschikbaar",
      hp2Status: hp2Panel ? (hp2Running ? "Actief" : hp2Waiting ? "Wacht" : "Beschikbaar") : "Niet aanwezig",
      cvStatus: boilerActive ? (currentModeId === "cm4" ? "Fallback" : "Actief") : "Uit",
      outsideTemp: formatControlReplayNumber("outsideTempSelected", 1, "°C", "—"),
      supplyTemp: formatControlReplayNumber("supplyTemp", 1, "°C", "—"),
      flow: formatControlReplayNumber("flowSelected", 0, "L/h", "—"),
      hp1Starts: getControlReplayCounterValue("hp1CompressorStarts24h", "—"),
      hp2Starts: getControlReplayCounterValue("hp2CompressorStarts24h", hp2Panel ? "—" : "n.v.t."),
      hp1Hours: formatControlReplayRuntimeHours("hp1RuntimeHours", "—"),
      hp2Hours: hp2Panel ? formatControlReplayRuntimeHours("hp2RuntimeHours", "—") : "n.v.t.",
      cooling: coolingContext,
      coolingProtection,
      startupInhibit,
      coolingCapped,
    };
  }

  function getDecisionLogEvents() {
    const payload = state.decisionLog;
    return payload?.ok && Array.isArray(payload.events) ? payload.events : [];
  }

  function getDecisionEventEpochMs(event) {
    const epochS = Number(event?.epoch_s);
    if (Number.isFinite(epochS) && epochS > 0) {
      return epochS * 1000;
    }
    const bootEpochS = Number(state.decisionLog?.meta?.boot_epoch_s);
    const uptimeS = Number(event?.uptime_s);
    if (Number.isFinite(bootEpochS) && bootEpochS > 0 && Number.isFinite(uptimeS) && uptimeS >= 0) {
      return (bootEpochS + uptimeS) * 1000;
    }
    return Number.NaN;
  }

  function getDecisionEventSortValue(event) {
    const epochMs = getDecisionEventEpochMs(event);
    if (Number.isFinite(epochMs)) {
      return epochMs / 1000;
    }
    const uptimeS = Number(event?.uptime_s);
    if (Number.isFinite(uptimeS)) {
      return uptimeS;
    }
    return Number(event?.seq) || 0;
  }

  function compareDecisionEvents(left, right) {
    const timeDifference = getDecisionEventSortValue(left) - getDecisionEventSortValue(right);
    if (timeDifference !== 0) {
      return timeDifference;
    }
    return (Number(left?.seq) || 0) - (Number(right?.seq) || 0);
  }

  function getDecisionEventAgeMinutes(event, nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    if (Number.isFinite(epochMs)) {
      return Math.max(0, Math.round((nowMs - epochMs) / 60000));
    }
    const payloadUptimeS = Number(state.decisionLog?.meta?.uptime_s);
    const eventUptimeS = Number(event?.uptime_s);
    if (Number.isFinite(payloadUptimeS) && Number.isFinite(eventUptimeS)) {
      return Math.max(0, Math.round((payloadUptimeS - eventUptimeS) / 60));
    }
    return Number.NaN;
  }

  function isControlWorkingSameLocalDay(left, right) {
    return left.getFullYear() === right.getFullYear()
      && left.getMonth() === right.getMonth()
      && left.getDate() === right.getDate();
  }

  function formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs = Date.now(), mode = "auto") {
    if (!Number.isFinite(epochMs)) {
      return "Onbekend";
    }
    const date = new Date(epochMs);
    const time = date.toLocaleTimeString("nl-NL", { hour: "2-digit", minute: "2-digit" });
    if (mode === "time") {
      return time;
    }
    if (mode === "weekday") {
      const day = date.toLocaleDateString("nl-NL", { weekday: "short" }).replace(".", "");
      return `${day} ${time}`;
    }
    const today = new Date(nowMs);
    const yesterday = new Date(today);
    yesterday.setDate(yesterday.getDate() - 1);
    if (isControlWorkingSameLocalDay(date, today)) {
      return time;
    }
    if (isControlWorkingSameLocalDay(date, yesterday)) {
      return `gisteren ${time}`;
    }
    const day = date.toLocaleDateString("nl-NL", { weekday: "short" }).replace(".", "");
    return `${day} ${time}`;
  }

  function getControlWorkingWindowEpochForMinute(minute, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const normalized = Math.max(0, Math.min(1440, Number(minute) || 0));
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    return bounds.start + ((normalized / 1440) * (bounds.end - bounds.start));
  }

  function getDecisionEventWindowMinute(event, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    const minuteInWindow = (value, start, end) => {
      if (!Number.isFinite(value) || value < start || value > end) {
        return Number.NaN;
      }
      return ((value - start) / Math.max(1, end - start)) * 1440;
    };

    if (Number.isFinite(epochMs)) {
      const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
      return minuteInWindow(epochMs, bounds.start, bounds.end);
    }

    const ageMinutes = getDecisionEventAgeMinutes(event, nowMs);
    if (!Number.isFinite(ageMinutes)) {
      return Number.NaN;
    }
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow);
    if (option?.calendarDay || option?.custom) {
      return Number.NaN;
    }
    const durationMinutes = getControlWorkingWindowDurationMinutes(selectedWindow, nowMs);
    return ageMinutes <= durationMinutes
      ? 1440 - ((ageMinutes / durationMinutes) * 1440)
      : Number.NaN;
  }

  function formatDecisionLogTimeLabel(event, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    if (!Number.isFinite(epochMs)) {
      const ageMinutes = getDecisionEventAgeMinutes(event, nowMs);
      return Number.isFinite(ageMinutes) ? formatControlWorkingRelativeOffset(ageMinutes) : "Onbekend";
    }
    if (selectedWindow === "week" || selectedWindow === "last48" || selectedWindow === "last3d" || selectedWindow === "custom") {
      return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "weekday");
    }
    if (selectedWindow.startsWith("last")) {
      return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "auto");
    }
    return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "time");
  }

  function formatDecisionDuration(seconds) {
    const normalized = Math.max(0, Math.round(Number(seconds) || 0));
    if (!normalized) {
      return "";
    }
    if (normalized < 60) {
      return `${normalized}s`;
    }
    if (normalized < 3600) {
      return `${Math.round(normalized / 60)} min`;
    }
    const hours = Math.floor(normalized / 3600);
    const minutes = Math.round((normalized % 3600) / 60);
    return minutes ? `${hours}u ${minutes}m` : `${hours}u`;
  }

  function getDecisionSubjectLabel(subject) {
    const normalized = String(subject || "").toUpperCase();
    const labels = {
      SYSTEM: "Systeem",
      HP1: "HP1",
      HP2: "HP2",
      BOTH: "HP1 + HP2",
      CV: "CV-ketel",
      COOLING: "Koeling",
      PUMP: "Pomp",
      CONTROLLER: "Regelaar",
    };
    return labels[normalized] || "Systeem";
  }

  function getDecisionModeSubjectLabel(subject, contextCm) {
    const normalized = String(subject || "").toUpperCase();
    const subjectLabel = getDecisionSubjectLabel(subject);
    if (normalized !== "HP1" && normalized !== "HP2" && normalized !== "BOTH") {
      return subjectLabel;
    }
    if (Number(contextCm) === 5) {
      return `${subjectLabel} (koelen)`;
    }
    if (Number(contextCm) > 0) {
      return `${subjectLabel} (verwarmen)`;
    }
    return subjectLabel;
  }

  function getDecisionCoolingSourceLabel(event) {
    const coolingSubject = String(event?._oq_active_cooling_subject || "").toUpperCase();
    if (coolingSubject === "HP1" || coolingSubject === "HP2" || coolingSubject === "BOTH") {
      return getDecisionModeSubjectLabel(coolingSubject, 5);
    }
    return getDecisionModeSubjectLabel(event?.subject, 5);
  }

  function getControlWorkingSingleTopologySource(event) {
    const subject = String(event?.subject || "").toUpperCase();
    return subject === "HP1" || subject === "HP2" ? subject : "";
  }

  function getDecisionEventCopy(event) {
    const eventType = String(event?.event_type || "");
    const subject = getDecisionSubjectLabel(event?.subject);
    const reasonCode = String(event?.reason || "unknown");
    const isCoolingModeEvent = Number(event?._oq_context_cm ?? event?.cm) === 5;
    const activeCoolingSource = event?._oq_active_cooling_source || "De warmtepomp";
    const activeHeatingSource = event?._oq_active_heating_source || "De warmtepomp";
    const coolingStopReason = String(event?._oq_cooling_stop_reason || (reasonCode === "dew_stop" ? "dew_stop" : ""));
    const coolingDemandEnded = ["less_power", "demand_decreased", "cooling_request_cleared"].includes(reasonCode);
    const heatingDemandEnded = reasonCode === "heating_request_cleared";
    const coolingRuntimeHold = Boolean(event?._oq_cooling_runtime_hold);
    const heatingRuntimeHold = Boolean(event?._oq_heating_runtime_hold);
    const coolingProtectionReason = isControlWorkingCoolingProtectionReason(reasonCode);
    const boilerStopBlocked = ["soft_guard", "sensor_fallback", "no_candidate", "flow_preflow"].includes(reasonCode);
    const reason = getControlWorkingReasonMeta(reasonCode);
    const isFlowPreStart = reasonCode === "flow_preflow";
    const isFlowFault = reasonCode === "flow_too_low";
    const incidentCopy = getControlReplayIncidentEventCopy(event, subject);
    if (incidentCopy) {
      return incidentCopy;
    }
    const fallback = {
      title: "Keuze van het systeem",
      summary: "De regelaar heeft een keuze vastgelegd.",
      detail: reason.summary,
      next: "Het systeem beoordeelt opnieuw zodra vraag, marge of beschikbaarheid verandert.",
    };
    const copies = {
      source_start: {
        title: isCoolingModeEvent ? `Koeling gestart (${subject})` : `${subject} gestart`,
        reasonLabel: isCoolingModeEvent ? "Koeling gestart" : "",
        reasonSummary: isCoolingModeEvent ? "Koeling is vrijgegeven en de gekozen warmtepomp start met koelen." : "",
        summary: isCoolingModeEvent
          ? `${subject} is gestart om te koelen. Dauwpunt, waterflow en aanvoertemperatuur blijven bewaakt.`
          : `${subject} is gekozen op basis van beschikbaarheid, wachttijd en draaiurenbalans.`,
        detail: isCoolingModeEvent
          ? "De koelvraag is vrijgegeven. HP1 en HP2 zijn gelijkwaardig; de regelaar kiest de beschikbare bron die nu het beste past."
          : "HP1 en HP2 zijn gelijkwaardig. De regelaar kiest de beschikbare bron die op dat moment het beste past.",
        next: isCoolingModeEvent
          ? "Koeling blijft actief zolang er koelvraag is en de veilige marges vrij blijven."
          : "Als de vraag hoog blijft, beoordeelt het systeem of extra vermogen nodig is.",
      },
      source_stop: {
        title: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? `${subject} gestopt door dauwpunt`
            : coolingDemandEnded
            ? `Koeling gestopt: geen koelvraag`
            : `Koeling afgerond (${subject})`
          : heatingDemandEnded
          ? "Verwarming gestopt: geen warmtevraag"
          : reasonCode === "less_power"
          ? "Eén warmtepomp stopt"
          : `${subject} gestopt`,
        reasonLabel: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? "Dauwpuntstop"
            : coolingDemandEnded
            ? "Geen koelvraag"
            : "Koeling afgerond"
          : heatingDemandEnded
          ? "Geen warmtevraag"
          : reasonCode === "less_power"
          ? "Eén warmtepomp is genoeg"
          : "",
        reasonSummary: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? "De warmtepomp stopte omdat de dauwpuntbewaking koelen pauzeerde."
            : coolingDemandEnded
            ? "De koelvraag is weggevallen of voldoende afgenomen."
            : "De koelactie is afgerond. Een korte pompnaloop kan daarna normaal zijn."
          : heatingDemandEnded
          ? "De warmtevraag is weggevallen. Een korte pompnaloop kan daarna normaal zijn."
          : reasonCode === "less_power"
          ? "De warmtevraag is afgenomen; één warmtepomp kan de resterende vraag dragen."
          : "",
        summary: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? `${subject} stopte omdat verder koelen te dicht bij het dauwpunt kwam.`
            : coolingDemandEnded
            ? "Er is geen koelvraag meer; de warmtepomp stopt met koelen."
            : `${subject} is klaar met koelen.`
          : heatingDemandEnded
          ? "Er is geen warmtevraag meer; de warmtepomp stopt met verwarmen."
          : reasonCode === "less_power"
          ? "De vraag is lager. Eén warmtepomp kan de resterende warmtevraag rustig dragen."
          : `${subject} is gestopt omdat minder vermogen voldoende is of bescherming voorrang kreeg.`,
        detail: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? "Dit is beschermingsgedrag. Het systeem voorkomt condens en kan later opnieuw koelen zodra de marge veilig is."
            : "De pomp kan daarna nog kort nalopen om het watercircuit netjes af te ronden."
          : heatingDemandEnded
          ? "De regeling vraagt geen warmte meer. De pomp kan daarna nog kort nalopen om het watercircuit netjes af te ronden."
          : "De regelaar voorkomt onnodig doordraaien en houdt tegelijk wachttijden en bescherming in de gaten.",
        next: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? "Bij blijvende koelvraag start koeling opnieuw zodra de dauwpuntmarge veilig genoeg is."
            : "Het systeem blijft standby of rondt de naloop af totdat er opnieuw koelvraag is."
          : heatingDemandEnded
          ? "Het systeem blijft standby totdat er opnieuw warmtevraag is."
          : "Bij stijgende vraag kan dezelfde of de andere warmtepomp opnieuw starten.",
      },
      topology_change: {
        title: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? "Koeling gestopt: geen koelvraag"
              : reasonCode === "dew_stop"
              ? "Koeling gestopt door dauwpunt"
              : "Koeling gestopt"
            : "Koeling actief"
          : event?.to === "idle" && heatingDemandEnded
          ? "Verwarming gestopt: geen warmtevraag"
          : event?.to === "duo"
          ? "Twee warmtepompen verwarmen"
          : "Eén warmtepomp verwarmt",
        reasonLabel: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? "Geen koelvraag"
              : reasonCode === "dew_stop"
              ? "Dauwpuntstop"
              : "Koeling gestopt"
            : "Koeling actief"
          : event?.to === "idle" && heatingDemandEnded
          ? "Geen warmtevraag"
          : "",
        reasonSummary: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? "De koelvraag is weggevallen. Eventuele naloop is normaal."
              : reasonCode === "dew_stop"
              ? "Koeling pauzeert om condens te voorkomen. Herstart kan zodra de marge veilig is."
              : "Er is geen warmtepomp meer actief voor koeling. Eventuele naloop is normaal."
            : "Koeling is actief. Het systeem bewaakt tegelijk de veilige marges."
          : event?.to === "idle" && heatingDemandEnded
          ? "De warmtevraag is weggevallen. Eventuele naloop is normaal."
          : "",
        summary: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? "De koelvraag is weg. Er is geen warmtepomp meer actief voor koeling."
              : reasonCode === "dew_stop"
              ? "Koeling stopt tijdelijk omdat verder koelen te dicht bij het dauwpunt komt."
              : "Er is geen warmtepomp meer actief voor koeling."
            : `${subject} koelt. Het systeem blijft dauwpunt, waterflow en aanvoertemperatuur bewaken.`
          : event?.to === "duo"
          ? "Samen leveren de warmtepompen rustiger vermogen dan één warmtepomp op hoge belasting."
          : event?.to === "idle" && heatingDemandEnded
          ? "Er is geen warmtepomp meer actief voor verwarmen."
          : "De vraag is lager. Eén warmtepomp kan de resterende vraag weer rustig dragen.",
        detail: isCoolingModeEvent
          ? "Koelen gebruikt dezelfde bronkeuze-logica als verwarmen: de warmtepompen zijn gelijkwaardig en de controller kiest de rustigste beschikbare bron."
          : "De duo-keuze gaat niet over hoofd- en hulppomp. De warmtepompen zijn gelijkwaardig; het systeem kiest de rustigste combinatie.",
        next: isCoolingModeEvent
          ? "Koeling blijft actief zolang er koelvraag is en bescherming geen beperking vraagt."
          : event?.to === "duo"
          ? "Duo-bedrijf blijft actief zolang de extra reserve nuttig is."
          : event?.to === "idle" && heatingDemandEnded
          ? "Het systeem blijft standby totdat er opnieuw warmtevraag is."
          : "De tweede warmtepomp blijft beschikbaar als de vraag opnieuw stijgt.",
      },
      decision_hold: {
        title: reasonCode === "defrost_hold" ? "Keuze kort vastgehouden" : "Start of wissel uitgesteld",
        summary: reasonCode === "defrost_hold"
          ? "De regelaar laat ontdooien rustig afronden voordat hij opnieuw schakelt."
          : "De regelaar wacht bewust even om korte cycli en onrustig gedrag te voorkomen.",
        detail: reason.summary,
        next: "Na de wachttijd beoordeelt het systeem opnieuw wat de rustigste keuze is.",
      },
      decision_blocked: {
        title: reasonCode === "flow_too_low"
          ? "Start geblokkeerd: waterflow te laag"
          : subject === "CV-ketel" ? "CV-ketel niet vrijgegeven" : "Actie geblokkeerd",
        reasonLabel: reasonCode === "flow_too_low" ? "Waterflow blijft te laag" : "",
        reasonSummary: reasonCode === "flow_too_low"
          ? "De normale voorlooptijd is verstreken. De warmtepomp blijft veilig uit totdat voldoende water circuleert."
          : "",
        summary: reasonCode === "flow_too_low"
          ? "De pomp draait, maar na de normale voorlooptijd is nog niet genoeg waterflow gemeten."
          : subject === "CV-ketel"
          ? "Er was een mogelijke hulpvraag, maar de CV-ketel was niet vrijgegeven."
          : "De gevraagde actie is tijdelijk niet toegestaan door een voorwaarde of bescherming.",
        detail: reasonCode === "flow_too_low"
          ? "Dit is pas een blokkade nadat de normale opbouwtijd is verstreken; een korte lage flow direct na het starten hoort hier niet bij."
          : reason.summary,
        next: reasonCode === "flow_too_low"
          ? "De regelaar blijft de waterflow volgen en geeft de start automatisch vrij zodra de circulatie voldoende en stabiel is."
          : "De regelaar probeert opnieuw zodra de voorwaarden vrij zijn.",
        checks: reasonCode === "flow_too_low"
          ? ["Voorlooptijd verstreken", "Warmtepomp blijft veilig uit", "Waterflow wordt opnieuw beoordeeld"]
          : null,
      },
      candidate_blocked: {
        title: `${subject} wacht nog`,
        summary: reasonCode === "candidate_in_rest"
          ? `${subject} zit nog in rusttijd na een vorige stop.`
          : `${subject} is nu nog geen veilige kandidaat om te starten.`,
        detail: reason.summary,
        next: "De regelaar probeert opnieuw zodra de voorwaarde vrij is en de vraag blijft bestaan.",
      },
      flow_hold_start: {
        title: reasonCode === "flow_postflow"
          ? coolingRuntimeHold ? "Koeling loopt nog kort door" : heatingRuntimeHold ? "Verwarming loopt nog kort door" : isCoolingModeEvent ? "Naloop na koelen actief" : "Naloop actief"
          : isFlowFault ? "Start wacht op voldoende waterflow"
          : isCoolingModeEvent ? "Voorloop voor koelen" : "Voorloop voor start",
        reasonLabel: reasonCode === "flow_postflow"
          ? coolingRuntimeHold || heatingRuntimeHold ? "Minimale looptijd" : isCoolingModeEvent ? "Naloop na koelen" : "Naloop actief"
          : isFlowFault ? "Waterflow blijft te laag"
          : isCoolingModeEvent ? "Voorloop voor koelen" : "Voorloop actief",
        reasonSummary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? `${activeCoolingSource} staat nog op Cooling terwijl het systeem al in CM1 naloop zit.`
              : "De pomp draait kort na om het koelbedrijf netjes af te ronden."
            : "De pomp draait eerst kort zodat de flow stabiel is voordat de warmtepomp met koelen start."
          : heatingRuntimeHold
          ? `${activeHeatingSource} verwarmt nog terwijl de regelaar al in CM1 naloop zit.`
          : "",
        summary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? `${activeCoolingSource} koelt nog kort door door minimale looptijd; het systeem zit al in naloop.`
              : "De pomp draait kort na zodat het koelbedrijf netjes wordt afgerond."
            : isFlowFault
            ? "De voorlooptijd is verstreken, maar de waterflow is nog niet voldoende om veilig met koelen te starten."
            : "De pomp draait eerst kort voor. Daarna mag de warmtepomp met koelen starten."
          : isFlowFault
          ? "De voorlooptijd is verstreken, maar de waterflow is nog niet voldoende om de warmtepomp veilig te starten."
          : isFlowPreStart
            ? "De pomp draait eerst kort voor zodat de flow stabiel is voordat de warmtepomp start."
          : heatingRuntimeHold
          ? `${activeHeatingSource} verwarmt nog kort door door minimale looptijd; het systeem zit al in naloop.`
          : reason.summary,
        detail: isCoolingModeEvent
          ? coolingRuntimeHold
            ? "De controller vraagt geen nieuwe koelactie meer, maar stopt de buitenunit niet abrupt. Eerst wordt de minimale looptijd afgerond; daarna volgt de normale pompnaloop."
            : "Dit is een normale startstap. De pomp krijgt eerst ongeveer 30 seconden om waterflow op te bouwen; daarna wordt de koelactie vrijgegeven."
          : heatingRuntimeHold
          ? "De regelaar vraagt geen nieuwe warmte meer, maar stopt de buitenunit niet abrupt. Eerst wordt de minimale looptijd afgerond; daarna volgt de normale pompnaloop."
          : "CM1 wordt gebruikt als korte flowfase. De pomp krijgt eerst even tijd om waterflow op te bouwen voordat de warmtepomp start of stopt.",
        next: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? `${activeCoolingSource} stopt zodra de minimale looptijd vrij is; daarna rondt de pomp de naloop af.`
              : "Daarna blijft het systeem standby of beoordeelt het een nieuwe koelvraag."
            : "Na de korte voorloop gaat het systeem automatisch door met koelen."
          : heatingRuntimeHold
          ? `${activeHeatingSource} stopt zodra de minimale looptijd vrij is; daarna rondt de pomp de naloop af.`
          : "De regelaar gaat automatisch verder zodra de flowfase klaar is.",
      },
      flow_hold_clear: {
        title: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? "Naloop na koelen klaar" : "Naloop klaar"
          : isFlowFault ? "Waterflow hersteld"
          : isCoolingModeEvent ? "Voorloop voor koelen klaar" : "Voorloop klaar",
        reasonLabel: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? "Naloop na koelen" : "Naloop actief"
          : isFlowFault ? "Waterflow hersteld"
          : isCoolingModeEvent ? "Koelen vrijgegeven" : "Voorloop klaar",
        reasonSummary: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? "De korte pompnaloop na koelen is afgerond." : "De korte pompnaloop is afgerond."
          : isFlowFault
          ? "De waterflow is hersteld en de tijdelijke startblokkade is opgeheven."
          : isCoolingModeEvent
          ? "De waterflow is voldoende; de warmtepomp kan met koelen verder."
          : "De waterflow is voldoende; de warmtepomp is vrijgegeven voor de volgende stap.",
        summary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? "De pomp heeft kort nagedraaid; het koelbedrijf is afgerond."
            : "De waterflow is voldoende; koeling kan verder."
          : reasonCode === "flow_postflow"
          ? "De pomp heeft kort nagedraaid; het systeem kan terug naar standby."
          : "De waterflowfase is afgerond; de normale regeling kan verder.",
        detail: isCoolingModeEvent
          ? "De flowfase hoort bij het koeltraject. Dit is normaal gedrag rond starten of stoppen van koeling."
          : reasonCode === "flow_postflow"
          ? "De warmtepomp is gestopt en de pomp heeft de korte naloop afgerond."
          : "De pomp heeft voldoende circulatie opgebouwd. De startvoorwaarde voor waterflow is nu vrij.",
        next: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? "Het systeem blijft standby totdat er opnieuw koelvraag of bescherming nodig is."
            : "De controller vervolgt met koelen en blijft dauwpunt en aanvoer bewaken."
          : "De controller vervolgt met verwarmen, koelen, vorstbescherming of standby.",
        checks: reasonCode === "flow_postflow"
          ? ["Naloop afgerond", "Warmtepomp gestopt", "Regeling gaat naar standby"]
          : isFlowFault
          ? ["Waterflow hersteld", "Startblokkade opgeheven", "Regeling gaat verder"]
          : ["Waterflow voldoende", "Warmtepomp vrijgegeven", "Regeling gaat verder"],
      },
      startup_inhibit_start: {
        title: Number(event?.value_a) === 1 ? "Koeling wacht na herstart" : "Verwarming wacht na herstart",
        reasonLabel: "Wachttijd na herstart",
        reasonSummary: "De compressor blijft na een herstart kort uit om een te snelle herstart te voorkomen.",
        summary: Number(event?.value_a) === 1
          ? "Er is koelvraag, maar de warmtepomp wacht nog kort na de herstart."
          : "Er is warmtevraag, maar de warmtepomp wacht nog kort na de herstart.",
        detail: "De controller kent na een reboot de voorgaande stoptijd niet meer. Daarom houdt hij eenmaal de ingestelde minimale uit-tijd aan voordat een compressor mag starten.",
        next: Number(event?.value_a) === 1
          ? "De warmtepomp start automatisch met koelen zodra de wachttijd voorbij is."
          : "De warmtepomp start automatisch met verwarmen zodra de wachttijd voorbij is.",
        checks: ["Comfortvraag aanwezig", "Compressor blijft nog uit", "Start volgt automatisch"],
      },
      startup_inhibit_clear: {
        title: "Wachttijd na herstart voorbij",
        reasonLabel: "Wachttijd afgerond",
        reasonSummary: "De compressor mag weer starten als de vraag nog aanwezig is.",
        summary: "De wachttijd na de herstart is verstreken.",
        detail: "De minimale uit-tijd na de reboot is afgerond. Alle normale startvoorwaarden blijven van toepassing.",
        next: "Bij aanhoudende vraag gaat de controller automatisch verder met de gekozen warmtepomp.",
        checks: ["Wachttijd verstreken", "Start weer toegestaan", "Regeling gaat verder"],
      },
      startup_inhibit_refresh: {
        title: Number(event?.value_a) === 1 ? "Koelvraag tijdens wachttijd gewijzigd" : "Warmtevraag tijdens wachttijd gewijzigd",
        reasonLabel: "Wachttijd blijft actief",
        reasonSummary: "De gekozen warmtepomp of doelmodus veranderde, maar de wachttijd na de herstart loopt door.",
        summary: "De controller heeft de actuele vraag opnieuw beoordeeld. De compressor blijft wachten tot dezelfde wachttijd voorbij is.",
        detail: "Tijdens de wachttijd veranderde welke warmtepomp of doelmodus gewenst is. De blokkering is niet opgeheven; alleen de context van de wachtperiode is bijgewerkt.",
        next: "Zodra de wachttijd voorbij is, mag de dan gekozen warmtepomp automatisch starten.",
        checks: ["Vraag opnieuw beoordeeld", "Wachttijd blijft actief", "Start volgt automatisch"],
      },
      defrost_seen_start: {
        title: `Ontdooien gestart (${subject})`,
        summary: `${subject} ontdooit kort. Dat is normaal bij koud en vochtig weer.`,
        detail: "De buitenunit bepaalt zelf hoe lang ontdooien duurt. De regelaar voorkomt ondertussen onnodige wissels.",
        next: "Na ontdooien levert de warmtepomp automatisch weer normaal mee.",
      },
      defrost_seen_clear: {
        title: `Ontdooien klaar (${subject})`,
        summary: `${subject} heeft ontdooien afgerond en kan weer normaal vermogen leveren.`,
        detail: "De regelaar ziet dat de ontdooifase voorbij is en laat de normale regeling weer doorlopen.",
        next: "Bij aanhoudende vraag blijft de warmtepomp actief of schakelt duo-bedrijf bij.",
      },
      cooling_limited: {
        title: reasonCode === "dew_stop"
          ? "Koeling gestopt door dauwpunt"
          : reasonCode === "restart_wait"
          ? "Koeling wacht op veilige herstart"
          : reasonCode === "buffer_stop"
          ? "Koeling wacht: water al koud genoeg"
          : coolingProtectionReason ? "Koeling tijdelijk beperkt" : "Koeling op ingesteld maximum",
        summary: reasonCode === "dew_stop"
          ? `${activeCoolingSource} stopt omdat verder koelen te dicht bij het dauwpunt komt.`
          : reasonCode === "restart_wait"
          ? "De koelvraag is nog aanwezig. Het systeem wacht met opnieuw starten tot de veilige marge voldoende is hersteld."
          : reasonCode === "buffer_stop"
          ? "Er is koelvraag, maar het water is al koud genoeg. De warmtepomp hoeft daarom nu niet te starten."
          : coolingProtectionReason
          ? "Er is koelvraag, maar het systeem houdt het koelvermogen tijdelijk lager."
          : "Er is koelvraag. Het systeem koelt binnen het actuele softwaremaximum.",
        detail: reason.summary,
        next: reasonCode === "restart_wait"
          ? "De warmtepomp start automatisch opnieuw zodra de veilige marge voldoende en stabiel is."
          : reasonCode === "buffer_stop"
          ? "De warmtepomp start automatisch zodra opnieuw actieve koeling nodig is."
          : coolingProtectionReason
          ? "Koeling wordt vrijgegeven zodra de veilige marge stabiel genoeg is."
          : "Koeling blijft binnen dit maximum zolang de instelling en koelvraag gelijk blijven.",
      },
      cooling_released: {
        title: "Koeling vrijgegeven",
        summary: "De veilige marge is terug. De warmtepomp mag weer normaal koelen.",
        detail: "De dauwpunt- en temperatuurmarge is voldoende hersteld om de begrenzing los te laten.",
        next: "De regelaar blijft koelen zolang de kamer daarom vraagt.",
      },
      sticky_pump_run: {
        title: "Pompbescherming uitgevoerd",
        summary: "De pomp draaide kort na langere stilstand. Dit is geen verwarmings- of koelvraag.",
        detail: "Deze korte run voorkomt dat de pomp na stilstand vast gaat zitten.",
        next: "De volgende preventieve run volgt pas na de ingestelde beschermingstijd.",
      },
      frost_protection_start: {
        title: "Vorstbescherming actief",
        summary: "Het systeem laat water circuleren om bevriezing te voorkomen.",
        detail: "Dit is beschermingsgedrag. Er hoeft geen verwarmings- of koelvraag te zijn.",
        next: "Vorstbescherming stopt zodra het risico weg is of de normale regeling weer voorrang krijgt.",
      },
      frost_protection_clear: {
        title: "Vorstbescherming gestopt",
        summary: "Het systeem verlaat de vorstbescherming en gaat terug naar normale regeling.",
        detail: "Het watercircuit hoeft niet langer apart beschermd te worden.",
        next: "Bij nieuw vorstrisico kan de bescherming automatisch opnieuw starten.",
      },
      boiler_assist_start: {
        title: "CV-ketel ondersteunt tijdelijk",
        summary: "De CV-ketel helpt omdat extra capaciteit tijdelijk nuttig is.",
        detail: "De warmtepompen blijven de basis leveren. De CV-ketel vult alleen aan zolang de vraag daar om vraagt.",
        next: "De CV-ketel stopt zodra de warmtepompen de vraag weer rustig zelf kunnen dragen.",
      },
      boiler_assist_stop: boilerStopBlocked
        ? {
          title: reasonCode === "sensor_fallback"
            ? "CV-ondersteuning gestopt: meting ontbreekt"
            : reasonCode === "no_candidate"
            ? "CV-ondersteuning niet beschikbaar"
            : reasonCode === "flow_preflow"
            ? "CV-ondersteuning wacht op voorloop"
            : "CV-ondersteuning veilig gestopt",
          summary: reasonCode === "sensor_fallback"
            ? "De CV-ketel is gestopt omdat een betrouwbare aanvoertemperatuur ontbreekt."
            : reasonCode === "no_candidate"
            ? "De CV-ketel is uitgeschakeld of kan nu niet worden ingezet."
            : reasonCode === "flow_preflow"
            ? "De CV-ketel wacht tijdens de test kort tot de waterflow stabiel is."
            : "De CV-ketel is gestopt omdat een veiligheidsgrens voor de watertemperatuur actief is.",
          detail: "Dit is een beschermende of configuratiegebonden keuze, niet een teken dat de warmtevraag vanzelf is afgenomen.",
          next: "De regelaar beoordeelt automatisch opnieuw zodra de blokkade is opgeheven.",
        }
        : {
          title: "CV-ondersteuning gestopt",
          summary: "De extra ondersteuning is niet meer nodig.",
          detail: "De warmtevraag is genoeg gedaald of de warmtepompen kunnen het weer zelf dragen.",
          next: "De CV-ketel blijft beschikbaar als er later opnieuw extra capaciteit nodig is.",
        },
      attention_pattern: {
        title: "Aandachtspunt gezien",
        summary: reasonCode === "start_stop_rate_high"
          ? "Er zijn relatief veel starts/stops gezien. Dat is nuttig om te volgen."
          : "Het systeem ziet een patroon dat extra aandacht verdient.",
        detail: reason.summary,
        next: "Als het patroon aanhoudt, blijft dit zichtbaar voor support en analyse.",
      },
    };
    return copies[eventType] || fallback;
  }

  function getDecisionEventGraphEndMinute(startMinute, event, selectedWindow) {
    const durationS = Number(event?.duration_s);
    if (!Number.isFinite(durationS) || durationS <= 0) {
      return startMinute;
    }
    const minutes = getControlWorkingEventDurationChartMinutes(event, selectedWindow);
    return Math.max(startMinute, Math.min(1440, startMinute + Math.max(5, minutes)));
  }

  function getDecisionEventDisplaySeverity(event) {
    const eventType = String(event?.event_type || "");
    const reason = String(event?.reason || "");
    const incidentSeverity = getControlReplayIncidentDisplaySeverity(event);
    if (incidentSeverity) {
      return incidentSeverity;
    }
    if (isDecisionCoolingAdjustmentEvent(event)) {
      return "normal";
    }
    if (reason === "buffer_stop") {
      return "normal";
    }
    if (isControlWorkingCoolingProtectionReason(reason)) {
      return "limited";
    }
    if (eventType === "flow_hold_start" || eventType === "flow_hold_clear") {
      if (reason === "flow_preflow" || reason === "flow_postflow") {
        return "normal";
      }
      if (reason === "flow_too_low") {
        return eventType === "flow_hold_start" ? "limited" : "normal";
      }
    }
    return String(event?.severity || "normal");
  }

  function isDecisionCoolingAdjustmentEvent(event) {
    if (String(event?.event_type || "") !== "cooling_limited") {
      return false;
    }
    const reason = String(event?.reason || "");
    if (["capacity_cap", "room_cap", "cooling_limiter", "simmer", "falling_gap", "level1_hold"].includes(reason)) {
      return true;
    }
    return reason === "projected_floor" && Number(event?.value_a) > 0;
  }

  function mapDecisionEventToControlWorkingItem(event, selectedWindow, nowMs) {
    const eventType = String(event?.event_type || "");
    const reasonCode = String(event?.reason || "unknown");
    if (!eventType || eventType === "boot_marker" || event?._oq_hidden) {
      return null;
    }
    if ((eventType === "defrost_seen_start" || eventType === "defrost_seen_clear") && Number(event?._oq_context_cm ?? event?.cm) === 5) {
      return null;
    }
    if (isDecisionCoolingAdjustmentEvent(event) || eventType === "cooling_released") {
      return null;
    }
    const graphStart = getDecisionEventWindowMinute(event, selectedWindow, nowMs);
    if (!Number.isFinite(graphStart)) {
      return null;
    }
    const copy = getDecisionEventCopy(event);
    const contextCm = Number(event?._oq_context_cm ?? event?.cm);
    const source = eventType === "cooling_limited" || eventType === "cooling_released"
      ? getDecisionCoolingSourceLabel(event)
      : eventType === "source_start" || eventType === "source_stop" || eventType === "topology_change"
      ? getDecisionModeSubjectLabel(event?.subject, contextCm)
      : getDecisionSubjectLabel(event?.subject);
    const duration = formatDecisionDuration(event?.duration_s);
    const displaySeverity = getDecisionEventDisplaySeverity(event);
    return {
      id: `fw-${event.seq || event.uptime_s || eventType}`,
      kind: "event",
      severity: displaySeverity,
      time: formatDecisionLogTimeLabel(event, selectedWindow, nowMs),
      title: copy.title,
      summary: copy.summary,
      detailTitle: "Waarom gebeurde dit?",
      detail: copy.detail,
      next: copy.next,
      source,
      reasonLabel: copy.reasonLabel || "",
      reasonSummary: copy.reasonSummary || "",
      reasonCode,
      modeLabel: Number(event?.cm) > 0 ? `CM${Number(event.cm)}` : "CM?",
      modeTransitionLabel: event?._oq_mode_transition || "",
      duration,
      graphStart: Math.max(0, Math.min(1440, graphStart)),
      graphEnd: getDecisionEventGraphEndMinute(graphStart, event, selectedWindow),
      realEventType: eventType,
      rawDecisionEvent: event,
      checks: Array.isArray(copy.checks) ? copy.checks : null,
      timelineHidden: ((eventType === "source_start" || eventType === "topology_change") && contextCm === 5) ||
        (eventType === "source_stop" && (event?._oq_cooling_stop_reason === "dew_stop" || reasonCode === "dew_stop")) ||
        eventType === "startup_inhibit_start" || eventType === "startup_inhibit_refresh" || eventType === "startup_inhibit_clear",
    };
  }

  function getControlWorkingVisibleEpochRange(startEpochMs, endEpochMs, selectedWindow, nowMs) {
    if (!Number.isFinite(startEpochMs) || !Number.isFinite(endEpochMs) || endEpochMs <= startEpochMs) {
      return null;
    }
    const windowBounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const visibleStart = Math.max(startEpochMs, windowBounds.start);
    const visibleEnd = Math.min(endEpochMs, windowBounds.end);
    if (visibleEnd <= visibleStart) {
      return null;
    }
    const windowMs = Math.max(1, windowBounds.end - windowBounds.start);
    return {
      start: ((visibleStart - windowBounds.start) / windowMs) * 1440,
      end: ((visibleEnd - windowBounds.start) / windowMs) * 1440,
      durationS: Math.max(0, Math.round((visibleEnd - visibleStart) / 1000)),
    };
  }

  function getControlWorkingDerivedModeLabel(event) {
    const cm = Number(event?._oq_context_cm ?? event?.cm);
    return Number.isFinite(cm) && cm > 0 ? `CM${cm}` : "CM?";
  }

  function createControlWorkingDerivedSpan(config, selectedWindow, nowMs) {
    const range = getControlWorkingVisibleEpochRange(config.startEpochMs, config.endEpochMs, selectedWindow, nowMs);
    if (!range || range.durationS < Number(config.minDurationS || 60)) {
      return null;
    }
    return {
      id: config.id,
      kind: "span",
      severity: config.severity || "normal",
      time: getControlWorkingIntervalTimeLabel(range.start, range.end, Boolean(config.isOpen)),
      duration: formatDecisionDuration(range.durationS),
      title: config.title,
      summary: config.summary,
      detailTitle: config.detailTitle || "Waarom liep deze periode?",
      detail: config.detail,
      next: config.next,
      source: config.source || "Systeem",
      reasonCode: config.reasonCode || "keep_current",
      reasonLabel: config.reasonLabel || "",
      reasonSummary: config.reasonSummary || "",
      modeLabel: config.modeLabel || getControlWorkingDerivedModeLabel(config.startEvent),
      modeTransitionLabel: "",
      graphStart: Math.max(0, Math.min(1440, range.start)),
      graphEnd: Math.max(0, Math.min(1440, range.end)),
      derivedFromDecisionLog: true,
    };
  }

  function buildControlWorkingDerivedItems(events, selectedWindow, nowMs) {
    const windowBounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const intervals = { HP1: [], HP2: [], cooling: [], boiler: [], frost: [], startupInhibit: [] };
    const open = { HP1: null, HP2: null, cooling: null, boiler: null, frost: null, startupInhibit: null };
    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const eventEpoch = (event) => getDecisionEventEpochMs(event);
    const openInterval = (key, event) => {
      const startEpochMs = eventEpoch(event);
      if (!Number.isFinite(startEpochMs) || open[key]) {
        return;
      }
      open[key] = { key, startEvent: event, startEpochMs };
    };
    const closeInterval = (key, event) => {
      const active = open[key];
      const endEpochMs = eventEpoch(event);
      if (!active || !Number.isFinite(endEpochMs)) {
        return;
      }
      if (endEpochMs > active.startEpochMs) {
        intervals[key].push({ ...active, endEvent: event, endEpochMs });
      }
      open[key] = null;
    };
    const closeCoolingIfNoActiveCoolingSource = (event) => {
      const hpCoolingActive = ["HP1", "HP2"].some((key) => open[key] && Number(open[key].startEvent?._oq_context_cm ?? open[key].startEvent?.cm) === 5);
      if (!hpCoolingActive) {
        closeInterval("cooling", event);
      }
    };

    events
      .filter((event) => event && !event._oq_hidden)
      .sort(compareDecisionEvents)
      .forEach((event) => {
        const eventType = String(event?.event_type || "");
        const contextCm = Number(event?._oq_context_cm ?? event?.cm);
        if (eventType === "boot_marker") {
          Object.keys(open).forEach((key) => closeInterval(key, event));
        } else if (eventType === "source_start") {
          sourceKeys(event.subject).forEach((key) => openInterval(key, event));
          if (contextCm === 5) {
            openInterval("cooling", event);
          }
        } else if (eventType === "source_stop") {
          sourceKeys(event.subject).forEach((key) => closeInterval(key, event));
          if (contextCm === 5 || open.cooling) {
            closeCoolingIfNoActiveCoolingSource(event);
          }
        } else if (eventType === "topology_change") {
          if (event.to === "duo") {
            openInterval("HP1", event);
            openInterval("HP2", event);
          } else if (event.to === "single") {
            const activeSource = getControlWorkingSingleTopologySource(event);
            if (activeSource) {
              openInterval(activeSource, event);
              closeInterval(activeSource === "HP1" ? "HP2" : "HP1", event);
            } else {
              closeInterval("HP2", event);
            }
            closeCoolingIfNoActiveCoolingSource(event);
          } else if (event.to === "idle") {
            closeInterval("HP1", event);
            closeInterval("HP2", event);
            closeInterval("cooling", event);
          }
        } else if (eventType === "boiler_assist_start"
          || eventType === "boiler_fallback_start") {
          openInterval("boiler", event);
        } else if (eventType === "boiler_assist_stop"
          || eventType === "boiler_fallback_stop") {
          closeInterval("boiler", event);
        } else if (eventType === "frost_protection_start") {
          openInterval("frost", event);
        } else if (eventType === "frost_protection_clear") {
          closeInterval("frost", event);
        } else if (eventType === "startup_inhibit_start") {
          openInterval("startupInhibit", event);
        } else if (eventType === "startup_inhibit_refresh") {
          closeInterval("startupInhibit", event);
          openInterval("startupInhibit", event);
        } else if (eventType === "startup_inhibit_clear") {
          closeInterval("startupInhibit", event);
        } else if (eventType === "flow_hold_clear" && event.reason === "flow_postflow") {
          closeInterval("cooling", event);
        }
      });

    Object.keys(open).forEach((key) => {
      if (open[key]) {
        const openEndEpochMs = selectedWindow === "today"
          ? Math.min(windowBounds.end, nowMs)
          : windowBounds.end;
        intervals[key].push({ ...open[key], endEvent: null, endEpochMs: openEndEpochMs, isOpen: true });
      }
    });

    const items = [];
    const addItem = (item) => {
      if (item) {
        items.push(item);
      }
    };
    const intervalsOverlap = (left, right) =>
      left.startEpochMs < right.endEpochMs && right.startEpochMs < left.endEpochMs;
    const getCoolingIntervalSource = (coolingInterval) => {
      const coolingSources = ["HP1", "HP2"].filter((key) =>
        intervals[key].some((interval) =>
          Number(interval.startEvent?._oq_context_cm ?? interval.startEvent?.cm) === 5 &&
          intervalsOverlap(interval, coolingInterval)));
      if (coolingSources.length === 2) {
        return getDecisionModeSubjectLabel("BOTH", 5);
      }
      if (coolingSources.length === 1) {
        return getDecisionModeSubjectLabel(coolingSources[0], 5);
      }
      return getDecisionModeSubjectLabel(coolingInterval.startEvent?.subject, 5);
    };

    intervals.startupInhibit.forEach((interval, index) => {
      const targetMode = Number(interval.startEvent?.value_a) || 0;
      const coolingWait = targetMode === 1;
      const contextRefreshed = String(interval.endEvent?.event_type || "") === "startup_inhibit_refresh";
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-startup-inhibit-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "normal",
        title: interval.isOpen ? "Warmtepomp wacht na herstart" : "Warmtepomp wachtte na herstart",
        summary: coolingWait
          ? "Er was koelvraag, maar de compressor bleef na de herstart nog kort uit."
          : "Er was warmtevraag, maar de compressor bleef na de herstart nog kort uit.",
        detail: "Na een reboot houdt de controller eenmaal de minimale uit-tijd aan. Zo kan een compressor niet te snel opnieuw starten wanneer de vorige stoptijd onbekend is.",
        next: interval.isOpen
          ? coolingWait
            ? "De warmtepomp start automatisch met koelen zodra de wachttijd voorbij is."
            : "De warmtepomp start automatisch met verwarmen zodra de wachttijd voorbij is."
          : contextRefreshed
          ? "De gewenste warmtepomp of doelmodus veranderde, maar de wachttijd bleef actief."
          : "Na deze periode ging de normale regeling automatisch verder.",
        source: getDecisionModeSubjectLabel(interval.startEvent?.subject, coolingWait ? 5 : 2),
        reasonCode: "startup_inhibit",
        reasonLabel: "Wachttijd na herstart",
        reasonSummary: "De compressor werd bewust nog niet gestart.",
        modeLabel: coolingWait ? "CM5" : "CM2",
        minDurationS: 1,
      }, selectedWindow, nowMs));
    });

    intervals.boiler.forEach((interval, index) => {
      const isFallback = String(interval.startEvent?.event_type || "") === "boiler_fallback_start";
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-boiler-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: isFallback ? "limited" : "normal",
        title: isFallback ? "CV-ketel nam verwarming tijdelijk over" : "CV-ketel ondersteunde tijdelijk",
        summary: isFallback
          ? "Geen warmtepomp was veilig inzetbaar; de CV-ketel verwarmde tijdelijk in CM4."
          : "De CV-ketel hielp tijdelijk mee toen extra vermogen nuttig was.",
        detail: isFallback
          ? "De foutfallback start pas na bevestigde HP-uitval, verse stopbevestiging en geldige installatiebeveiligingen."
          : "De warmtepompen blijven de basis leveren. De CV-ketel vult alleen aan zolang de vraag daar om vraagt.",
        next: isFallback
          ? "OpenQuatt stopt CM4 zodra een warmtepomp stabiel is hersteld of een veiligheidsvoorwaarde de fallback blokkeert."
          : "De CV-ketel stopt zodra de warmtepompen de vraag weer rustig zelf kunnen dragen.",
        source: "CV-ketel",
        reasonCode: isFallback ? "boiler_fallback" : "boiler_assist",
        modeLabel: isFallback ? "CM4" : "CM3",
        minDurationS: isFallback ? 1 : 120,
      }, selectedWindow, nowMs));
    });

    intervals.cooling.forEach((interval, index) => {
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-cooling-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "normal",
        title: "Koeling actief",
        summary: "Er was koelvraag en de warmtepomp koelde binnen de normale regeling.",
        detail: "Tijdens koelen bewaakt de controller continu waterflow, aanvoertemperatuur en dauwpuntmarge. Een tijdelijk softwaremaximum hoort bij die normale regeling.",
        next: "Koeling stopt zodra de koelvraag wegvalt of tijdelijk pauzeert als een veiligheidsmarge daarom vraagt.",
        source: getCoolingIntervalSource(interval),
        reasonCode: "keep_current",
        reasonLabel: "Koeling gestart",
        reasonSummary: "De koelrun is gestart en liep binnen de normale regeling.",
        modeLabel: "CM5",
        // An active run must be visible immediately; only completed micro-runs
        // are suppressed to keep historical timelines calm.
        minDurationS: interval.isOpen ? 1 : 120,
      }, selectedWindow, nowMs));
    });

    intervals.frost.forEach((interval, index) => {
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-frost-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "limited",
        title: "Vorstbescherming actief",
        summary: "Het systeem liet water circuleren om bevriezing te voorkomen.",
        detail: "Dit is beschermingsgedrag. Er hoeft geen verwarmings- of koelvraag te zijn.",
        next: "Vorstbescherming stopt zodra het risico weg is of de normale regeling weer voorrang krijgt.",
        source: "Systeem",
        reasonCode: "frost_protection",
        modeLabel: "CM98",
        minDurationS: 60,
      }, selectedWindow, nowMs));
    });

    intervals.HP1.forEach((hp1, index) => {
      intervals.HP2.forEach((hp2) => {
        const startEpochMs = Math.max(hp1.startEpochMs, hp2.startEpochMs);
        const endEpochMs = Math.min(hp1.endEpochMs, hp2.endEpochMs);
        const startEvent = hp1.startEpochMs >= hp2.startEpochMs ? hp1.startEvent : hp2.startEvent;
        const hp1ContextCm = Number(hp1.startEvent?._oq_context_cm ?? hp1.startEvent?.cm);
        const hp2ContextCm = Number(hp2.startEvent?._oq_context_cm ?? hp2.startEvent?.cm);
        const contextCm = Number(startEvent?._oq_context_cm ?? startEvent?.cm);
        if (contextCm === 5 || hp1ContextCm === 5 || hp2ContextCm === 5) {
          return;
        }
        const isOpen = Boolean(hp1.isOpen && hp2.isOpen);
        addItem(createControlWorkingDerivedSpan({
          id: `fw-span-duo-${index}-${hp1.startEvent?.seq || hp1.startEpochMs}-${hp2.startEvent?.seq || hp2.startEpochMs}`,
          startEpochMs,
          endEpochMs,
          isOpen,
          startEvent,
          severity: "normal",
          title: "Twee warmtepompen verwarmen",
          summary: "HP1 en HP2 draaiden tegelijk omdat extra capaciteit nuttig was.",
          detail: "De warmtepompen zijn gelijkwaardig. Twee bronnen verdelen de belasting wanneer één warmtepomp de vraag minder rustig kan dragen.",
          next: "Het systeem schakelt terug naar één warmtepomp zodra single-bedrijf weer voldoende of rustiger is.",
          source: getDecisionModeSubjectLabel("BOTH", 2),
          reasonCode: "better_heat",
          modeLabel: "CM2",
          minDurationS: 300,
        }, selectedWindow, nowMs));
      });
    });

    return items;
  }

  function enrichControlWorkingDecisionLogEvents(events) {
    const sorted = [...events].sort(compareDecisionEvents);
    const activeSourceCm = { HP1: 0, HP2: 0 };
    const defrostOpen = { HP1: false, HP2: false };
    let activeTopologyCm = 0;
    let activeFlowCm = 0;
    let previousModeCm = 0;
    let pendingCoolingStopReason = "";

    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const upcomingFlowContextCm = (index) => {
      const currentTime = getDecisionEventSortValue(sorted[index]);
      for (let offset = 1; offset <= 6 && index + offset < sorted.length; offset += 1) {
        const next = sorted[index + offset];
        const nextTime = getDecisionEventSortValue(next);
        if (Number.isFinite(currentTime) && Number.isFinite(nextTime) && nextTime - currentTime > 300) {
          break;
        }
        const nextType = String(next?.event_type || "");
        if (nextType === "flow_hold_clear" && Number(next?.value_a) === 5) {
          return 5;
        }
        if ((nextType === "source_start" || nextType === "topology_change" || nextType === "cooling_limited") && Number(next?.cm) === 5) {
          return 5;
        }
        if (nextType === "flow_hold_start") {
          break;
        }
      }
      return 0;
    };

    return sorted.map((event, index) => {
      const enriched = { ...event };
      const eventType = String(event?.event_type || "");
      const subject = String(event?.subject || "").toUpperCase();
      const reason = String(event?.reason || "");
      const cm = Number(event?.cm) || 0;
      if (eventType === "boot_marker") {
        activeSourceCm.HP1 = 0;
        activeSourceCm.HP2 = 0;
        defrostOpen.HP1 = false;
        defrostOpen.HP2 = false;
        activeTopologyCm = 0;
        activeFlowCm = 0;
        previousModeCm = 0;
        pendingCoolingStopReason = "";
      }
      let contextCm = cm;
      let hidden = false;
      let activeCoolingSource = "";
      let activeCoolingSubject = "";
      let coolingRuntimeHold = false;
      let activeHeatingSource = "";
      let heatingRuntimeHold = false;
      let coolingStopReason = "";
      const previousCm = previousModeCm;
      const coolingSources = () => ["HP1", "HP2"].filter((key) => activeSourceCm[key] === 5);
      const heatingSources = () => ["HP1", "HP2"].filter((key) => activeSourceCm[key] > 0 && activeSourceCm[key] !== 5);

      if (eventType === "source_start") {
        contextCm = cm || contextCm;
        sourceKeys(subject).forEach((key) => {
          activeSourceCm[key] = contextCm;
        });
      } else if (eventType === "source_stop") {
        const sourceCm = sourceKeys(subject).map((key) => activeSourceCm[key]).find((value) => value > 0);
        contextCm = sourceCm || contextCm;
        if (contextCm === 5 && pendingCoolingStopReason) {
          coolingStopReason = pendingCoolingStopReason;
          pendingCoolingStopReason = "";
        }
        sourceKeys(subject).forEach((key) => {
          activeSourceCm[key] = 0;
        });
      } else if (eventType === "topology_change") {
        if (event?.to === "idle") {
          contextCm = activeTopologyCm || contextCm;
          activeTopologyCm = 0;
        } else if (event?.to === "single" || event?.to === "duo") {
          contextCm = cm || activeTopologyCm || contextCm;
          activeTopologyCm = contextCm;
        }
      } else if (eventType === "flow_hold_start") {
        const activeCoolingSources = coolingSources();
        const activeHeatingSources = heatingSources();
        const nextAfterCm = Number(event?.value_a);
        contextCm = reason === "flow_postflow"
          ? activeTopologyCm || contextCm
          : nextAfterCm || upcomingFlowContextCm(index) || contextCm;
        if (reason === "flow_postflow" && contextCm === 5 && activeCoolingSources.length) {
          activeCoolingSource = activeCoolingSources.join(" + ");
          coolingRuntimeHold = true;
        }
        if (reason === "flow_postflow" && contextCm !== 5 && activeHeatingSources.length) {
          activeHeatingSource = activeHeatingSources.join(" + ");
          heatingRuntimeHold = true;
        }
        activeFlowCm = contextCm;
      } else if (eventType === "flow_hold_clear") {
        contextCm = Number(event?.value_a) || activeFlowCm || activeTopologyCm || contextCm;
        activeFlowCm = 0;
      } else if (eventType === "cooling_limited" || eventType === "cooling_released") {
        contextCm = 5;
        const activeCoolingSources = coolingSources();
        if (activeCoolingSources.length) {
          activeCoolingSource = activeCoolingSources.join(" + ");
          activeCoolingSubject = activeCoolingSources.length === 2 ? "BOTH" : activeCoolingSources[0];
        }
        if (eventType === "cooling_limited" && reason === "dew_stop") {
          pendingCoolingStopReason = "dew_stop";
        }
      }

      if (eventType === "defrost_seen_start" || eventType === "defrost_seen_clear") {
        const key = subject === "HP1" || subject === "HP2" ? subject : "HP1";
        if (contextCm === 5 || cm === 5) {
          hidden = true;
        } else if (eventType === "defrost_seen_start") {
          defrostOpen[key] = true;
        } else if (!defrostOpen[key]) {
          hidden = true;
        } else {
          defrostOpen[key] = false;
        }
      }

      enriched._oq_context_cm = contextCm;
      enriched._oq_hidden = hidden;
      enriched._oq_active_cooling_source = activeCoolingSource;
      enriched._oq_active_cooling_subject = activeCoolingSubject;
      enriched._oq_cooling_runtime_hold = coolingRuntimeHold;
      enriched._oq_active_heating_source = activeHeatingSource;
      enriched._oq_heating_runtime_hold = heatingRuntimeHold;
      enriched._oq_cooling_stop_reason = coolingStopReason;
      enriched._oq_previous_cm = previousCm;
      enriched._oq_mode_transition = deriveControlWorkingModeTransition(event, previousCm);
      const nextModeCm = getControlWorkingModeAfterEvent(event);
      if (Number.isFinite(nextModeCm)) {
        previousModeCm = nextModeCm;
      }
      return enriched;
    });
  }

  function getControlWorkingDecisionLogItems() {
    const events = getDecisionLogEvents();
    const selectedWindow = getControlWorkingSelectedWindow();
    const nowMs = Date.now();
    const enrichedEvents = enrichControlWorkingDecisionLogEvents(events);
    const eventItems = enrichedEvents
      .map((event) => mapDecisionEventToControlWorkingItem(event, selectedWindow, nowMs))
      .filter(Boolean);
    const derivedItems = buildControlWorkingDerivedItems(enrichedEvents, selectedWindow, nowMs);
    return [...eventItems, ...derivedItems]
      .sort((left, right) => {
        const startDelta = getControlWorkingItemMinuteRange(right).start - getControlWorkingItemMinuteRange(left).start;
        if (startDelta !== 0) {
          return startDelta;
        }
        const weights = { event: 0, span: 1, aggregate: 2 };
        return (weights[left.kind] ?? 3) - (weights[right.kind] ?? 3);
      });
  }

  function getControlWorkingItems(heatPumpPanels) {
    const decisionLogItems = getControlWorkingDecisionLogItems();
    if (decisionLogItems.length) {
      return decisionLogItems;
    }
    return [];
  }

  function getControlWorkingSelectedItem(items) {
    const visibleItems = items.filter((item) => !item.timelineHidden);
    if (visibleItems.some((item) => item.id === state.controlReplaySelectedEpisode)) {
      return visibleItems.find((item) => item.id === state.controlReplaySelectedEpisode);
    }
    return visibleItems.find((item) => item.kind === "span" && item.reasonCode === "better_heat")
      || visibleItems.find((item) => item.kind === "span")
      || visibleItems[0]
      || null;
  }

  function parseControlWorkingClockMinute(value) {
    const match = String(value || "").match(/(\d{1,2}):(\d{2})/);
    if (!match) {
      return Number.NaN;
    }
    const hours = Number.parseInt(match[1], 10);
    const minutes = Number.parseInt(match[2], 10);
    if (!Number.isFinite(hours) || !Number.isFinite(minutes)) {
      return Number.NaN;
    }
    return Math.max(0, Math.min(1440, (hours * 60) + minutes));
  }

  function getControlWorkingItemMinuteRange(item) {
    if (Number.isFinite(Number(item?.graphStart))) {
      const start = Math.max(0, Math.min(1440, Number(item.graphStart)));
      const end = Number.isFinite(Number(item?.graphEnd))
        ? Math.max(start, Math.min(1440, Number(item.graphEnd)))
        : start;
      return { start, end };
    }
    const matches = String(item?.time || "").match(/\d{1,2}:\d{2}/g) || [];
    const start = parseControlWorkingClockMinute(matches[0]);
    const end = parseControlWorkingClockMinute(matches[1]);
    if (!Number.isNaN(start) && !Number.isNaN(end)) {
      return { start, end: Math.max(start, end) };
    }
    if (!Number.isNaN(start)) {
      return { start, end: start };
    }
    return { start: 430, end: 430 };
  }

  function getControlWorkingGraphMinute() {
    const minute = Number(state.controlReplayGraphMinute);
    return Number.isFinite(minute) ? Math.max(0, Math.min(1440, Math.round(minute / 5) * 5)) : 430;
  }

  function formatControlWorkingRelativeOffset(minutesBeforeNow) {
    const normalized = Math.max(0, Math.round(Number(minutesBeforeNow) || 0));
    if (normalized <= 5) {
      return "Nu";
    }
    const days = Math.floor(normalized / 1440);
    const hours = Math.floor((normalized % 1440) / 60);
    const minutes = normalized % 60;
    if (days > 0) {
      return hours > 0 ? `${days}d ${hours}u geleden` : `${days}d geleden`;
    }
    if (hours > 0) {
      return minutes > 0 ? `${hours}u ${minutes}m geleden` : `${hours}u geleden`;
    }
    return `${minutes}m geleden`;
  }

  function formatControlWorkingGraphCursorLabel(minute, windowModel = getControlWorkingWindowModel()) {
    const normalized = Math.max(0, Math.min(1440, Number(minute) || 0));
    if (windowModel.calendarDay === "today") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, "today"),
        Date.now(),
        "time",
      );
    }
    if (windowModel.calendarDay === "yesterday") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, "yesterday"),
        Date.now(),
        "time",
      );
    }
    if (windowModel.id === "week" || windowModel.id === "last48" || windowModel.id === "last3d" || windowModel.id === "custom") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, windowModel.id),
        Date.now(),
        "weekday",
      );
    }
    return formatControlWorkingAbsoluteTimeLabel(
      getControlWorkingWindowEpochForMinute(normalized, windowModel.id),
      Date.now(),
      "auto",
    );
  }

  function getControlWorkingItemForMinute(items, minute) {
    const normalizedMinute = Math.max(0, Math.min(1440, Number(minute) || 0));
    const weights = { span: 0, aggregate: 1, event: 2 };
    const selectedItem = items
      .filter((item) => !item.timelineHidden)
      .map((item) => {
        const range = getControlWorkingGraphHitRange(item);
        if (normalizedMinute < range.start || normalizedMinute > range.end) {
          return null;
        }
        const span = Math.max(1, range.end - range.start);
        return { item, score: span + ((weights[item.kind] ?? 3) * 0.1) };
      })
      .filter(Boolean)
      .sort((a, b) => a.score - b.score)[0]?.item || null;
    return selectedItem || getControlWorkingActiveGraphContextForMinute(items, normalizedMinute);
  }

  function getControlWorkingEventDurationChartMinutes(event, selectedWindow = getControlWorkingSelectedWindow()) {
    const durationS = Number(event?.duration_s);
    if (!Number.isFinite(durationS) || durationS <= 0) {
      return 0;
    }
    return (durationS / 60) * (1440 / getControlWorkingWindowDurationMinutes(selectedWindow));
  }

  function getControlWorkingGraphHitRange(item) {
    const range = getControlWorkingItemMinuteRange(item);
    const eventType = String(item?.realEventType || "");
    const durationMinutes = getControlWorkingEventDurationChartMinutes(item?.rawDecisionEvent);
    if (eventType === "defrost_seen_clear" && durationMinutes > 0) {
      const width = Math.max(5, durationMinutes);
      return { start: Math.max(0, range.start - width), end: range.start };
    }
    if ((eventType === "flow_hold_clear" || eventType === "frost_protection_clear") && durationMinutes > 0) {
      const width = Math.max(1, durationMinutes);
      return { start: Math.max(0, range.start - width), end: range.start };
    }
    if (range.end > range.start) {
      return range;
    }
    if (item?.kind === "event") {
      return { start: range.start, end: Math.min(1440, range.start + 12) };
    }
    return range;
  }

  function getControlWorkingIntervalTimeLabel(startMinute, endMinute, isOpen = false) {
    const windowModel = getControlWorkingWindowModel();
    const start = formatControlWorkingGraphCursorLabel(startMinute, windowModel);
    const end = isOpen || endMinute >= 1440
      ? "nu"
      : formatControlWorkingGraphCursorLabel(endMinute, windowModel);
    return `${start}-${end}`;
  }

  function getControlWorkingOpenEndMinute(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    if (selectedWindow !== "today") {
      return 1440;
    }
    const now = new Date(nowMs);
    return Math.max(0, Math.min(1440, Math.round((now.getHours() * 60) + now.getMinutes() + (now.getSeconds() / 60))));
  }

  function getControlWorkingActiveGraphContextForMinute(items, minute) {
    const intervals = [];
    const open = new Map();
    const sortedItems = [...items]
      .filter((item) => item.rawDecisionEvent)
      .sort((left, right) => getControlWorkingItemMinuteRange(left).start - getControlWorkingItemMinuteRange(right).start);
    const openInterval = (label, item, startMinute) => {
      if (!open.has(label)) {
        open.set(label, { label, item, start: startMinute });
      }
    };
    const closeInterval = (label, endMinute) => {
      const active = open.get(label);
      if (!active) {
        return;
      }
      intervals.push({ ...active, end: Math.max(active.start, endMinute) });
      open.delete(label);
    };
    const closeCoolingIfNoHeatPumpSource = (endMinute) => {
      if (open.has("Koeling") && !open.has("HP1") && !open.has("HP2")) {
        closeInterval("Koeling", endMinute);
      }
    };
    const sourceLabels = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      const labels = [];
      if (normalized === "HP1" || normalized === "BOTH") {
        labels.push("HP1");
      }
      if (normalized === "HP2" || normalized === "BOTH") {
        labels.push("HP2");
      }
      return labels;
    };

    const activeAtWindowStart = getControlWorkingChartSourceStateAtWindowStart();
    const windowStartItem = {
      reasonCode: "keep_current",
      severity: "normal",
      modeLabel: activeAtWindowStart.sourceModes.HP1 || activeAtWindowStart.sourceModes.HP2
        ? `CM${activeAtWindowStart.sourceModes.HP1 || activeAtWindowStart.sourceModes.HP2}`
        : "CM?",
    };
    if (activeAtWindowStart.HP1) {
      openInterval("HP1", windowStartItem, 0);
    }
    if (activeAtWindowStart.HP2) {
      openInterval("HP2", windowStartItem, 0);
    }
    if (activeAtWindowStart.boiler) {
      openInterval("CV-ketel", windowStartItem, 0);
    }
    if (activeAtWindowStart.cooling) {
      openInterval("Koeling", windowStartItem, 0);
    }

    sortedItems.forEach((item) => {
      const range = getControlWorkingItemMinuteRange(item);
      const eventType = String(item.realEventType || "");
      const event = item.rawDecisionEvent || {};
      const contextCm = Number(event._oq_context_cm ?? event.cm);
      const labels = sourceLabels(event.subject);
      if (eventType === "source_start") {
        labels.forEach((label) => openInterval(label, item, range.start));
        if (contextCm === 5) {
          openInterval("Koeling", item, range.start);
        }
      } else if (eventType === "source_stop") {
        labels.forEach((label) => closeInterval(label, range.start));
        if (contextCm === 5 || open.has("Koeling")) {
          closeCoolingIfNoHeatPumpSource(range.start);
        }
      } else if (eventType === "topology_change") {
        if (event.to === "duo") {
          openInterval("HP1", item, range.start);
          openInterval("HP2", item, range.start);
        } else if (event.to === "single") {
          const activeSource = getControlWorkingSingleTopologySource(event);
          if (activeSource) {
            openInterval(activeSource, item, range.start);
            closeInterval(activeSource === "HP1" ? "HP2" : "HP1", range.start);
          } else {
            closeInterval("HP2", range.start);
          }
          closeCoolingIfNoHeatPumpSource(range.start);
        } else if (event.to === "idle") {
          closeInterval("HP1", range.start);
          closeInterval("HP2", range.start);
          closeInterval("Koeling", range.start);
        }
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        openInterval("CV-ketel", item, range.start);
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        closeInterval("CV-ketel", range.start);
      } else if (eventType === "flow_hold_clear" && event.reason === "flow_postflow") {
        closeInterval("Koeling", range.start);
      }
    });
    const openEndMinute = getControlWorkingOpenEndMinute();
    open.forEach((active) => {
      if (active.start <= openEndMinute) {
        intervals.push({ ...active, end: openEndMinute });
      }
    });

    const activeIntervals = intervals.filter((interval) => minute >= interval.start && minute <= interval.end);
    if (!activeIntervals.length) {
      return null;
    }
    const labels = new Set(activeIntervals.map((interval) => interval.label));
    const hpLabels = ["HP1", "HP2"].filter((label) => labels.has(label));
    const cvActive = labels.has("CV-ketel");
    const coolingActive = labels.has("Koeling");
    const primaryInterval = activeIntervals
      .filter((interval) => hpLabels.includes(interval.label) || interval.label === "CV-ketel" || interval.label === "Koeling")
      .sort((left, right) => left.start - right.start)[0] || activeIntervals[0];
    const startMinute = Math.max(...activeIntervals.map((interval) => interval.start));
    const endMinute = Math.min(...activeIntervals.map((interval) => interval.end));
    let source = [
      ...hpLabels,
      cvActive ? "CV-ketel" : "",
      coolingActive ? "Koeling" : "",
    ].filter(Boolean).join(" + ");
    let title = "Bron actief";
    let summary = "Deze bron was op dit tijdstip actief.";
    let detail = "De grafiek toont hier een lopende periode. De start of stop staat als los beslismoment in de tijdlijn.";
    let next = "De controller blijft opnieuw beoordelen of deze bron nodig blijft.";
    let reasonCode = primaryInterval.item?.reasonCode || "keep_current";
    let severity = "normal";

    if (coolingActive) {
      title = "Koeling actief";
      summary = hpLabels.length
        ? `${hpLabels.join(" en ")} koelde${hpLabels.length === 1 ? "" : "n"} op dit tijdstip binnen de normale regeling.`
        : "De koeling was op dit tijdstip actief.";
      detail = "De controller bewaakt daarbij waterflow, aanvoertemperatuur en dauwpuntmarge. Een tijdelijk softwaremaximum hoort bij de normale regeling.";
      next = "Koeling gaat door zolang er koelvraag is en de veiligheidsmarges vrij blijven.";
      source = hpLabels.length === 2
        ? getDecisionModeSubjectLabel("BOTH", 5)
        : hpLabels.length === 1
        ? getDecisionModeSubjectLabel(hpLabels[0], 5)
        : "Koeling";
      reasonCode = primaryInterval.item?.reasonCode || "keep_current";
      severity = primaryInterval.item?.severity || "normal";
    } else if (hpLabels.length === 2 && cvActive) {
      title = "Warmtepompen en CV-ketel actief";
      summary = "Beide warmtepompen draaiden en de CV-ketel ondersteunde tijdelijk.";
      detail = "De warmtepompen leverden de basis. De CV-ketel vulde alleen aan zolang extra vermogen nodig was.";
      next = "CV-ondersteuning stopt zodra de warmtepompen de vraag weer zelf rustig kunnen dragen.";
      reasonCode = "boiler_assist";
      severity = "limited";
    } else if (hpLabels.length === 2) {
      title = "Twee warmtepompen verwarmen";
      summary = "HP1 en HP2 verwarmden tegelijk op dit tijdstip.";
      detail = "Twee gelijkwaardige warmtepompen kunnen hoge vraag rustiger leveren dan één warmtepomp op hoge belasting.";
      next = "Eén warmtepomp stopt zodra single-bedrijf weer voldoende of rustiger is.";
      source = getDecisionModeSubjectLabel("BOTH", 2);
      reasonCode = "better_heat";
    } else if (hpLabels.length === 1 && cvActive) {
      title = `${hpLabels[0]} en CV-ketel actief`;
      summary = "De warmtepomp draaide en de CV-ketel ondersteunde tijdelijk.";
      detail = "De CV-ketel vult alleen aan wanneer de warmtepomp de actuele vraag niet rustig genoeg kan dragen.";
      next = "De CV-ketel stopt zodra aanvullende ondersteuning niet meer nodig is.";
      reasonCode = "boiler_assist";
      severity = "limited";
    } else if (hpLabels.length === 1) {
      title = `${hpLabels[0]} verwarmt`;
      summary = `${hpLabels[0]} leverde op dit tijdstip warmte.`;
      detail = "De andere warmtepomp blijft beschikbaar. De controller schakelt pas bij of wisselt pas wanneer dat rustiger of nuttiger is.";
      next = "Bij stijgende vraag kan een tweede warmtepomp bijschakelen; bij dalende vraag stopt deze bron.";
      source = getDecisionModeSubjectLabel(hpLabels[0], 2);
      reasonCode = primaryInterval.item?.reasonCode || "runtime_lead";
    } else if (cvActive) {
      title = "CV-ketel ondersteunt";
      summary = "De CV-ketel leverde op dit tijdstip extra vermogen.";
      detail = "CV-ondersteuning is aanvullend op de warmtepompen en blijft tijdelijk.";
      next = "De CV-ketel stopt zodra de extra capaciteit niet meer nodig is.";
      reasonCode = "boiler_assist";
      severity = "limited";
    }

    return {
      id: `graph-context-${Math.round(minute)}-${Array.from(labels).join("-")}`,
      kind: "span",
      severity,
      time: getControlWorkingIntervalTimeLabel(startMinute, endMinute),
      duration: "",
      title,
      summary,
      detailTitle: "Wat gebeurt hier?",
      detail,
      next,
      source: source || "Systeem",
      reasonCode,
      modeLabel: primaryInterval.item?.modeLabel || "CM?",
      graphStart: startMinute,
      graphEnd: endMinute,
    };
  }

  function renderControlWorkingTabs() {
    const selectedTab = getControlWorkingSelectedTab();
    return `
      <div class="oq-working-control-group">
        <span class="oq-working-control-label">Weergave</span>
        <div class="oq-working-tabs" role="tablist" aria-label="Beslislog weergave">
          ${getControlWorkingTabs().map((tab) => `
            <button
              class="oq-working-tab${selectedTab === tab.id ? " is-active" : ""}"
              type="button"
              role="tab"
              aria-selected="${selectedTab === tab.id ? "true" : "false"}"
              data-oq-action="select-control-replay-tab"
              data-replay-tab="${escapeHtml(tab.id)}"
            >
              ${renderOqIcon(tab.icon, "oq-working-tab-icon")}
              <span>${escapeHtml(tab.label)}</span>
            </button>
          `).join("")}
        </div>
      </div>
    `;
  }

  function renderControlWorkingWindowChoices() {
    const selectedWindow = getControlWorkingSelectedWindow();
    const selectedModel = getControlWorkingWindowModel();
    const quickOptions = getControlWorkingQuickWindowOptions();
    const moreOptions = getControlWorkingWindowOptions().filter((option) => !option.quick && !option.custom);
    const customDraft = getControlWorkingCustomDraft();
    const customInputBounds = getControlWorkingCustomInputBounds(customDraft);
    const customStart = getControlWorkingCustomDateTimeParts(customDraft.start);
    const customEnd = getControlWorkingCustomDateTimeParts(customDraft.end);
    const menuOpen = state.controlReplayPeriodMenuOpen;
    const menuLabel = selectedWindow === "custom"
      ? "Eigen periode"
      : quickOptions.some((option) => option.id === selectedWindow)
      ? "Kies periode"
      : selectedModel.shortLabel;
    return `
      <div class="oq-working-control-group oq-working-control-group--period">
        <span class="oq-working-control-label">Periode</span>
        <div class="oq-working-window-controls" role="group" aria-label="Periode">
          <div class="oq-working-window-choices" aria-label="Snelle periodekeuzes">
          ${quickOptions.map((option) => `
            <button
              class="oq-working-window-choice${selectedWindow === option.id ? " is-active" : ""}"
              type="button"
              data-oq-action="select-control-replay-window"
              data-replay-window="${escapeHtml(option.id)}"
              aria-pressed="${selectedWindow === option.id ? "true" : "false"}"
              aria-label="${escapeHtml(option.label)}"
            >
              ${escapeHtml(option.shortLabel)}
            </button>
          `).join("")}
          </div>
          <div class="oq-working-period-menu" data-oq-control-replay-period-menu>
            <button
              class="oq-working-period-menu-toggle${menuOpen || !quickOptions.some((option) => option.id === selectedWindow) ? " is-active" : ""}"
              type="button"
              aria-expanded="${menuOpen ? "true" : "false"}"
              aria-haspopup="dialog"
              data-oq-action="toggle-control-replay-period-menu"
            >
              <span>${escapeHtml(menuLabel)}</span>
              <span class="oq-working-period-menu-chevron" aria-hidden="true"></span>
            </button>
            ${menuOpen ? `
              <section class="oq-working-period-popover" role="dialog" aria-label="Kies periode">
                <div class="oq-working-period-popover-head">
                  <strong>Ander tijdvenster</strong>
                </div>
                <div class="oq-working-period-option-grid">
                  ${moreOptions.map((option) => `
                    <button
                      class="oq-working-period-option${selectedWindow === option.id ? " is-active" : ""}"
                      type="button"
                      data-oq-action="select-control-replay-window"
                      data-replay-window="${escapeHtml(option.id)}"
                      aria-pressed="${selectedWindow === option.id ? "true" : "false"}"
                    >${escapeHtml(option.shortLabel)}</button>
                  `).join("")}
                </div>
                <div class="oq-working-period-custom">
                  <button
                    class="oq-working-period-custom-toggle${state.controlReplayCustomPeriodOpen || selectedWindow === "custom" ? " is-active" : ""}"
                    type="button"
                    aria-expanded="${state.controlReplayCustomPeriodOpen ? "true" : "false"}"
                    data-oq-action="toggle-control-replay-custom-period"
                  >
                    <span>Eigen periode</span>
                    <span class="oq-working-period-custom-toggle-copy">Datum en uur</span>
                  </button>
                  ${state.controlReplayCustomPeriodOpen ? `
                    <div class="oq-working-period-custom-fields">
                      <label>
                        <span>Van</span>
                        <div class="oq-working-period-date-hour">
                          <input type="date" min="${escapeHtml(customInputBounds.earliestDate)}" max="${escapeHtml(customInputBounds.startMaxDate)}" value="${escapeHtml(customStart.date)}" data-oq-control-replay-custom-start-date data-oq-control-replay-custom-input>
                          <select aria-label="Uur van" data-oq-control-replay-custom-start-hour data-oq-control-replay-custom-input>
                            ${renderControlWorkingHourOptions(customStart.hour)}
                          </select>
                        </div>
                      </label>
                      <label>
                        <span>Tot</span>
                        <div class="oq-working-period-date-hour">
                          <input type="date" min="${escapeHtml(customInputBounds.endMinDate)}" max="${escapeHtml(customInputBounds.endMaxDate)}" value="${escapeHtml(customEnd.date)}" data-oq-control-replay-custom-end-date data-oq-control-replay-custom-input>
                          <select aria-label="Uur tot" data-oq-control-replay-custom-end-hour data-oq-control-replay-custom-input>
                            ${renderControlWorkingHourOptions(customEnd.hour)}
                          </select>
                        </div>
                      </label>
                    </div>
                    <div class="oq-working-period-custom-actions">
                      <span>Maximaal 7 dagen</span>
                      <button class="oq-working-period-apply" type="button" data-oq-action="apply-control-replay-custom-period">Toepassen</button>
                    </div>
                    ${state.controlReplayCustomPeriodError ? `<p class="oq-working-period-error" role="alert">${escapeHtml(state.controlReplayCustomPeriodError)}</p>` : ""}
                  ` : ""}
                </div>
              </section>
            ` : ""}
          </div>
        </div>
      </div>
    `;
  }

  function renderControlWorkingNowCard(current) {
    const status = getControlWorkingSeverityMeta(current.severity);
    return `
      <section class="oq-working-now oq-working-now--${escapeHtml(status.tone)}">
        <div class="oq-working-now-main">
          <span class="oq-working-eyebrow">Actuele situatie</span>
          <h2>${escapeHtml(current.title)}${renderControlWorkingModeBadge(current)}</h2>
          <p>${escapeHtml(current.copy)}</p>
          <div class="oq-working-pill-row">
            ${renderControlWorkingPill(status.label, status.tone, "shield")}
            ${renderControlWorkingPill(current.reasonLabel, "info", "target")}
            ${renderControlWorkingPill(current.sinceLabel, "context")}
          </div>
        </div>
        <div class="oq-working-now-next">
          <span>Wat doet het systeem daarna?</span>
          <strong>${escapeHtml(current.expectation)}</strong>
          <div class="oq-working-source-strip">
            <span>HP1 · ${escapeHtml(current.hp1Status)}</span>
            <span>HP2 · ${escapeHtml(current.hp2Status)}</span>
            <span>CV · ${escapeHtml(current.cvStatus)}</span>
          </div>
        </div>
      </section>
    `;
  }

  function renderControlWorkingTimelineItem(item, selectedItem) {
    const status = getControlWorkingSeverityMeta(item.severity);
    const selected = selectedItem && selectedItem.id === item.id;
    const kindLabel = getControlWorkingKindLabel(item.kind);
    const modeMetaLabel = getControlWorkingModeMetaLabel(item);
    return `
      <button
        class="oq-working-entry oq-working-entry--${escapeHtml(item.kind)} oq-working-entry--${escapeHtml(status.tone)}${selected ? " is-active" : ""}"
        type="button"
        data-oq-action="select-control-replay-episode"
        data-replay-episode="${escapeHtml(item.id)}"
      >
        <span class="oq-working-entry-time">
          <strong>${escapeHtml(item.time)}</strong>
          <small>${escapeHtml(kindLabel)}</small>
        </span>
        <span class="oq-working-entry-rail" aria-hidden="true"></span>
        <span class="oq-working-entry-body">
          <span class="oq-working-entry-title">
            <strong>${escapeHtml(item.title)}</strong>
            ${renderControlWorkingModeBadge(item)}
            ${item.count ? `<em>${escapeHtml(item.count)}</em>` : ""}
          </span>
          <span class="oq-working-entry-summary">${escapeHtml(item.summary)}</span>
          <span class="oq-working-entry-meta">
            <span>${escapeHtml(item.source)}</span>
            ${modeMetaLabel ? `<span class="oq-working-entry-meta-mode">${escapeHtml(modeMetaLabel)}</span>` : ""}
            <span>${escapeHtml(item.reasonLabel || getControlWorkingReasonLabel(item.reasonCode))}</span>
            ${item.duration ? `<span>Duur: ${escapeHtml(item.duration)}</span>` : ""}
          </span>
        </span>
        <span class="oq-working-entry-status">${escapeHtml(status.label)}</span>
      </button>
    `;
  }

  function renderControlWorkingDetails(item) {
    if (!item) {
      return "";
    }
    const status = getControlWorkingSeverityMeta(item.severity);
    const reason = getControlWorkingReasonMeta(item.reasonCode);
    const reasonLabel = item.reasonLabel || reason.label;
    const reasonSummary = item.reasonSummary || reason.summary;
    const optimizer = getControlWorkingOptimizerModel(item);
    const modeMetaLabel = getControlWorkingModeMetaLabel(item);
    const checks = Array.isArray(item.checks) ? item.checks : reason.checks;
    return `
      <aside class="oq-working-detail oq-working-detail--${escapeHtml(status.tone)}">
        <div>
          <span class="oq-working-eyebrow">Geselecteerd</span>
          <h3>${escapeHtml(item.title)}${renderControlWorkingModeBadge(item)}</h3>
          <p>${escapeHtml(item.summary)}</p>
        </div>
        <div class="oq-working-detail-block">
          <strong>Waarom?</strong>
          <span>${escapeHtml(item.detail)}</span>
        </div>
        <div class="oq-working-detail-block">
          <strong>Is dit normaal?</strong>
          <span>${escapeHtml(reasonSummary)}</span>
        </div>
        <div class="oq-working-detail-block">
          <strong>Wat gebeurt daarna?</strong>
          <span>${escapeHtml(item.next)}</span>
        </div>
        ${renderControlWorkingOptimizer(optimizer)}
        ${checks.length ? `
          <div class="oq-working-checks" aria-label="Beslisfactoren">
            ${checks.map((check) => `<span>${renderOqIcon("shield", "oq-working-reason-icon")} ${escapeHtml(check)}</span>`).join("")}
          </div>
        ` : ""}
        <div class="oq-working-pill-row">
          ${renderControlWorkingPill(status.label, status.tone, "shield")}
          ${renderControlWorkingPill(reasonLabel, "info", "target")}
          ${renderControlWorkingPill(item.source, "context")}
        </div>
        <details class="oq-working-support" data-replay-support-item="${escapeHtml(item.id)}"${state.controlReplaySupportDetailsItemId === item.id ? " open" : ""}>
          <summary data-oq-action="toggle-control-replay-support-details">Details voor support</summary>
          <dl>
            <div><dt>Record</dt><dd>${escapeHtml(getControlWorkingKindLabel(item.kind))}</dd></div>
            <div><dt>Bron</dt><dd>${escapeHtml(item.source)}</dd></div>
            <div><dt>Control mode</dt><dd>${escapeHtml(item.modeLabel)}</dd></div>
            ${modeMetaLabel ? `<div><dt>CM wijziging</dt><dd>${escapeHtml(modeMetaLabel)}</dd></div>` : ""}
            <div><dt>Reason code</dt><dd>${escapeHtml(item.reasonCode)}</dd></div>
          </dl>
        </details>
      </aside>
    `;
  }

  function renderControlWorkingGraphEmptyDetails(timeLabel) {
    return `
      <aside class="oq-working-detail">
        <div>
          <span class="oq-working-eyebrow">Tussen beslismomenten</span>
          <h3>Geen nieuw beslismoment om ${escapeHtml(timeLabel)}</h3>
          <p>Op dit moment veranderde de controller niets. De laatst gekozen situatie blijft gelden.</p>
        </div>
        <div class="oq-working-detail-block">
          <strong>Wat betekent dit?</strong>
          <span>In deze grafiek worden alleen controllerkeuzes, bescherming en bronwissels toegelicht. Tussen die momenten blijft de laatste keuze gewoon gelden.</span>
        </div>
      </aside>
    `;
  }

  function renderControlWorkingEmptyState(title, copy) {
    return `
      <div class="oq-working-empty">
        <strong>${escapeHtml(title)}</strong>
        <span>${escapeHtml(copy)}</span>
      </div>
    `;
  }

  function renderControlWorkingTimelineTab(items, selectedItem) {
    const windowModel = getControlWorkingWindowModel();
    const visibleItems = items.filter((item) => !item.timelineHidden);
    const timelineItems = visibleItems.slice(0, 80);
    const decisionLogError = String(state.decisionLogError || "").trim();
    const waitingForDecisionLog = !timelineItems.length && !state.decisionLog && !decisionLogError;
    return `
      <div class="oq-working-split">
        <section class="oq-working-list" aria-label="${escapeHtml(windowModel.eyebrow)}">
          <div class="oq-working-list-head">
            <div>
              <span class="oq-working-eyebrow">${escapeHtml(windowModel.eyebrow)}</span>
              <h3>${escapeHtml(windowModel.title)}</h3>
            </div>
            <p>${escapeHtml(windowModel.copy)}</p>
          </div>
          ${timelineItems.length
            ? `<div class="oq-working-timeline">
                ${timelineItems.map((item) => renderControlWorkingTimelineItem(item, selectedItem)).join("")}
              </div>`
            : decisionLogError
            ? renderControlWorkingEmptyState("Beslislog niet beschikbaar", `De firmwarelog kon niet worden geladen (${decisionLogError}). Dit betekent niet dat deze periode leeg is.`)
            : waitingForDecisionLog
            ? renderControlWorkingEmptyState("Beslislog laden", "De controllerkeuzes worden opgehaald. Dit duurt meestal maar heel kort.")
            : renderControlWorkingEmptyState("Nog geen gebeurtenissen", "De beslislog is leeg voor deze periode. Nieuwe controllerkeuzes verschijnen hier zodra de firmware ze vastlegt.")}
        </section>
        ${selectedItem ? renderControlWorkingDetails(selectedItem) : ""}
      </div>
    `;
  }

  function renderControlWorkingSourceCard(title, status, starts, hours, active, note = "") {
    return `
      <article class="oq-working-source-card${active ? " is-active" : ""}">
        <div>
          <span>${escapeHtml(title)}</span>
          <strong>${escapeHtml(status)}</strong>
        </div>
        ${note ? `<p class="oq-working-source-card-note">${escapeHtml(note)}</p>` : `<dl>
          <div><dt>Starts 24u</dt><dd>${escapeHtml(starts)}</dd></div>
          <div><dt>Draaiuren</dt><dd>${escapeHtml(hours)}</dd></div>
        </dl>`}
      </article>
    `;
  }

  function renderControlWorkingStatusTab(current) {
    const reason = getControlWorkingReasonMeta(current.primaryReason);
    const optimizer = getControlWorkingOptimizerModel({
      primaryReason: current.primaryReason,
      source: current.hp1Running && current.hp2Running ? "HP1 + HP2" : current.hp1Running ? "HP1" : current.hp2Running ? "HP2" : "Geen bron",
    });
    const isCoolingGuard = Boolean(current.coolingProtection);
    const isCoolingCap = Boolean(current.coolingCapped);
    const isCoolingRestartWait = current.primaryReason === "restart_wait";
    const isCoolingWaterSatisfied = current.primaryReason === "buffer_stop";
    const isStartupInhibit = current.primaryReason === "startup_inhibit";
    const isSticky = current.primaryReason === "sticky_protection";
    const guardEyebrow = isStartupInhibit ? "Startvoorwaarde" : isCoolingWaterSatisfied ? "Koelregeling" : "Bescherming";
    const guardTitle = isStartupInhibit
      ? "Wacht na herstart"
      : isCoolingWaterSatisfied
      ? "Water al koud genoeg"
      : isCoolingGuard
      ? isCoolingRestartWait ? "Wacht op veilige herstart" : "Koeling tijdelijk beperkt"
      : isCoolingCap
      ? "Koeling met ingesteld maximum"
      : isSticky
      ? "Geen comfortvraag actief"
      : "Geen beperking actief";
    const guardCopy = isStartupInhibit
      ? "Na een reboot blijft de compressor eenmaal de minimale uit-tijd uit. Bij aanhoudende vraag start de gekozen warmtepomp daarna automatisch."
      : isCoolingWaterSatisfied
      ? "Dit is normale regeling. De koelvraag blijft actief, maar de warmtepomp hoeft nu geen extra koude aan het water toe te voegen."
      : isCoolingGuard
      ? isCoolingRestartWait
        ? "De koelvraag blijft aanwezig. De warmtepomp start opnieuw zodra de veilige marge voldoende is hersteld."
        : "De aanvoer blijft boven de veilige grens. Daarom koelt het systeem tijdelijk minder hard."
      : isCoolingCap
      ? "Dit is normale koeling binnen de ingestelde softwaregrens. Dauwpunt en waterflow blijven wel gewoon bewaakt."
      : isSticky
      ? "Alleen de pomp draait kort. De warmtepompen blijven uit en er worden geen compressorstarts geteld."
      : "Ontdooien, minimum rusttijd, dauwpunt en waterflow blijven bewaakt. Ze verschijnen hier zodra ze gedrag begrenzen.";
    const guardPills = isStartupInhibit
      ? [
        ["Vraag actief", "info", "activity"],
        [current.startupInhibit?.remainingLabel || "Wachttijd actief", "normal", "clock"],
        ["Automatische start", "context", "play"],
      ]
      : isCoolingWaterSatisfied
      ? [
        ["Koelvraag actief", "info", "snowflake"],
        ["Water koud genoeg", "normal", "droplet"],
        ["Automatische herstart", "context", "activity"],
      ]
      : isCoolingGuard
      ? [
        ["Dauwpunt bewaakt", "limited", "droplet"],
        [`Max. niveau ${current.cooling.allowedMax}`, "info", "target"],
        [`Nu niveau ${current.cooling.limitedDemand}`, "context", "bar-chart"],
      ]
      : isCoolingCap
      ? [
        [`Ingesteld max. ${current.cooling.allowedMax}`, "info", "target"],
        [`Nu niveau ${current.cooling.limitedDemand}`, "normal", "bar-chart"],
        ["Marge bewaakt", "context", "shield"],
      ]
      : isSticky
      ? [
        ["Korte pomprun", "normal", "shield"],
        ["Geen koelvraag", "context", "snowflake"],
        ["Geen warmtepompstart", "info", "activity"],
      ]
      : [
        ["Ontdooien vrij", "normal", "snowflake"],
        ["Rusttijd vrij", "normal", "activity"],
        ["Waterflow bewaakt", "info", "waves"],
      ];
    const coolingContextActive = current.cooling.requestActive || isCoolingGuard || isCoolingCap || current.strategyLabel === "Koeling";
    const telemetryRows = [
      ["Aanvoer", current.supplyTemp],
      ["Buiten", current.outsideTemp],
      ["Flow", current.flow],
    ];
    if (!coolingContextActive) {
      telemetryRows.push(["Strategie", current.strategyLabel]);
    }
    if (coolingContextActive) {
      telemetryRows.push(["Dauwpunt", current.cooling.dewPoint]);
      telemetryRows.push(["Veilige min.", current.cooling.safeSupply]);
    }
    return `
      <div class="oq-working-status">
        ${renderControlWorkingNowCard(current)}
        <div class="oq-working-status-grid">
          <section class="oq-working-status-main${optimizer ? "" : " oq-working-status-main--wide"}">
            <span class="oq-working-eyebrow">Waarom deze keuze?</span>
            <h3>${escapeHtml(reason.label)}</h3>
            <p>${escapeHtml(reason.summary)}</p>
            <div class="oq-working-reason-list">
              ${reason.checks.map((check) => `<span>${renderOqIcon("target", "oq-working-reason-icon")} ${escapeHtml(check)}</span>`).join("")}
            </div>
          </section>
          ${optimizer ? `
            <section class="oq-working-optimizer-panel">
              ${renderControlWorkingOptimizer(optimizer)}
            </section>
          ` : ""}
          <section class="oq-working-source-grid" aria-label="Bronnen">
            ${renderControlWorkingSourceCard("HP1", current.hp1Status, current.hp1Starts, current.hp1Hours, current.hp1Running)}
            ${renderControlWorkingSourceCard("HP2", current.hp2Status, current.hp2Starts, current.hp2Hours, current.hp2Running)}
            ${renderControlWorkingSourceCard("CV", current.cvStatus, "", "", current.cvStatus === "Actief", coolingContextActive ? "Geen rol bij koelen." : "Tijdelijke ondersteuning bij extra warmtevraag.")}
          </section>
          <section class="oq-working-guard-panel">
            <span class="oq-working-eyebrow">${escapeHtml(guardEyebrow)}</span>
            <h3>${escapeHtml(guardTitle)}</h3>
            <p>${escapeHtml(guardCopy)}</p>
            <div class="oq-working-pill-row">
              ${guardPills.map(([label, tone, icon]) => renderControlWorkingPill(label, tone, icon)).join("")}
            </div>
          </section>
          <section class="oq-working-telemetry">
            <span class="oq-working-eyebrow">Context</span>
            <dl>
              ${telemetryRows.map(([label, value]) => `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd></div>`).join("")}
            </dl>
          </section>
        </div>
      </div>
    `;
  }

  function renderControlWorkingChartLane(label, tone, segments) {
    return `
      <div class="oq-working-chart-lane">
        <span>${escapeHtml(label)}</span>
        <div class="oq-working-chart-track">
          ${segments.map((segment) => `
            <i class="oq-working-chart-segment oq-working-chart-segment--${escapeHtml(segment.tone || tone)}" style="--oq-chart-left:${clampControlReplayPercent(segment.start)}%;--oq-chart-width:${clampControlReplayPercent(segment.width)}%;"></i>
          `).join("")}
        </div>
      </div>
    `;
  }

  function getControlWorkingChartSourceStateAtWindowStart() {
    const windowBounds = getControlWorkingWindowBounds();
    const active = { HP1: false, HP2: false, boiler: false, cooling: false };
    const sourceModes = { HP1: 0, HP2: 0 };
    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const events = enrichControlWorkingDecisionLogEvents(getDecisionLogEvents())
      .filter((event) => event && !event._oq_hidden)
      .sort((left, right) => {
        const leftEpochMs = getDecisionEventEpochMs(left);
        const rightEpochMs = getDecisionEventEpochMs(right);
        return (Number.isFinite(leftEpochMs) ? leftEpochMs : Number.POSITIVE_INFINITY) -
          (Number.isFinite(rightEpochMs) ? rightEpochMs : Number.POSITIVE_INFINITY);
      });

    events.forEach((event) => {
      const epochMs = getDecisionEventEpochMs(event);
      if (!Number.isFinite(epochMs) || epochMs > windowBounds.start) {
        return;
      }
      const eventType = String(event.event_type || "");
      const contextCm = Number(event._oq_context_cm ?? event.cm);
      if (eventType === "source_start") {
        sourceKeys(event.subject).forEach((key) => {
          active[key] = true;
          sourceModes[key] = contextCm;
        });
      } else if (eventType === "source_stop") {
        sourceKeys(event.subject).forEach((key) => {
          active[key] = false;
          sourceModes[key] = 0;
        });
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        active.boiler = true;
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        active.boiler = false;
      }
    });
    active.cooling = ["HP1", "HP2"].some((key) => active[key] && sourceModes[key] === 5);
    return { ...active, sourceModes };
  }

  function getControlWorkingDecisionLogChartLanes(items) {
    if (!items.some((item) => item.rawDecisionEvent)) {
      return null;
    }
    const lanes = [
      { label: "HP1", tone: "running", segments: [] },
      { label: "HP2", tone: "running", segments: [] },
      { label: "CV-ketel", tone: "assist", segments: [] },
      { label: "Koeling", tone: "cooling", segments: [] },
      { label: "Ontdooien", tone: "defrost", segments: [] },
      { label: "Bescherming", tone: "limited", segments: [] },
    ];
    const byLabel = Object.fromEntries(lanes.map((lane) => [lane.label, lane]));
    const addMinuteSegment = (label, startMinute, endMinute, tone, minWidth = 0.5) => {
      if (!byLabel[label] || !Number.isFinite(startMinute)) {
        return;
      }
      const start = Math.max(0, Math.min(1440, Number(startMinute)));
      const end = Number.isFinite(endMinute)
        ? Math.max(start, Math.min(1440, Number(endMinute)))
        : start;
      const width = Math.max(minWidth, ((end - start) / 1440) * 100);
      byLabel[label].segments.push({ start: (start / 1440) * 100, width, tone });
    };
    const addEventSegment = (label, item, tone, minWidth = 0.5) => {
      const range = getControlWorkingItemMinuteRange(item);
      addMinuteSegment(label, range.start, range.end, tone, minWidth);
    };
    const sortedItems = [...items]
      .filter((item) => item.rawDecisionEvent)
      .sort((left, right) => getControlWorkingItemMinuteRange(left).start - getControlWorkingItemMinuteRange(right).start);
    const openSource = { HP1: null, HP2: null, "CV-ketel": null, Koeling: null };
    const openLane = (label, startMinute) => {
      if (openSource[label] == null) {
        openSource[label] = startMinute;
      }
    };
    const closeLane = (label, endMinute, tone = "running", minWidth = 0.8) => {
      if (openSource[label] == null) {
        return false;
      }
      addMinuteSegment(label, openSource[label], endMinute, tone, minWidth);
      openSource[label] = null;
      return true;
    };
    const closeCoolingLaneIfNoHeatPumpSource = (endMinute) => {
      if (openSource.Koeling != null && openSource.HP1 == null && openSource.HP2 == null) {
        closeLane("Koeling", endMinute, "cooling", 0.8);
      }
    };
    const openDefrost = {};
    const activeAtWindowStart = getControlWorkingChartSourceStateAtWindowStart();
    if (activeAtWindowStart.HP1) {
      openLane("HP1", 0);
    }
    if (activeAtWindowStart.HP2) {
      openLane("HP2", 0);
    }
    if (activeAtWindowStart.boiler) {
      openLane("CV-ketel", 0);
    }
    if (activeAtWindowStart.cooling) {
      openLane("Koeling", 0);
    }

    sortedItems.forEach((item) => {
      const range = getControlWorkingItemMinuteRange(item);
      const eventType = String(item.realEventType || "");
      const subject = String(item.rawDecisionEvent?.subject || "").toUpperCase();
      const contextCm = Number(item.rawDecisionEvent?._oq_context_cm ?? item.rawDecisionEvent?.cm);
      const targetSources = [];
      if (subject === "HP1" || subject === "BOTH") {
        targetSources.push("HP1");
      }
      if (subject === "HP2" || subject === "BOTH") {
        targetSources.push("HP2");
      }

      if (eventType === "source_start") {
        targetSources.forEach((label) => openLane(label, range.start));
        if (contextCm === 5) {
          openLane("Koeling", range.start);
        }
      } else if (eventType === "source_stop") {
        targetSources.forEach((label) => {
          if (!closeLane(label, range.start, "running")) {
            addEventSegment(label, item, "standby", 0.55);
          }
        });
        if (contextCm === 5 || openSource.Koeling != null) {
          closeCoolingLaneIfNoHeatPumpSource(range.start);
        }
      } else if (eventType === "topology_change") {
        if (item.rawDecisionEvent?.to === "duo") {
          openLane("HP1", range.start);
          openLane("HP2", range.start);
        } else if (item.rawDecisionEvent?.to === "single") {
          const activeSource = getControlWorkingSingleTopologySource(item.rawDecisionEvent);
          if (activeSource) {
            openLane(activeSource, range.start);
            closeLane(activeSource === "HP1" ? "HP2" : "HP1", range.start, "running", 0.8);
          } else {
            closeLane("HP2", range.start, "running", 0.8);
          }
          closeCoolingLaneIfNoHeatPumpSource(range.start);
        } else if (item.rawDecisionEvent?.to === "idle") {
          closeLane("HP1", range.start, "running", 0.8);
          closeLane("HP2", range.start, "running", 0.8);
          closeLane("Koeling", range.start, "cooling", 0.8);
        }
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        openLane("CV-ketel", range.start);
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        if (!closeLane("CV-ketel", range.start, "assist", 0.65)) {
          addEventSegment("CV-ketel", item, "standby", 0.65);
        }
      } else if (eventType === "candidate_blocked" || eventType === "flow_hold_start") {
        addEventSegment("Bescherming", item, "limited", 0.7);
      } else if (eventType === "flow_hold_clear") {
        const durationMinutes = Math.max(1, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Bescherming", Math.max(0, range.start - durationMinutes), range.start, "limited", 0.7);
        if (item.rawDecisionEvent?.reason === "flow_postflow") {
          closeLane("Koeling", range.start, "cooling", 0.8);
        }
      }

      if (eventType === "defrost_seen_start") {
        openDefrost[subject || "SYSTEM"] = range.start;
      } else if (eventType === "defrost_seen_clear" && openDefrost[subject || "SYSTEM"] != null) {
        addMinuteSegment("Ontdooien", openDefrost[subject || "SYSTEM"], range.start, "defrost", 0.7);
        openDefrost[subject || "SYSTEM"] = null;
      } else if (eventType === "defrost_seen_clear" && Number(item.rawDecisionEvent?.duration_s) > 0) {
        const durationMinutes = Math.max(5, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Ontdooien", Math.max(0, range.start - durationMinutes), range.start, "defrost", 0.7);
      }
      const protectionAlreadyMapped = eventType === "candidate_blocked" ||
        eventType === "flow_hold_start" ||
        eventType === "flow_hold_clear";
      if (!protectionAlreadyMapped &&
          (item.severity === "limited" || item.severity === "attention" || eventType === "decision_blocked" || eventType === "decision_hold")) {
        addEventSegment("Bescherming", item, item.severity === "attention" ? "assist" : "limited", 0.7);
      }
      if (eventType === "sticky_pump_run") {
        addEventSegment("Bescherming", item, "safe", 0.6);
      }
      if (eventType === "frost_protection_start") {
        addEventSegment("Bescherming", item, "limited", 0.8);
      } else if (eventType === "frost_protection_clear") {
        const durationMinutes = Math.max(1, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Bescherming", Math.max(0, range.start - durationMinutes), range.start, "limited", 0.8);
      }
    });
    const openEndMinute = getControlWorkingOpenEndMinute();
    Object.entries(openSource).forEach(([label, startMinute]) => {
      if (startMinute != null) {
        if (startMinute <= openEndMinute) {
          addMinuteSegment(label, startMinute, openEndMinute, label === "CV-ketel" ? "assist" : label === "Koeling" ? "cooling" : "running", 0.8);
        }
      }
    });
    Object.values(openDefrost).forEach((startMinute) => {
      if (startMinute != null) {
        addMinuteSegment("Ontdooien", startMinute, Math.min(1440, startMinute + 7), "defrost", 0.7);
      }
    });

    return lanes.filter((lane) => lane.segments.length);
  }

  function getControlWorkingChartLanes(items) {
    const decisionLogLanes = getControlWorkingDecisionLogChartLanes(items);
    if (decisionLogLanes) {
      return decisionLogLanes;
    }
    return [];
  }

  function renderControlWorkingGraphsTab(selectedItem, items) {
    const graphMinute = getControlWorkingGraphMinute();
    const graphPercent = (graphMinute / 1440) * 100;
    const windowModel = getControlWorkingWindowModel();
    const graphTimeLabel = formatControlWorkingGraphCursorLabel(graphMinute, windowModel);
    const lanes = getControlWorkingChartLanes(items);
    const chartBody = lanes.length
      ? lanes.map((lane) => renderControlWorkingChartLane(lane.label, lane.tone, lane.segments)).join("")
      : renderControlWorkingEmptyState("Nog geen grafiekdata", "De grafiek gebruikt alleen echte beslislog-records. Nieuwe bronwissels, defrosts of begrenzingen verschijnen hier vanzelf.");
    return `
      <div class="oq-working-graphs">
        <section class="oq-working-chart-panel">
          <div class="oq-working-chart-head">
            <div>
              <span class="oq-working-eyebrow">${escapeHtml(windowModel.eyebrow)}</span>
              <h3>Grafieken met beslismomenten</h3>
            </div>
            <p>${escapeHtml(windowModel.graphCopy)}</p>
          </div>
          <div class="oq-working-chart-axis" aria-hidden="true">
            ${windowModel.axis.map((label) => `<span>${escapeHtml(label)}</span>`).join("")}
          </div>
          <div class="oq-working-chart-body">
            <div class="oq-working-chart-control" data-oq-control-replay-scrub="true">
              <input
                class="oq-working-time-slider"
                type="range"
                min="0"
                max="1440"
                step="5"
                value="${escapeHtml(String(graphMinute))}"
                aria-label="Tijd in grafiek"
                data-oq-control-replay-time="true"
              >
              <span class="oq-working-chart-cursor" style="--oq-chart-left:${escapeHtml(String(graphPercent))}%;">
                <strong>${escapeHtml(graphTimeLabel)}</strong>
              </span>
            </div>
            ${chartBody}
          </div>
        </section>
        ${selectedItem ? renderControlWorkingDetails(selectedItem) : renderControlWorkingGraphEmptyDetails(graphTimeLabel)}
      </div>
    `;
  }

  function getControlWorkingSignature(heatPumpPanels) {
    const current = getControlWorkingCurrent(heatPumpPanels);
    return getRenderSignature({
      tab: getControlWorkingSelectedTab(),
      window: getControlWorkingSelectedWindow(),
      periodMenuOpen: state.controlReplayPeriodMenuOpen,
      customPeriodOpen: state.controlReplayCustomPeriodOpen,
      customStart: state.controlReplayCustomStart,
      customEnd: state.controlReplayCustomEnd,
      customPeriodError: state.controlReplayCustomPeriodError,
      selected: state.controlReplaySelectedEpisode,
      supportDetailsItem: state.controlReplaySupportDetailsItemId,
      graphMinute: getControlWorkingGraphMinute(),
      mode: current.modeLabel,
      title: current.title,
      reason: current.primaryReason,
      hp1Running: current.hp1Running,
      hp2Running: current.hp2Running,
      hp1Starts: current.hp1Starts,
      hp2Starts: current.hp2Starts,
      hp1Hours: current.hp1Hours,
      hp2Hours: current.hp2Hours,
      cvStatus: current.cvStatus,
      strategy: current.strategyLabel,
      outside: current.outsideTemp,
      supply: current.supplyTemp,
      flow: current.flow,
      cooling: current.cooling,
      coolingProtection: current.coolingProtection,
      coolingCapped: current.coolingCapped,
      decisionLog: state.decisionLogSignature,
      decisionLogError: state.decisionLogError,
      theme: state.overviewTheme,
    });
  }

  function renderControlWorkingPanel(heatPumpPanels) {
    const current = getControlWorkingCurrent(heatPumpPanels);
    const items = getControlWorkingItems(heatPumpPanels);
    const selectedItem = getControlWorkingSelectedItem(items);
    const selectedTab = getControlWorkingSelectedTab();
    const visibleItem = selectedTab === "graphs"
      ? getControlWorkingItemForMinute(items, getControlWorkingGraphMinute())
      : selectedItem;
    const body = selectedTab === "status"
      ? renderControlWorkingStatusTab(current)
      : selectedTab === "graphs"
      ? renderControlWorkingGraphsTab(visibleItem, items)
      : renderControlWorkingTimelineTab(items, visibleItem);
    const periodChoices = selectedTab === "status" ? "" : renderControlWorkingWindowChoices();
    return `
      <section class="oq-working" data-render-signature="${escapeHtml(getControlWorkingSignature(heatPumpPanels))}">
        <header class="oq-working-head">
          <div class="oq-working-head-copy">
            <span class="oq-working-kicker">
              <span class="oq-working-eyebrow">Beslislog</span>
              <span class="oq-working-beta">BETA</span>
            </span>
            <h2>Keuzes van de controller, uitgelegd</h2>
            <p>Actueel toont wat het systeem nu doet. Tijdlijn toont hoe het zover kwam. Grafieken tonen het verloop.</p>
          </div>
          <div class="oq-working-head-actions">
            ${renderControlWorkingTabs()}
            ${periodChoices}
          </div>
        </header>
        ${body}
      </section>
    `;
  }

  export function renderControlReplayView() {
    const heatPumpPanels = getHeatPumpPanels();
    return `
      <section class="oq-helper-panel oq-helper-panel--flush">
        <div class="oq-overview-board oq-overview-board--${escapeHtml(state.overviewTheme)}">
          ${renderControlWorkingPanel(heatPumpPanels)}
        </div>
      </section>
    `;
  }

  function patchControlReplayDom() {
    if (!state.root || state.appView !== "control") {
      return false;
    }
    const board = state.root.querySelector(".oq-overview-board");
    const panel = board ? board.querySelector(".oq-working") : null;
    if (!board || !panel) {
      return false;
    }
    const activeElement = document.activeElement;
    if (activeElement && activeElement.closest("[data-oq-control-replay-period-menu]") &&
        activeElement.matches("[data-oq-control-replay-custom-input]")) {
      return true;
    }
    const nextBoardClass = `oq-overview-board oq-overview-board--${state.overviewTheme}`;
    if (board.className !== nextBoardClass) {
      board.className = nextBoardClass;
    }
    const heatPumpPanels = getHeatPumpPanels();
    return replaceOuterHtmlIfSignatureChanged(
      panel,
      getControlWorkingSignature(heatPumpPanels),
      renderControlWorkingPanel(heatPumpPanels),
    ) || true;
  }

  setViewPatchControls({ patchControlReplayDom });
