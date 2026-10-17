# Heating Strategy Development

Deze pagina beschrijft het uitbreidingscontract van de actieve thermal stack.
Voor het werkingsprincipe lees je eerst [Verwarmingsregeling](verwarmingsregeling.md)
en [Koelregeling](koelregeling.md). Het [template](heating-strategy-template.yaml)
is een niet-ingeladen, standaard geblokkeerd startpunt; er bestaat geen
algemeen plug-inmechanisme dat een nieuwe strategie vanzelf aansluit.

## Eigenaarschap en bronindeling

De keten bestaat uit strategie/selectie, strategie-dispatch, gedeelde request
arbitrage en de actuator. Supervisory bepaalt parallel de Control Mode en de
startvoorwaarden. Een vraag berekenen is iets anders dan een HP vrijgeven.

| Onderdeel | Eigenaar en bron |
| --- | --- |
| Strategie kiezen en resetten | `openquatt/oq_strategy_manager.yaml` en `openquatt/includes/control/oq_strategy_runtime.h`. |
| Power House-vraag en dispatch | `openquatt/oq_power_house_strategy.yaml`, `oq_power_house_runtime.h` en bijbehorende `*_logic.h`. |
| Stooklijndoel, PID-adapter en dispatch | `openquatt/oq_heating_curve_strategy.yaml`, `oq_heating_curve_runtime.h` en bijbehorende `*_logic.h`. |
| Koelvraag en dispatch | `openquatt/oq_cooling_strategy.yaml`, `oq_cooling_runtime.h` en bijbehorende `*_logic.h`. |
| Control Mode, flow- en startvoorwaarden, ketelassistentie | `openquatt/oq_supervisory_controlmode.yaml` en `oq_supervisory_state_runtime.h`. |
| Requestselectie, gedeelde beperkingen en minimumlooptijd | `openquatt/oq_thermal_request_control.yaml`, `oq_thermal_request_runtime.h` en `oq_thermal_request_logic.h`. |
| Laatste HP-guards, mode-/levelwrites en bookkeeping | `openquatt/oq_thermal_actuator.yaml` en `oq_thermal_actuator_runtime.h`. |

De genoemde headers staan onder `openquatt/includes/control/`.

YAML declareert entities, globals, configuratie en scheduler-wiring. De huidige
`*_runtime.h`-adapters zijn stateful: zij lezen `id(...)`, beheren runtime-state,
publiceren status en verzorgen binnen hun eigen subsysteem side effects.
`*_logic.h` bevat de afzonderlijk testbare beslislogica met expliciete inputs,
state en outputs. Pure logica mag state teruggeven of bijwerken, maar doet geen
hardwarewrites, netwerk-I/O of ESPHome-publicatie. Een runtimeheader is dus
niet automatisch een pure helper.

Houd eigenaarschap zichtbaar: een strategy-runtime mag eigen berekeningen,
requests en diagnostiek publiceren. HP-mode-/levelwrites blijven bij de actuator;
een nieuwe strategie mag die grens niet omzeilen.

## Gedeelde status en geldigheid

`oq_heat_mode_code` kiest de verwarmingsstrategie: `0` Power House en `1`
Water Temperature Control. `oq_strategy_active_code` gebruikt een andere
nummering: `1` koelen, `2` stooklijn en `3` Power House. De geselecteerde
strategie kan ook in stand-by actief zijn om een toekomstige startvraag te
berekenen; deze code bewijst geen draaiende compressor.

| Output | Contract |
| --- | --- |
| `oq_demand_raw` | Genormaliseerde verwarmingsvraag `0..20`, onafhankelijk van fysieke V2-standnummers. |
| `oq_strategy_phase_code` | Strategie-lokale fase; de betekenis moet per strategie worden vastgelegd. |
| `oq_strategy_requested_power_w` | Gevraagde watts wanneer deze strategie daarop regelt, anders `NAN`. |
| `oq_strategy_supply_target_temp` | Aanvoerdoel wanneer deze strategie daarop regelt, anders `NAN`. |
| `oq_strategy_heat_request_active` | Warmte-intentie die supervisory gebruikt; geen hardwarevrijgave. |
| `oq_strategy_hp_expected_power_w`, `oq_strategy_hp_max_power_w`, `oq_strategy_hp_saturated` | Capaciteitsdiagnostiek voor consumenten die dit ondersteunen; geen verzonnen meetwaarden. |
| `oq_strategy_output_valid` | Alleen waar wanneer de relevante inputs en berekening geldig zijn. |
| `oq_strategy_output_source_code` | Identiteit van de producer, overeenkomstig de strategiecode. |
| `oq_strategy_output_updated_ms` | Tijdstip van de berekende output; nul betekent nog niet gepubliceerd. |

Publiceer de samenhangende velden in dezelfde loopcontext en zet de geldigheid
pas na het invullen van de bijbehorende output. Een inactieve strategie wist
haar lokale requests en mag geen output van de nieuwe eigenaar overschrijven.

Supervisory gebruikt voor ketelassistentie onder meer
[`strategy_output_is_current()`](../../openquatt/includes/boiler/oq_boiler_logic.h):
geldig, juiste producer en niet-nul timestamp. Die functie controleert zelf
geen maximale leeftijd. Een nieuw of gewijzigd pad moet zijn benodigde
bronversheid en eventuele age-timeout daarom expliciet ontwerpen en testen.
Gebruik geen nul/default als vervanging voor een ontbrekende proceswaarde.

## Per-strategie requests

De huidige consumers lezen verschillende lokale interfaces:

