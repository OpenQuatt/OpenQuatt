# HIL: Duo-overname zonder onnodige bijstart, 17 oktober 2026

Gerichte vervolgvalidatie van [PR #852](https://github.com/OpenQuatt/OpenQuatt/pull/852).
De [OTA-proeven van 16 oktober](hil-ota-continuity-result-2026-10-16.md)
blijven afzonderlijke historische resultaten. Deze run gebruikt de waterketenregeling
uit `dev` op `1601f6b5` en de nieuwe Duo-correctie. Alle tijden zijn UTC.

## Probleem en correctie

Een echte opstartopname liet zien dat HP2 bleef draaien, terwijl Power House
HP1 aanvroeg en HP2 uit vroeg. De bestaande bescherming hield HP2 toch actief;
de nieuw gestarte HP1 bleef vervolgens door zijn minimale draaitijd ongeveer
vijf minuten onnodig meedraaien.

Power House neemt nu de uitgelezen lopende cyclus al vóór software-adoptie mee
als bestaande kandidaat. Zolang overname nog loopt, worden geen stilstaande
units gestart. Daarna worden de bestaande boot-hold en minimale draaitijd in
de kandidaatselectie meegenomen. Een boot-hold fixeert ook in de voorspelling
de werkelijk toegepaste stand; minimale draaitijd laat positieve modulatie vrij.
Na adoptie wordt de bestaande PH-cadence ververst. Extra capaciteitsvraag en
veiligheidsstops blijven mogelijk. Geen nieuwe timer, NVS-record of blijvende state.

## Opstelling en images

- Alleen het desktoplab: Q Duo `openquatt-test`, beide ODU-profielen V1.5,
  ESPHome 2026.9.0. Testconfig includeert `configs/heatpump_controller_q/duo_hil.yaml`
  met bestaande stack-/bootdiagnoses en een eigen revisiemarker.
- Simulator v0.6.0, contract `openquatt-modbus-opentherm-v2`. Vóór de proef
  geflasht met het watchdog-image; `wd=` live bevestigd. Watchdog 10 ms per
  telling, grens 12.000; ook reads blijven hem voeden.
- Regeling Power House, kamertemperatuur/setpoint/buiten/toestemming/externe
  vraag expliciet via API gezet. Waterbron Local met HP2-terugval; flowbron
  Outdoor unit / Flowmeter HP2, circa 800 L/h. Minimale draaitijd 300 s,
  minimale uit-tijd 240 s. Doorverwarmen aan, stopmarge 0,4 °C, herstartmarge 0,2 °C.
- Beginvraag bouwt uitsluitend HP2 op via een tijdelijke frequentie-uitsluiting
  van HP1. Die uitsluiting is vóór de overnameproeven volledig verwijderd.
  Tijdens de continuïteitsproeven: kamer 23,1 °C, setpoint 23 °C en externe vraag 0 W.
- Bron-HEAD `5d26ad3f` plus functionele patch
  SHA256 `f9f1b2645c34b30d5b6c279249026c5190ccf2f8a52a0722442a4ad83749146a`.

| Image | OTA-bytes | SHA256 |
| --- | ---: | --- |
| A1, bron van native OTA | 2485600 | `32c81b207cc707a702b66b2d67f5b19f45ea2477281e8d7c49829e780af61b24` |
| B2, definitieve kandidaat | 2485600 | `9020bbec6f82d0677f0f7183b5b0d3ee66b0338bf1b3e68b2266cbffb5b3c0cb` |
| ODU-watchdogsimulator | 961776 | `12be9c736f22fe254c90ef28eaf0f43085ca3a067d2a937709bbd0bef719b560` |

A1 bevat dezelfde functionele correctie, maar gebruikte nog `int fixed_level`.
B2 gebruikt `uint8_t` in bestaande padding. De oorspronkelijke geheugenassert
blijft behouden: op de 64-bits host blijven `HpInput` 264 B en `DispatchInput` 560 B. De gewone herstart,
extra-capaciteitsproef en veiligheidsstop zijn rechtstreeks op B2 uitgevoerd.

## Resultaten

| Scenario | Waarneming | Oordeel |
| --- | --- | --- |
| Native OTA A1 → B2, doorverwarmen boven setpoint | Upload 13.68 s, totaal 14.33 s. 141 samples tot +450.28 s: HP1 0 Hz, HP2 30 Hz in alle samples; ook vraag HP1 0 Hz en cumulatieve `maxF=0`. HP2 is als bestaande F1-cyclus overgenomen. | PASS |
| Na boot-hold en opnieuw begonnen minimale draaitijd | OTA-venster eindigt 353.90 s na bevestigde adoptie, dus voorbij 180 s en 300 s. Basisvraag 0 W, doorverwarmvloer circa 2162 W, status `extending`; HP1 blijft uit. Hoogste watchdogstand 2190/12000. | PASS |
| Geplande softwareherstart B2 | 45 samples tot +149.89 s: HP1 0 Hz, HP2 30 Hz; opnieuw adoptie en `extending` boven setpoint. Hoogste watchdogstand 925/12000. | PASS |
| Echte extra capaciteitsvraag tijdens boot-hold | Kamer 19 °C, externe vraag 10 kW, rise time 2 min. HP1 start na 41.05 s, op 119.60 s na HP2-adoptie, binnen de hold van 180 s. HP2 blijft op werkelijk toegepaste F1 / 30 Hz. | PASS |
| Thermische stop tijdens bescherming/minimale draaitijd | Maximum water temperature 40 °C, beide gesimuleerde water-inwaarden naar 60 °C. Hard-trip daadwerkelijk gemeten; beide compressorvragen en frequenties nul na 24.88 s. | PASS |

De simulator is tijdens deze meetvensters niet herstart; `maxF` is de cumulatief
hoogste ontvangen R1999-vraag. Dit dekt voor HP1 ook korte positieve opdrachten
tussen twee samples. Frequenties zijn gepolld met ongeveer drie seconden tussen
samples; een niet-bemonsterde onderbreking van HP2 is daarmee niet absoluut uitgesloten.
Tijdens native OTA en het terugkomen van HTTP ontbreken delen van de controllertelemetrie;
de simulatorfrequenties en watchdog zijn onafhankelijk doorlopend bemonsterd.
Per scenario is één begrensde controllerlogstream gebruikt en daarna gesloten.

## Offline checks en review

- `bash scripts/run_host_regression_tests.sh`: 127 binaries PASS.
- `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`: 450 tests PASS,
  2 overgeslagen; lokale macOS SDK C++-headers ingesteld.
- `npm run check:cpp-format`, `npm run check:docs`, `git diff --check`: PASS.
- Q Duo-configvalidatie en geïnstrumenteerde A1/B2-compilaties PASS.
  De bestaande productie-NVS-budgetcheck blijft FAIL: 571/630 geschatte entries,
  59 beschikbaar bij 100 vereist. De Duo-correctie vergroot dat NVS-record niet.
- Nieuwe tests reproduceren beide eigenaarrichtingen, pending vóór adoptie,
  ontbrekende readback, abort/deadline, must_stop, hold-expiratie en werkelijk
  uitvoerbare extra capaciteit. De runtime-test voert productie-dispatch,
  Policy-adoptie en `apply_level_` uit; bestaande safety-asserts zijn behouden.
- Volledige featurediff en de finale correctie onafhankelijk koud beoordeeld.
  De gevonden adoptierace en fictieve positieve stand tijdens boot-hold zijn
  gecorrigeerd en met uitvoerbare regressies plus HIL afgedekt.

## Eindstatus en grenzen

Definitieve labfirmware B2. Na de thermische stop is API heating-enable uit;
beide compressoren zijn stil. Rise time 2 min, waterlimiet 40 °C en simulator
water-in 60 °C blijven als teststaat staan. Alle foutinjecties zijn uit;
gewone bronkeuzes/waarden worden overeenkomstig de labprocedure niet teruggezet.
Ruwe JSONL, begrensde SSE-captures, upload-/buildlogs en hashes blijven lokaal in
`.tmp/hil/2026-10-17-duo-resume/` van de primaire checkout.

Dit is functionele HIL-validatie van de nieuwe Duo-correctie. De bestaande P1
voor volledige geheugenkwalificatie blijft open: geen fysieke koude power-cycle,
identieke baseline/kandidaatmatrix of maximale HA/API/MQTT/TLS/OTA-belasting.
Web-/download-OTA en defrost/oil-return-interleavings zijn in deze run niet fysiek
herhaald; zie het eerdere verslag voor de afzonderlijke web-OTA-proeven.
De 120 s ODU-watchdog komt uit statische analyse van één firmwarebackup;
fysieke stoptijd en andere ODU-firmwareversies zijn hiermee niet bewezen.
