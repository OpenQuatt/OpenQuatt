import { hasEntity } from "../core/app-shared.js";
import { renderModalShell } from "../core/modal-shell.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { getHeatingEnableAdvice, getHeatingEnableCurrent, getHeatingEnableRecommendation } from "../core/heating-strategy-matrix.js";

function formatLabel(value) {
  const v = String(value || "").trim();
  if (!v) return "—";
  if (v === "Disabled") return "Niet gebruiken";
  if (v === "OT thermostat") return "OpenTherm-thermostaat";
  if (v === "HA input") return "HA input";
  if (v === "CIC") return "CIC";
  if (v === "API input") return "API-invoer";
  if (v === "MQTT") return "MQTT";
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
  const recommendedLabel = recommendationAvailable ? formatLabel(recommended) : "Eerst bron activeren";
  const currentLabel = formatLabel(current);
  const deviant = Boolean(advice.deviant && hasEntity("heatingEnableSource"));
  const busy = state.busyAction === "quickstart-heating-enable";

  const strategyLabel = isCurve ? "Water Temperature Control" : "Power House";
  const strategySub = isCurve ? "Stooklijn · buitentemp bepaalt aanvoer" : "Automatisch · huismodel + kamer";
  const isPH = !isCurve;

  // Dynamic texts per strategy
  const currentDesc = (() => {
    if (current === "Disabled") return isCurve ? "Stooklijn kan verwarmen terwijl kamer al warm is." : "Power House bepaalt zelf wanneer warmte nodig is.";
    if (current === "OT thermostat") return "OpenTherm-thermostaat bepaalt óf verwarming mag starten.";
    if (current === "CIC") return "CIC bepaalt óf verwarming mag starten.";
    if (current === "HA input") return "Home Assistant bepaalt óf verwarming mag starten.";
    if (current === "API input") return "API bepaalt óf verwarming mag starten.";
    return "Externe bron bepaalt óf verwarming mag starten.";
  })();
  const recommendedDesc = isPH
    ? "Power House bepaalt zelf wanneer warmte nodig is."
    : recommendationAvailable
      ? "Thermostaat bepaalt óf verwarming nodig is."
      : "Configureer en activeer eerst één gekoppelde thermostaatbron.";
  const currentMini = deviant ? (isPH ? "externe gate actief" : recommendationAvailable ? "andere bron" : "bron niet actief") : "";
  const recommendedMini = isPH ? "voor Power House" : recommendationAvailable ? "voor stooklijn" : "eerst configureren";
  const statusBadge = deviant
    ? '<span class="status-badge"><span class="status-dot"></span>Aanpassing aanbevolen</span>'
    : '<span class="status-badge status-badge--ok"><span class="status-dot"></span>Komt overeen</span>';
  const title = !recommendationAvailable && isCurve ? "Thermostaatbron activeren" : deviant ? "Regeling controleren" : "Regeling — advies gevolgd";
  const subtitle = !recommendationAvailable && isCurve
    ? "Quick Start past de warmtetoestemming pas aan zodra de gekozen thermostaatbron actief en gekoppeld is."
    : deviant
      ? `De huidige keuze voor warmtetoestemming past niet goed bij ${strategyLabel}.`
    : `Je warmtetoestemming komt overeen met het advies voor ${strategyLabel}.`;
  const decisionClass = deviant ? "decision" : "decision decision--ok";

  const whyTitle = isPH ? "Waarom dit advies?" : "Waarom thermostaat bij stooklijn?";
  const whyText = isPH
    ? "Power House gebruikt al buitentemperatuur, kamertemperatuur, setpoint en het huismodel. Een tweede aan/uit-regelaar kan die modulatie onnodig onderbreken."
    : "De stooklijn bepaalt hoe warm het water moet zijn; de thermostaat of zone-regeling bepaalt of verwarming nodig is. Zonder thermostaat verwarmt de stooklijn ook als de kamer al warm is.";
  const whenTitle = isPH ? "Wanneer wél een externe toestemming?" : "Wanneer Niet gebruiken behouden?";
  const whenText = isPH ? "Alleen als een externe zone-regeling bewust als harde gate moet dienen." : "Alleen bij permanent open afgifte zonder thermostaat, volledig weersafhankelijk.";
  const example = isPH ? "geen zone open → blokkeren" : "open vloer altijd open";


  return renderModalShell({
    modalId: "system",
    titleId: "oq-heating-advice-modal-title",
    kicker: "Regeling",
    title,
    copy: subtitle,
    closeAction: "close-system-modal",
    closeLabel: "Sluit advies",
    className: "oq-helper-modal--wide",
    bodyMarkup: `
      <div class="oq-advice-redesign">
        <section class="decision ${deviant ? "" : "decision--ok"}" aria-label="Huidige en aanbevolen instelling">
          <div class="strategy-row">
            <span class="strategy-icon" aria-hidden="true">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 11.5 12 4l9 7.5"/><path d="M5.5 10.5V20h13v-9.5"/><path d="M9 20v-5h6v5"/></svg>
            </span>
            <div class="strategy-copy">
              <span class="eyebrow">Huidige strategie</span>
              <span class="strategy-title">${escapeHtml(strategyLabel)}</span>
              <span class="strategy-sub">${escapeHtml(strategySub)}</span>
            </div>
            ${statusBadge}
          </div>
          <div style="padding: 8px 16px 0; color: #7d8797; font-size: 11px; font-weight: 800; letter-spacing: .055em; text-transform: uppercase;">Warmtetoestemming</div>
          <div class="comparison">
            <article class="choice current">
              <div class="choice-label"><span>Huidig</span>${currentMini ? `<span class="mini">${escapeHtml(currentMini)}</span>` : ""}</div>
              <strong>${escapeHtml(currentLabel)}</strong>
              <p>${escapeHtml(currentDesc)}</p>
            </article>
            <div class="arrow-wrap" aria-hidden="true">
              <span class="arrow-circle">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"/><path d="m14 7 5 5-5 5"/></svg>
              </span>
            </div>
            <article class="choice recommended">
              <div class="choice-label"><span>Aanbevolen</span><span class="mini">${escapeHtml(recommendedMini)}</span></div>
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
            <strong>Verwarming blijft gewoon actief</strong>
            <p><b>Niet gebruiken</b> betekent: geen externe warmtetoestemming. Het betekent niet “verwarming uit”.</p>
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
              <li><span class="check">✓</span><span>${escapeHtml(isPH ? "Rustiger regelen, met minder kans op extra stops en starts." : "Voorkomt verwarmen terwijl de kamer al warm is.")}</span></li>
              <li><span class="check">✓</span><span>${escapeHtml(isPH ? "Één duidelijke regelaar bepaalt de warmtevraag." : "Thermostaat bepaalt óf, stooklijn hoe warm.")}</span></li>
            </ul>
          </section>
          <aside class="panel">
            <div class="panel-head">
              <span class="section-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16"/><path d="M7 4v6"/><path d="M17 4v6"/><rect x="4" y="7" width="16" height="13" rx="2"/><path d="M8 13h3"/><path d="M13 13h3"/></svg>
              </span>
              <h2>${escapeHtml(whenTitle)}</h2>
            </div>
            <p>${escapeHtml(isPH ? "Alleen als een externe zone-regeling bewust als harde gate moet dienen." : "Alleen bij permanent open afgifte zonder thermostaat.")}</p>
            <span class="example"><b>Voorbeeld</b> ${escapeHtml(example)}</span>
          </aside>
        </div>

        <details class="strategy-details">
          <summary>
            <span class="other-icon" aria-hidden="true">▦</span>
            <span class="other-copy">
              <strong>Volledige matrix per strategie</strong>
              <small>Kamer, buiten, aanvoer, flow en toestemming — wat is vereist of aanbevolen.</small>
            </span>
            <span class="chevron" aria-hidden="true">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>
            </span>
          </summary>
          <div class="details-content" style="padding-top:10px">
            <div class="oq-advice-matrix-wrap" style="margin:0">
              <table class="oq-advice-matrix">
                <thead><tr><th>Instelling</th><th>Power House</th><th>Stooklijn</th></tr></thead>
                <tbody>
                  <tr><td>Kamertemperatuur</td><td>${pill("vereist","required")}</td><td>${pill("actief als correctie","recommended")}</td></tr>
                  <tr><td>Kamer-setpoint</td><td>${pill("vereist","required")}</td><td>${pill("actief als correctie","recommended")}</td></tr>
                  <tr><td>Buitentemperatuur</td><td>${pill("vereist","required")}</td><td>${pill("vereist","required")}</td></tr>
                  <tr><td>Aanvoertemperatuur</td><td>${pill("nodig voor begrenzing","muted")}</td><td>${pill("vereist","required")}</td></tr>
                  <tr><td>Flow</td><td>${pill("vereist","required")}</td><td>${pill("vereist","required")}</td></tr>
                  <tr><td>Warmtetoestemming</td><td>${pill("Niet gebruiken","muted")}</td><td>${pill(recommendationAvailable ? recommendedLabel : "actieve thermostaatbron","recommended")}</td></tr>
                </tbody>
              </table>
            </div>
            <p style="margin:10px 0 0;color:#5f6b7d;font-size:12.5px;line-height:1.5">Andere instellingen blijven relevant, maar de keuze hierboven is de kern van #474. Meer uitleg in de <a href="https://openquatt.github.io/OpenQuatt/instellingen-en-meetwaarden.html#5-bronselectie" target="_blank" rel="noreferrer">documentatie</a>.</p>
          </div>
        </details>

        ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
        <div class="modal-footer">
          ${deviant && recommendationAvailable ? `<div class="change-note">Alleen <strong>Warmtetoestemming</strong> wordt aangepast.</div><button class="button secondary" type="button" data-oq-action="close-system-modal">Huidige keuze behouden</button><button class="button primary" type="button" data-oq-action="apply-heating-strategy-advice" data-heating-enable-target="${escapeHtml(recommended)}" ${busy ? "disabled" : ""}>${busy ? "Opslaan..." : `Instellen op ‘${escapeHtml(recommendedLabel)}’`}</button>` : `<button class="button secondary" type="button" data-oq-action="close-system-modal" style="margin-left:auto">Sluiten</button>`}
        </div>
      </div>
    `,
  });
}
