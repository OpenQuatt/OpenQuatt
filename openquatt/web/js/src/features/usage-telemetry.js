import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { renderSettingsCompactSwitchControl } from "../settings/controls.js";

const USAGE_TELEMETRY_EXAMPLE_JSON = JSON.stringify({
  schema_version: 1,
  message_id: "c8272f30-b64d-4af0-a13c-bf8e0cbde842",
  installation_id: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f",
  timestamp_s: 1784527200,
  uptime_s: 86420,
  firmware_version: "v0.44.0",
  release_channel: "main",
  hardware_profile: "heatpump_controller_q",
  hardware_revision: "1.0 (batch 42)",
  topology: "duo",
  connection: "wifi",
  quatt_hybrid_generation_config: "v1_5",
  flow_source_config: "outdoor_unit",
  heating_strategy: "power_house",
  room_temperature_source: "opentherm",
  room_setpoint_source: "opentherm",
  outside_temperature_source: "auto",
  heating_enable_source: "disabled",
  cooling_enable_source: "disabled",
  cooling_dew_point_source: "auto",
  heap_free_b: 178432,
  heap_min_free_b: 151008,
  heap_largest_block_b: 98304,
  psram_free_b: 7023616,
  loop_time_ms: 14,
  esp_internal_temp_c: 47.8,
  wifi_rssi_dbm: -61,
  reset_reason: "power_on",
  cic_polling_enabled: true,
  cic_compatibility_enabled: false,
  ot_thermostat_enabled: true,
  boiler_assist_enabled: true,
  boiler_connection: "on_off",
  mqtt_inputs_enabled: false,
  trend_ram_enabled: true,
  trend_flash_enabled: false,
  decision_log_flash_enabled: false,
  energy_history_flash_enabled: true,
  ram_log_history_enabled: true,
}, null, 2);

const CRASH_TELEMETRY_EXAMPLE_JSON = JSON.stringify({
  schema_version: 1,
  message_id: "304ace72-ad57-42b1-8c5b-8598ae7418da",
  installation_id: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f",
  event: "crash",
  timestamp_s: 1787290540,
  uptime_s: 18342,
  firmware_version: "v0.48.0",
  release_channel: "dev",
  current_build_id: "198e34bf1ad051107db928af5ba37ba186a6d37be17a956954b25277209fd953",
  hardware_profile: "heatpump_controller_q",
  topology: "duo",
  connection: "wifi",
  reset_reason: "panic",
  crash: {
    captured_build_id: "198e34bf1ad051107db928af5ba37ba186a6d37be17a956954b25277209fd953",
    captured_source_repository: "OpenQuatt/OpenQuatt",
    captured_source_commit: "a5c127aee5495c75eaa71a03ab2fabd968650b40",
    captured_build_epoch: 1787289120,
    captured_build_target: "configs/heatpump_controller_q/duo_wifi.yaml",
    captured_firmware_version: "v0.48.0-dev.314+a5c127a",
    captured_release_channel: "dev",
    captured_by_current_build: true,
    exception_type: "Fault",
    reason: "LoadProhibited",
    raw_cause: 28,
    core: 0,
    pc: "0x400fe12d",
    fault_addr: "0x00000000",
    backtrace: ["0x400fe12d", "0x40082819"],
    other_core_backtrace: [],
    backtrace_truncated: false,
  },
}, null, 2);

export function renderUsageTelemetryConsent({ enabled, busy, settings = false }) {
  const scheduleCopy = settings
    ? "Na inschakelen verstuurt OpenQuatt vrijwel direct, daarna ongeveer elk uur en na een echte firmwarecrash beperkte technische gegevens naar de OpenQuatt-loggingserver."
    : "Na het afronden verstuurt OpenQuatt vrijwel direct, daarna ongeveer elk uur en na een echte firmwarecrash beperkte technische gegevens naar de OpenQuatt-loggingserver.";
  const value = settings && enabled && hasEntity("usageTelemetryInstallationId")
    ? String(getEntityValue("usageTelemetryInstallationId") || "").trim()
    : "";
  const installationId = ["unknown", "unavailable", "nan"].includes(value.toLowerCase()) ? "" : value;
  return `
    <div class="oq-usage-consent${enabled ? " is-enabled" : ""}${settings ? " oq-usage-consent--settings" : ""}">
      <div class="oq-usage-consent-copy">
        <span class="oq-usage-consent-icon" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-consent-icon-svg")}</span>
        <div>
          <span class="oq-usage-consent-kicker">Vrijwillige keuze</span>
          <h3>Beperkte statistieken delen</h3>
          <p>${scheduleCopy}</p>
          ${installationId ? `<div class="oq-usage-consent-installation-id"><strong>Installatie-ID</strong><code>${escapeHtml(installationId)}</code></div>` : ""}
        </div>
      </div>
      <div class="oq-usage-consent-action">
        ${renderSettingsCompactSwitchControl(
          "usageTelemetryEnabled",
          "Technische gebruiksstatistieken delen",
          enabled,
          busy,
          "Delen",
          "Niet delen",
        )}
      </div>
    </div>
  `;
}

