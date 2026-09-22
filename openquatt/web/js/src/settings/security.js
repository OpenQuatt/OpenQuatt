import { getApiSecurityStatusDetail, getApiSecurityStatusLabel, getWebAuthStatusDetail, getWebAuthStatusLabel } from "../features/security-access.js";
import { renderSettingsSection } from "./controls.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";

export { getApiSecurityStatusDetail, getApiSecurityStatusLabel } from "../features/security-access.js";

  export function renderSettingsAccessSecuritySection() {
    const items = [
      ["login", t("settingsSecurity.loginLabel"), getWebAuthStatusLabel(), getWebAuthStatusDetail(), "open-login-modal"],
      ["api", t("settingsSecurity.apiLabel"), getApiSecurityStatusLabel(), getApiSecurityStatusDetail(), "open-api-security-modal"],
    ];
    return renderSettingsSection(
      t("settingsSecurity.sectionGroup"),
      t("settingsSecurity.sectionTitle"),
      t("settingsSecurity.sectionCopy"),
      `
        <div class="oq-settings-access-security-shell">
          ${items.map(([id, label, status, detail, action]) => `
          <div class="oq-settings-quickstart-status" data-oq-access-security-item="${id}">
            <div class="oq-settings-quickstart-status-row">
              <div>
                <p class="oq-settings-quickstart-status-label">${escapeHtml(label)}</p>
                <strong class="oq-settings-quickstart-status-value">${escapeHtml(status)}</strong>
                <p class="oq-settings-quickstart-status-copy">${escapeHtml(detail)}</p>
              </div>
              <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="${action}">${escapeHtml(id === "api" ? t("settingsSecurity.statusAction") : t("settingsSecurity.adjustAction"))}</button>
            </div>
          </div>
          `).join("")}
        </div>
      `,
    );
  }
