# Handoff: Power House Comfort Velden Analyse

## 📌 Context
Deze sessie heeft de **invulvelden onder `view=settings&section=heating` → `<h4>Power House — comfort</h4>`** geanalyseerd, specifiek:
- Afkoeling onder gewenste temperatuur
- Afremmen boven gewenste temperatuur
- Langer doorverwarmen
- Stopgrens boven gewenste temperatuur
- Afkoeling na stoppen

## 🔍 Conclusies

### 1. Velddefinities en ID's
Er zijn **twee logische groepen** in de Power House comfortregeling:

#### **Groep A: Basis Comfortregeling (altijd actief)**
| Veld (UI) | JS ID | C-code ID | HA Entity | YAML ID | Type | Bereik | Standaard |
|-----------|-------|-----------|-----------|---------|------|--------|----------|
| Afkoeling onder gewenste temperatuur | `phComfortBelow` | `ph_comfort_band_below_c` | `number.openquatt_ph_comfort_band_below_c` | `ph_comfort_band_below_c` | number | 0–2.0 °C | 0.1 °C |
| Afremmen boven gewenste temperatuur | `phComfortAbove` | `ph_comfort_band_above_c` | `number.openquatt_ph_comfort_band_above_c` | `ph_comfort_band_above_c` | number | 0–2.0 °C | 0.3 °C |

#### **Groep B: Langer Doorverwarmen (optioneel)**
| Veld (UI) | JS ID | C-code ID | HA Entity | YAML ID | Type | Bereik | Standaard |
|-----------|-------|-----------|-----------|---------|------|--------|----------|
| Langer doorverwarmen | `phRunExtension` | `ph_run_extension_enabled` | `switch.openquatt_ph_run_extension_enabled` | `ph_run_extension_enabled` | switch | true/false | false |
| Stopgrens boven gewenste temperatuur | `phRunExtensionStopMargin` | `ph_run_extension_stop_margin_c` | `number.openquatt_ph_run_extension_stop_margin_c` | `ph_run_extension_stop_margin_c` | number | 0.1–1.0 °C | 0.5 °C |
| Afkoeling na stoppen | `phRunExtensionRestartCooldown` | `ph_run_extension_restart_cooldown_c` | `number.openquatt_ph_run_extension_restart_cooldown_c` | `ph_run_extension_restart_cooldown_c` | number | 0.1–3.0 °C | 0.2 °C |

---

### 2. Achterliggende Regeling

#### **Basis Comfortregeling (Groep A)**
- **Comfortband**: `[setpoint - phComfortBelow, setpoint + phComfortAbove]`
  - **Onder de band** (`room_c < setpoint - phComfortBelow`):
    - Extra warmtevraag: `error_c = (setpoint - phComfortBelow) - room_c`
    - Geschaald met `phKp` (W/K) → extra vermogen in Watt.
    - **Comfort memory** bouwt op bij langdurige afkoeling (max. ~0.20 °C).
  - **Binnen de band**:
    - Directe reactie is **vlak** (geen scherpe PID-correctie).
    - Comfort memory kan nog doorwerken.
  - **Boven de band** (`room_c > setpoint + phComfortAbove`):
    - Warmte wordt **teruggenomen**: `error_c = setpoint - room_c` (negatief).
    - Geschaald met `phKp` → vermogen wordt afgetrokken.
    - **Geen harde stop**: Warmtepomp kan blijven draaien op laag vermogen.

#### **Langer Doorverwarmen (Groep B)**
- **Alleen actief** als `phRunExtension = true` **EN** er een **actieve verwarmingscyclus** is.
- **Stopgrens**: `comfort_stop_c = setpoint + phRunExtensionStopMargin`
  - Stopverzoek als `room_c >= comfort_stop_c` (na minimale looptijd).
  - Restwarmte kan de kamer verder opwarmen.
- **Herstart**:
  - `warm_restart_c = comfort_stop_c - phRunExtensionRestartCooldown`
  - Opnieuw verwarmen mogelijk als `room_c <= warm_restart_c` **EN** minimale uit-tijd is verstreken.
  - **`phComfortBelow` gaat voor**: Herstarttemperatuur kan niet lager zijn dan `setpoint - phComfortBelow`.

---

### 3. Belangrijke Code Locaties

| Onderdeel | Bestandslocatie | Relevante Functies/Structuren |
|-----------|-----------------|-------------------------------|
| **Basis demand logic** | `openquatt/includes/control/oq_power_house_demand_logic.h` | `decide_demand()`, `DemandTuning`, `DemandInput` |
| **Run extension logic** | `openquatt/includes/control/oq_power_house_run_extension_logic.h` | `evaluate()`, `Phase`, `Tuning` |
| **Runtime integratie** | `openquatt/includes/control/oq_power_house_runtime.h` | `tick()`, `Runtime` |
| **YAML configuratie** | `openquatt/oq_power_house_strategy.yaml` | `ph_comfort_band_below_c`, `ph_run_extension_*` |
| **JS UI configuratie** | `openquatt/web/js/src/core/config.js` | `phComfortBelow`, `phComfortAbove`, `phRunExtension*` |
| **JS UI renderen** | `openquatt/web/js/src/settings/heating.js` | `renderPowerHouseRunExtensionField()`, `renderPowerHouseComfortThresholds()` |
| **UI tekst (NL)** | `openquatt/web/js/src/i18n/nl.js` | `phComfortBelowTitle`, `runExtension.margin`, etc. |

---

### 4. Diagnostische Sensoren

