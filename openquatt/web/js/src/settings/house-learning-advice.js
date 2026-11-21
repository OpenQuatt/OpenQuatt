import { escapeHtml } from "../core/html.js";
import { getNumberMeta, normalizeNumber } from "../core/entity-store.js";
import { formatNumber, t } from "../i18n/index.js";
import { configuredLinePowerAt, learnedLinePowerAt } from "./house-learning-chart.js";
import { houseLearningCheckFailureKey } from "./house-learning-readiness.js";

const format = (value, maximumFractionDigits = 1) => formatNumber(value, { maximumFractionDigits });

function settingWithinRange(key, value) {
  const { min, max, step } = getNumberMeta(key);
  return Number.isFinite(value) && value >= min && value <= max && step > 0
    ? normalizeNumber(key, value) : null;
}

export function proposedHouseLineSettings(coldC, line) {
  if (line.settings !== undefined) return line.settings?.coldC === coldC ? line.settings : null;
  const ratedW = settingWithinRange("housePower", learnedLinePowerAt(line, coldC));
  const zeroC = settingWithinRange("houseOutdoorMax", line.t0);
  return ratedW > 0 && zeroC != null && zeroC > coldC ? { coldC, zeroC, ratedW } : null;
}

export function renderHouseLearningAdvice(configured, proposal, { canRestore, busy, notice, retesting, applyPending } = {}) {
  const { coldC, zeroC, ratedW } = configured;
  if (![coldC, zeroC, ratedW].every(Number.isFinite) || ratedW <= 0 || zeroC <= coldC) return "";
  const { h, t0, checkMinC, checkMaxC, checkDays, referenceRoomC, check } = proposal || {};
  const hasProposal = Number.isFinite(h) && h > 0 && Number.isFinite(t0);
  if (!hasProposal && !canRestore && !notice && !retesting) return "";

  const checked = check && [checkMinC, checkMaxC, referenceRoomC].every(Number.isFinite)
    && checkMaxC >= checkMinC && Number.isInteger(checkDays) && checkDays >= 1;
  const kw = watts => format(watts / 1000, 2) + " kW";
  const celsius = value => format(value, 2) + " °C";
  const referenceC = -10;
  const settings = hasProposal ? proposedHouseLineSettings(coldC, proposal) : null;
  const applied = settings && settings.ratedW === ratedW && settings.zeroC === zeroC;
  const cutoffC = settings?.zeroC ?? t0;
  const powerAtReference = settings
    ? configuredLinePowerAt(settings, referenceC)
    : learnedLinePowerAt({ h, t0 }, referenceC);
  const outsideCheck = temperature => !checked || temperature < checkMinC || temperature > checkMaxC;
  const row = (label, current, proposed, temperature) => `
    <tr><th scope="row">${escapeHtml(label)}</th><td>${escapeHtml(current)}</td>
    <td><strong>${escapeHtml(proposed)}</strong>${outsideCheck(temperature)
      ? `<small>${escapeHtml(t("houseLearning.advice.estimate"))}</small>` : ""}</td></tr>`;
  const range = checked ? t("houseLearning.advice.range", {
    min: format(checkMinC, 2), max: format(checkMaxC, 2), count: format(checkDays, 0), room: format(referenceRoomC),
  }) : "";
  const warning = retesting ? t("houseLearning.advice.retesting") : !hasProposal ? "" : !settings ? t("houseLearning.advice.outOfSettingsRange")
    : !checked ? t("houseLearning.advice.unchecked") : check.passed ? "" : t(houseLearningCheckFailureKey(checkMaxC - checkMinC, check.minSpreadC));
  const message = notice || (applied && !canRestore ? t("houseLearning.advice.matches") : "");
  const title = escapeHtml(t("houseLearning.advice.title"));
  const expected = `data-cold-c="${coldC}" data-expected-rated-w="${ratedW}" data-expected-zero-c="${zeroC}"`;
  return `<section class="oq-house-learning-readiness" aria-label="${title}">
    <h5>${title}</h5>
    ${hasProposal ? `<table class="oq-house-learning-advice-table" aria-label="${title}">
      <thead><tr><th scope="col">${escapeHtml(t("houseLearning.advice.setting"))}</th>
      <th scope="col">${escapeHtml(t("houseLearning.advice.current"))}</th>
      <th scope="col">${escapeHtml(t("houseLearning.advice.proposed"))}</th></tr></thead>
      <tbody>
        ${row(t("houseLearning.advice.power"), kw(configuredLinePowerAt(configured, referenceC)), kw(powerAtReference), referenceC)}
        ${row(t("houseLearning.advice.balance"), celsius(zeroC), celsius(cutoffC), cutoffC)}
      </tbody>
    </table>` : ""}
    ${range ? `<p>${escapeHtml(range)}${outsideCheck(referenceC) || outsideCheck(cutoffC)
      ? ` ${escapeHtml(t("houseLearning.advice.extrapolation"))}` : ""}</p>` : ""}
    ${warning ? `<p class="oq-settings-action-note${retesting ? "" : " oq-settings-action-note--warning"}" id="oq-house-line-advice-warning"${retesting ? ' role="status"' : ""}>${escapeHtml(warning)}</p>` : ""}
    <div class="oq-house-learning-advice-action">
      ${settings && (!applied || retesting) ? `<button class="oq-helper-button oq-helper-button--primary" type="button"
        data-oq-action="apply-house-line" data-rated-w="${settings.ratedW}" data-zero-c="${settings.zeroC}"
        ${warning ? 'aria-describedby="oq-house-line-advice-warning"' : ""}
        ${expected} ${busy || retesting || applyPending ? "disabled" : ""}>${escapeHtml(t("houseLearning.advice.apply"))}</button>` : ""}
      ${canRestore ? `<button class="oq-helper-button oq-helper-button--ghost" type="button"
        data-oq-action="restore-house-line" ${expected} ${busy ? "disabled" : ""}>${escapeHtml(t("houseLearning.advice.restore"))}</button>` : ""}
      ${message ? `<span role="status">${escapeHtml(message)}</span>` : ""}
    </div>
  </section>`;
}
