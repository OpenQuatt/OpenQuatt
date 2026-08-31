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
| Supervisory safety | Vermogenslimiet plus flow-/frost-interlocks | Gereed | #582 |
| Supervisory state-machine | Resterende hoofdloop naar C++ | Gereed | #583 |
| Strategy runtimes | Heating Curve, Power House, Cooling en managerbinding | Gereed, PR volgt | Deze PR |
| Hydraulics en outputs | Flow Control, Thermal Limits en Auxiliary Relay | Gepland | Nog te openen |
| Boiler runtime | Commandocapture, outputcontroller en transportbinding | Gepland | Nog te openen |
| Bronselectie-opruiming | Generieke selectie/freshness waar dit YAML werkelijk verkleint | Beslispunt na bovenstaande blokken | Nog te bepalen |

## Actueel werkblok: Strategy runtimes

Doel: de resterende stateful ESPHome-lambdas van Heating Curve, Power House en
Cooling vervangen door compacte runtime-aanroepen. De Strategy Manager blijft
het declaratieve contract voor selectie en diagnostische entities, maar draagt
modewissels, bronselectie en gedeelde statuspublicatie over aan C++.

Omvang:

1. Heating Curve-runtime:
   - PID-output naar vraagbeslissing en freshness van kamermetingen;
   - buitentemperatuurfilter, stooklijndoel en PID-lifecycle;
   - Single/Duo-dispatch, defrost-/oil-return-hold en rolloverveilige cadence.
2. Power House-runtime:
   - vraagcaptatie, filtering en gedeelde strategy-output;
   - performancecandidate-opbouw en Single/Duo-dispatch;
   - responseprofielbinding en lifecycle-reset.
3. Cooling-runtime:
   - demand/safety-inputcaptatie, eventtransities en minimum-off-diagnostiek;
   - ownerdispatch, stopbevestiging en strategy-output;
   - lifecycle-reset buiten CM5.
4. Strategy Manager-binding:
   - atomische reset bij strategywissel;
   - actieve-strategy- en waterlimitpublicatie;
   - lokale buitentemperatuuraggregatie met freshness.

Gezamenlijke HIL-acceptatie:

- Power House en Heating Curve onder normale vraag, ramp-up en vraagwegval;
- Heating Curve stop/hervatting, waterlimit en Single/Duo-dispatch;
- Cooling start/stop, minimum-off, flowverlies, dauwpunt/fallback en herstel;
- strategywissels tijdens idle en actieve vraag zonder stale output of extra
  compressorstart;
- defrost/oil-return en incidentgestuurde eigenaarwissel;
- herstel naar schone `dev`-firmware en normale simulatorinstellingen.

Afronding:

- de vier strategy-YAML's zijn samen 1.330 regels kleiner; inclusief de nieuwe
  runtimeheaders en gedeelde helper is de productiecode netto 26 regels kleiner;
- de volledige hostset (55), Python-contractset (163) en C++-formatcontrole zijn groen;
- Q Single en Q Duo compileren; Q Duo gebruikt statisch 16 bytes meer DIRAM en
  het image is 208 bytes groter dan de schone `dev`-basis;
- HIL is geslaagd voor Power House- en Heating Curve-vraag/dispatch, een actieve
  strategieswitch zonder extra compressorstart, Cooling fail-closed zonder
  dauwpunt, minimum-off, flowverlies/herstel en defrost-hold;
- bij geïnjecteerd ODU2-linkverlies werd HP2 bevestigd onbeschikbaar en gestopt,
  nam HP1 de aanvraag over en keerde ODU2 na stabiele telemetrie terug naar
  `healthy/available`; Power House hervatte daarna via de normale startbevestiging;
- Heating Curve-stop/hervatting en waterlimiet zijn hostmatig afgedekt; de HIL-rig
  kan de geselecteerde lokale PT1000-watertemperatuur niet injecteren;
- na HIL is schone `dev` (`e9b6290e`) teruggezet en zijn de normale controller-
  en simulatorinstellingen hersteld.

## Vorig werkblok: Supervisory state-machine

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

Tussenstand na implementatie:

- `oq_supervisory_controlmode.yaml`: 1.915 naar 677 regels (-1.238);
- productiecode van dit werkblok: 1.915 naar 2.093 regels (+178), binnen de
  acceptatiegrens van 2.150; tests en dit plandocument tellen afzonderlijk;
- Q Single en Q Duo compileren; statisch DIRAM is voor Q Duo +8 bytes ten
  opzichte van schone `dev` (210.671 tegenover 210.663 bytes);
- host- en contracttests dekken low-load, startbevestiging, idle-exit, override,
  silent window, sticky-pump, ongeldige invoer, timergrenzen en rollover;
- HIL geslaagd voor CM-overrides, CM0/CM1/CM2 met flowverlies en herstel,
  Power House-vraag, Heating Curve-hervatting tijdens postflow, CM5 met pre- en
  postflow, defrost-hold, silent mode en CM4/OpenTherm-fallback na bevestigde
  Duo-fouten;
- na HIL is schone `dev` (`14002173`) teruggezet en staan controller en
  simulator weer op de normale uitgangsinstellingen.

## Bewust in YAML houden

- targetconfiguraties, packages, profiles en substitutions;
- entitydefinities en gebruikersinstellingen;
- Modbus-, OpenTherm- en CiC-registercontracten;
- PID-componentconfiguratie;
- korte declaratieve lambdas die alleen een state of label doorgeven.

De bestanden `oq_HP_io.yaml`, `oq_common.yaml` en protocolmappings worden dus
niet op regelaantal alleen gemigreerd. Alleen herhaalde of risicovolle
beslislogica verhuist wanneer dat de contractgrens aantoonbaar verbetert.
