import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";
import { renderSettingsCompactSwitchControl } from "../settings/controls.js";

export function renderPerformanceTelemetryConsent({ enabled, busy, settings = false, disclosure = "" }) {
  const scheduleCopy = settings
    ? `${t("performance.consentSettingsPre")} <strong>${escapeHtml(t("performance.consentSettingsStrong"))}</strong>${escapeHtml(t("performance.consentSettingsPost"))}`
    : t("performance.consentWizardCopy");
  return `
    <div class="oq-usage-consent${enabled ? " is-enabled" : ""} oq-usage-consent--settings">
      <div class="oq-usage-consent-copy">
        <span class="oq-usage-consent-icon" aria-hidden="true">${renderOqIcon("activity", "oq-usage-consent-icon-svg")}</span>
        <div>
          <h3>${escapeHtml(t("performance.consentTitle"))}</h3>
          <p>${scheduleCopy}</p>
        </div>
      </div>
      <div class="oq-usage-consent-action">
        ${renderSettingsCompactSwitchControl(
          "performanceTelemetryEnabled",
          t("performance.consentTitle"),
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

const PERFORMANCE_PAYLOAD_EXAMPLE = `{
  "v": 1,
  "iid": "3f9a…",
  "bid": "7c4e…",
  "ws": 1757760000,
  "fw": "v0.50.0",
  "top": "duo",
  "gen": "v1_5",
  "map": "v1-2026-09-a",
  "pem": "pinput-v1",
  "mk": "system_actual",
  "m": [
    {
      "t": 1757760000,
      "a": 1,
      "o": 7.25,
      "s": 32.50,
      "f": 807.0,
      "h": [
        {"l": 2, "hz": 30.0, "ti": 28.10, "to": 31.40, "el": 5.67, "et": 17.28, "ep": 0.42, "b": false},
        null
      ]
    }
  ]
}`;

export function renderPerformanceTelemetryDisclosure({ collapsible = false, idPrefix = "oq-performance", open = false } = {}) {
  const safePrefix = escapeHtml(idPrefix);
  const includedTitleId = `${safePrefix}-included-title`;
  const excludedTitleId = `${safePrefix}-excluded-title`;
  const sharedDetail = `
    <section class="oq-usage-disclosure-column" aria-labelledby="${includedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon is-included" aria-hidden="true">${renderOqIcon("activity", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${includedTitleId}">${escapeHtml(t("performance.includedTitle"))}</h4>
      </div>
      <ul>
        <li><strong>${escapeHtml(t("performance.inSystem"))}</strong><span>${escapeHtml(t("performance.inSystemCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.inHp"))}</strong><span>${escapeHtml(t("performance.inHpCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.inPoint"))}</strong><span>${escapeHtml(t("performance.inPointCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.inPerHp"))}</strong><span>${escapeHtml(t("performance.inPerHpCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.inRange"))}</strong><span>${escapeHtml(t("performance.inRangeCopy"))}</span></li>
      </ul>
    </section>
  `;
  const excludedDetail = `
    <section class="oq-usage-disclosure-column is-excluded" aria-labelledby="${excludedTitleId}">
      <div class="oq-usage-disclosure-column-head">
        <span class="oq-usage-disclosure-column-icon" aria-hidden="true">${renderOqIcon("shield", "oq-usage-disclosure-icon-svg")}</span>
        <h4 id="${excludedTitleId}">${escapeHtml(t("performance.excludedTitle"))}</h4>
      </div>
      <ul>
        <li><strong>${escapeHtml(t("performance.exIdentityAccess"))}</strong><span>${escapeHtml(t("performance.exIdentityAccessCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.exHome"))}</strong><span>${escapeHtml(t("performance.exHomeCopy"))}</span></li>
        <li><strong>${escapeHtml(t("performance.exSelection"))}</strong><span>${escapeHtml(t("performance.exSelectionCopy"))}</span></li>
      </ul>
    </section>
  `;
  const facts = `
    <div class="oq-usage-facts">
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("clock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>${escapeHtml(t("performance.oftenTitle"))}</h5>
          <p>${escapeHtml(t("performance.oftenCopy"))}</p>
        </div>
      </div>
      <div class="oq-usage-fact">
        <span class="oq-usage-fact-icon" aria-hidden="true">${renderOqIcon("lock", "oq-usage-fact-icon-svg")}</span>
        <div>
          <h5>${escapeHtml(t("performance.notTitle"))}</h5>
          <p>${escapeHtml(t("performance.notCopy"))}</p>
        </div>
      </div>
    </div>
  `;
  const why = `
    <aside class="oq-usage-why">
      <span class="oq-usage-why-icon" aria-hidden="true">${renderOqIcon("info", "oq-usage-why-icon-svg")}</span>
      <div>
        <h5>${escapeHtml(t("performance.whyTitle"))}</h5>
        <p>${escapeHtml(t("performance.whyCopy"))}</p>
      </div>
    </aside>
  `;
  const note = `
    <p class="oq-usage-network-note">${renderOqIcon("server", "oq-usage-network-note-icon")} ${escapeHtml(t("performance.networkNote"))}</p>
  `;
  const example = `
    <details class="oq-usage-payload-example">
      <summary>${escapeHtml(t("performance.exampleTitle"))}</summary>
      <p>${escapeHtml(t("performance.exampleCopy"))}</p>
      <pre><code>${escapeHtml(PERFORMANCE_PAYLOAD_EXAMPLE)}</code></pre>
    </details>
  `;

  if (collapsible) {
    return `
      <details class="oq-usage-consent-details"${open ? " open" : ""}>
        <summary data-oq-action="toggle-performance-telemetry-details">
          <span class="oq-usage-consent-details-title">${escapeHtml(t("performance.detailsTitle"))}</span>
          <span class="oq-settings-section-summary-toggle" aria-hidden="true"></span>
        </summary>
        <div class="oq-usage-consent-details-body">
          <div class="oq-usage-facts-grid">
            ${sharedDetail}
            ${facts}
            ${why}
          </div>
          ${example}
          ${note}
        </div>
      </details>
    `;
  }

  return `
    <div class="oq-usage-disclosure">
      <div class="oq-usage-disclosure-head">
        <h3>${escapeHtml(t("performance.headTitle"))}</h3>
        <span>${escapeHtml(t("performance.headSub"))}</span>
      </div>
      <div class="oq-usage-disclosure-grid">
        ${sharedDetail}
        ${excludedDetail}
      </div>
      ${example}
      ${note}
    </div>
  `;
}
