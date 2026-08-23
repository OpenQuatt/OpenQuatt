import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { state } from "../core/state.js";
import { renderUsageTelemetryConsent, renderUsageTelemetryDisclosure } from "../features/usage-telemetry.js";
import { renderSettingsSection } from "./controls.js";

export function renderSettingsPrivacySection() {
  if (!hasEntity("usageTelemetryEnabled")) {
    return "";
  }
  const enabled = isEntityActive("usageTelemetryEnabled");
  const busy = state.loadingEntities || state.busyAction === "switch-usageTelemetryEnabled";

  return renderSettingsSection(
    "Privacy",
    "Gebruiksstatistieken",
    "Hier kies je of OpenQuatt beperkte technische gebruiksstatistieken en na een echte firmwarecrash beperkte ruwe crashinformatie deelt. Reguliere logs, wifi-netwerknaam, wifi-wachtwoord en andere wachtwoorden of inloggegevens worden nooit meegestuurd. Een niet-bevestigde keuze blijft uit.",
    `<div class="oq-usage-settings">
      ${renderUsageTelemetryConsent({ enabled, busy, settings: true })}
      ${renderUsageTelemetryDisclosure({ collapsible: true, idPrefix: "oq-settings-usage", open: state.usageTelemetryDetailsOpen })}
    </div>`,
  );
}
