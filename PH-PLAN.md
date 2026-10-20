# Power House Regeling Herontwerp - Implementatieplan

## 🎯 Doelstelling

Vereenvoudiging van de Power House comfortregeling door:
1. **Speciale comfortband regeling te laten vervallen** → `phKp` is nu altijd leidend
2. **Veldnaam wijzigingen** voor betere semantiek
3. **Vereenvoudigde logica** zonder onderscheid tussen warme/koude herstart
4. **Uitlegstroom in de UI**: verklarende tekst blokken tussen de invulvelden door (zie § Nieuwe UI Structuur)
5. **Traagheid als schakelaar**: de comfort memory blijft behouden, maar wordt instelbaar `aan/uit`
6. **Backward compatibility** behouden voor opslag en Home Assistant API

---

## 📋 Randvoorwaarden

### Temperatuur Relaties
- **Setpoint**: de gevraagde kamertemperatuur van de thermostaat (`roomSetpoint`); alle drempels zijn relatief aan dit setpoint
- **Stoptemperatuur** (`phStopTemperature`): Moet ≥ 0 zijn
- **Herstarttemperatuur** (`phRestartTemperature`):
  - Mag positief **of negatief** zijn
  - Moet **altijd tenminste 0.1°C lager** zijn dan Stoptemperatuur
  - Voorbeeld: Als Stoptemperatuur = 0.3, dan Herstarttemperatuur ≤ 0.2

### Regeling Gedrag
- **Geen comfortband meer**: `phKp` is altijd leidend, zowel binnen als buiten de band; de afwijking wordt direct ten opzichte van het setpoint berekend
- **Comfort memory blijft, als schakelaar "Traagheid"**: de integrerende regeling (opbouw tot maximaal **0,2 °C** afwijking op de kamervraag) wordt behouden, maar is standaard **aan** en per stukelaar uit te zetten
- **Geen onderscheid warm/koud herstart**: Eén herstarttemperatuur
- **Minimaal vermogen**: Als `phRunExtension` aan staat EN temperatuur < Stoptemperatuur, dan altijd minimaal compressor level 1

### Traagheid (Comfort Memory)
- **Aan (standaard)**: gedrag gelijk aan de huidige firmware; bij aanhoudende afwijking onder het setpoint bouwt er een extra afwijking op, gemodelleerd als offset op de kamervraag, begrensd op **0,2 °C**
- **Uit**: geen opbouw; de opgebouwde waarde loopt af naar 0 en de regeling werkt puur met de directe `phKp`-correctie t.o.v. het setpoint
- De diagnostische sensor `oq_phouse_comfort_memory_c` blijft bestaan en toont 0,0 °C als traagheid uit staat

---

## 🔄 Veld Transformatie

### Actieve Velden (Nieuw)

| Oude Naam | Nieuwe Naam | Type | Bereik | Standaard | Opmerkingen |
|-----------|--------------|------|--------|-----------|-------------|
| `phComfortAbove` | **`phStopTemperature`** | number | ≥ 0 | 0.3 °C | Temperatuur boven setpoint waarbij WP stopt |
| `phRunExtensionRestartCooldown` | **`phRestartTemperature`** | number | onbeperkt | 0.1 °C | Herstarttemperatuur (relatief aan setpoint) |
| *(nieuw)* | **`phInertia`** | switch | true/false | **true** | Traagheid (comfort memory) aan/uit |
| `phRunExtension` | `phRunExtension` | switch | true/false | false | Blijft ongewijzigd |

> ⚠️ **Naamgeving**: de JS/HA sleutel `phComfortMemory` is al bezet door de **diagnostische sensor** (`oq_phouse_comfort_memory_c`). De nieuwe schakelaar krijgt daarom de sleutel **`phInertia`** / YAML id **`ph_inertia_enabled`** / entity `switch.openquatt_ph_inertia_enabled`. UI-label: "Traagheid" (NL) / "Inertia" (EN).

### Inactieve Velden (Behouden voor Backward Compatibility)

| Veld | Status | Reden |
|------|--------|-------|
| `phComfortBelow` | **Verwijderd uit actieve logica** | Vervallen functionaliteit |
| `phRunExtensionStopMargin` | **Verwijderd uit actieve logica** | Vervallen functionaliteit |

> ⚠️ **Belangrijk**: Deze velden **blijven in opslag** voor backward compatibility, maar worden **niet meer gebruikt** in de regeling.

---

## 🏗️ Nieuwe UI Structuur: De Uitlegstroom

De Power House instellingen worden gerenderd als **één kolom opeenvolgende blokken**: een verklarend tekstblok, gevolgd door het bijbehorende invulveld. De velden tonen alleen nog hun korte label (in bullet-vorm zoals hieronder); de toelichtende tekst staat los vóór het veld in plaats van als kopie per veldkaart.

### Blokvolgorde (NL copy, exact)

