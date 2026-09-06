# Power House-learning: meetbroncontract

Dit document beschrijft de bronvoorwaarden voor de passieve, replaybare leerketen. De oorspronkelijke
bronaudit gebruikt baseline `0282c2e9b938e53aab0aac7bd63901ffae58fcc5`; de actuele werkboom is bijgewerkt
naar `dev` op `b4eb58c37034970f402dac5bde652611aca8a939` (PR #633). Raw ontvangstmetadata wordt nu op
bestaande firmwarecallbacks vastgelegd. De geselecteerde-bronbinding, PSRAM-collector, journal en learnercapability zijn aangesloten op Q en
Waveshare. Er zijn geen extra Modbuspolls of regelwrites toegevoegd.

## Fail-closed snapshotcontract

`oq_ph_learning_source_logic.h` accepteert per fysiek veld:

- een waarde en expliciete validity;
- de stabiele identiteit van het exacte endpoint/veld en de betrokken unit;
- de eigen generatie van die bron;
- het monotone tijdstip waarop de fysieke meting werkelijk is ontvangen;
- een veldspecifieke maximumleeftijd en maximaal toegestane onderlinge tijdsafwijking;
- provenance `PHYSICAL_RECEIPT`, of een gecontroleerde `PHYSICAL_COMPOSITION` met alle raw receipts.
  `CONTROL_CONTRACT` is uitsluitend toegestaan voor het gecontroleerde geen-ketelvraagcontract in CM2.

`HELD`, `SYNTHESIZED`, `REPUBLISHED` en onbekende provenance worden geweigerd. De live binding bewaart
de gekozen route en bindt elke cohort aan één `context_revision`. De revision verandert alleen wanneer
de fysieke meetcontext verandert; de leerkern bewaart de gebonden meetcontext en wijzigt als enige de
modelstate. Er is geen aparte generation-owner, fingerprint of boot-token in NVS.


Een snapshot vereist actuele room-, setpoint-, outside- en flowmetingen, water-in en water-uit per
aanwezige unit, HP-mode en werkelijke compressoractiviteit, defrost-, klep- en olie-retourstatus en
positief bewijs dat de ketel geen warmte levert. Eén ontbrekend veld, een onbekende status, NaN/Inf,
staleness of te grote tijdskew maakt de hele snapshot ongeldig. Een onbekende waarde wordt nooit nul.
De caller moet ook expliciet geldige operationele bewijzen leveren voor verwarmende Control Mode, geen
actieve cap, geen service/OTA, geen setpoint-herstelperiode en comfort binnen de door de gebruiker
gekozen band. De capture draagt een monotone timestamp en dezelfde contextrevision; toekomst,
ouder dan `QualityConfig::max_interval_ms` of een revision mismatch wordt geweigerd. De adapter leidt
geen vaste comfortband af uit room en setpoint. Alle gate-validity en het comfortbewijs staan standaard
uit.

Een afgewezen raw event verdwijnt niet. Als monotone en UTC-tijd bekend zijn, retourneert de adapter een
gedateerde `LearningSnapshot` met `invalid_reasons`; `has_snapshot` betekent hier “event moet naar de
aggregate”, terwijl `measurement_valid` aangeeft of de meetwaarden bruikbaar zijn. Gebruik
`observe_source_input()` zodat een ontbrekende 10-secondenmeting het lopende segment vergiftigt. Zonder
bruikbare eventtijd reset deze helper het segment, zodat aggregatie nooit stil over het gat heen loopt.

Alleen `SINGLE` en bewezen `DUO_SERIES` worden geaccepteerd. `DUO_PARALLEL` en onbekende topologie
worden geweigerd. De installatiecontracten zijn vast: water met `cp = 4180 J/(l·K)`, en bij Duo HP1 vóór
HP2. Het contract bevat daarom alleen de gemeten onzekerheid, een gekalibreerde maximumflow en voor Duo
de maximale junction-afwijking. De gemeten flow en `T_out` van HP1 naar `T_in` van HP2 worden hiertegen
getoetst. Een wijziging van sensorroute, kalibratie of ander fysiek meetcontract maakt de revision nieuw
en wist de onverenigbare meetgeschiedenis.

Per-veld max-age en max-skew blijven cadence-specifiek. Als ontwikkelvangrail weigert deze pure laag
contractwaarden boven één uur; dit is geen aanbevolen freshnesswaarde. De live binding moet veel
striktere grenzen uit de werkelijke updatecadence afleiden.

De signed calorimetrie is:

```text
Single:      Q = flow_lph / 3600 * 4180 * (T_out,1 - T_in,1)
Duo series: Q = flow_lph / 3600 * 4180 * ((T_out,1 - T_in,1) + (T_out,2 - T_in,2))
```

Compressor-uitperioden blijven meetellen: de adapter rekent met het gemeten signed temperatuurverschil
en vervangt dit niet door nul. Exact nuldebiet levert nulwarmte uit de fysieke flowmeting. Watermetingen
blijven ook dan verplicht, omdat `LearningSnapshot` een actuele gemiddelde watertemperatuur nodig heeft
voor de opslagcontrole.

## Bronkaart van de baseline

De geselecteerde sensoren zijn geen veilige ontvangstreeksbron. Water en flow worden iedere 5 seconden
opnieuw gepubliceerd; outside, room en setpoint iedere 10 seconden
([`oq_sensor_sources.yaml`](../openquatt/oq_sensor_sources.yaml), regels 215-284). De C++-resolvers kennen
route, hold en synthesized-zero intern, maar geven aan hun callers alleen een `float` terug
([`oq_sensor_source_runtime.h`](../openquatt/includes/control/oq_sensor_source_runtime.h), regels 194-261;
[`oq_input_source_logic.h`](../openquatt/includes/control/oq_input_source_logic.h), regels 135-172 en
219-258). Een callback op een `*_selected`-sensor zou daardoor republish-tijd als fysieke ontvangsttijd
presenteren.

| Grootheid | Mogelijke fysieke route | Bewijsbare ontvangstmetadata op baseline | Blokkade voor live collector |
|---|---|---|---|
| Outside | HP1/HP2, HA, API of MQTT; `Auto` kan per sample wisselen | HP R2110 zet een lokale monotone ontvangsttijd | De gekozen route en generatie verlaten de resolver niet; HA heeft geen ontvangsttijd |
| Room / setpoint | Q: OT, CIC, HA, API of MQTT; Waveshare: CIC, HA, API of MQTT | OT, API en MQTT registreren lokale acceptatie; CIC alleen succes van de gehele HTTP-response | OT-tijd is niet publiek; API/MQTT bewijzen geen upstream fysieke meettijd; CIC heeft geen per-veld presence/tijd; HA heeft geen age |
| Flow | CIC, Q-pulsmeter, HP1, HP2, Duo-aggregate of synthesized zero | Geen veldspecifieke timestamp voor Q-pulsen of HP R2138 | Aggregate is geen bronidentiteit; synthesized zero is verboden; metergrens en kalibratie zijn niet bewezen |
| Water per HP | HP R2133/R2134; selected supply kan Local, CIC of HA met fallback zijn | Alleen raw water-out R2134 heeft een eigen timestamp | Water-in mist een timestamp; gekalibreerde en selected sensors zijn republishes; fallback/hold ontbreekt in de broncode-identiteit |
| HP-mode / protection | R2099, R2108, R2118 en afleiding uit R2119 | Alleen generieke unitactiviteit, geen veldspecifieke timestamps | Freshness van mode, klep, defrost en olie-retour is niet bewijsbaar; NaN van de olie-retourbasis kan als `false` eindigen |
| Boiler | R1-relais of OpenTherm-boilerstatus | OpenTherm bewaart intern per veld een geldige READ_ACK-tijd | R1 is aangestuurde relaisstate en geen fysiek vlam-/warmtebewijs; OpenTherm-timestamp is niet publiek |

Onderliggende codeplaatsen:

- bronselecties en fresh-install defaults: [`oq_sensor_sources.yaml`](../openquatt/oq_sensor_sources.yaml),
  regels 40-83; [`oq_sensor_source_selects_opentherm.yaml`](../openquatt/oq_sensor_source_selects_opentherm.yaml),
  regels 14-43; [`oq_sensor_source_selects_no_opentherm.yaml`](../openquatt/oq_sensor_source_selects_no_opentherm.yaml),
  regels 13-40. Alle selects gebruiken restore-state, dus deze defaults bewijzen niet de live route;
- raw HP-metingen: [`oq_HP_io.yaml`](../openquatt/oq_HP_io.yaml), regels 664-678, 832-857, 1031-1103,
  1154-1172, 1253-1273 en 1406-1434;
- CIC per-response versus per-veld: [`OpenQuattCIC.cpp`](../components/openquatt_cic/OpenQuattCIC.cpp),
  regels 409-455 en 473-487;
- HA-values en losse externe validity-bits: [`oq_ha_inputs.yaml`](../openquatt/oq_ha_inputs.yaml),
  regels 20-63 en 105-127;
- API-acceptatietijd: [`oq_api_ingress.yaml`](../openquatt/oq_api_ingress.yaml), regels 28-59;
- MQTT-acceptatie en retained setpoint: [`OpenQuattMqttConfig.cpp`](../components/openquatt_mqtt_config/OpenQuattMqttConfig.cpp),
  regels 1727-1732, 1997-2020 en 2164-2189;
- OT room/setpoint: [`OpenQuattOTSlave.cpp`](../components/openquatt_ot_slave/OpenQuattOTSlave.cpp),
  regels 158-171 en 341-379;
- R1 en OpenTherm-boilerbewijs: [`oq_boiler_runtime.h`](../openquatt/includes/control/oq_boiler_runtime.h),
  regels 318-326; [`oq_otb_telemetry.h`](../openquatt/includes/boiler/oq_otb_telemetry.h), regels 109-180.

## Toegevoegd na de bronaudit

De bestaande HP-polling legt nu afzonderlijke receipts vast voor R2099, R2108, R2118, R2119,
R2103, R2133, R2134, R2138 en R2110. R2103 bewijst gemeten compressorfrequentie; de gevraagde frequentie R2102 is geen activiteitsmeting. Temperatuurreceipts worden vóór de bestaande out-of-range-clamp
bijgewerkt: een afgewezen nieuw frame mag een oude geldige waarde niet laten staan. R2118 wordt
bij iedere geparste response vastgelegd, vóór deduplicatie van de binary sensor. De Q-pulsmeter
bewaart het omgerekende debiet in L/h vóór throttling; NaN en niet-eindige invoer trekken de
receiptvalidity in. Bestaande sensoroutput en regelgedrag blijven behouden.

Nieuwe receipt-tijden gebruiken de 64-bits ESP32-monotone klok. Daarmee kan een ontvangst van vóór
de 49,7-dagenomloop van `millis()` niet opnieuw actueel lijken. De bestaande 32-bits controltimers
blijven apart. HP-offline trekt alle nieuwe receiptvalidity in; een synthetische offline-NaN wordt
niet als nieuwe fysieke ontvangst getimestamped. De callback- en leespaden blijven in de main loop;
een toekomstige collector vanuit een andere taak moet synchronisatie toevoegen.

OT room/setpoint hebben publieke value/receipt-getters. Stop en ongeldige frames trekken de nieuwe
metadata in zonder de bestaande control-freshness-timestamps te vernieuwen. Boiler-READ_ACK-metadata
bevat de werkelijk ontvangen payload en vereiste requestcorrelatie; een compatibele aanroep zonder
payload mag nooit `0` als waargenomen geen-warmte-status aanbieden. De live binding gebruikt het actuele CM2-controllercontract. Bij geselecteerde OpenTherm kan een verse fysieke CH/flame-melding een sample vetoën; ontbrekende OT-telemetrie is geen extra CM2-voorwaarde.

Een gewijzigde meetcontext wist het lopende bewijs en start beide modellen opnieuw. Een wijziging van
de beoordelings- of regelinstellingen onderbreekt alleen het lopende segment en de fit; de fysieke
meetrecords blijven behouden en worden opnieuw beoordeeld. Instellingevents pauzeren direct, zodat ook
A→B→A tussen twee learnerticks wordt verwerkt. Alle mutaties blijven in de mainloop; HTTP leest
uitsluitend gesynchroniseerde caches. Gewone firmwarebuilds behouden compatibele historie;
opslagherstel vergelijkt schema, algoritmeversie en meetcontext.

Voor de aanvullende 1R1C-route bestaat `SnapshotPurpose::THERMAL_DYNAMIC`. Die route laat
kamerrespons tijdens herstel of comfortafwijkingen toe, met alle fysieke meet-, ketel-, protection-,
mode-, limiet- en servicevoorwaarden intact. Gebruik de raw adapter met dit doel; verwijder nooit
achteraf invaliditeitsbits uit een batch-snapshot die zijn meetwaarden al kwijt is.

## Live binding en resterend meetbewijs

`oq_sensor_source_runtime.h` bewaart de werkelijk gekozen route bij iedere resolver-call.
Een bronwissel A→B→A krijgt ook tussen twee learnerticks een nieuwe configuratiegeneratie.
Een fysiek aggregaat bewaart beide unitidentiteiten, ontvangsttijden en de operator (mean/min/max).
Hold en synthesized zero blijven ongeschikt. CIC bewaart presence en receipt per veld; een ontbrekend
veld trekt de oude validity in. HA/API/MQTT zonder aantoonbare upstream meetleeftijd blijven geblokkeerd.

De huidige cadencegrenzen zijn voor HP-velden maximaal 45 seconden oud en 30 seconden onderlinge skew.
Room/setpoint-receipts mogen maximaal 120 seconden oud zijn; de onderlinge fysieke skew blijft begrensd.
Een instelling, source-route, kalibratie of contextwijziging onderbreekt het bewijs en de lopende fit.
Na reboot start opt-in uit; UTC en exact bekende context zijn nodig vóór journalherstel.

De learner vraagt geen hydraulisch installatieprofiel. Het Q Duo-compileprofiel bepaalt de bekende
HP1 → HP2-meetgrens; gebruikers selecteren daarvoor geen vloeistof, topologie of extra warmtebron.
De learner gebruikt het bestaande CM2-ketelcontract; een R1-commando wordt niet als fysieke
vlammeting behandeld. Flowkalibratie, calorimetrische onzekerheid en de grens aan zon/interne warmte
moeten nog onderbouwd worden. Zolang dat ontbreekt blijven training of modelkwaliteit geblokkeerd en
worden onbekende waarden als onbekend geëxporteerd.

## Vast installatiecontract

De live firmware gebruikt altijd `WATER_CP_4180`. Single/Duo volgt uitsluitend uit het compileprofiel;
Duo gebruikt `HP1_TO_HP2` als vaste meetgrens. Dit is een implementatiegrens, geen installatiewizard
of opgeslagen gebruikerskeuze. De pure bronadapter behoudt zijn negatieve tests voor onbekende,
parallelle en afwijkende meetcontracten. De meetgrens verleent geen calorimetrisch meetbewijs;
onzekerheid, kalibratie en alle operationele gates blijven verplicht.
