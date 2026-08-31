# YAML-naar-C++-migratieplan

Dit document volgt de resterende migratie van regel- en veiligheidslogica uit
ESPHome-YAML naar testbare C++-modules. YAML blijft het compacte contract voor
entities, configuratie, koppelingen en één of enkele runtime-aanroepen.

## Doel en meetwijze

- Verklein grote inline lambdas en maak beslissingen zonder ESPHome op de host testbaar.
- Behoud entity-ID's, standaardwaarden, timing en Single/Duo-gedrag, tenzij een
  expliciet vastgelegde fail-closed aanscherping nodig is.
- Rapporteer per PR afzonderlijk:
  - YAML-regels;
  - totale productiecode (YAML en C++);
  - tests en totale repositorydelta.
- Streef over de volledige migratie naar neutrale of kleinere productiecode.
  Nieuwe regressietests tellen apart en mogen de repository bewust laten groeien.

## Acceptatiecriteria per werkblok

- De YAML bevat hoofdzakelijk declaratieve configuratie en compacte runtimebinding.
- Pure input/state/output-besliskernen hebben hosttests voor normale paden,
  grenswaarden, ontbrekende/stale/ongeldige inputs, timeronderbrekingen, reboot en
  `millis()`-rollover waar relevant.
- `npm run check:cpp-format`, `npm run check:python-contracts` en de volledige
  hostregressies zijn groen.
- Q Single en Q Duo valideren en compileren met de vastgelegde ESPHome-versie.
- Safety-, flow-, actuator-, boiler- of timingwijzigingen krijgen HIL tegen de
  ODU-/OpenTherm-simulator, inclusief foutinjectie en herstelpad.
- Na HIL draait de controller weer op schone `dev`-firmware en staat de simulator
  terug op de normale uitgangsinstellingen.
- Voor publicatie wordt de volledige diff én een afzonderlijke adversarial review
  uitgevoerd op lifecycle, timing, fail-closed gedrag, interleavings en side effects.

## Voortgang

| Werkblok | Scope | Status | PR |
|---|---|---|---|
| Thermal Actuator | Runtime en fail-closed besliskern | Gereed | #564, #566 |
| Heating Curve-kern | Vermogenscap en vraagregime | Gereed | #567, #570 |
| Power House-kern | Vraaglogica en dispatch | Gereed | #568, #571 |
| Cooling-kern | Herstel/timing, vraag/dispatch en safety/handover | Gereed | #573, #577, #578 |
| Thermal Request | Arbitrage en actuatorrequests | Gereed | #581 |
| Supervisory safety | Vermogenslimiet plus flow-/frost-interlocks | Klaar voor merge | #582 |
| Supervisory state-machine | Resterende hoofdloop naar C++ | In uitvoering | Nog te openen |
| Strategy runtimes | Heating Curve, Power House, Cooling en managerbinding | Gepland | Nog te openen |
| Hydraulics en outputs | Flow Control, Thermal Limits en Auxiliary Relay | Gepland | Nog te openen |
| Boiler runtime | Commandocapture, outputcontroller en transportbinding | Gepland | Nog te openen |
| Bronselectie-opruiming | Generieke selectie/freshness waar dit YAML werkelijk verkleint | Beslispunt na bovenstaande blokken | Nog te bepalen |

## Actueel werkblok: Supervisory state-machine

Doel: de resterende grote lambda in `oq_supervisory_controlmode.yaml` vervangen
door compacte inputcaptatie en één runtime-aanroep. Dit werkblok blijft één PR,
maar mag intern meerdere controleerbare commits bevatten.

Omvang:

1. Warmtevraag- en sessiestatus:
   - Power House low-load-hysterese, startbevestiging en re-entryblock;
   - CM2 idle-exit en heating-enable-gate;
   - compressor-/modeactiviteit en manual-HP-vraag.
2. ControlMode-resolver:
   - CM1 pre-/postflowtimers;
   - override en commissioning;
   - cooling/heating/frost/fallback-keuze;
   - cold-water-start en CM3 promote/demote.
3. Overgangen en side effects:
   - boilerownership bij modewissels;
   - cooling-energiesessie;
   - modepublicatie en decision-logevents.
4. Supervisory policies:
   - silent window en low-noise-uitvoer;
   - CM0 sticky-pumpbescherming en pump ownership.

Specifieke HIL-scenario's:

- CM0 ↔ CM1 ↔ CM2 en CM5 met pre-/postflow;
- wegvallende en herstellende flow;
- CM98 hysterese en ontbrekende buitentemperatuur;
- Single/Duo-fallback, defrost en actieve-ODU stopbevestiging;
- override-timeout en terugkeer naar Auto;
- Power House startbevestiging/idle-exit en Heating Curve hervatting;
- CM3 assist/fallback-handover voor relais en OpenTherm;
- silent window en sticky-pumprun zonder extra Modbus-chatter.

## Bewust in YAML houden

- targetconfiguraties, packages, profiles en substitutions;
- entitydefinities en gebruikersinstellingen;
- Modbus-, OpenTherm- en CiC-registercontracten;
- PID-componentconfiguratie;
- korte declaratieve lambdas die alleen een state of label doorgeven.

De bestanden `oq_HP_io.yaml`, `oq_common.yaml` en protocolmappings worden dus
niet op regelaantal alleen gemigreerd. Alleen herhaalde of risicovolle
beslislogica verhuist wanneer dat de contractgrens aantoonbaar verbetert.