```text
[Prose 1 — setpoint, dynamische waarde uit roomSetpoint]
De gevraagde kamertemperatuur door de thermostaat noemen we het setpoint.
Momenteel is deze ingesteld op 20,0 °C.

[Prose 2 — warmteverlies]
Het warmteverlies geeft aan welk vermogen er nodig is om een huis op temperatuur
te *houden*.

[Veldgroep: woningmodel — houseColdTemp, houseOutdoorMax, housePower]
  (bestaande velden, ongewijzigde volgorde; housePower toont "Warmteverlies")

[Prose 3 — reageren op afwijkingen]
Bij afwijkingen van het setpoint kan het systeem op verschillende manieren
reageren. Wat handig is voor een systeem met radiatoren, is niet altijd handig
voor een systeem met vloerverwarming.

[Veld: phResponseProfile — responsprofiel (Calm/Balanced/Responsive/Custom)]

[Prose 4 — kamercorrectie]
De kamercorrectie geeft aan hoe sterk er bijgestuurd wordt bij een afwijking van
het setpoint.

[Veld: phKp]  * Kamercorrectie [1000] W/°C

[Prose 5 — traagheid]
Bij een snel systeem (radiatoren) kan het nodig zijn om wat traagheid (een
integrerende regeling) in te bouwen. Dit wordt gemodelleerd als een afwijking op
de kamervraag tot maximaal 0,2 °C.

[Veld: phInertia]  * Traagheid: [aan/uit]

[Prose 6 — stoptemperatuur]
De buitenunit gaat uit bij een vaste afwijking op het setpoint:

[Veld: phStopTemperature]  * Stoptemperatuur regeling: setpoint + [0,3] °C

[Prose 7 — herstarttemperatuur]
Om te voorkomen dat de warmtepomp snel achter elkaar uit en aan schakelt, moet
er een herstartpunt gekozen worden, lager dan de stoptemperatuur. Dit kan zowel
boven als onder het setpoint liggen.

[Veld: phRestartTemperature]  * Herstarttemperatuur regeling: setpoint + [0,1] °C

[Samenvatting: drempels preview (setpoint/stop/herstart), bestaand component]

[Prose 8 — langer doorverwarmen]
Voor een warmtepomp is het niet goed om te vaak af te slaan. Bij gematigde
temperaturen kan dit snel gebeuren. Om dit te voorkomen kan er tot aan de
stoptemperatuur altijd minimaal op de laagste stand worden doorverwarmd.

[Veld: phRunExtension]  * Langer doorverwarmen: [aan/uit]
```

Notities:
- **Prose 1** toont de **actuele** waarde van `roomSetpoint` (1 decimaal, locale notatie); dit blok wordt opnieuw gerenderd als het setpoint wijzigt
- `[...]` markeert het invulveld; de getoonde waarden (0,3 / 0,1 / aan-uit) zijn de live waarden van de betreffende velden
- De geneste subpanel-koppen ("Power House — comfort", "Op temperatuur houden") met hun `tuningCopy`/`maintainCopy` **vervallen**; hun uitlegende rol wordt overgenomen door de tekstblokken
- `houseColdTemp` en `houseOutdoorMax` blijven gewoon onderdeel van de bestaande woningmodel-groep na Prose 2
- Validatiefouten (herstart ≥ 0,1 °C lager dan stop) blijven direct onder het betreffende veld verschijnen

### English copy (en.js)

```text
[1] The requested room temperature from the thermostat is called the setpoint.
    It is currently set to {value} °C.
[2] The heat loss indicates the power needed to *keep* a house at temperature.
[3] When the room deviates from the setpoint, the system can respond in
    different ways. What works well for a radiator system is not always
    suitable for underfloor heating.
[4] The room correction indicates how strongly the system corrects when the
    room deviates from the setpoint.
[5] A fast system (radiators) may need some lag (an integrating control action).
    This is modelled as an offset on the room demand of up to 0.2 °C.
[6] The outdoor unit switches off at a fixed offset from the setpoint:
[7] To prevent the heat pump from switching off and on in quick succession, a
    restart point lower than the stop temperature must be chosen. This can lie
    either above or below the setpoint.
[8] It is not good for a heat pump to shut down too often. At moderate
    temperatures this can happen quickly. To prevent this, the system can always
    continue at the lowest setting up to the stop temperature.
```

### Validatie Regels

1. **Stoptemperatuur** (`phStopTemperature`):
   - Moet een numerieke waarde zijn
   - Moet ≥ 0 zijn
   - Standaard: 0.3 °C

2. **Herstarttemperatuur** (`phRestartTemperature`):
   - Moet een numerieke waarde zijn
   - Moet **strikt < Stoptemperatuur - 0.1** zijn
   - Mag negatief zijn (bv. -0.5 voor herstart onder setpoint)
   - Standaard: 0.1 °C

---

## 📁 Bestandswijzigingen

### 1. C-Code Header Bestanden

#### `oq_power_house_demand_logic.h`

**Wijzigingen:**
- **DemandTuning struct**: nieuwe velden
  ```c
  struct DemandTuning {
    float temperature_guard_c = 0.0f;
    float reaction_w_per_k = NAN;      // phKp
    float stop_temperature_c = NAN;    // phStopTemperature (nieuwe naam)
    bool inertia_enabled = true;       // phInertia (traagheid / comfort memory)
    float rise_time_min = NAN;
    float fall_time_min = NAN;
    int demand_max = 0;
  };
  ```
  > `comfort_below_c` en `comfort_above_c` vervallen; `inertia_enabled` komt erbij.

