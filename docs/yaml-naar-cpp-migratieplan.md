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
| Strategy runtimes | Heating Curve, Power House, Cooling en managerbinding | Gereed | #584 |
| Hydraulics en outputs | Flow Control, Thermal Limits en Auxiliary Relay | Gereed | #589 |
| Boiler runtime | Commandocapture, outputcontroller en transportbinding | Gereed | #595 |
| Externe inputs en bronselectie | API-freshness plus generieke, brongebonden selectie | Go onder harde LOC- en testbaarheidsgrens | Nog te bepalen |

## Besluit volgend werkblok: externe inputs en bronselectie

De go/no-go-analyse op `dev` commit `1afdf23c` geeft een voorwaardelijke **go**
voor één gebundeld vervolgblok. De reden is niet alleen YAML-grootte:

- `oq_sensor_sources.yaml` en `oq_api_ingress.yaml` tellen samen 1.461 regels,
  waarvan 31 inline lambdas samen 815 regels uitvoerbare C++ bevatten;
- de API-ingress-lifecycle en de bronselectiematrix hebben nog geen directe
  hostregressies. Alleen de bestaande supply-calibratie- en holdhelpers zijn
  rechtstreeks afgedekt;
- de huidige HA-hold voor buiten-, kamer- en setpointtemperatuur onthoudt niet
  welke bron de cache vulde. Na een bronwissel kan daardoor maximaal 300 s een
  waarde van bijvoorbeeld Outdoor unit, OpenTherm, CIC, API of MQTT als
  HA-helde waarde terugkomen;
- OpenTherm-freshness wordt in Heating Curve aanvullend gecontroleerd, maar niet
  in het canonieke geselecteerde kamer-/setpointsignaal dat ook Power House en
  Cooling gebruiken;
- de API-ingress gebruikt `last_valid_ms == 0` als ontbrekend-sentinel, waardoor
  een update exact op `millis()`-rollover tijdelijk als ontbrekend geldt. De
  externe-warmtevraagpublicatie wordt bovendien pas in de 5 s-cadans geldig,
  anders dan de overige API-inputs.

Scope van één PR:

1. Een pure freshness-/holdkern met bronidentiteit, rolloverveilige tijd en
   expliciete missing/finite-contracten.
2. Een API-ingressruntime voor de zeven bestaande inputslots, met behoud van
   entity-ID's, stale-tijden en reboot-reset.
3. Een bronselectieruntime voor wateraanvoer, flow, buiten-/kamertemperatuur,
   kamer-setpoint, externe warmtevraag en heating/cooling enable.
4. Compacte YAML-binding op de bestaande updatecadans; geen centralisatie die
   de huidige 5 s/10 s- of eventtiming verandert.

Harde uitvoeringsgrenzen:

- YAML-doel voor de twee hoofdbestanden: maximaal 750 regels samen;
- totale productiecode van het werkblok groeit niet; tests en dit plan worden
  afzonderlijk gerapporteerd;
- entities, opties, standaardwaarden, stale-tijden en HA/MQTT/CIC/OpenTherm-
  contracten blijven gelijk, behalve expliciet geteste fail-closed correcties;
- hosttests dekken alle bronmatrices, cross-source hold, stale/NaN/Inf,
  bronwissels, reboot, timestamp nul en `millis()`-rollover;
- HIL dekt API-write/expiry/herstel, bronwissels tijdens actieve vraag,
  heating/cooling-enable fail-closed en reboot zonder stale replay. Niet lokaal
  injecteerbare PT1000-paden blijven aanvullend hostmatig afgedekt.

Als een compacte pure kern plus runtime deze grenzen niet haalt, stopt dit
werkblok vóór publicatie. Grote declaratieve YAML, protocolmapping of korte
bindingslambdas worden niet ter wille van de vorm naar C++ verplaatst.

## Afgerond werkblok: Boiler runtime

Doel: dispatch, outputbeslissingen en de R1/OpenTherm-lifecycle achter één
transportneutraal C++-contract brengen. YAML blijft eigenaar van entities,
configuratie, protocoltelemetrie en compacte interval-/eventbinding.

Omvang:

1. Boiler-dispatch:
   - koude-startassist, Power House, Heating Curve, fallback en commissioning;
   - bronprovenance, freshness, restvermogen en hydraulisch doelsetpoint.
2. Boiler-controller:
   - centrale fail-closed outputbeslissing, minimale aan/uit-tijden en re-arm;
   - flow-, temperatuur-, incident-, selectie- en runtime-pauzegates;
   - R1-output, roltransities, logging en incidentstatus.
3. OpenTherm-binding:
   - commandotoepassing en urgente off-frames;
   - link-, selectie- en startup-probe-lifecycle;
   - break-before-make en geen stale replay na herstel.

Afronding:

