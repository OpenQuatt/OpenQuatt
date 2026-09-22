import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { renderSettingsCompactSwitchControl } from "../settings/controls.js";

export function renderUsageTelemetryConsent({ enabled, busy, settings = false, disclosure = "" }) {
  const scheduleCopy = settings
    ? t("usage.consentSettingsCopy")
    : t("usage.consentWizardCopy");
  const value = settings && enabled && hasEntity("usageTelemetryInstallationId")
    ? String(getEntityValue("usageTelemetryInstallationId") || "").trim()
    : "";
  const installationId = ["unknown", "unavailable", "nan"].includes(value.toLowerCase()) ? "" : value;
  return `
    <div class="oq-usage-consent${enabled ? " is-enabled" : ""}${settings ? " oq-usage-consent--settings" : ""}">
      <div class="oq-usage-consent-copy">
        <span class="oq-usage-consent-icon" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-consent-icon-svg")}</span>
        <div>
          <h3>${escapeHtml(t("usage.consentTitle"))}</h3>
          <p>${escapeHtml(scheduleCopy)}</p>
          ${installationId ? `<div class="oq-usage-consent-installation-id"><strong>${escapeHtml(t("usage.installId"))}</strong><code>${escapeHtml(installationId)}</code></div>` : ""}
        </div>
      </div>
      <div class="oq-usage-consent-action">
        ${renderSettingsCompactSwitchControl(
          "usageTelemetryEnabled",
          t("usage.consentAria"),
          enabled,
          busy,
          t("common.on"),
          t("common.off"),
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
  const previewJson = preview ? JSON.stringify(preview, null, 2) : t("usage.previewLoading");
  const safePrefix = escapeHtml(idPrefix);
  const includedTitleId = `${safePrefix}-included-title`;
  const excludedTitleId = `${safePrefix}-excluded-title`;
  const sharedDetail = `
    <section class="oq-usage-disclosure-column" aria-labelledby="${includedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon is-included" aria-hidden="true">${renderOqIcon("bar-chart", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${includedTitleId}">${escapeHtml(t("usage.includedTitle"))}</h4>
      </div>
      <ul>
        <li><strong>${escapeHtml(t("usage.inInstall"))}</strong><span>${escapeHtml(t("usage.inInstallCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inSoftware"))}</strong><span>${escapeHtml(t("usage.inSoftwareCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inPlatform"))}</strong><span>${escapeHtml(t("usage.inPlatformCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inConfig"))}</strong><span>${escapeHtml(t("usage.inConfigCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inStatus"))}</strong><span>${escapeHtml(t("usage.inStatusCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inCrash"))}</strong><span>${escapeHtml(t("usage.inCrashCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.inFeatures"))}</strong><span>${escapeHtml(t("usage.inFeaturesCopy"))}</span></li>
      </ul>
    </section>
  `;
  const excludedDetail = `
    <section class="oq-usage-disclosure-column is-excluded" aria-labelledby="${excludedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon" aria-hidden="true">${renderOqIcon("shield", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${excludedTitleId}">${escapeHtml(t("usage.excludedTitle"))}</h4>
      </div>
      <ul>
        <li><strong>${escapeHtml(t("usage.exIdentity"))}</strong><span>${escapeHtml(t("usage.exIdentityCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.exWifi"))}</strong><span>${escapeHtml(t("usage.exWifiCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.exBehavior"))}</strong><span>${escapeHtml(t("usage.exBehaviorCopy"))}</span></li>
        <li><strong>${escapeHtml(t("usage.exLocal"))}</strong><span>${escapeHtml(t("usage.exLocalCopy"))}</span></li>
      </ul>
    </section>
  `;
  const facts = `
    <div class="oq-usage-facts">
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("clock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>${escapeHtml(t("usage.oftenTitle"))}</h5>
          <p>${escapeHtml(t("usage.oftenCopy"))}</p>
        </div>
      </div>
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("lock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>${escapeHtml(t("usage.notTitle"))}</h5>
          <p>${escapeHtml(t("usage.notCopy"))}</p>
        </div>
      </div>
    </div>
  `;
  const why = `
    <aside class="oq-usage-why">
      <span class="oq-usage-why-icon" aria-hidden="true">${renderOqIcon("info", "oq-usage-why-icon-svg")}</span>
      <div>
        <h5>${escapeHtml(t("usage.whyTitle"))}</h5>
        <p>${escapeHtml(t("usage.whyCopy"))}</p>
      </div>
    </aside>
  `;
  const exampleIntro = preview ? t("usage.exampleLive") : t("usage.exampleStatic");
  const columns = `
    <div class="oq-usage-disclosure-grid">
      ${sharedDetail}
      ${excludedDetail}
    </div>
    <details class="oq-usage-payload-example">
      <summary>${escapeHtml(t("usage.exampleTitle"))}</summary>
      <p>${escapeHtml(exampleIntro)} ${escapeHtml(t("usage.exampleCounters"))}</p>
      <pre><code>${escapeHtml(previewJson)}</code></pre>
    </details>
    <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} ${escapeHtml(t("usage.networkNote"))}</p>
  `;

  if (collapsible) {
    return `
      <details class="oq-usage-consent-details"${open ? " open" : ""}>
        <summary data-oq-action="toggle-usage-telemetry-details">
          <span class="oq-usage-consent-details-title">${escapeHtml(t("usage.detailsTitle"))}</span>
          <span class="oq-settings-section-summary-toggle" aria-hidden="true"></span>
        </summary>
        <div class="oq-usage-consent-details-body">
          <div class="oq-usage-facts-grid">
            ${sharedDetail}
            ${facts}
            ${why}
          </div>
          <details class="oq-usage-payload-example">
            <summary>${escapeHtml(t("usage.exampleTitle"))}</summary>
            <p>${escapeHtml(exampleIntro)} ${escapeHtml(t("usage.exampleCounters"))}</p>
            <pre><code>${escapeHtml(previewJson)}</code></pre>
          </details>
          <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} ${escapeHtml(t("usage.networkNote"))}</p>
        </div>
      </details>
    `;
  }

  return `
    <div class="oq-usage-disclosure">
      <div class="oq-usage-disclosure-head">
        <h3>${escapeHtml(t("usage.headTitle"))}</h3>
        <span>${escapeHtml(t("usage.headSub"))}</span>
      </div>
      ${columns}
    </div>
  `;
}