- **Vereenvoudig error_c berekening** (geen comfortband meer):
  ```c
  // Nieuw (na afbouw/traagheid, zie onder):
  const float effective_setpoint_c = in.setpoint_c + memory_c;  // memory_c = 0 als traagheid uit
  float error_c = effective_setpoint_c - in.room_c;             // Directe error
  ```
  De `valid`-check stelt **geen** eisen meer aan `comfort_below_c`/`comfort_above_c`.

- **Comfort memory → traagheid** (bestaande code rond regels ~88-144):
  ```c
  // Maximum vast: de comfortband (above_c) bepaalt het maximum niet meer.
  const float memory_max_c = 0.20f;
  float memory_c = isfinite(state.comfort_memory_c) ? state.comfort_memory_c : 0.0f;
  memory_c = clamp_power(memory_c, 0.0f, memory_max_c);

  if (tuning.inertia_enabled) {
    const float undershoot = clamp_power((in.setpoint_c - in.room_c) / 0.45f, 0.0f, 1.0f);
    if (in.room_c < in.setpoint_c) {
      // Zelfde vorm als voorheen, nu t.o.v. het setpoint:
      // volledige opbouw in ~24 min bij volle afwijking, ~90 min bij kleine afwijking
      const float build_per_min = memory_max_c / 90.0f + (memory_max_c / 24.0f - memory_max_c / 90.0f) * undershoot;
      memory_c += build_per_min * dt_s / 60.0f;
    } else if (in.room_c >= in.setpoint_c) {
      // Afbouw 40 min; versneld (12 min) boven de stoptemperatuur
      const float decay_minutes = in.room_c > in.setpoint_c + below_or_stop_reference ? 12.0f : 40.0f;
      memory_c -= (memory_max_c / decay_minutes) * dt_s / 60.0f;
    }
  } else {
    // Uit: geen opbouw, bestaande waarde loopt snel af naar nul
    memory_c -= (memory_max_c / 12.0f) * dt_s / 60.0f;
  }
  memory_c = clamp_power(memory_c, 0.0f, memory_max_c);
  ```
  De referentie voor de snelle afbouw is de stoptemperatuur (`tuning.stop_temperature_c`,
  `setpoint + stop`); de oude afhankelijkheid van `above_c` vervalt. Oud en nieuw
  gedrag zijn identiek zolang traagheid aan staat, behalve dat de bandranden
  vervallen en het maximum vast 0,2 °C is.

- **Verwijder comfort_below_c / low_base_c / high_base_c / mid_base_c** uit alle berekeningen

#### `oq_power_house_run_extension_logic.h`

**Wijzigingen**
- **Tuning struct**: Alleen nieuwe velden
  ```c
  struct Tuning {
    float stop_temperature_c = 0.3f;    // phStopTemperature
    float restart_temperature_c = 0.1f; // phRestartTemperature
    // room_resume_delta_c VERWIJDERD
  };
  ```

- **Decision struct**: Verwijder `comfort_stop_c` en `warm_restart_c`
  ```c
  struct Decision {
    State next;
    bool force_comfort_stop = false;
    bool warm_restart_intent = true;   // Blijft voor interne logica
    bool floor_active = false;
    float floor_w = 0.0f;
    // comfort_stop_c VERWIJDERD
    // warm_restart_c VERWIJDERD
  };
  ```

- **evaluate() functie**:
  - Gebruik alleen `stop_temperature_c` en `restart_temperature_c`
  - **Verwijder `room_resume_delta_c`** parameter
  - **Verwijder `cold_edge_c`** berekening
  - Nieuwe berekening:
    ```c
    const float comfort_stop_c = std::isfinite(in.setpoint_c)
                                 ? in.setpoint_c + tuning.stop_temperature_c
                                 : NAN;
    const float warm_restart_c = std::isfinite(comfort_stop_c)
                                 ? comfort_stop_c - tuning.restart_temperature_c
                                 : NAN;
    ```

#### `oq_power_house_runtime.h`

**Wijzigingen:**
- **DemandTuning initialisatie**: nieuwe velden
  ```c
  const oq_power_house::DemandTuning demand_tuning{
      config.temperature_guard_c,
      id(ph_kp_w_per_k).state,
      id(ph_stop_temperature_c).state,     // phStopTemperature
      static_cast<bool>(id(ph_inertia_enabled).state),  // phInertia, standaard true
      id(ph_demand_rise_time_min).state,
      id(ph_demand_fall_time_min).state,
      config.demand_max_f,
  };
  ```

- **Run extension tuning**: Gebruik nieuwe velden
  ```c
  const auto run_ext_decision = oq_power_house_run_extension::evaluate(
      run_ext_input,
      {id(ph_stop_temperature_c).state, id(ph_restart_temperature_c).state},
      this->run_extension_state_);
  ```

- **Backward Compatibility (read-only)**:
  - **Lees** oude velden als nieuwe velden ontbreken (voor opslag)
  - **Schrijf** alleen naar nieuwe velden
  - **Geen foutmeldingen** bij schrijven naar oude velden

