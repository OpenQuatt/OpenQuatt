import { getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon, SETTINGS_GROUP_IDS, SETTINGS_GROUPS } from "../core/config.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { formatValue, getEntityValue, normalizeNumber, toTimeInputValue } from "../core/entity-store.js";
import { state } from "../core/state.js";
import { setSettingsRenderControls } from "../core/settings-render-controls.js";
import { formatDiagnosticsDateTime, formatUptimeFromMeta, getDeviceIpAddress, getInstallationLabel } from "../features/device-context.js";
import { getUpdateStatus } from "../features/firmware-update.js";
import { getEspTemperatureLabel } from "../features/header-status.js";
import { getWebAuthStatusDetail, getWebAuthStatusLabel } from "../features/security-access.js";
import { getCommissioningStatusValue, getSelectEntityOptions, renderSettingsSection } from "./controls.js";
import { renderSettingsCoolingSection } from "./cooling.js";
import { renderSettingsFlowSection, renderSettingsHeatingSection } from "./heating.js";
import { renderSettingsElectricalCurrentLimitSection } from "./electrical-limit.js";
import { renderSettingsAuxRelaySection, renderSettingsBoilerCvSection, renderSettingsCompressorSection, renderSettingsDiagnosticsSection, renderSettingsGenerationSection, renderSettingsInstallationMonitoringSection, renderSettingsOduRuntimeFrequencySection, renderSettingsQuickStartSection } from "./installation.js";
import { renderSettingsMqttSection, renderSettingsOpenThermCicSection, renderSettingsSensorSelectionSection } from "./integrations.js";
import { renderSettingsPrivacySection } from "./privacy.js";
import { getApiSecurityStatusDetail, getApiSecurityStatusLabel, renderSettingsAccessSecuritySection } from "./security.js";
import { renderSettingsCounterServiceSection, renderSettingsServiceSection } from "./service.js";
import { renderSettingsSilentSection } from "./silent.js";
import { renderSettingsBackupSection, renderSettingsTrendSection } from "./storage.js";
import { renderSettingsWaterSection } from "./water.js";
import { escapeHtml } from "../core/html.js";

