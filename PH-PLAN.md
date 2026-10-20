# Power House Regeling Herontwerp - Implementatieplan

## 🎯 Doelstelling

Vereenvoudiging van de Power House comfortregeling door:
1. **Speciale comfortband regeling te laten vervallen** → `phKp` is nu altijd leidend
2. **Veldnaam wijzigingen** voor betere semantiek
3. **Vereenvoudigde logica** zonder onderscheid tussen warme/koude herstart
4. **Backward compatibility** behouden voor opslag en Home Assistant API

---

## 📋 Randvoorwaarden

### Temperatuur Relaties
- **Stoptemperatuur** (`phStopTemperature`): Moet ≥ 0 zijn
- **Herstarttemperatuur** (`phRestartTemperature`): 
  - Mag positief **of negatief** zijn
  - Moet **altijd tenminste 0.1°C lager** zijn dan Stoptemperatuur
  - Voorbeeld: Als Stoptemperatuur = 0.3, dan Herstarttemperatuur ≤ 0.2

### Regeling Gedrag
- **Geen comfortband meer**: `phKp` is altijd leidend, zowel binnen als buiten de band
- **Geen comfort memory meer**: Deze functionaliteit vervalt
- **Geen onderscheid warm/koud herstart**: Eén herstarttemperatuur
- **Minimaal vermogen**: Als `phRunExtension` aan staat EN temperatuur < Stoptemperatuur, dan altijd minimaal compressor level 1

---

## 🔄 Veld Transformatie

### Actieve Velden (Nieuw)

| Oude Naam | Nieuwe Naam | Type | Bereik | Standaard | Opmerkingen |
|-----------|--------------|------|--------|-----------|-------------|
| `phComfortAbove` | **`phStopTemperature`** | number | ≥ 0 | 0.3 °C | Temperatuur boven setpoint waarbij WP stopt |
| `phRunExtensionRestartCooldown` | **`phRestartTemperature`** | number | onbeperkt | 0.1 °C | Herstarttemperatuur (relatief aan setpoint) |
| `phRunExtension` | `phRunExtension` | switch | true/false | false | Blijft ongewijzigd |

### Inactieve Velden (Behouden voor Backward Compatibility)

| Veld | Status | Reden |
|------|--------|-------|
| `phComfortBelow` | **Verwijderd uit actieve logica** | Vervallen functionaliteit |
| `phRunExtensionStopMargin` | **Verwijderd uit actieve logica** | Vervallen functionaliteit |

> ⚠️ **Belangrijk**: Deze velden **blijven in opslag** voor backward compatibility, maar worden **niet meer gebruikt** in de regeling.

---

## 🏗️ Nieuwe UI Structuur

### Power House Instellingen (vereenvoudigd)