---

### 2. YAML Configuratie (`oq_power_house_strategy.yaml`)

#### Nieuwe, Actieve Entities

```yaml
# Nieuwe velden - actief in UI en regeling
- platform: template
  id: ph_stop_temperature_c
  name: "Power House stop temperature"
  icon: mdi:thermometer-chevron-up
  unit_of_measurement: "°C"
  min_value: 0
  max_value: 2.0
  step: 0.05
  restore_value: true
  optimistic: true
  initial_value: 0.3
  entity_category: config

- platform: template
  id: ph_restart_temperature_c
  name: "Power House restart temperature"
  icon: mdi:thermometer-chevron-down
  unit_of_measurement: "°C"
  min_value: -2.0  # Mag negatief zijn
  max_value: 1.9  # Moet < stop_temperature_c - 0.1
  step: 0.05
  restore_value: true
  optimistic: true
  initial_value: 0.1
  entity_category: config

- platform: template
  id: ph_inertia_enabled
  name: "Power House inertia"
  icon: mdi:gesture-tap-hold
  restore_value: true
  optimistic: true
  initial_value: "true"   # Standaard aan: gelijk aan huidig gedrag
  entity_category: config
```

> `ph_kp_w_per_k` (initial_value 3000) en de comfort memory sensor
> `oq_phouse_comfort_memory_c` blijven ongewijzigd. De afgeleide sensor
> `room_setpoint_selected + oq_phouse_comfort_memory_c` blijft geldig en toont
> het setpoint zelf zolang traagheid uit staat.

#### Oude Entities (Behouden voor Backward Compatibility)

```yaml
# Oude velden - behouden voor backward compatibility (read-only in nieuwe firmware)
- platform: template
  id: ph_comfort_band_below_c
  name: "Power House comfort below setpoint"
  icon: mdi:thermometer-chevron-down
  unit_of_measurement: "°C"
  min_value: 0
  max_value: 2.0
  step: 0.05
  restore_value: true
  optimistic: true
  initial_value: 0.1
  entity_category: config
  disabled_by_default: true

- platform: template
  id: ph_comfort_band_above_c
  name: "Power House comfort above setpoint"
  icon: mdi:thermometer-chevron-up
  unit_of_measurement: "°C"
  min_value: 0
  max_value: 2.0
  step: 0.05
  restore_value: true
  optimistic: true
  initial_value: 0.3
  entity_category: config
  disabled_by_default: true

- platform: template
  id: ph_run_extension_stop_margin_c
  name: "Power House run extension stop margin"
  icon: mdi:thermometer-chevron-up
  unit_of_measurement: "°C"
  min_value: 0.1
  max_value: 1.0
  step: 0.1
  restore_value: true
  optimistic: true
  initial_value: 0.5
  entity_category: config
  disabled_by_default: true

- platform: template
  id: ph_run_extension_restart_cooldown_c
  name: "Power House run extension restart cooldown"
  icon: mdi:thermometer-chevron-down
  unit_of_measurement: "°C"
  min_value: 0.1
  max_value: 3.0
  step: 0.1
  restore_value: true
  optimistic: true
  initial_value: 0.2
  entity_category: config
  disabled_by_default: true
```

---

### 3. JavaScript UI Configuratie (`config.js`)

#### POWER_HOUSE_KEYS

```javascript
export const POWER_HOUSE_KEYS = [
    "roomSetpoint",
    "housePower",
    "houseColdTemp",
    "houseOutdoorMax",
    "phResponseProfile",
    "phKp",
    // Nieuwe, actieve velden
    "phStopTemperature",
    "phRestartTemperature",
    "phInertia",
    "phRunExtension",
    "phRunExtensionStatus",
    // Oude velden (voor backward compatibility, maar niet actief in UI)
    "phComfortBelow",
    "phComfortAbove",
    "phRunExtensionStopMargin",
    "phRunExtensionRestartCooldown",
];
```

#### ENTITY_DEFS

```javascript
// Nieuwe, actieve entities
["phStopTemperature", DOMAIN_NUMBER, "Power House stop temperature", false],
["phRestartTemperature", DOMAIN_NUMBER, "Power House restart temperature", false],
["phInertia", DOMAIN_SWITCH, "Power House inertia", false],

// Oude entities (behouden voor backward compatibility)
["phComfortBelow", DOMAIN_NUMBER, "Power House comfort below setpoint", true],
["phComfortAbove", DOMAIN_NUMBER, "Power House comfort above setpoint", true],
["phRunExtensionStopMargin", DOMAIN_NUMBER, "Power House run extension stop margin", true],
["phRunExtensionRestartCooldown", DOMAIN_NUMBER, "Power House run extension restart cooldown", true],
```

> `phComfortMemory` (diagnostische sensor) blijft ongewijzigd in `ENTITY_DEFS`.

---

### 4. JavaScript UI Vertalingen (`nl.js`, `en.js`)

#### Nieuwe Vertalingen (nl.js) — `settingsHeating`