function syncFrequencyRangeControl(control) {
  const minInput = control?.querySelector('[data-oq-range-role="min"]');
  const maxInput = control?.querySelector('[data-oq-range-role="max"]');
  if (!minInput || !maxInput) {
    return;
  }
  let minValue = Number(minInput.value);
  let maxValue = Number(maxInput.value);
  const scaleMin = Number(minInput.min);
  const scaleMax = Number(minInput.max);
  const span = Math.max(1, scaleMax - scaleMin);
  const disabled = minValue === 0 || maxValue === 0;
  if (disabled) {
    minValue = maxValue = 0;
    minInput.value = maxInput.value = "0";
  }
  const invalid = !disabled && minValue > maxValue;
  control.classList.toggle("is-disabled", disabled);
  control.classList.toggle("is-invalid", invalid);
  control.style.setProperty("--oq-range-start", `${((minValue - scaleMin) / span) * 100}%`);
  control.style.setProperty("--oq-range-end", `${((maxValue - scaleMin) / span) * 100}%`);
  const value = control.querySelector("[data-oq-range-value]");
  if (value) {
    value.textContent = disabled ? "Geen uitsluiting" : invalid ? "Ongeldig bereik" : `${minValue}–${maxValue} Hz`;
  }
}



  export function renderSettingsGroupNav() {
    const activeGroup = SETTINGS_GROUP_IDS.has(state.settingsGroup) ? state.settingsGroup : SETTINGS_GROUPS[0].id;
    return `
      <nav class="oq-settings-group-nav" aria-label="Instellingen groepen">
        ${SETTINGS_GROUPS.map((group) => `
          <button
            class="oq-settings-group-button${group.id === activeGroup ? " is-active" : ""}"
            type="button"
            data-oq-action="select-settings-group"
            data-group-id="${escapeHtml(group.id)}"
            aria-pressed="${group.id === activeGroup ? "true" : "false"}"
          >
            ${renderOqIcon(group.icon, "oq-settings-group-button-icon")}
            <span class="oq-settings-group-button-label">${escapeHtml(group.label)}</span>
          </button>
        `).join("")}
      </nav>
    `;
  }

  export function renderSettingsGroupContent() {
    const activeGroup = SETTINGS_GROUP_IDS.has(state.settingsGroup) ? state.settingsGroup : SETTINGS_GROUPS[0].id;
    const sections = activeGroup === "installation"
      ? [
          renderSettingsGenerationSection(),
          renderSettingsBoilerCvSection(),
          renderSettingsAuxRelaySection(),
          renderSettingsFlowSection(),
          renderSettingsSilentSection(),
          renderSettingsWaterSection(),
          renderSettingsCompressorSection(),
          renderSettingsElectricalCurrentLimitSection(),
          renderSettingsOduRuntimeFrequencySection(),
        ]
      : activeGroup === "service"
        ? [
            renderSettingsInstallationMonitoringSection(),
            renderSettingsServiceSection(),
            renderSettingsCounterServiceSection(),
          ]
      : activeGroup === "heating"
        ? [renderSettingsHeatingSection()]
      : activeGroup === "cooling"
        ? [renderSettingsCoolingSection()]
        : activeGroup === "integrations"
            ? [
                renderSettingsOpenThermCicSection(),
                renderSettingsMqttSection(),
                renderSettingsSensorSelectionSection(),
              ]
            : [
                renderSettingsQuickStartSection(),
                renderSettingsTrendSection(),
                renderSettingsAccessSecuritySection(),
                renderSettingsPrivacySection(),
                renderSettingsBackupSection(),
                renderSettingsDiagnosticsSection(),
              ];

    return `
      <div class="oq-settings-group-stack">
        ${sections.filter(Boolean).join("")}
      </div>
    `;
  }

  export function patchSettingsDom() {
    if (!state.root || state.appView !== "settings") {
      return false;
    }

    const nav = state.root.querySelector(".oq-settings-group-nav");
    const stack = state.root.querySelector(".oq-settings-group-stack");
    if (!nav || !stack) {
      return false;
    }

    const activeGroup = SETTINGS_GROUP_IDS.has(state.settingsGroup) ? state.settingsGroup : SETTINGS_GROUPS[0].id;
    if (activeGroup === "service" || (activeGroup === "integrations" && state.focusedField)) {
      return false;
    }

    const navButtons = nav.querySelectorAll(".oq-settings-group-button");
    if (navButtons.length !== SETTINGS_GROUPS.length) {
      return false;
    }

    navButtons.forEach((button) => {
      const groupId = String(button.dataset.groupId || "");
      const active = groupId === activeGroup;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", active ? "true" : "false");
    });

    stack.querySelectorAll(".oq-settings-info").forEach((info) => {
      const infoId = String(info.dataset.oqSettingsInfo || "");
      const open = state.settingsInfoOpen === infoId;
      info.classList.toggle("is-open", open);
      const popover = info.querySelector(".oq-settings-info-popover");
      if (popover) {
        popover.hidden = !open;
      }
      const button = info.querySelector(".oq-settings-info-button");
      if (button) {
        button.setAttribute("aria-expanded", open ? "true" : "false");
      }
    });

    stack.querySelectorAll("[data-oq-settings-field]").forEach((card) => {
      const key = String(card.dataset.oqSettingsField || "");
      if (!key) {
        return;
      }

      const staticValue = card.querySelector(".oq-settings-static-value");
      if (staticValue) {
        const text = getEntityStateText(key);
        if (staticValue.textContent !== text) {
          staticValue.textContent = text;
        }
      }

      card.querySelectorAll('select[data-oq-field]').forEach((select) => {
        const fieldKey = String(select.dataset.oqField || key);
        const value = String(getEntityValue(fieldKey) || "");
        if (select.value !== value) {
          select.value = value;
        }
      });

      card.querySelectorAll('input[data-oq-field]').forEach((input) => {
        const fieldKey = String(input.dataset.oqField || key);
        if (input.type === "time" && input === document.activeElement) return;
        const value = input.type === "time"
          ? toTimeInputValue(getInputDraftValue(fieldKey))
          : String(getInputDraftValue(fieldKey) ?? "");
        if (input.type === "time") input.disabled = state.loadingEntities || state.savingTimeFields.has(fieldKey);
        if (input.value !== value) {
          input.value = value;
        }
      });

      const frequencyRange = card.querySelector('[data-oq-dual-range="true"]');
      if (frequencyRange) {
        syncFrequencyRangeControl(frequencyRange);
        return;
      }

      const sliderValue = card.querySelector(".oq-helper-slider-meta strong");
      const rangeInput = card.querySelector('input[type="range"][data-oq-field]');
      if (sliderValue && rangeInput) {
        const text = formatValue(key, normalizeNumber(key, getEntityValue(key)));
        if (sliderValue.textContent !== text) {
          sliderValue.textContent = text;
        }
      }
    });

    stack.querySelectorAll('[data-select-key]').forEach((button) => {
      const key = String(button.dataset.selectKey || "");
      const option = String(button.dataset.selectOption || "");
      const currentValue = String(getEntityValue(key) || "");
      const active = option === currentValue;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", active ? "true" : "false");
      if (key === "strategy") {
        button.disabled = state.loadingEntities || state.busyAction === "save-strategy" || state.busyAction === "save-heatingEnableSource";
      } else if (key === "hpGeneration") {
        button.disabled = state.loadingEntities || state.busyAction === "save-hpGeneration";
      } else if (key === "curveControlProfile") {
        button.disabled = state.loadingEntities || state.busyAction === "save-curveControlProfile";
      } else if (key === "phResponseProfile") {
        button.disabled = state.loadingEntities || state.busyAction === "save-phResponseProfile";
      }

      const shell = button.closest(".oq-settings-choice-card-shell");
      if (shell) {
        shell.classList.toggle("is-active", active);
      }
    });

    const customProfileCard = stack.querySelector(".oq-settings-choice-card--static.oq-settings-choice-card--custom");
    if (customProfileCard) {
      const customActive = String(getEntityValue("phResponseProfile") || "") === "Custom";
      customProfileCard.classList.toggle("is-active", customActive);
      const numberInputs = customProfileCard.querySelectorAll("input[data-oq-field]");
      numberInputs.forEach((input) => {
        const key = String(input.dataset.oqField || "");
        const value = String(getInputDraftValue(key) || "");
        if (input.value !== value) {
          input.value = value;
        }
      });
    }

    stack.querySelectorAll('[data-oq-action="toggle-overview-control"][data-control-key]').forEach((button) => {
      const key = String(button.dataset.controlKey || "");
      const current = Boolean(getEntityValue(key));
      const onLabel = String(button.dataset.onLabel || "Aan");
      const offLabel = String(button.dataset.offLabel || "Uit");
      const title = String(button.dataset.switchTitle || key);
      const stateLabel = current ? onLabel : offLabel;
      button.dataset.controlState = current ? "off" : "on";
      button.classList.toggle("is-on", current);
      button.setAttribute("aria-checked", current ? "true" : "false");
      button.setAttribute("aria-label", `${title}: ${stateLabel}`);
      button.disabled = state.loadingEntities || state.busyAction === `switch-${key}`;
    });

    stack.querySelectorAll("[data-oq-switch-pill]").forEach((pill) => {
      const key = String(pill.dataset.oqSwitchPill || "");
      const enabled = Boolean(getEntityValue(key));
      const onLabel = String(pill.dataset.onLabel || "Aan");
      const offLabel = String(pill.dataset.offLabel || "Uit");
      const label = enabled ? onLabel : offLabel;
      pill.classList.toggle("is-on", enabled);
      if (pill.textContent !== label) {
        pill.textContent = label;
      }
    });

    stack.querySelectorAll("[data-oq-switch-copy]").forEach((copyNode) => {
      const key = String(copyNode.dataset.oqSwitchCopy || "");
      const enabled = Boolean(getEntityValue(key));
      const onCopy = String(copyNode.dataset.onCopy || "");
      const offCopy = String(copyNode.dataset.offCopy || "");
      const copy = enabled ? onCopy : offCopy;
      copyNode.hidden = !copy;
      if (copyNode.textContent !== copy) {
        copyNode.textContent = copy;
      }
    });

    const generationStatus = stack.querySelector('button[data-oq-action="open-generation-modal"]')?.closest(".oq-settings-quickstart-status");
    if (generationStatus) {
      const valueNode = generationStatus.querySelector(".oq-settings-quickstart-status-value");
      const copyNode = generationStatus.querySelector(".oq-settings-quickstart-status-copy");
      const button = generationStatus.querySelector('button[data-oq-action="open-generation-modal"]');
      const currentLabel = getInstallationLabel();
      const entity = state.entities.hpGeneration || {};
      const canEdit = hasEntity("hpGeneration") && getSelectEntityOptions(entity).length > 0;
      if (valueNode) {
        const value = currentLabel || "Onbekend";
        if (valueNode.textContent !== value) {
          valueNode.textContent = value;
        }
      }
      if (copyNode) {
        const copy = "Pas dit aan als je een andere Quatt Hybrid hebt.";
        if (copyNode.textContent !== copy) {
          copyNode.textContent = copy;
        }
      }
      if (button) {
        button.disabled = !canEdit || state.loadingEntities || state.busyAction === "save-hpGeneration";
      }
    }

    const commissioningTeaser = stack.querySelector('button[data-oq-action="open-cm100-commissioning-modal"]')?.closest(".oq-settings-quickstart-status");
    if (commissioningTeaser) {
      const valueNode = commissioningTeaser.querySelector(".oq-settings-quickstart-status-value");
      const copyNode = commissioningTeaser.querySelector(".oq-settings-quickstart-status-copy");
      const button = commissioningTeaser.querySelector('button[data-oq-action="open-cm100-commissioning-modal"]');
      const cm100Status = getCommissioningStatusValue();
      const cm100Active = isEntityActive("cm100Active");
      if (valueNode && valueNode.textContent !== cm100Status) {
        valueNode.textContent = cm100Status;
      }
      const copy = cm100Active
        ? "CM100 is actief en klaar voor commissioning."
        : "Open de modal om CM100 te starten en de taken hieronder te ontgrendelen.";
      if (copyNode && copyNode.textContent !== copy) {
        copyNode.textContent = copy;
      }
      if (button) {
        button.disabled = state.loadingEntities;
      }
    }

    const quickStartStatus = stack.querySelector('button[data-oq-action="reset"]')?.closest(".oq-settings-quickstart-status");
    if (quickStartStatus) {
      const valueNode = quickStartStatus.querySelector(".oq-settings-quickstart-status-value");
      const copyNode = quickStartStatus.querySelector(".oq-settings-quickstart-status-copy");
      const button = quickStartStatus.querySelector('button[data-oq-action="reset"]');
      const statusLabel = state.complete === true ? "Afgerond" : state.complete === false ? "Open" : "Laden...";
      const statusCopy = state.complete === true
        ? "Quick Start is afgerond. Je kunt de status hier altijd weer openen met een reset."
        : state.complete === false
          ? "Quick Start staat nog open. Gebruik de resetknop om opnieuw te beginnen."
          : "De status van Quick Start wordt nog geladen.";
      if (valueNode && valueNode.textContent !== statusLabel) {
        valueNode.textContent = statusLabel;
      }
      if (copyNode && copyNode.textContent !== statusCopy) {
        copyNode.textContent = statusCopy;
      }
      if (button) {
        button.disabled = state.busyAction === "reset";
      }
    }

    const accessRows = stack.querySelectorAll('[data-oq-access-security-item]');
    if (accessRows.length) {
      accessRows.forEach((row) => {
        const item = String(row.dataset.oqAccessSecurityItem || "");
        const valueNode = row.querySelector(".oq-settings-quickstart-status-value");
        const copyNode = row.querySelector(".oq-settings-quickstart-status-copy");
        const button = row.querySelector("button[data-oq-action]");
        if (item === "login") {
          const statusLabel = getWebAuthStatusLabel();
          const statusCopy = getWebAuthStatusDetail();
          if (valueNode && valueNode.textContent !== statusLabel) {
            valueNode.textContent = statusLabel;
          }
          if (copyNode && copyNode.textContent !== statusCopy) {
            copyNode.textContent = statusCopy;
          }
        } else if (item === "api") {
          const statusLabel = getApiSecurityStatusLabel();
          const statusCopy = getApiSecurityStatusDetail();
          if (valueNode && valueNode.textContent !== statusLabel) {
            valueNode.textContent = statusLabel;
          }
          if (copyNode && copyNode.textContent !== statusCopy) {
            copyNode.textContent = statusCopy;
          }
        }
        if (button) {
          button.disabled = false;
        }
      });
    }


    const systemSummary = stack.querySelector(".oq-settings-system-summary");
    if (systemSummary) {
      const rows = systemSummary.querySelectorAll(".oq-settings-system-row");
      const values = {
        uptime: formatUptimeFromMeta(),
        ip: getDeviceIpAddress(),
        updates: getUpdateStatus(),
        datetime: formatDiagnosticsDateTime(),
        espTemp: getEspTemperatureLabel(),
        restart: "Opnieuw opstarten",
      };

      rows.forEach((row) => {
        const valueNode = row.querySelector(".oq-settings-system-row-value");
        const key = row.dataset.oqDiagnosticsRow || "";
        if (!valueNode) {
          return;
        }
        if (Object.prototype.hasOwnProperty.call(values, key)) {
          const nextValue = values[key];
          if (valueNode.textContent !== nextValue) {
            valueNode.textContent = nextValue;
          }
        }
      });

      const updateButton = systemSummary.querySelector('button[data-oq-action="open-update-modal"]');
      if (updateButton) {
        updateButton.disabled = false;
      }
      const restartButton = systemSummary.querySelector('button[data-oq-action="open-restart-confirm"]');
      if (restartButton) {
        const busyRestart = state.busyAction === "restartAction";
        restartButton.disabled = busyRestart;
        restartButton.textContent = busyRestart ? "Herstarten..." : "Herstarten";
      }
    }

    const curveShell = stack.querySelector(".oq-settings-curve-shell");
    const currentCurveMode = isCurveMode();
    if (Boolean(curveShell) !== currentCurveMode) {
      return false;
    }

    const customCardExists = Boolean(customProfileCard);
    const customProfileActive = String(getEntityValue("phResponseProfile") || "") === "Custom";
    if (customCardExists !== customProfileActive) {
      return false;
    }

    return true;
  }

  setSettingsRenderControls({ patch: patchSettingsDom });