```
Kamercorrectie [3000] W/°C
└─ Bepaalt hoe sterk er bijgestuurd wordt bij afwijking van het setpoint

Stoptemperatuur regeling: setpoint + [0,3] °C
└─ Als temperatuur boven (setpoint + 0.3) komt, schakelt WP uit

Herstarttemperatuur regeling: setpoint + [0,1] °C  
└─ WP start opnieuw als temperatuur onder (setpoint + 0.1) komt
   (moet ≥ 0.1°C lager zijn dan Stoptemperatuur)

Langer doorverwarmen: [Aan/uit]
└─ Als aan: WP blijft op minimaal niveau draaien als temperatuur < Stoptemperatuur
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
- **DemandTuning struct**: Alleen nieuwe velden
  ```c
  struct DemandTuning {
    float temperature_guard_c = 0.0f;
    float reaction_w_per_k = NAN;      // phKp
    float stop_temperature_c = NAN;    // phStopTemperature (nieuwe naam)
    float rise_time_min = NAN;
    float fall_time_min = NAN;
    int demand_max = 0;
  };
  ```

- **Verwijder comfort memory logica** (regels ~88-104)
- **Vereenvoudig error_c berekening**:
  ```c
  // Oud:
  const float low_c = fminf(effective_setpoint_c - below_c, high_base_c);
  float error_c = 0.0f;
  if (in.room_c < low_c) error_c = low_c - in.room_c;
  else if (in.room_c > high_base_c) error_c = high_base_c - in.room_c;
  
  // Nieuw:
  float error_c = in.setpoint_c - in.room_c;  // Directe error
  ```

- **Verwijder comfort_below_c** uit alle berekeningen

#### `oq_power_house_run_extension_logic.h`

**Wijzigingen:**
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
    bool warm_restart_intent = false;  // Blijft voor interne logica
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
- **DemandTuning initialisatie**: Gebruik alleen nieuwe velden
  ```c
  const oq_power_house::DemandTuning demand_tuning{
      config.temperature_guard_c,
      id(ph_kp_w_per_k).state,
      id(ph_stop_temperature_c).state,  // phStopTemperature
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
```

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

// Oude entities (behouden voor backward compatibility)
["phComfortBelow", DOMAIN_NUMBER, "Power House comfort below setpoint", true],
["phComfortAbove", DOMAIN_NUMBER, "Power House comfort above setpoint", true],
["phRunExtensionStopMargin", DOMAIN_NUMBER, "Power House run extension stop margin", true],
["phRunExtensionRestartCooldown", DOMAIN_NUMBER, "Power House run extension restart cooldown", true],
```

---

### 4. JavaScript UI Vertalingen (`nl.js`, `en.js`)

#### Nieuwe Vertalingen (nl.js)

```javascript
settingsHeating: {
    // Kamercorrectie
    phKpTitle: "Kamercorrectie",
    phKpCopy: "Bepaalt hoe sterk Power House het verwarmingsvermogen aanpast aan de afwijking van de gewenste kamertemperatuur. Een hogere waarde betekent meer extra warmte bij een te koude kamer en sterker terugregelen bij een te warme kamer.",
    
    // Stoptemperatuur
    phStopTemperatureTitle: "Stoptemperatuur regeling",
    phStopTemperatureCopy: "Temperatuur boven de gewenste temperatuur waarbij de warmtepomp stopt. Voorbeeld: bij een setpoint van 20,0 °C en een stoptemperatuur van 0,3 °C, schakelt de warmtepomp uit als de kamertemperatuur 20,3 °C bereikt.",
    phStopTemperatureDirection: "Setpoint + {value} °C",
    
    // Herstarttemperatuur
    phRestartTemperatureTitle: "Herstarttemperatuur regeling",
    phRestartTemperatureCopy: "Temperatuur waarbij de warmtepomp opnieuw start. Dit kan zowel boven als onder het setpoint liggen. Moet tenminste 0,1 °C lager zijn dan de stoptemperatuur. Voorbeeld: bij een stoptemperatuur van 0,3 °C en herstarttemperatuur van 0,1 °C, start de warmtepomp weer als de temperatuur onder 20,2 °C komt (bij setpoint 20,0 °C).",
    phRestartTemperatureDirection: "Setpoint + {value} °C",
    
    // Validatie
    phTemperatureValidationError: "Herstarttemperatuur moet tenminste 0,1 °C lager zijn dan de stoptemperatuur.",
    phStopTemperatureMinError: "Stoptemperatuur moet ≥ 0 zijn.",
    
    // Sectie koppen
    tuningTitle: "Power House instellingen",
    tuningCopy: "Stel in hoe Power House reageert op temperatuurverschillen.",
    maintainTitle: "Stop- en herstarttemperaturen",
    maintainCopy: "Bepaal wanneer de warmtepomp stopt en opnieuw start.",
    
    // Run Extension
    phRunExtensionTitle: "Langer doorverwarmen",
    phRunExtensionCopy: "Als ingeschakeld, blijft de warmtepomp op minimaal niveau draaien als de temperatuur onder de stoptemperatuur ligt. Dit voorkomt te vaak afslaan bij gematigde temperaturen.",
},

runExtension: {
    // Aangepast voor nieuwe logica
    title: "Langer doorverwarmen",
    copy: "Bij weinig warmtebehoefte kan een draaiende warmtepomp op laag vermogen blijven verwarmen tot de stoptemperatuur. Dit voorkomt te vaak afslaan.",
    
    allow: "Langer doorverwarmen",
    allowCopy: "Deze optie laat een draaiende warmtepomp op zijn laagste geschikte vermogen doorgaan als de temperatuur onder de stoptemperatuur ligt. Een stilstaande warmtepomp wordt door deze schakelaar niet gestart.",
    on: "Aan: de warmtepomp mag bij lage temperaturen blijven doorverwarmen.",
    off: "Uit: Power House regelt het vermogen en stopt wanneer het geen warmte meer vraagt.",
    
    // Status
    extending: "Doorverwarmen op minimumvermogen",
    comfortStop: "Stop aangevraagd",
    waitRestart: "Wacht op herstart",
    warmRestart: "Herstart aangevraagd",
    normal: "Normaal verwarmen",
    blocked: "Langer doorverwarmen kan nu niet: het water is te warm of er is geen geschikt laag vermogen beschikbaar.",
    disabled: "Uitgeschakeld",
    waiting: "Wacht tot de warmtepomp verwarmt",
},
```

#### Nieuwe Vertalingen (en.js)

```javascript
settingsHeating: {
    phKpTitle: "Room correction",
    phKpCopy: "Determines how strongly Power House adjusts the heating power based on the deviation from the desired room temperature. A higher value means more extra heat when the room is too cold and stronger reduction when the room is too warm.",
    
    phStopTemperatureTitle: "Stop temperature control",
    phStopTemperatureCopy: "Temperature above the setpoint at which the heat pump stops. Example: with a setpoint of 20.0 °C and a stop temperature of 0.3 °C, the heat pump switches off when the room temperature reaches 20.3 °C.",
    phStopTemperatureDirection: "Setpoint + {value} °C",
    
    phRestartTemperatureTitle: "Restart temperature control",
    phRestartTemperatureCopy: "Temperature at which the heat pump restarts. This can be both above and below the setpoint. Must be at least 0.1 °C lower than the stop temperature. Example: with a stop temperature of 0.3 °C and restart temperature of 0.1 °C, the heat pump restarts when the temperature drops below 20.2 °C (with setpoint 20.0 °C).",
    phRestartTemperatureDirection: "Setpoint + {value} °C",
    
    phTemperatureValidationError: "Restart temperature must be at least 0.1 °C lower than the stop temperature.",
    phStopTemperatureMinError: "Stop temperature must be ≥ 0.",
    
    tuningTitle: "Power House settings",
    tuningCopy: "Set how Power House responds to temperature differences.",
    maintainTitle: "Stop and restart temperatures",
    maintainCopy: "Determine when the heat pump stops and restarts.",
    
    phRunExtensionTitle: "Extended heating",
    phRunExtensionCopy: "When enabled, the heat pump continues to run at minimum level if the temperature is below the stop temperature. This prevents too frequent switching off at moderate temperatures.",
},
```

---

### 5. JavaScript UI Renderen (`heating.js`)

#### renderPowerHouseAdvancedField()

```javascript
export function renderPowerHouseAdvancedField() {
    const fields = [
        renderSettingsNumberField("phKp", t("settingsHeating.phKpTitle"), 
                                  t("settingsHeating.phKpCopy"), "", { 
                                      unitOverride: "W/°C",
                                      footerMarkup: `<p class="oq-settings-note">${escapeHtml(t("settingsHeating.phKpDirection"))}</p>`
                                  }),
        renderPowerHouseResponseProfilesField(),
    ].filter(Boolean);

    if (!fields.length && !hasEntity("phStopTemperature") && !hasEntity("phRunExtension")) {
        return "";
    }

    return `
      <div class="oq-settings-subpanel oq-settings-subpanel--nested oq-ph-comfort">
        <div class="oq-settings-subpanel-head">
          <h4>${escapeHtml(t("settingsHeating.tuningTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.tuningCopy"))}</p>
        </div>
        <section class="oq-settings-subpanel oq-settings-subpanel--nested">
          <div class="oq-settings-subpanel-head">
            <h4>${escapeHtml(t("settingsHeating.maintainTitle"))}</h4>
            <p>${escapeHtml(t("settingsHeating.maintainCopy"))}</p>
          </div>
          <div class="oq-settings-grid">
            ${renderSettingsNumberField("phStopTemperature", 
                t("settingsHeating.phStopTemperatureTitle"), 
                t("settingsHeating.phStopTemperatureCopy"), "", {
                    min: 0,
                    step: 0.05,
                    validate: (value) => {
                        const stop = parseFloat(value);
                        const restart = parseFloat(getEntityValue("phRestartTemperature") || "0.1");
                        if (stop < 0) return t("settingsHeating.phStopTemperatureMinError");
                        if (stop - restart < 0.1) return t("settingsHeating.phTemperatureValidationError");
                        return null;
                    }
                })}
            ${renderSettingsNumberField("phRestartTemperature", 
                t("settingsHeating.phRestartTemperatureTitle"), 
                t("settingsHeating.phRestartTemperatureCopy"), "", {
                    step: 0.05,
                    validate: (value) => {
                        const restart = parseFloat(value);
                        const stop = parseFloat(getEntityValue("phStopTemperature") || "0.3");
                        if (stop - restart < 0.1) return t("settingsHeating.phTemperatureValidationError");
                        return null;
                    }
                })}
          </div>
        </section>
        ${renderPowerHouseRunExtensionField()}
        ${renderSettingsAdvancedDisclosure("power-house", 
            t("settingsHeating.phAdvancedTitle"), 
            t("settingsHeating.phAdvancedCopy"), 
            `<div class="oq-settings-grid">${fields.join("")}</div>`)}
      </div>
    `;
}
```

#### renderPowerHouseRunExtensionField()

```javascript
export function renderPowerHouseRunExtensionField() {
    if (!hasEntity("phRunExtension")) return "";
    
    const enabled = Boolean(getEntityValue("phRunExtension"));
    
    return `
      <section class="oq-settings-subpanel oq-settings-subpanel--nested oq-run-extension" 
               aria-label="${escapeHtml(t("runExtension.title"))}">
        <div class="oq-run-extension-intro">
          <div class="oq-settings-subpanel-head">
            <h4>${escapeHtml(t("settingsHeating.phRunExtensionTitle"))}</h4>
            <p>${escapeHtml(t("settingsHeating.phRunExtensionCopy"))}</p>
          </div>
          <span class="oq-run-extension-status" data-oq-run-extension-status>
            ${escapeHtml(getRunExtensionCurrentStatus())}
          </span>
        </div>
        <div class="oq-settings-grid">
          ${renderSettingsSwitchField("phRunExtension", 
                t("runExtension.allow"), 
                t("runExtension.allowCopy"), 
                t("runExtension.on"), 
                t("runExtension.off"))}
        </div>
        <p class="oq-run-extension-note">${escapeHtml(t("runExtension.disableCopy"))}</p>
      </section>
    `;
}
```

#### getRunExtensionThresholds() - Vereenvoudigd

```javascript
export function getRunExtensionThresholds(useDrafts = true) {
    const numeric = (key) => {
        const entity = state.entities[key];
        const value = useDrafts ? getInputDraftValue(key) : entity?.value ?? entity?.state ?? "";
        return value === "" || value == null ? NaN : Number(value);
    };
    
    const setpoint = hasEntity("roomSetpoint") ? numeric("roomSetpoint") : NaN;
    const stopTemp = hasEntity("phStopTemperature") ? numeric("phStopTemperature") : 0.3;
    const restartTemp = hasEntity("phRestartTemperature") ? numeric("phRestartTemperature") : 0.1;
    
    const stop = Number.isFinite(setpoint) && Number.isFinite(stopTemp) 
                 ? setpoint + stopTemp 
                 : NaN;
    const restart = Number.isFinite(setpoint) && Number.isFinite(restartTemp)
                   ? setpoint + restartTemp
                   : NaN;
    
    return { setpoint, stop, restart, stopTemp, restartTemp };
}
```

#### renderRunExtensionThresholds() - Vereenvoudigd

```javascript
export function renderRunExtensionThresholds() {
    const thresholds = getRunExtensionThresholds();
    const active = getRunExtensionThresholds(false);
    const preview = ["setpoint", "stopTemp", "restartTemp"].some((key) => 
        !Object.is(thresholds[key], active[key]));
    
    const formatTemp = (value) => Number.isFinite(value) 
        ? `${formatNumber(value, { minimumFractionDigits: 1, maximumFractionDigits: 2 })} °C` 
        : "─";
    
    return `
        <p class="oq-run-extension-note">${escapeHtml(t(preview ? "runExtension.preview" : "runExtension.confirmed"))}</p>
        <div class="oq-run-extension-thresholds">
            <div>
                <span>${escapeHtml(t("runExtension.stop"))}</span>
                <strong>${escapeHtml(formatTemp(thresholds.stop))}</strong>
                <small>${escapeHtml(t("runExtension.stopNote"))}</small>
            </div>
            <div>
                <span>${escapeHtml(t("runExtension.restart"))}</span>
                <strong>${escapeHtml(formatTemp(thresholds.restart))}</strong>
                <small>${escapeHtml(t("runExtension.restartNote"))}</small>
            </div>
        </div>
        ${preview ? `<p class="oq-run-extension-note">${escapeHtml(t("runExtension.activeThresholds", { 
            stop: formatTemp(active.stop), 
            restart: formatTemp(active.restart) 
        }))}</p>` : ""}
    `;
}
```

---

### 6. Debug Recorder (`debug_recorder_default_fields.h`)

```c
// Nieuwe, actieve velden
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phStopTemperature", "number", "Power House stop temperature")
OQ_DEBUG_RECORDER_CONFIGURATION_FIELD("phRestartTemperature", "number", "Power House restart temperature")

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

// In de entity write handler:
function handleEntityWrite(entityId, value) {
    // Nieuwe velden: verwerk normaal
    if (["phStopTemperature", "phRestartTemperature", "phRunExtension"].includes(entityId)) {
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
| **Regeling** | Comfort memory vervallen | Geen opbouw meer |
| **Regeling** | Geen warm/koud herstart onderscheid | Eén herstarttemperatuur |
| **Velden** | `phComfortAbove` → `phStopTemperature` | Naamwijziging |
| **Velden** | `phRunExtensionRestartCooldown` → `phRestartTemperature` | Naamwijziging |
| **Velden** | `phComfortBelow` verwijderd | Niet meer gebruikt |
| **Velden** | `phRunExtensionStopMargin` verwijderd | Niet meer gebruikt |
| **UI** | Vereenvoudigde instellingen | 3 velden in plaats van 5 |
| **Validatie** | Herstart < Stop - 0.1 | Strenge controle |
| **Backward Compat** | Oude velden behouden | Read-only in nieuwe firmware |

---

## 🎯 Implementatie Volgorde

1. **C-Code Header Bestanden** (demand_logic, run_extension_logic)
2. **C-Code Runtime** (oq_power_house_runtime.h)
3. **YAML Configuratie** (oq_power_house_strategy.yaml)
4. **JavaScript Config** (config.js)
5. **JavaScript Vertalingen** (nl.js, en.js)
6. **JavaScript UI Renderen** (heating.js)
7. **Debug Recorder** (debug_recorder_default_fields.h)
8. **Validatie Logica** (frontend + backend)
9. **Backward Compatibility Layer**

---

## ✅ Acceptatie Criteria

- [ ] Nieuwe velden (`phStopTemperature`, `phRestartTemperature`) werken in UI
- [ ] Validatie: Herstart < Stop - 0.1 wordt gehandhaafd
- [ ] Validatie: Stop ≥ 0 wordt gehandhaafd
- [ ] Oude velden (`phComfortBelow`, `phComfortAbove`, etc.) zijn verborgen maar beschikbaar
- [ ] Schrijven naar oude velden geeft geen foutmeldingen
- [ ] Lezen van oude velden werkt (voor backward compatibility)
- [ ] Regeling gebruikt alleen nieuwe velden
- [ ] Minimaal vermogen logica werkt (phRunExtension + temp < stop)
- [ ] Geen comfort memory meer in de regeling
- [ ] Geen onderscheid meer tussen warm/koud herstart

---

## 📚 Referentie: Nieuwe Regeling Logica

```
1. Bepaal basis warmtevraag:
   - Huismodel: f(T_outside, T_cold, T_zero, rated_power)
   - Kamercorrectie: phKp * (setpoint - room_c)
   
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

*Document versie: 1.0*
*Laatste update: 2024*
*Status: Concept*