```javascript
settingsHeating: {
    // --- Uitlegstroom (prose blokken) ---
    phFlowSetpointCopy: "De gevraagde kamertemperatuur door de thermostaat noemen we het setpoint. Momenteel is deze ingesteld op {value} °C.",
    phFlowHousePowerCopy: "Het warmteverlies geeft aan welk vermogen er nodig is om een huis op temperatuur te houden.",
    phFlowResponseCopy: "Bij afwijkingen van het setpoint kan het systeem op verschillende manieren reageren. Wat handig is voor een systeem met radiatoren, is niet altijd handig voor een systeem met vloerverwarming.",
    phFlowKpCopy: "De kamercorrectie geeft aan hoe sterk er bijgestuurd wordt bij een afwijking van het setpoint.",
    phFlowInertiaCopy: "Bij een snel systeem (radiatoren) kan het nodig zijn om wat traagheid (een integrerende regeling) in te bouwen. Dit wordt gemodelleerd als een afwijking op de kamervraag tot maximaal 0,2 °C.",
    phFlowStopCopy: "De buitenunit gaat uit bij een vaste afwijking op het setpoint:",
    phFlowRestartCopy: "Om te voorkomen dat de warmtepomp snel achter elkaar uit en aan schakelt, moet er een herstartpunt gekozen worden, lager dan de stoptemperatuur. Dit kan zowel boven als onder het setpoint liggen.",
    phFlowRunExtensionCopy: "Voor een warmtepomp is het niet goed om te vaak af te slaan. Bij gematigde temperaturen kan dit snel gebeuren. Om dit te voorkomen kan er tot aan de stoptemperatuur altijd minimaal op de laagste stand worden doorverwarmd.",

    // --- Veldlabels (bullet-stijl, zonder aparte kopie per veld) ---
    phKpLabel: "Kamercorrectie",
    phInertiaTitle: "Traagheid",
    phStopTemperatureLabel: "Stoptemperatuur regeling",
    phRestartTemperatureLabel: "Herstarttemperatuur regeling",
    phRunExtensionLabel: "Langer doorverwarmen",
    phSetpointPrefix: "setpoint +",

    // Traagheid
    phInertiaCopy: "Traagheid zorgt voor een integrerende regeling: bij een aanhoudende afwijking onder het setpoint bouwt de vraag langzaam op, tot maximaal 0,2 °C.",
    phInertiaOn: "Aan: traagheid is ingeschakeld.",
    phInertiaOff: "Uit: er wordt alleen direct geregeld op de afwijking van het setpoint.",

    // Kamercorrectie (behouden als fallback/tooltip)
    phKpTitle: "Kamercorrectie",
    phKpCopy: "Bepaalt hoe sterk Power House het verwarmingsvermogen aanpast aan de afwijking van de gewenste kamertemperatuur.",

    // Stop/herstart labels en validatie (behouden)
    phStopTemperatureTitle: "Stoptemperatuur regeling",
    phRestartTemperatureTitle: "Herstarttemperatuur regeling",
    phTemperatureValidationError: "Herstarttemperatuur moet tenminste 0,1 °C lager zijn dan de stoptemperatuur.",
    phStopTemperatureMinError: "Stoptemperatuur moet ≥ 0 zijn.",

    // Run Extension
    phRunExtensionTitle: "Langer doorverwarmen",
    phRunExtensionCopy: "Als ingeschakeld, blijft de warmtepomp op minimaal niveau draaien als de temperatuur onder de stoptemperatuur ligt. Dit voorkomt te vaak afslaan bij gematigde temperaturen.",
},

runExtension: {
    title: "Langer doorverwarmen",
    copy: "Bij weinig warmtebehoefte kan een draaiende warmtepomp op laag vermogen blijven verwarmen tot de stoptemperatuur. Dit voorkomt te vaak afslaan.",
    allow: "Langer doorverwarmen",
    allowCopy: "Deze optie laat een draaiende warmtepomp op zijn laagste geschikte vermogen doorgaan als de temperatuur onder de stoptemperatuur ligt. Een stilstaande warmtepomp wordt door deze schakelaar niet gestart.",
    on: "Aan: de warmtepomp mag bij lage temperaturen blijven doorverwarmen.",
    off: "Uit: Power House regelt het vermogen en stopt wanneer het geen warmte meer vraagt.",
    // ...status copy ongewijzigd...
},
```

#### Nieuwe Vertalingen (en.js)

