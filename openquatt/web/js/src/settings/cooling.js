import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { formatValue } from "../core/entity-store.js";
import { formatSettingsOptionLabel, renderSettingsAdvancedDisclosure, renderSettingsFieldCard, renderSettingsNumberField, renderSettingsOptionCardsField, renderSettingsSection, renderSettingsSelectField, renderSettingsSliderField, renderSettingsSwitchField } from "./controls.js";
import { escapeHtml } from "../core/html.js";

  export function renderSettingsCoolingFact(label, value) {
    return `
      <div class="oq-settings-cooling-fact">
        <span>${escapeHtml(label)}</span>
        <strong>${escapeHtml(value)}</strong>
      </div>
    `;
  }

  export function formatCoolingBlockReason(reason) {
    const value = String(reason || "").trim();
    if (!value) {
      return "";
    }

    const labels = {
      Ready: "Gereed om te koelen",
      "Waiting for room request": "Koeling toegestaan, wacht op kamertemperatuur boven koel-setpoint",
      "Cooling enabled, waiting for room temperature above cooling setpoint": "Koeling toegestaan, wacht op kamertemperatuur boven koel-setpoint",
      "No dew point source": "Geen dauwpuntbron",
      "OpenQuatt paused": "OpenQuatt gepauzeerd",
      "Cooling disabled": "Koeling uitgeschakeld",
      "Cooling minimum unavailable": "Minimale koel-aanvoer onbekend",
      "Flow too low": "Flow te laag",
      "Fallback active": "Dauwpuntsbenadering actief",
      "Fallback active (+0.5°C warm night)": "Dauwpuntsbenadering actief (+0,5°C warme nacht)",
      "Fallback active (+1.0°C very warm night)": "Dauwpuntsbenadering actief (+1,0°C zeer warme nacht)",
      "Fallback active (+1.5°C tropical night)": "Dauwpuntsbenadering actief (+1,5°C tropische nacht)",
      "User responsibility (no dew point or fallback)": "Expliciet toegestaan (geen dauwpunt of benadering)",
      "Fallback cooling active": "Dauwpuntsbenadering actief",
      "Fallback corrected by warm night": "Dauwpuntsbenadering gecorrigeerd door warme nacht",
      "Fallback blocked by tropical night": "Dauwpuntsbenadering geblokkeerd door tropische nacht",
    };

    return labels[value] || value;
  }

  function renderCoolingSilentLimitWarning() {
    const silentModeOverride = getEntityStateText("silentModeOverride", "").trim().toLowerCase();
    if (silentModeOverride === "off") {
      return "";
    }

    if (hasEntity("silentCoolingMaxHz")) {
      const silentCoolingMaxHz = getEntityNumericValue("silentCoolingMaxHz");
      if (!Number.isFinite(silentCoolingMaxHz) || silentCoolingMaxHz >= 120) {
        return "";
      }
      const prefix = isEntityActive("silentActive")
        ? "Stille modus is nu actief. Koelen wordt"
        : "Tijdens stille modus wordt koelen";
      return `<p class="oq-settings-cooling-limit-warning"><span class="oq-settings-cooling-limit-warning-icon" aria-hidden="true">!</span><span>${prefix} begrensd op een compressorfrequentie van ${escapeHtml(formatValue("silentCoolingMaxHz"))}.</span></p>`;
    }

    const coolingDemandMax = getEntityNumericValue("coolingDemandMax");
    const silentMax = getEntityNumericValue("silentMax");
    if (!hasEntity("silentMax") || !Number.isFinite(coolingDemandMax) || !Number.isFinite(silentMax) || coolingDemandMax <= silentMax) {
      return "";
    }

    const prefix = isEntityActive("silentActive")
      ? "Stille modus is nu actief. Koelen wordt"
      : "Tijdens stille modus wordt koelen";
    return `<p class="oq-settings-cooling-limit-warning"><span class="oq-settings-cooling-limit-warning-icon" aria-hidden="true">!</span><span>${prefix} begrensd op niveau ${escapeHtml(formatValue("silentMax"))}. Deze maximale koelsterkte wordt dan niet volledig gebruikt.</span></p>`;
  }

  export function renderSettingsCoolingSection() {
    const roomRequestRequired = !hasEntity("coolingRoomRequestRequired") || isEntityActive("coolingRoomRequestRequired");
    const restartByMinimumOffTime = hasEntity("coolingRestartMode") &&
      getEntityStateText("coolingRestartMode", "Water temperature") === "Minimum off time";
    const tuningFields = [
      renderSettingsNumberField("coolingMinimumSupplyTemp", "Minimale koel-aanvoer", "Ondergrens voor het koeldoel. OpenQuatt gebruikt de hoogste waarde van deze instelling en de dauwpuntveilige grens."),
      renderSettingsSliderField("coolingDemandMax", "Maximale koelsterkte", "Bepaalt hoe krachtig OpenQuatt mag koelen. Lager geeft langere, rustigere runs; hoger geeft meer koelvermogen bij warm weer.", "", {
        minLabel: "Rustig",
        maxLabel: "Krachtig",
        valueLabel: `${formatValue("coolingDemandMax")} max`,
        footerMarkup: renderCoolingSilentLimitWarning(),
      }),
      hasEntity("coolingRestartMode") ? renderSettingsSelectField("coolingRestartMode", "Herstartvoorwaarde", "Kies of koeling herstart nadat het water voldoende is opgewarmd of na een vaste minimale uit-tijd. Een minimale uit-tijd remt snelle opeenvolgende koelstarts af en helpt zo pendelgedrag te verminderen. De vaste minimale uit-tijd van iedere compressor (4 minuten) blijft in beide modi altijd gelden.") : "",
      restartByMinimumOffTime
        ? renderSettingsNumberField("coolingMinimumOffTime", "Minimale uit-tijd koelen", "Na een werkelijke koelstop blijft de warmtepomp gedurende deze tijd uit. Bij Duo geldt dit voor beide warmtepompen. OpenQuatt start pas wanneer ook de vaste minimale compressor-uit-tijd (4 minuten) voorbij is.")
        : renderSettingsNumberField("coolingRestartDelta", "Herstartmarge watertemperatuur", "Na het bereiken van het koel-aanvoerdoel start de watercyclus pas opnieuw zodra de aanvoer deze marge boven het doel ligt."),
      renderSettingsNumberField("coolingSafetyMargin", "Dauwpunt veiligheidsmarge", "Extra marge boven het geselecteerde dauwpunt voor de minimale veilige watertemperatuur."),
    ].filter(Boolean);
    const roomRequestFields = [
      hasEntity("coolingRoomRequestRequired") ? renderSettingsSwitchField(
        "coolingRoomRequestRequired",
        "Koelvraag via kamerthermostaat",
        "Aan: OpenQuatt wacht op echte koelvraag vanuit de kamer. Uit: koeltoestemming geldt direct als koelvraag.",
        "Koelvraag start en stopt met de marges hieronder.",
        "Koeltoestemming geldt direct als koelvraag. De start- en stopmarge worden nu niet gebruikt.",
        "oq-settings-field--span-2",
      ) : "",
      roomRequestRequired ? renderSettingsNumberField("coolingRequestOnDelta", "Koelvraag start boven setpoint", "Koelvraag wordt actief zodra de kamer warmer is dan setpoint plus deze marge.") : "",
      roomRequestRequired ? renderSettingsNumberField("coolingRequestOffDelta", "Koelvraag stopt boven setpoint", "Koelvraag valt weer af zodra de kamer koeler is dan setpoint plus deze marge.") : "",
    ].filter(Boolean);
    const hasRoomRequestSettings = roomRequestFields.length > 0;
    const hasFallbackSettings = hasEntity("coolingWithoutDewPointMode");
    const guardStatusFacts = [
      hasEntity("coolingGuardMode") ? renderSettingsCoolingFact("Route", formatSettingsOptionLabel(getEntityStateText("coolingGuardMode", "Onbekend"))) : "",
      hasEntity("coolingEffectiveMinSupplyTemp") ? renderSettingsCoolingFact("Actieve ondergrens", getEntityStateText("coolingEffectiveMinSupplyTemp", "—")) : "",
    ].filter(Boolean);
    const guardStatusPanel = guardStatusFacts.length ? renderSettingsFieldCard(
      "coolingGuardStatus",
      "Actuele beveiliging",
      "Laat zien welke route koeling nu begrenst en welke ondergrens daadwerkelijk geldt.",
      `<div class="oq-settings-cooling-facts">${guardStatusFacts.join("")}</div>`,
      "oq-settings-field--span-2 oq-settings-field--cooling-status",
    ) : "";
    const fallbackMetricFacts = [
      hasEntity("outsideTempSelected") ? renderSettingsCoolingFact("Actuele buitentemperatuur", getEntityStateText("outsideTempSelected", "—")) : "",
      hasEntity("coolingFallbackNightMinOutdoorTemp") ? renderSettingsCoolingFact("Nachtminimum buitentemperatuur", getEntityStateText("coolingFallbackNightMinOutdoorTemp", "—")) : "",
      hasEntity("coolingFallbackMinSupplyTemp") ? renderSettingsCoolingFact("Berekende minimum watertemperatuur", getEntityStateText("coolingFallbackMinSupplyTemp", "—")) : "",
    ].filter(Boolean);
    const fallbackMetricsMarkup = fallbackMetricFacts.length ? `<div class="oq-settings-cooling-fallback-metrics">${fallbackMetricFacts.join("")}</div>` : "";
    const hasFallbackDetails = hasFallbackSettings || fallbackMetricFacts.length > 0;
    const activeCoolingGuardMode = getEntityStateText("coolingGuardMode", "");
    const openFallbackDetails = activeCoolingGuardMode.toLowerCase().includes("fallback");
    const pidFields = [
      renderSettingsNumberField("coolingPidKp", "Proportionele reactie (Kp)", "Bepaalt hoe sterk de koelregeling direct reageert op het verschil tussen gewenste en gemeten aanvoertemperatuur."),
      renderSettingsNumberField("coolingPidKi", "Langdurige correctie (Ki)", "Corrigeert een klein temperatuurverschil dat langere tijd blijft bestaan. Verhoog alleen in kleine stappen."),
      renderSettingsNumberField("coolingPidKd", "Demping (Kd)", "Remt snelle veranderingen af. Een te hoge waarde kan de koelregeling onnodig traag of onrustig maken."),
    ].filter(Boolean).join("");
    const advancedPidMarkup = renderSettingsAdvancedDisclosure(
      "cooling",
      "Geavanceerde koelafstelling",
      "Deze PID-waarden verfijnen hoe OpenQuatt het koel-aanvoerdoel volgt. Laat ze op de standaardwaarden staan zolang koeling stabiel en zonder pendelen werkt.",
      pidFields ? `<div class="oq-settings-grid oq-settings-grid--pid">${pidFields}</div>` : "",
    );

    if (!tuningFields.length && !hasRoomRequestSettings && !hasFallbackSettings && !guardStatusPanel && !hasFallbackDetails && !advancedPidMarkup) {
      return "";
    }

    const fallbackModeCopy = {
      "Dew point required": "Gebruik alleen een betrouwbare dauwpuntmeting. Zonder meting blijft koeling uit.",
      "Allow without dew point": "Gebruik dauwpunt waar mogelijk. Zonder meting geldt de conservatieve benadering hieronder.",
      "Allow without dew point, use fallback": "Gebruik dauwpunt waar mogelijk. Zonder meting geldt de conservatieve benadering hieronder.",
      "Allow without dew point, use dew point approximation": "Gebruik dauwpunt waar mogelijk. Zonder meting geldt de conservatieve benadering hieronder.",
      "Allow without dew point, user responsibility": "Negeer dauwpunt en benadering; alleen de ingestelde minimale koel-aanvoer geldt.",
    };

    return renderSettingsSection(
      "Koeling",
      "Koelingsinstellingen",
      "Stel hier in wanneer koelvraag ontstaat, hoe koud het water mag worden en wanneer een gestopte koelcyclus opnieuw mag starten.",
      `
        ${tuningFields.length ? `
          <div class="oq-settings-grid">
            ${tuningFields.join("")}
          </div>
        ` : ""}
        ${hasRoomRequestSettings ? `
          <div class="oq-settings-subpanel oq-settings-subpanel--nested">
            <div class="oq-settings-subpanel-head">
              <p class="oq-helper-label">Koelvraag</p>
              <h4>Kamerthermostaat</h4>
              <p>Bepaalt of koelen pas start bij kamervraag, of dat koeltoestemming direct als koelvraag telt.</p>
            </div>
            <div class="oq-settings-grid">
              ${roomRequestFields.join("")}
            </div>
          </div>
        ` : ""}
        ${(hasFallbackSettings || guardStatusPanel || hasFallbackDetails) ? `
          <div class="oq-settings-grid">
            ${hasFallbackSettings ? renderSettingsOptionCardsField("coolingWithoutDewPointMode", "Keuze koelbeveiliging", "Kies welke veiligheidsgrens OpenQuatt gebruikt: dauwpuntmeting, dauwpuntsbenadering bij ontbrekende meting, of expliciet toestaan zonder dauwpuntgrens.", fallbackModeCopy, "oq-settings-field--span-2 oq-settings-field--cooling-guard-choice") : ""}
            ${guardStatusPanel}
            ${hasFallbackDetails ? `
              <details class="oq-settings-callout oq-settings-callout--cooling oq-settings-callout--inline"${openFallbackDetails ? " open" : ""}>
              <summary>Dauwpuntsbenadering bekijken</summary>
              <div class="oq-settings-callout-body">
                ${fallbackMetricsMarkup}
                <p>Zonder dauwpuntmeting weet OpenQuatt niet zeker hoe koud het water mag worden zonder condensrisico. De dauwpuntsbenadering gebruikt daarom een voorzichtige minimum watertemperatuur.</p>
                <p>Onder de 20°C buiten blijft koeling via deze benadering uit. Daarboven loopt de ondergrens geleidelijk op van 19°C bij 20°C buiten naar 22°C bij 32°C buiten. Warme nachten verhogen die grens nog iets.</p>
                <p>Wordt die grens hoger dan zinvol is voor de kamer, dan verlaagt OpenQuatt hem beperkt: ongeveer 1°C onder de kamertemperatuur, maar nooit lager dan 20°C. Voorbeeld: bij 22°C kamer en een berekende grens van 23,5°C wordt de grens ongeveer 21°C. Zo kan OpenQuatt nog voorzichtig koelen. Een echte dauwpuntmeting blijft veiliger.</p>
                <p>Kies je expliciet toestaan, dan gebruikt OpenQuatt geen dauwpuntgrens: ook een beschikbare dauwpuntmeting wordt genegeerd. Koeling mag dan doorgaan op basis van de ingestelde minimale koel-aanvoer. Dat kan nuttig zijn bij een installatie die je zelf goed bewaakt, maar het condensrisico ligt dan volledig bij jou.</p>
                <div class="oq-settings-rule-groups">
                  <section class="oq-settings-rule-group">
                    <h4>Buitentemperatuur</h4>
                    <div class="oq-settings-rule-table">
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">Onder 20°C</span>
                        <span class="oq-settings-rule-value">Uit</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">20-32°C</span>
                        <span class="oq-settings-rule-value">19°C → 22°C</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">Vanaf 32°C</span>
                        <span class="oq-settings-rule-value">Min. water 22°C</span>
                      </div>
                    </div>
                  </section>
                  <section class="oq-settings-rule-group">
                    <h4>Nachtcorrectie</h4>
                    <div class="oq-settings-rule-table">
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">Onder 18°C</span>
                        <span class="oq-settings-rule-value">+0°C</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">18-19°C</span>
                        <span class="oq-settings-rule-value">+0,5°C</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">19-20°C</span>
                        <span class="oq-settings-rule-value">+1,0°C</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">Vanaf 20°C</span>
                        <span class="oq-settings-rule-value">+1,5°C</span>
                      </div>
                    </div>
                  </section>
                </div>
              </div>
            </details>
            ` : ""}
          </div>
        ` : ""}
        ${advancedPidMarkup}
      `,
    );
  }