- Power House: `oq_ph_request_hp1_level`, `oq_ph_request_hp2_level`,
  `oq_ph_request_owner_hp` en `oq_ph_request_reason_code`.
- Stooklijn: `oq_curve_dispatch_hp1_level`, `oq_curve_dispatch_hp2_level`,
  `oq_curve_request_owner_hp` en `oq_curve_capacity_mode_code`.
- Koelen: `oq_cooling_request_hp1_level`, `oq_cooling_request_hp2_level`,
  `oq_cooling_request_owner_hp` en `oq_cooling_request_reason_code`.

De allocator kiest een combinatie; request control maakt daarvan de gedeelde
`oq_request_*`-status en begrensde `oq_actuator_hp1_req` / `oq_actuator_hp2_req`.
De actuator en teruggelezen HP-toestand bepalen pas de uitvoering. Een
`..._request_hp1_level` schrijven is geen bevestiging van Modbus-uitvoering.

## Een strategie toevoegen

1. **Definieer het regelprincipe.** Leg inputsignalen, units, vereiste
   bronversheid, toestanden, resetgedrag, vraag en stop-/herstartcriteria vast.
   Onderscheid een gewone regelstop van een harde veiligheidsingreep.
2. **Voeg package en logica toe.** Begin met het template, plaats pure
   beslislogica onder `openquatt/includes/control/` en gebruik zo nodig een
   stateful runtime-adapter. Houd buffers/state en hun allocatiebeleid expliciet.
3. **Sluit de package aan** in `openquatt/packages/20_strategies.yaml`.
   Die bundel wordt via `openquatt/oq_packages_common.yaml` door Single en Duo
   geladen. De vroegere `oq_packages.yaml` / `oq_packages_single.yaml` bestaan niet meer.
4. **Breid selectie en reset uit.** Pas `oq_strategy_manager.yaml` en
   `oq_strategy_runtime.h` aan, kies een vrije strategiecode en zorg dat
   omschakelen oude intenties, lokale requests en relevante integratoren intrekt.
   Alleen een selectoroptie toevoegen maakt een strategie nog niet actief.
5. **Breid consumers expliciet uit.** Controleer `ModeContext`,
   `resolve_mode_context()`, `StrategyRequestInput`, `select_strategy_request()`
   in `oq_thermal_request_logic.h`, de adapter in `oq_thermal_request_runtime.h`
   en de supervisory-keuzes in `oq_supervisory_state_runtime.h`. De huidige
   vertakkingen kennen de bestaande strategieën; een onbekende selectorwaarde
   mag niet stilzwijgend in het Power House-pad terechtkomen.
6. **Ontwerp ketelgedrag afzonderlijk.** Bepaal of en hoe de strategie CM3 kan
   aanvragen. Ondersteun benodigde watts-/target-/capaciteitssignalen eerlijk
   of blokkeer die route; `NAN` vervangen door nepvermogen is geen integratie.
   CM4-foutfallback houdt zijn eigen beschikbaarheids- en stopbevestigingscontract.
7. **Behoud de laatste guards.** Controleer CM5, CM98, CM100, OTA/recovery,
   ontbrekende bronnen, harde watertrip, HP-incidenten, toegestane frequenties,
   minimum-uit-tijd, minimumlooptijd en de startlimiet. Een actieve strategie
   mag voorflow voorbereiden vóór CM2 zonder de actuator vroeg vrij te geven.
8. **Werk diagnostiek en uitleg bij.** Leg reden-/fasecodes vast en controleer
   beslislog, web-app, de gebruikershandleiding en deze ontwikkelaarsdocs.

Controleer codes in alle consumers: `oq_request::StrategyCode` gebruikt `4`
al voor `STRATEGY_MANUAL_HP`. Het template gebruikt daarom `5` als illustratie,
niet als vooraf aangesloten strategie. Een extra code is een keuze in een
volledig contract; bestaande consumers doen niet automatisch mee. Strategiecodes,
`oq_heat_mode_code` en Control Modes zijn afzonderlijke nummeringen.

## Validatie bij een wijziging

- Test de pure logica deterministisch: positieve vraag, nulvraag, ongeldige en
  verouderde inputs, grensovergangen, timerverloop en `millis()`-wraparound.
- Test bron- en strategieswitches met nog bestaande output, cancelled start,
  vroege/late busantwoorden, HP-onbeschikbaarheid en minimumlooptijd tegenover
  harde stop. Neem Single en Duo en boot/herstart afzonderlijk mee.
- Verifieer dat alleen de bedoelde producer gedeelde output publiceert en dat
  ongeldige output geen ketelassistentie of nieuwe HP-start kan vrijgeven.
- Gebruik de passende host-/Python-regressies en `npm run check:cpp-format`
  bij C/C++-wijzigingen. Voor webwijzigingen geldt de webontwikkelhandleiding.
- Valideer de twee enabled targets uit `build_targets.yaml` met
  `python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/single.yaml --config configs/heatpump_controller_q/duo.yaml`.
  Rapporteer een mislukte projectgate als mislukt; configvalidatie is geen
  firmwarecompile of runtimekwalificatie.
- Voer noodzakelijke firmwarebuilds sequentieel uit met maximaal twee
  compilerjobs. Control-, geheugen- of timingwijzigingen vereisen passende HIL
  en een onafhankelijke koude review volgens `AGENTS.md`.

Bewaar concrete testresultaten in de PR. De repo bewaart herbruikbare
procedures, contracten en regressies. Zie ook [HIL-testing](hil-testing.md)
en [de bijdrageafspraken](../../CONTRIBUTING.md).