```javascript
settingsHeating: {
    phFlowSetpointCopy: "The requested room temperature from the thermostat is called the setpoint. It is currently set to {value} °C.",
    phFlowHousePowerCopy: "The heat loss indicates the power needed to keep a house at temperature.",
    phFlowResponseCopy: "When the room deviates from the setpoint, the system can respond in different ways. What works well for a radiator system is not always suitable for underfloor heating.",
    phFlowKpCopy: "The room correction indicates how strongly the system corrects when the room deviates from the setpoint.",
    phFlowInertiaCopy: "A fast system (radiators) may need some lag (an integrating control action). This is modelled as an offset on the room demand of up to 0.2 °C.",
    phFlowStopCopy: "The outdoor unit switches off at a fixed offset from the setpoint:",
    phFlowRestartCopy: "To prevent the heat pump from switching off and on in quick succession, a restart point lower than the stop temperature must be chosen. This can lie either above or below the setpoint.",
    phFlowRunExtensionCopy: "It is not good for a heat pump to shut down too often. At moderate temperatures this can happen quickly. To prevent this, the system can always continue at the lowest setting up to the stop temperature.",

    phKpLabel: "Room correction",
    phInertiaTitle: "Inertia",
    phStopTemperatureLabel: "Stop temperature control",
    phRestartTemperatureLabel: "Restart temperature control",
    phRunExtensionLabel: "Extended heating",
    phSetpointPrefix: "setpoint +",

    phInertiaCopy: "Inertia adds an integrating control action: with a persistent deviation below the setpoint the demand slowly builds up, up to 0.2 °C.",
    phInertiaOn: "On: inertia is enabled.",
    phInertiaOff: "Off: only the direct deviation from the setpoint is controlled.",
    // ...overige bestaande sleutels ongewijzigd...
},
```

---

### 5. JavaScript UI Renderen (`controls.js`, `heating.js`, CSS)

#### Nieuwe helper: `renderSettingsProseBlock()` (`controls.js`)

```javascript
// Verklarend tekstblok tussen de invulvelden door.
export function renderSettingsProseBlock(text, className = "") {
    if (!text) return "";
    return `<p class="oq-settings-prose ${escapeHtml(className)}">${escapeHtml(text)}</p>`;
}
```

#### Nieuwe optie: flow-presentatie voor velden (`controls.js`)

`renderSettingsNumberField()` en `renderSettingsSwitchField()` krijgen de optie
`flow: true` (of een className `oq-settings-field--flow`) waarmee het veld als
compacte bullet-rij wordt gerenderd zonder eigen kopieblok, met label voor het
control en een eventuele suffix:

```html
<div class="oq-settings-flow-item">
  <span class="oq-settings-flow-label" id="<key>-label">Stoptemperatuur regeling: setpoint +</span>
  <input ... aria-labelledby="<key>-label" />
  <span class="oq-settings-flow-unit">°C</span>
</div>
```

Switchvariant toont `Traagheid: [aan/uit]` als label + aan/uit-pill.
Validatiefouten blijven onder de rij staan (bestaand foutmechanisme).

#### `renderPowerHouseFlow()` (`heating.js`)

De huidige `renderPowerHouseBaseFields()` + `renderPowerHouseAdvancedField()`
worden vervangen door één volgorde-gedreven renderer:

```javascript
function getSetpointFlowValue() {
    const raw = hasEntity("roomSetpoint") ? getEntityValue("roomSetpoint") : NaN;
    return Number.isFinite(Number(raw))
        ? formatNumber(Number(raw), { minimumFractionDigits: 1, maximumFractionDigits: 1 })
        : "—";
}

export function renderPowerHouseFlow() {
    return `
      <div class="oq-settings-flow">
        ${renderSettingsProseBlock(t("settingsHeating.phFlowSetpointCopy", { value: getSetpointFlowValue() }), "oq-settings-prose--setpoint")}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowHousePowerCopy"))}
        ${renderPowerHouseBaseFields()}  ${/* woningmodel: coldTemp, outdoorMax, housePower */}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowResponseCopy"))}
        ${renderPowerHouseResponseProfilesField()}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowKpCopy"))}
        ${renderSettingsNumberField("phKp", t("settingsHeating.phKpLabel"), "", "", {
            flow: true, unitOverride: "W/°C",
        })}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowInertiaCopy"))}
        ${renderSettingsSwitchField("phInertia", t("settingsHeating.phInertiaTitle"),
            t("settingsHeating.phInertiaCopy"),
            t("settingsHeating.phInertiaOn"), t("settingsHeating.phInertiaOff"),
            "oq-settings-field--flow")}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowStopCopy"))}
        ${renderSettingsNumberField("phStopTemperature", t("settingsHeating.phStopTemperatureLabel"), "", "",
            { flow: true, labelPrefix: `${t("settingsHeating.phSetpointPrefix")} `, step: 0.05, min: 0, validate: stopValidate })}
        ${renderSettingsProseBlock(t("settingsHeating.phFlowRestartCopy"))}
        ${renderSettingsNumberField("phRestartTemperature", t("settingsHeating.phRestartTemperatureLabel"), "", "",
            { flow: true, labelPrefix: `${t("settingsHeating.phSetpointPrefix")} `, step: 0.05, validate: restartValidate })}
        <div class="oq-run-extension-summary" data-oq-power-house-comfort-thresholds>${renderPowerHouseComfortThresholds()}</div>
        ${renderSettingsProseBlock(t("settingsHeating.phFlowRunExtensionCopy"))}
        ${renderPowerHouseRunExtensionField()}
      </div>
    `;
}
```

