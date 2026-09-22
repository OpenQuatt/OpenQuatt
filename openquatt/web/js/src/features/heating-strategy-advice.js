import { hasEntity } from "../core/app-shared.js";
import { renderModalShell } from "../core/modal-shell.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { getHeatingEnableAdvice, getHeatingEnableCurrent, getHeatingEnableRecommendation } from "../core/heating-strategy-matrix.js";

function formatLabel(value) {
  const v = String(value || "").trim();
  if (!v) return "—";
  if (v === "Disabled") return t("heatingAdvice.labelDisabled");
  if (v === "OT thermostat") return t("heatingAdvice.labelOt");
  if (v === "HA input") return t("heatingAdvice.labelHa");
  if (v === "CIC") return t("heatingAdvice.labelCic");
  if (v === "API input") return t("heatingAdvice.labelApi");
  if (v === "MQTT") return t("heatingAdvice.labelMqtt");
  return v;
}
function pill(text, tone) {
  return `<span class="oq-advice-pill oq-advice-pill--${tone}">${escapeHtml(text)}</span>`;
}

export function renderHeatingStrategyAdviceModal() {
  if (state.systemModal !== "heating-strategy-advice") {
    return "";
  }
  const isCurve = isCurveMode();
  const advice = getHeatingEnableAdvice();
  const recommended = getHeatingEnableRecommendation();
  const recommendationAvailable = Boolean(recommended);
  const current = getHeatingEnableCurrent();
  const recommendedLabel = recommendationAvailable ? formatLabel(recommended) : t("heatingAdvice.activateSource");
  const currentLabel = formatLabel(current);
  const deviant = Boolean(advice.deviant && hasEntity("heatingEnableSource"));
  const busy = state.busyAction === "quickstart-heating-enable";

  const strategyLabel = isCurve ? "Water Temperature Control" : "Power House";
  const strategySub = isCurve ? t("heatingAdvice.strategySubCurve") : t("heatingAdvice.strategySubPh");
  const isPH = !isCurve;

  // Dynamic texts per strategy
  const currentDesc = (() => {
    if (current === "Disabled") return isCurve ? t("heatingAdvice.currentDisabledCurve") : t("heatingAdvice.currentDisabledPh");
    if (current === "OT thermostat") return t("heatingAdvice.currentOt");
    if (current === "CIC") return t("heatingAdvice.currentCic");
    if (current === "HA input") return t("heatingAdvice.currentHa");
    if (current === "API input") return t("heatingAdvice.currentApi");
    return t("heatingAdvice.currentExternal");
  })();
  const recommendedDesc = isPH
    ? t("heatingAdvice.currentDisabledPh")
    : recommendationAvailable
      ? t("heatingAdvice.recThermo")
      : t("heatingAdvice.recConfigure");
  const currentMini = deviant ? (isPH ? t("heatingAdvice.miniExternalGate") : recommendationAvailable ? t("heatingAdvice.miniOtherSource") : t("heatingAdvice.miniSourceInactive")) : "";
  const recommendedMini = isPH ? t("heatingAdvice.miniForPh") : recommendationAvailable ? t("heatingAdvice.miniForCurve") : t("heatingAdvice.miniConfigureFirst");
  const statusBadge = deviant
    ? `<span class="status-badge"><span class="status-dot"></span>${escapeHtml(t("heatingAdvice.badgeAdjust"))}</span>`
    : `<span class="status-badge status-badge--ok"><span class="status-dot"></span>${escapeHtml(t("heatingAdvice.badgeMatch"))}</span>`;
  const title = !recommendationAvailable && isCurve ? t("heatingAdvice.titleActivate") : deviant ? t("heatingAdvice.titleCheck") : t("heatingAdvice.titleFollowed");
  const subtitle = !recommendationAvailable && isCurve
    ? t("heatingAdvice.subActivate")
    : deviant
      ? t("heatingAdvice.subDeviant", { strategy: strategyLabel })
    : t("heatingAdvice.subMatch", { strategy: strategyLabel });
  const decisionClass = deviant ? "decision" : "decision decision--ok";

  const whyTitle = isPH ? t("heatingAdvice.whyPh") : t("heatingAdvice.whyCurve");
  const whyText = isPH
    ? t("heatingAdvice.whyPhText")
    : t("heatingAdvice.whyCurveText");
  const whenTitle = isPH ? t("heatingAdvice.whenPh") : t("heatingAdvice.whenCurve");
  const whenText = isPH ? t("heatingAdvice.whenPhText") : t("heatingAdvice.whenCurveText");
  const example = isPH ? t("heatingAdvice.examplePh") : t("heatingAdvice.exampleCurve");


  return renderModalShell({
    modalId: "system",
    titleId: "oq-heating-advice-modal-title",
    kicker: t("settingsHeating.sectionGroup"),
    title,
    copy: subtitle,
    closeAction: "close-system-modal",
    closeLabel: t("heatingAdvice.closeLabel"),
    className: "oq-helper-modal--wide",
    bodyMarkup: `
      <div class="oq-advice-redesign">
        <section class="decision ${deviant ? "" : "decision--ok"}" aria-label="${escapeHtml(t("heatingAdvice.sectionAria"))}">
          <div class="strategy-row">
            <span class="strategy-icon" aria-hidden="true">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 11.5 12 4l9 7.5"/><path d="M5.5 10.5V20h13v-9.5"/><path d="M9 20v-5h6v5"/></svg>
            </span>
            <div class="strategy-copy">
              <span class="eyebrow">${escapeHtml(t("heatingAdvice.currentStrategy"))}</span>
              <span class="strategy-title">${escapeHtml(strategyLabel)}</span>
              <span class="strategy-sub">${escapeHtml(strategySub)}</span>
            </div>
            ${statusBadge}
          </div>
          <div style="padding: 8px 16px 0; color: #7d8797; font-size: 11px; font-weight: 800; letter-spacing: .055em; text-transform: uppercase;">${escapeHtml(t("heatingAdvice.permissionKicker"))}</div>
          <div class="comparison">
            <article class="choice current">
              <div class="choice-label"><span>${escapeHtml(t("heatingAdvice.currentChoice"))}</span>${currentMini ? `<span class="mini">${escapeHtml(currentMini)}</span>` : ""}</div>
              <strong>${escapeHtml(currentLabel)}</strong>
              <p>${escapeHtml(currentDesc)}</p>
            </article>
            <div class="arrow-wrap" aria-hidden="true">
              <span class="arrow-circle">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"/><path d="m14 7 5 5-5 5"/></svg>
              </span>
            </div>
            <article class="choice recommended">
              <div class="choice-label"><span>${escapeHtml(t("heatingAdvice.recommendedChoice"))}</span><span class="mini">${escapeHtml(recommendedMini)}</span></div>
              <strong>${escapeHtml(recommendedLabel)}</strong>
              <p>${escapeHtml(recommendedDesc)}</p>
            </article>
          </div>
        </section>

        <section class="meaning">
          <span class="info-icon" aria-hidden="true">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v5"/><path d="M12 8h.01"/></svg>
          </span>
          <div>
            <strong>${escapeHtml(t("heatingAdvice.activeHeating"))}</strong>
            <p><b>${escapeHtml(t("heatingAdvice.activeHeatingOff"))}</b> ${escapeHtml(t("heatingAdvice.activeHeatingCopy"))}</p>
          </div>
        </section>

        <div class="explanation-grid">
          <section class="panel">
            <div class="panel-head">
              <span class="section-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6"/><path d="M10 22h4"/><path d="M8.5 14.5A6 6 0 1 1 15.5 14.5c-1 .7-1.5 1.5-1.5 2.5h-4c0-1-.5-1.8-1.5-2.5Z"/></svg>
              </span>
              <h2>${escapeHtml(whyTitle)}</h2>
            </div>
            <p>${escapeHtml(whyText)}</p>
            <ul class="reason-list">
              <li><span class="check">✓</span><span>${escapeHtml(isPH ? t("heatingAdvice.reasonPh1") : t("heatingAdvice.reasonCurve1"))}</span></li>
              <li><span class="check">✓</span><span>${escapeHtml(isPH ? t("heatingAdvice.reasonPh2") : t("heatingAdvice.reasonCurve2"))}</span></li>
            </ul>
          </section>
          <aside class="panel">
            <div class="panel-head">
              <span class="section-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16"/><path d="M7 4v6"/><path d="M17 4v6"/><rect x="4" y="7" width="16" height="13" rx="2"/><path d="M8 13h3"/><path d="M13 13h3"/></svg>
              </span>
              <h2>${escapeHtml(whenTitle)}</h2>
            </div>
            <p>${escapeHtml(isPH ? t("heatingAdvice.whenPhText") : t("heatingAdvice.whenCurveShort"))}</p>
            <span class="example"><b>${escapeHtml(t("heatingAdvice.exampleLabel"))}</b> ${escapeHtml(example)}</span>
          </aside>
        </div>

        <details class="strategy-details">
          <summary>
            <span class="other-icon" aria-hidden="true">▦</span>
            <span class="other-copy">
              <strong>${escapeHtml(t("heatingAdvice.matrixTitle"))}</strong>
              <small>${escapeHtml(t("heatingAdvice.matrixSub"))}</small>
            </span>
            <span class="chevron" aria-hidden="true">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>
            </span>
          </summary>
          <div class="details-content" style="padding-top:10px">
            <div class="oq-advice-matrix-wrap" style="margin:0">
              <table class="oq-advice-matrix">
                <thead><tr><th>${escapeHtml(t("heatingAdvice.colSetting"))}</th><th>Power House</th><th>${escapeHtml(t("heatingAdvice.colCurve"))}</th></tr></thead>
                <tbody>
                  <tr><td>${escapeHtml(t("heatingAdvice.rowRoomTemp"))}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td><td>${pill(t("heatingAdvice.pillCorrection"),"recommended")}</td></tr>
                  <tr><td>${escapeHtml(t("heatingAdvice.rowRoomSetpoint"))}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td><td>${pill(t("heatingAdvice.pillCorrection"),"recommended")}</td></tr>
                  <tr><td>${escapeHtml(t("heatingAdvice.rowOutside"))}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td></tr>
                  <tr><td>${escapeHtml(t("heatingAdvice.rowSupply"))}</td><td>${pill(t("heatingAdvice.pillLimit"),"muted")}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td></tr>
                  <tr><td>${escapeHtml(t("heatingAdvice.rowFlow"))}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td><td>${pill(t("heatingAdvice.pillRequired"),"required")}</td></tr>
                  <tr><td>${escapeHtml(t("heatingAdvice.permissionKicker"))}</td><td>${pill(t("heatingAdvice.labelDisabled"),"muted")}</td><td>${pill(recommendationAvailable ? recommendedLabel : t("heatingAdvice.pillInactiveSource"),"recommended")}</td></tr>
                </tbody>
              </table>
            </div>
            <p style="margin:10px 0 0;color:#5f6b7d;font-size:12.5px;line-height:1.5">${escapeHtml(t("heatingAdvice.matrixFootPre"))} <a href="https://openquatt.github.io/OpenQuatt/instellingen-en-meetwaarden.html#5-bronselectie" target="_blank" rel="noreferrer">${escapeHtml(t("heatingAdvice.matrixFootLink"))}</a>.</p>
          </div>
        </details>

        ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
        <div class="modal-footer">
          ${deviant && recommendationAvailable ? `<div class="change-note">${escapeHtml(t("heatingAdvice.changeNotePre"))} <strong>${escapeHtml(t("heatingAdvice.permissionKicker"))}</strong> ${escapeHtml(t("heatingAdvice.changeNotePost"))}</div><button class="button secondary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("heatingAdvice.keepChoice"))}</button><button class="button primary" type="button" data-oq-action="apply-heating-strategy-advice" data-heating-enable-target="${escapeHtml(recommended)}" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("heatingAdvice.saving")) : escapeHtml(t("heatingAdvice.applyLabel", { label: recommendedLabel }))}</button>` : `<button class="button secondary" type="button" data-oq-action="close-system-modal" style="margin-left:auto">${escapeHtml(t("heatingAdvice.close"))}</button>`}
        </div>
      </div>
    `,
  });
}