- de drie Boiler-YAML's zijn samen 646 regels kleiner: 2.020 naar 1.374 regels;
  inclusief de nieuwe runtimes en besliskernen is de productiecode netto 6
  regels kleiner; regressietests en dit plandocument tellen afzonderlijk;
- de volledige hostset (58), Python-contractset (171), webset (311),
  C++-formatcontrole en config-validatie voor Q, Waveshare en Listener
  (Single/Duo) zijn groen;
- Q Single en Q Duo compileren. Q Duo gebruikt 40 bytes minder statisch DIRAM
  en het image is 252 bytes groter; Q Single gebruikt eveneens 40 bytes minder
  DIRAM en het image is 12 bytes kleiner dan de schone basis;
- HIL is geslaagd voor CM100-dispatch, flowverlies en herstel, OpenTherm-
  linkverlies met expliciete herstart en break-before-make bij R1/OpenTherm.
  Na gemeten flowverlies waren Boiler-request, CH en `TSet` binnen 1.305 ms
  ingetrokken. De foutinjectierondes gebruikten uitsluitend lokale,
  niet-gecommitte testtimings; de productiebuild behoudt 120 s minimum-off en
  de normale commissioning-tijden;
- na HIL is schone actuele `dev` (`3f3217e1`, config hash `0xa853017d`)
  teruggezet. Controllerinstellingen zijn hersteld en beide ODU-diagnostics
  plus alle vier OpenTherm-fouttellers staan op nul.

## Vorig werkblok: Hydraulics en outputs

Doel: de stateful flowregeling, gedeelde watertemperatuurbeveiliging en
auxiliary-relaybeslissingen als één samenhangende runtimegrens naar C++
verplaatsen. YAML blijft eigenaar van entities, persistente instellingen,
intervalbinding en hardware-uitgangen.

Omvang:

1. Flow Control:
   - lokale Single/Duo-flowaggregatie en mismatch-hold;
   - AUTO-start, PI-lifecycle, normale/koel-last-good-banken en failsafe;
   - CM0/CM98/CM100, handmatig bedrijf, autotune en servicetaken.
2. Thermal Limits:
   - soft limiter en boiler-inhibithysterese;
   - trip/hard-trip, CM3-hold en rolloverveilige timing;
   - fail-closed behoud van een al actieve trip bij ontbrekende flowtemperatuur.
3. Auxiliary Relay:
   - CM- en CM1-bestemmingsbinding voor verwarmen/koelen;
   - temperatuurpoort met hysterese en ontbrekende-sensor-failsafe;
   - expliciet eigenaarschap voor externe bediening en write-on-change-output.

Gezamenlijke HIL-acceptatie:

- AUTO-flowstart, PI-regeling, handmatige iPWM, CM98 en flowverlies/herstel;
- normale en koel-flowsetpoints plus Single/Duo-pompuitvoer;
- thermische soft limit, boiler-inhibit en hard-trip/herstel voor zover de rig
  de geselecteerde aanvoertemperatuur betrouwbaar kan injecteren;
- auxiliary relay uit/verwarmen/koelen/gecombineerd, temperatuurpoort,
  ontbrekende temperatuur en externe bediening;
- na HIL schone `dev`-firmware en normale simulator-/controllerinstellingen.

Afronding:

- de drie doel-YAML's zijn samen 765 regels kleiner: 1.289 naar 524 regels;
- inclusief de nieuwe runtimes en pure besliskernen is de productiecode netto
  11 regels kleiner; 349 regels regressietests en 53 regels plandocumentatie
  tellen afzonderlijk, waardoor de volledige repositorydelta +391 regels is;
- de volledige hostset (57), Python-contractset (167), C++-formatcontrole en
  config-validatie voor Q, Waveshare en Listener (Single/Duo) zijn groen;
- Q Single en Q Duo compileren; beide gebruiken 184 bytes minder statisch DIRAM.
  Het image is respectievelijk 348 en 800 bytes kleiner dan schone `dev`;
- HIL is geslaagd voor AUTO-start/PI, Duo-mismatch met 30 s hold en herstel,
  handmatige iPWM, CM98, CM0, totale flowuitval en hervatting, plus externe en
  warmtevraaggestuurde R2-bediening met temperatuurhysterese;
- normale/koel-/manual-flowsetpointselectie, Thermal Limits-foutpaden en de
  cooling/gecombineerde auxiliary-relaymodi zijn hostmatig afgedekt. De rig kan
  de geselecteerde lokale PT1000-aanvoertemperatuur niet betrouwbaar injecteren,
  zodat soft limit, boiler-inhibit en hard-trip niet als HIL zijn afgevinkt;
- na HIL is schone `dev` (`88f34748`, config hash `0x9ddc4edd`) teruggezet. De
  controllerinstellingen zijn hersteld en beide ODU-diagnostics plus alle vier
  OpenTherm-fouttellers zijn nul gebleven.

## Vorig werkblok: Strategy runtimes

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