Details:
- **Setpoint-blok** wordt live bijgewerkt (samen met `patchRunExtensionThresholds()`, of een nieuwe `patchPowerHouseFlow()` die het `data-oq-ph-setpoint` element vult)
- De geneste subpanel-koppen (`tuningTitle`/`maintainTitle` + copy) vervallen; `renderPowerHouseComfortThresholds()` wordt hernoemd/gearrangeerd naar de stop/herstart-drempelspreview uit het basisplan (`getRunExtensionThresholds()` vereenvoudigd naar setpoint/stopTemp/restartTemp)
- Validatiefuncties `stopValidate`/`restartValidate` zoals in § Validatie Regels
- De geneste `oq-settings-subpanel`-structuur rond de comfortband vervalt; behoud de status-chip en drempelspreview van langer doorverwarmen binnen blok 8

#### CSS (`openquatt/web/css/src/`)

Nieuwe klassen met bestaande tokens:
- `.oq-settings-flow` — kolomopmaak, ruimte tussen blokken
- `.oq-settings-prose` — leestekst-stijl (bestaande body/copy-token)
- `.oq-settings-flow-item` — inline rij: label + control + suffix
- `.oq-settings-field--flow` — veldkaart zonder kopieblok

---

### 6. Debug Recorder (`debug_recorder_default_fields.h`)

```c
// Nieuwe, actieve velden
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phStopTemperature", "number", "Power House stop temperature")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phRestartTemperature", "number", "Power House restart temperature")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phInertia", "switch", "Power House inertia")

// Oude velden (behouden voor backward compatibility, read-only)
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phComfortBelow", "number", "Power House comfort below (legacy)")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phComfortAbove", "number", "Power House comfort above (legacy)")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phRunExtensionStopMargin", "number", "Power House run extension stop margin (legacy)")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phRunExtensionRestartCooldown", "number", "Power House run extension restart cooldown (legacy)")
```

---

### 7. Backward Compatibility Implementatie

#### C-Code (runtime.h)

```c
// In tick() functie:
// Lees nieuwe velden, met fallback naar oude voor backward compatibility
float stop_temperature_c = 0.3f;
if (id(ph_stop_temperature_c).has_state() && std::isfinite(id(ph_stop_temperature_c).state)) {
    stop_temperature_c = id(ph_stop_temperature_c).state;
} else if (id(ph_comfort_band_above_c).has_state() && std::isfinite(id(ph_comfort_band_above_c).state)) {
    // Fallback: gebruik oude waarde (read-only)
    stop_temperature_c = id(ph_comfort_band_above_c).state;
}

float restart_temperature_c = 0.1f;
if (id(ph_restart_temperature_c).has_state() && std::isfinite(id(ph_restart_temperature_c).state)) {
    restart_temperature_c = id(ph_restart_temperature_c).state;
} else if (id(ph_run_extension_restart_cooldown_c).has_state() &&
           std::isfinite(id(ph_run_extension_restart_cooldown_c).state)) {
    // Fallback: gebruik oude waarde (read-only)
    restart_temperature_c = id(ph_run_extension_restart_cooldown_c).state;
}

// Traagheid: standaard aan als de entiteit nog geen state heeft (oude firmware / eerste boot)
const bool inertia_enabled = id(ph_inertia_enabled).has_state()
                                 ? static_cast<bool>(id(ph_inertia_enabled).state)
                                 : true;

// Gebruik nieuwe velden voor actieve regeling
const auto run_ext_decision = oq_power_house_run_extension::evaluate(
    run_ext_input,
    {stop_temperature_c, restart_temperature_c},
    this->run_extension_state_);
```

#### JavaScript (entity-actions.js)

```javascript
// Backward compatibility: als Home Assistant schrijft naar oude velden,
// negeer dit stil (geen foutmeldingen)
// Alleen nieuwe velden worden actief gebruikt

function handleEntityWrite(entityId, value) {
    // Nieuwe velden: verwerk normaal
    if (["phStopTemperature", "phRestartTemperature", "phRunExtension", "phInertia"].includes(entityId)) {
        return processEntityWrite(entityId, value);
    }

    // Oude velden: negeer stil (geen foutmeldingen)
    if (["phComfortBelow", "phComfortAbove", "phRunExtensionStopMargin",
         "phRunExtensionRestartCooldown"].includes(entityId)) {
        console.log(`[Backward Compat] Ignoring write to legacy field: ${entityId}`);
        return { success: true, message: "OK" }; // Geen fout!
    }

    // Overige velden: normale verwerking
    return processEntityWrite(entityId, value);
}
```

---

## 🧪 Validatie Regels

### Frontend Validatie (JavaScript)

```javascript
// In het input veld voor phRestartTemperature:
validate: (value) => {
    const restart = parseFloat(value);
    const stop = parseFloat(getEntityValue("phStopTemperature") || "0.3");

    if (!Number.isFinite(restart)) {
        return t("settingsHeating.invalidNumber");
    }

    if (stop - restart < 0.1) {
        return t("settingsHeating.phTemperatureValidationError");
    }

    return null; // OK
}

// In het input veld voor phStopTemperature:
validate: (value) => {
    const stop = parseFloat(value);

    if (!Number.isFinite(stop)) {
        return t("settingsHeating.invalidNumber");
    }

    if (stop < 0) {
        return t("settingsHeating.phStopTemperatureMinError");
    }

    const restart = parseFloat(getEntityValue("phRestartTemperature") || "0.1");
    if (stop - restart < 0.1) {
        return t("settingsHeating.phTemperatureValidationError");
    }

    return null; // OK
}
```