| Sensor | HA Entity | C-code ID | Beschrijving |
|--------|-----------|-----------|--------------|
| Comfort memory | `sensor.openquatt_power_house_comfort_memory` | `oq_phouse_comfort_memory_c` | Opgebouwde correctie (0–0.20 °C) |
| Run extension status | `text_sensor.openquatt_power_house_run_extension_status` | `oq_ph_run_extension_status` | Status: `inactive`, `normal`, `extending`, `comfort_stop`, `wait_warm_restart`, `warm_restart` |
| Run extension comfort stop | `sensor.openquatt_power_house_run_extension_comfort_stop` | `oq_ph_run_extension_comfort_stop_c` | Berekende stopgrens (°C) |
| Run extension warm restart | `sensor.openquatt_power_house_run_extension_warm_restart` | `oq_ph_run_extension_warm_restart_c` | Berekende herstarttemperatuur (°C) |

---

### 5. Samenvatting van de Regeling

```
┌─────────────────────────────────────────────────────────────────┐
│                    POWER HOUSE COMFORT REGELING                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. BEREKEN HUISVRAAG (altijd):                                      │
│     - Huismodel: f(T_outside, T_cold, T_zero, rated_power)          │
│     - Externe vraag (optioneel)                                      │
│                                                                      │
│  2. KAMERCORRECTIE (altijd actief):                                  │
│     ┌─────────────────────────────────────────────────────────┐ │
│     │  Comfortband: [setpoint - below, setpoint + above]         │ │
│     │                                                             │ │
│     │  room_c < (setpoint - below)  →  +extra warmte (W/K * ΔT) │ │
│     │                                                             │ │
│     │  room_c > setpoint            →  -minder warmte (W/K * ΔT) │ │
│     │                                                             │ │
│     │  Binnen band:                 →  vlakke reactie             │ │
│     └─────────────────────────────────────────────────────────┘ │
│     - Comfort memory: bouwt op bij langdurige afkoeling           │
│                                                                      │
│  3. LANGER DOORVERWARMEN (optioneel, als phRunExtension=true):     │
│     - Alleen voor ACTIEVE runs (geen start van stilstaande WP)     │
│     - Stop als: room_c >= (setpoint + stop_margin)                 │
│     - Herstart als: room_c <= (setpoint + stop_margin - cooldown) │
│       EN minimale uit-tijd verstreken                                │
│     - phComfortBelow begrenst de herstarttemperatuur               │
│                                                                      │
│  4. BEGRENZINGEN (altijd):                                           │
│     - Waterbegrenzing (water_limit_factor)                         │
│     - Minimale looptijd                                             │
│     - Maximale vermogensvraag                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

### 6. Voorbeelden

#### **Voorbeeld 1: Standaardinstellingen (Groep A)**
- Setpoint: 21.0 °C
- `phComfortBelow`: 0.1 °C → Extra warmte onder **20.9 °C**
- `phComfortAbove`: 0.3 °C → Minder warmte boven **21.3 °C**
- `phKp`: 3000 W/K → 0.1 °C onder setpoint → **+300 W** extra vraag

#### **Voorbeeld 2: Langer Doorverwarmen (Groep B)**
- Setpoint: 21.0 °C
- `phRunExtension`: **Aan**
- `phRunExtensionStopMargin`: 0.7 °C → Stopverzoek bij **21.7 °C**
- `phRunExtensionRestartCooldown`: 0.2 °C → Herstart mogelijk bij **21.5 °C**
- `phComfortBelow`: 0.2 °C → **Begrensd herstart bij 20.8 °C** (gaat voor!)

---

### 7. Openstaande Vragen / Aandachtspunten

1. **Terminologie**:
   - In de UI heten de velden **Afkoeling onder gewenste temperatuur** en **Afremmen boven gewenste temperatuur**, maar in de backend (YAML/C-code) zijn het `comfort_band_below_c` en `comfort_band_above_c`.
   - **Langer doorverwarmen** is in de backend `run_extension`.

2. **Comfort Memory**:
   - Wordt opgebouwd bij `room_c < (setpoint - below_c)`.
   - Loopt af als `room_c > mid_base_c` (midden van de comfortband).
   - Maximaal: `0.05 + 0.50 * above_c` (dus afhankelijk van `phComfortAbove`).

3. **Interactie tussen Groep A en B**:
   - `phComfortBelow` begrenst de herstarttemperatuur van **Langer doorverwarmen**.
   - `phComfortAbove` heeft **geen directe interactie** met Groep B.

4. **Waterbegrenzing**:
   - Altijd leidend, ook als de kamer koud is.
   - Als `water_limit_factor < 0.999`, wordt de warmtevraag gedempt.

---

### 8. Branch Informatie
- **Huidige branch**: `feature/power-house-revised`
- **Base commit**: `078d63ec` (Herstel contrast en lichte vlakken in donkere modus (#875))
- **Base branch**: `origin/dev`

---

### 9. Volgende Stappen (voor nieuwe sessie)
- [ ] Verifieer of de terminologie in de UI en backend consistent is.
- [ ] Onderzoek of `phComfortAbove` ook een rol speelt in de **Langer doorverwarmen** logica.
- [ ] Controleer of de **comfort memory** correct wordt gereset bij een setpoint wijziging.
- [ ] Test de interactie tussen `phComfortBelow` en `phRunExtensionRestartCooldown`.

---

### 10. Gerelateerde Documentatie
- [Power House uitleg (officiële docs)](docs/power-house.md)
- [Langer doorverwarmen instellen](docs/web-app/instellingen.md#langer-doorverwarmen)
- [Dagelijks verwarmen](docs/dagelijks/verwarmen.md)
- [C-code: Demand Logic](openquatt/includes/control/oq_power_house_demand_logic.h)
- [C-code: Run Extension Logic](openquatt/includes/control/oq_power_house_run_extension_logic.h)
