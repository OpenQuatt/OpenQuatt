import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { renderSettingsCompactSwitchControl } from "../settings/controls.js";

export function renderUsageTelemetryConsent({ enabled, busy, settings = false, disclosure = "" }) {
  const scheduleCopy = settings
    ? "Help OpenQuatt stabieler en betrouwbaarder te maken door beperkte technische systeeminformatie te delen."
    : "Na het afronden verstuurt OpenQuatt vrijwel direct en daarna ongeveer elk uur technische gegevens naar de OpenQuatt-loggingserver. Na een echte firmwarecrash kan daarnaast het laatste technische crashrapport worden verstuurd.";
  const value = settings && enabled && hasEntity("usageTelemetryInstallationId")
    ? String(getEntityValue("usageTelemetryInstallationId") || "").trim()
    : "";
  const installationId = ["unknown", "unavailable", "nan"].includes(value.toLowerCase()) ? "" : value;
  return `
    <div class="oq-usage-consent${enabled ? " is-enabled" : ""}${settings ? " oq-usage-consent--settings" : ""}">
      <div class="oq-usage-consent-copy">
        <span class="oq-usage-consent-icon" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-consent-icon-svg")}</span>
        <div>
          <h3>Technische statistieken delen</h3>
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
          "Aan",
          "Uit",
        )}
      </div>
      ${disclosure}
    </div>
  `;
}

export function renderUsageTelemetryDisclosure({ collapsible = false, idPrefix = "oq-usage", open = false } = {}) {
  const previewSurface = collapsible ? "settings-system" : "quickstart";
  const preview = state.usageTelemetryPreviewSurface === previewSurface
    ? state.usageTelemetryPreviewPayload
    : null;
  const previewJson = preview ? JSON.stringify(preview, null, 2) : "Live waarden laden…";
  const safePrefix = escapeHtml(idPrefix);
  const includedTitleId = `${safePrefix}-included-title`;
  const excludedTitleId = `${safePrefix}-excluded-title`;
  const sharedDetail = `
    <section class="oq-usage-disclosure-column" aria-labelledby="${includedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon is-included" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${includedTitleId}">Wordt gedeeld</h4>
      </div>
      <ul>
        <li><strong>Installatie</strong><span>Willekeurig ID, tijdstip en uptime</span></li>
        <li><strong>Software</strong><span>Versie en releasekanaal</span></li>
        <li><strong>Platform</strong><span>Hardware, opstelling, actieve verbinding, verbindingsmodus en wifi-signaal</span></li>
        <li><strong>Configuratie</strong><span>Quatt Hybrid-versie, verwarmingsstrategie, flowbron en regelbronnen</span></li>
        <li><strong>Systeemstatus</strong><span>Geheugen, looptijd, chiptemperatuur, herstartreden en vier Modbus-betrouwbaarheidstellers (toename sinds de vorige succesvolle verzending)</span></li>
        <li><strong>Na een crash</strong><span>Het technische ESPHome-crashrapport, de ELF-SHA256 en voldoende firmware-identificatie om een passende rebuild te controleren</span></li>
        <li><strong>Functies</strong><span>Aan/uit-status van CiC, OpenTherm-thermostaat, ketelondersteuning, MQTT-inputs en lokale historie; plus de ketelaansluiting (aan/uit of OpenTherm)</span></li>
      </ul>
    </section>
  `;
  const excludedDetail = `
    <section class="oq-usage-disclosure-column is-excluded" aria-labelledby="${excludedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon" aria-hidden="true">${renderOqIcon("shield", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${excludedTitleId}">Wordt niet gedeeld</h4>
      </div>
      <ul>
        <li><strong>Identiteit</strong><span>Geen MAC-adres of netwerkadres</span></li>
        <li><strong>Wifi en toegang</strong><span>Nooit een wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of inloggegevens</span></li>
        <li><strong>Installatiegedrag</strong><span>Geen verwarmingsmetingen of regelwaarden</span></li>
        <li><strong>Lokale data</strong><span>Geen gemeten of ingestelde temperaturen, grenzen, MQTT-topics, logregels of Modbus-frames; alleen communicatiebetrouwbaarheidstellers, behalve het technische crashrapport na een firmwarecrash</span></li>
      </ul>
    </section>
  `;
  const facts = `
    <div class="oq-usage-facts">
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("clock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>Hoe vaak?</h5>
          <p>Na inschakelen verstuurt OpenQuatt vrijwel direct en daarna ongeveer elk uur technische gegevens naar de OpenQuatt-loggingserver.</p>
        </div>
      </div>
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("lock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>Wat niet?</h5>
          <p>Geen wifi- of inloggegevens, geen persoonlijke gegevens, geen locatie.</p>
        </div>
      </div>
    </div>
  `;
  const why = `
    <aside class="oq-usage-why">
      <span class="oq-usage-why-icon" aria-hidden="true">${renderOqIcon("info", "oq-usage-why-icon-svg")}</span>
      <div>
        <h5>Waarom?</h5>
        <p>Met deze informatie kunnen we problemen sneller opsporen en OpenQuatt verder verbeteren.</p>
      </div>
    </aside>
  `;
  const columns = `
    <div class="oq-usage-disclosure-grid">
      ${sharedDetail}
      ${excludedDetail}
    </div>
    <details class="oq-usage-payload-example">
      <summary>Voorbeeld van het verzonden bericht (JSON)</summary>
      <p>${preview ? "Live momentopname bij het openen van deze pagina. message_id en timestamp_s worden voor de echte verzending opnieuw bepaald; reset_reason is niet lokaal uitleesbaar en staat hier daarom op null." : "De actuele controllerwaarden worden eenmalig opgehaald."} De vier Modbus-tellers worden bij verzending rechtstreeks uit de ODU-bus gelezen en staan in deze lokale preview daarom op null. Een crashrapport wordt alleen na een echte firmwarecrash als laatste retained crash gepubliceerd.</p>
      <pre><code>${escapeHtml(previewJson)}</code></pre>
    </details>
    <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} De OpenQuatt-loggingserver kan, zoals iedere internetdienst, technisch wel het bron-IP-adres zien. OpenQuatt slaat dit IP-adres niet op.</p>
  `;

  if (collapsible) {
    return `
      <details class="oq-usage-consent-details"${open ? " open" : ""}>
        <summary data-oq-action="toggle-usage-telemetry-details">
          <span class="oq-usage-consent-details-title">Welke gegevens worden gedeeld?</span>
          <span class="oq-settings-section-summary-toggle" aria-hidden="true"></span>
        </summary>
        <div class="oq-usage-consent-details-body">
          <div class="oq-usage-facts-grid">
            ${sharedDetail}
            ${facts}
            ${why}
          </div>
          <details class="oq-usage-payload-example">
            <summary>Voorbeeld van het verzonden bericht (JSON)</summary>
            <p>${preview ? "Live momentopname bij het openen van deze pagina. message_id en timestamp_s worden voor de echte verzending opnieuw bepaald; reset_reason is niet lokaal uitleesbaar en staat hier daarom op null." : "De actuele controllerwaarden worden eenmalig opgehaald."} De vier Modbus-tellers worden bij verzending rechtstreeks uit de ODU-bus gelezen en staan in deze lokale preview daarom op null. Een crashrapport wordt alleen na een echte firmwarecrash als laatste retained crash gepubliceerd.</p>
            <pre><code>${escapeHtml(previewJson)}</code></pre>
          </details>
          <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} De OpenQuatt-loggingserver kan, zoals iedere internetdienst, technisch wel het bron-IP-adres zien. OpenQuatt slaat dit IP-adres niet op.</p>
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