### Backend Validatie (C-Code)

```c
// In oq_power_house_run_extension_logic.h
inline bool validate_tuning(const Tuning& tuning) {
    // Stoptemperatuur moet ≥ 0
    if (!std::isfinite(tuning.stop_temperature_c) || tuning.stop_temperature_c < 0.0f) {
        return false;
    }

    // Herstarttemperatuur moet < Stoptemperatuur - 0.1
    if (!std::isfinite(tuning.restart_temperature_c) ||
        tuning.stop_temperature_c - tuning.restart_temperature_c < 0.1f) {
        return false;
    }

    return true;
}
```

---

## 📝 Samenvatting van Wijzigingen

| **Onderdeel** | **Wijziging** | **Impact** |
|--------------|---------------|------------|
| **Regeling** | Comfortband vervallen | `phKp` altijd leidend |
| **Regeling** | Comfort memory behouden als "Traagheid" | Schakelaar `phInertia`, standaard aan, max 0,2 °C |
| **Regeling** | Geen warm/koud herstart onderscheid | Eén herstarttemperatuur |
| **Velden** | `phComfortAbove` → `phStopTemperature` | Naamwijziging |
| **Velden** | `phRunExtensionRestartCooldown` → `phRestartTemperature` | Naamwijziging |
| **Velden** | `phInertia` toegevoegd | Nieuwe schakelaar |
| **Velden** | `phComfortBelow` verwijderd | Niet meer gebruikt |
| **Velden** | `phRunExtensionStopMargin` verwijderd | Niet meer gebruikt |
| **UI** | Uitlegstroom: prose blokken tussen velden | Verklarende tekst vóór elk veld, korte bullet-labels |
| **UI** | Vereenvoudigde instellingen | 4 velden + 2 schakelaars in de flow |
| **Validatie** | Herstart < Stop - 0.1 | Strenge controle |
| **Backward Compat** | Oude velden behouden | Read-only in nieuwe firmware |

---

## 🎯 Implementatie Volgorde

1. **C-Code Header Bestanden** (demand_logic incl. traagheid, run_extension_logic)
2. **C-Code Runtime** (oq_power_house_runtime.h)
3. **YAML Configuratie** (oq_power_house_strategy.yaml)
4. **JavaScript Config** (config.js)
5. **JavaScript Vertalingen** (nl.js, en.js)
6. **JavaScript UI Renderen** (controls.js helpers, heating.js flow, CSS)
7. **Debug Recorder** (debug_recorder_default_fields.h)
8. **Validatie Logica** (frontend + backend)
9. **Backward Compatibility Layer**

---

## ✅ Acceptatie Criteria

- [ ] Nieuwe velden (`phStopTemperature`, `phRestartTemperature`, `phInertia`) werken in UI
- [ ] Validatie: Herstart < Stop - 0.1 wordt gehandhaafd
- [ ] Validatie: Stop ≥ 0 wordt gehandhaafd
- [ ] Oude velden (`phComfortBelow`, `phComfortAbove`, etc.) zijn verborgen maar beschikbaar
- [ ] Schrijven naar oude velden geeft geen foutmeldingen
- [ ] Lezen van oude velden werkt (voor backward compatibility)
- [ ] Regeling gebruikt alleen nieuwe velden
- [ ] Minimaal vermogen logica werkt (phRunExtension + temp < stop)
- [ ] Traagheid uit: comfort memory loopt af naar 0 en bouwt niet op; regeling volgt direct het setpoint
- [ ] Traagheid aan: opbouw maximaal 0,2 °C; gedrag gelijk aan huidige firmware
- [ ] Geen onderscheid meer tussen warm/koud herstart
- [ ] UI rendert de uitlegstroom in de vastgelegde volgorde, met de exacte NL/EN copy
- [ ] Setpoint-tekst toont de actuele waarde en werkt live bij
- [ ] Velden in de flow tonen alleen kort label + control (geen dubbele kopie per veld)

---

## 📚 Referentie: Nieuwe Regeling Logica

```
1. Bepaal basis warmtevraag:
   - Huismodel: f(T_outside, T_cold, T_zero, rated_power)
   - Traagheid (phInertia):
     * memory_c = clamp(memory_c + opbouw(setpoint - room), 0, 0,2 °C)  [alleen als aan]
     * memory_c = 0 als uit (afbouw)
   - Kamercorrectie: phKp * ((setpoint + memory_c) - room_c)

2. Bepaal stop/herstart drempels:
   - stop_temp = setpoint + phStopTemperature
   - restart_temp = setpoint + phRestartTemperature

3. Langer doorverwarmen (als phRunExtension = true):
   - Als room_c < stop_temp EN base_request < minimum_viable:
     → floor_w = minimum_viable_w (minimaal compressor level 1)

4. Stop logica:
   - Als room_c >= stop_temp:
     → force_comfort_stop = true

5. Herstart logica:
   - Als room_c <= restart_temp EN base_request > 0:
     → warm_restart_intent = true
```

---

*Document versie: 2.1.0*
*Laatste update: 2026*
*Status: Concept*