export function renderUsageTelemetryDisclosure({ collapsible = false, idPrefix = "oq-usage", open = false } = {}) {
  const safePrefix = escapeHtml(idPrefix);
  const includedTitleId = `${safePrefix}-included-title`;
  const excludedTitleId = `${safePrefix}-excluded-title`;
  const columns = `
    <div class="oq-usage-disclosure-grid">
      <section class="oq-usage-disclosure-column" aria-labelledby="${includedTitleId}">
        <div class="oq-usage-disclosure-column-head">
          <span class="oq-usage-disclosure-column-icon is-included" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-disclosure-icon-svg")}</span>
          <h4 id="${includedTitleId}">In het bericht</h4>
        </div>
        <ul>
          <li><strong>Installatie</strong><span>Willekeurig ID, tijdstip en uptime</span></li>
          <li><strong>Software</strong><span>Versie en releasekanaal</span></li>
          <li><strong>Platform</strong><span>Hardware, opstelling, verbinding en wifi-signaal</span></li>
          <li><strong>Configuratie</strong><span>Quatt Hybrid-versie, verwarmingsstrategie, flowbron en regelbronnen</span></li>
          <li><strong>Systeemstatus</strong><span>Geheugen, looptijd, chiptemperatuur en herstartreden</span></li>
          <li><strong>Na een crash</strong><span>Crashreden, oorzaakcode, processorcore, foutadres indien bruikbaar, ruwe backtrace per core en de exacte firmwarebuild</span></li>
          <li><strong>Functies</strong><span>Aan/uit-status van CiC, OpenTherm-thermostaat, ketelondersteuning, MQTT-inputs en lokale historie; plus de ketelaansluiting (aan/uit of OpenTherm)</span></li>
        </ul>
      </section>
      <section class="oq-usage-disclosure-column is-excluded" aria-labelledby="${excludedTitleId}">
        <div class="oq-usage-disclosure-column-head">
          <span class="oq-usage-disclosure-column-icon" aria-hidden="true">${renderOqIcon("shield", "oq-usage-disclosure-icon-svg")}</span>
          <h4 id="${excludedTitleId}">Niet in het bericht</h4>
        </div>
        <ul>
          <li><strong>Identiteit</strong><span>Geen MAC-adres of netwerkadres</span></li>
          <li><strong>Wifi en toegang</strong><span>Nooit een wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of inloggegevens</span></li>
          <li><strong>Installatiegedrag</strong><span>Geen verwarmingsmetingen of regelwaarden</span></li>
          <li><strong>Lokale data</strong><span>Geen gemeten of ingestelde temperaturen, grenzen, MQTT-topics of reguliere logs</span></li>
        </ul>
      </section>
    </div>
    <details class="oq-usage-payload-example">
      <summary>Voorbeeld van het verzonden bericht (JSON)</summary>
      <p>Voorbeeldwaarden; de velden en vorm komen overeen met het werkelijke bericht.</p>
      <pre><code>${escapeHtml(USAGE_TELEMETRY_EXAMPLE_JSON)}</code></pre>
    </details>
    <details class="oq-usage-payload-example">
      <summary>Voorbeeld van een crashbericht (JSON)</summary>
      <p>Alleen na een echte firmwarecrash. Ruwe adressen mogen uitsluitend tegen de exact overeenkomende firmwarebuild worden vertaald.</p>
      <pre><code>${escapeHtml(CRASH_TELEMETRY_EXAMPLE_JSON)}</code></pre>
    </details>
    <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} De OpenQuatt-loggingserver kan, zoals iedere internetdienst, technisch wel het bron-IP-adres zien. OpenQuatt slaat dit IP-adres niet op.</p>
  `;

  if (collapsible) {
    return `
      <details class="oq-settings-section oq-settings-section--collapsible oq-usage-disclosure oq-usage-disclosure--collapsible"${open ? " open" : ""}>
        <summary class="oq-settings-section-summary" data-oq-action="toggle-usage-telemetry-details">
          <div class="oq-settings-section-head">
            <h3>Wat gaat er mee?</h3>
            <p>Bekijk precies welke technische gegevens wel en niet worden gedeeld.</p>
          </div>
          <span class="oq-settings-section-summary-toggle" aria-hidden="true"></span>
        </summary>
        <div class="oq-settings-section-collapsible-body">
          ${columns}
        </div>
      </details>
    `;
  }

  return `
    <div class="oq-usage-disclosure">
      <div class="oq-usage-disclosure-head">
        <h3>Wat gaat er mee?</h3>
        <span>Geen verwarmings- of regeldata</span>
      </div>
      ${columns}
    </div>
  `;
}
