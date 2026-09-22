import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { default: nlCatalogue } = await import("../js/src/i18n/nl.js");

test("privacy settings compose two cards with nested detail disclosures", async () => {
  const settingsSource = await readFile(new URL("../js/src/settings/privacy.js", import.meta.url), "utf8");

  assert.match(settingsSource, /t\("settingsPrivacy\.sectionCopy"\)/);
  assert.match(nlCatalogue.settingsPrivacy.sectionCopy, /Beide opties staan standaard uit en kunnen onafhankelijk van elkaar worden ingeschakeld/);
  assert.match(nlCatalogue.settingsPrivacy.sectionCopy, /Jouw privacy blijft altijd beschermd/);
  assert.match(settingsSource, /renderSettingsSection\(/);
  assert.match(settingsSource, /disclosure: renderUsageTelemetryDisclosure\(\{ collapsible: true/);
  assert.match(settingsSource, /disclosure: renderPerformanceTelemetryDisclosure\(\{ collapsible: true/);
  assert.match(settingsSource, /renderPerformanceTelemetryConsent\(\{ enabled: performanceEnabled, busy: performanceBusy, settings: true,/);
  assert.doesNotMatch(settingsSource, /\? renderUsageTelemetryDisclosure/);
  assert.doesNotMatch(settingsSource, /\? renderPerformanceTelemetryDisclosure/);
});

test("consent cards lead with why and toggle Aan/Uit", async () => {
  const usageConsentSource = await readFile(new URL("../js/src/features/usage-telemetry.js", import.meta.url), "utf8");
  const performanceConsentSource = await readFile(
    new URL("../js/src/features/performance-telemetry.js", import.meta.url),
    "utf8",
  );

  for (const source of [usageConsentSource, performanceConsentSource]) {
    assert.doesNotMatch(source, /oq-usage-consent-kicker/);
    assert.match(source, /\$\{disclosure\}/);
    assert.match(source, /t\("common\.on"\),\s*\n?\s*t\("common\.off"\),/);
  }
  assert.match(usageConsentSource, /t\("usage\.consentTitle"\)/);
  assert.equal(nlCatalogue.usage.consentTitle, "Technische statistieken delen");
  assert.match(usageConsentSource, /t\("usage\.consentSettingsCopy"\)/);
  assert.match(nlCatalogue.usage.consentSettingsCopy, /Help OpenQuatt stabieler en betrouwbaarder te maken/);
  assert.match(performanceConsentSource, /t\("performance\.consentTitle"\)/);
  assert.equal(nlCatalogue.performance.consentTitle, "Warmtepompprestaties delen");
  assert.match(performanceConsentSource, /t\("performance\.consentSettingsPre"\)/);
  assert.match(nlCatalogue.performance.consentSettingsPre, /vermogens- en COP-modellen van OpenQuatt te controleren en verbeteren/);
  assert.match(performanceConsentSource, /t\("performance\.consentSettingsStrong"\)/);
  assert.match(performanceConsentSource, /t\("performance\.consentSettingsPost"\)/);
  assert.match(nlCatalogue.performance.consentSettingsPost, /-verwarmingsstrategie\./);
});

test("nested disclosures pair detail columns with facts and why boxes", async () => {
  const usageDisclosureSource = await readFile(new URL("../js/src/features/usage-telemetry.js", import.meta.url), "utf8");
  const performanceDisclosureSource = await readFile(
    new URL("../js/src/features/performance-telemetry.js", import.meta.url),
    "utf8",
  );
  const viewActionsSource = await readFile(new URL("../js/src/features/view-actions.js", import.meta.url), "utf8");
  const stateSlicesSource = await readFile(new URL("../js/src/core/state-slices.js", import.meta.url), "utf8");

  for (const [source, ns] of [[usageDisclosureSource, "usage"], [performanceDisclosureSource, "performance"]]) {
    assert.match(source, /oq-usage-consent-details/);
    assert.match(source, new RegExp(`t\\("${ns}\\.detailsTitle"\\)`));
    assert.equal(nlCatalogue[ns].detailsTitle, "Welke gegevens worden gedeeld?");
    assert.match(source, /oq-usage-facts-grid/);
    assert.match(source, new RegExp(`t\\("${ns}\\.includedTitle"\\)`));
    assert.match(source, new RegExp(`t\\("${ns}\\.excludedTitle"\\)`));
    assert.match(source, new RegExp(`t\\("${ns}\\.oftenTitle"\\)`));
    assert.match(source, new RegExp(`t\\("${ns}\\.notTitle"\\)`));
    assert.match(source, new RegExp(`t\\("${ns}\\.whyTitle"\\)`));
    assert.doesNotMatch(source, /oq-usage-disclosure-intro/);
    assert.doesNotMatch(source, /oq-usage-disclosure--collapsible/);
  }
  assert.match(usageDisclosureSource, /t\("usage\.oftenCopy"\)/);
  assert.match(nlCatalogue.usage.oftenCopy, /Na inschakelen verstuurt OpenQuatt vrijwel direct en daarna ongeveer elk uur/);
  assert.match(nlCatalogue.usage.whyCopy, /problemen sneller opsporen en OpenQuatt verder verbeteren/);
  assert.match(performanceDisclosureSource, /t\("performance\.oftenCopy"\)/);
  assert.match(nlCatalogue.performance.oftenCopy, /Maximaal één keer per kwartier\. De metingen worden lokaal samengevat/);
  assert.match(nlCatalogue.performance.whyCopy, /Zo kan Power House nog slimmer en efficiënter verwarmen/);
  // Quick Start keeps the standalone disclosure untouched.
  assert.match(usageDisclosureSource, /t\("usage\.headTitle"\)/);
  assert.equal(nlCatalogue.usage.headTitle, "Wat gaat er mee?");
  for (const source of [usageDisclosureSource, performanceDisclosureSource]) {
    assert.equal(source.split("${excludedDetail}").length - 1, 1);
  }
  assert.match(viewActionsSource, /"toggle-performance-telemetry-details": \(button, event\) => \{\s*toggleDetails\(event, button, "\.oq-usage-consent-details", "performanceTelemetryDetailsOpen"\);/);
  assert.match(stateSlicesSource, /performanceTelemetryDetailsOpen: false,/);
});

test("performance disclosure matches the firmware payload scope", async () => {
  const disclosureSource = await readFile(new URL("../js/src/features/performance-telemetry.js", import.meta.url), "utf8");
  const telemetryCpp = await readFile(
    new URL("../../../components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetry.cpp", import.meta.url),
    "utf8",
  );

  assert.match(telemetryCpp, /"bid"/);
  assert.match(telemetryCpp, /"v":1/);
  assert.doesNotMatch(telemetryCpp, /"pem"/);
  assert.doesNotMatch(telemetryCpp, /"mk"/);
  for (const [key, needle] of [
    ["inSystemCopy", /Willekeurig installatie-ID, OpenQuatt-versie en Single of Duo/],
    ["inHpCopy", /Generatie \(V1 \/ V1\.5 \/ V2\) en versie van het prestatiemodel/],
    ["inPointCopy", /Buitentemperatuur en waterflow/],
    ["inPerHpCopy", /Compressorlevel en frequentie, water in\/uit/],
    ["inPerHpCopy", /bodemplaatverwarming/],
    ["inRangeCopy", /Alleen stabiele verwarmingsminuten/],
    ["exIdentityAccessCopy", /Geen wifi- of inloggegevens, MAC-adres of gebruikersnaam/],
    ["exHomeCopy", /Geen kamer-\/thermostaatgegevens of coolingmetingen/],
    ["exSelectionCopy", /ongeldige\/incomplete meetperioden/],
  ]) {
    assert.match(disclosureSource, new RegExp(`t\\("performance\\.${key}"\\)`));
    assert.match(nlCatalogue.performance[key], needle);
  }
  assert.match(disclosureSource, /t\("performance\.exampleTitle"\)/);
  assert.equal(nlCatalogue.performance.exampleTitle, "Voorbeeld van het verzonden bericht (JSON)");
  for (const needle of [
    /batch-ID/,
    /vensterstart/,
    /minuutstart/,
    /prestatiekaart/,
    /Power Input-model/,
  ]) {
    assert.doesNotMatch(disclosureSource, needle);
  }
});
