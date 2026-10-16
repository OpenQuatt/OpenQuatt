# HIL-resultaat: verwarmingscyclus na geplande herstart, 16 oktober 2026

> Historische meting van firmware `2eba4115-memory4` (commit `6335f7a0`).
> Deze firmware gebruikte nog een RTC-marker van 60 B en ondersteunde uitsluitend
> geplande herstarts met hetzelfde image. De latere NVS-overdracht van 76 B en
> OTA-continuïteit zijn hiermee niet gevalideerd; zie het [OTA-vervolgrapport](hil-ota-continuity-result-2026-10-16.md).
> Het toenmalige NVS-budget was 570/630; de uitbreiding gebruikt 571/630 geschatte
> entries (59 beschikbaar tegenover 100 vereist).

Gerichte test voor [PR #852](https://github.com/OpenQuatt/OpenQuatt/pull/852),
op de aparte `openquatt-test`-controller en de gecombineerde desktopsimulator.
Geen productieapparatuur. Tijden hieronder zijn UTC.

## Firmware en opstelling

- Baseline: `9c5688c1dd95fdfc1f6e16bfb707b9bef383f75e` (`dev`).
- Kandidaat: PR-head `2eba4115dc069afcf4ff981ce5451c8568fd6f97` plus de
  in deze PR opgenomen HIL-correcties voor live flowreceipts, switch-state,
  opstartvrijgave, Power House-geheugen en de klokvolgorde van de eerste hold.
- Geteste overlay: `configs/heatpump_controller_q/duo_hil.yaml`, met vijf
  taak-stackmetingen, debugmeting iedere 5 s en twee tijdelijke bootdiagnoses.
  De tijdelijke diagnostiek wordt niet in productie opgenomen.
- Teruggelezen `HIL Revision`: `2eba4115-memory4`; config-hash uit de build `0xa7657315`;
  buildtijd `2026-10-16 09:31:54 +0200`; ESPHome `2026.9.0`.
- OTA-bestand: 2.483.120 bytes, SHA256
  `755084f91dcade6398a0bf5ccde1690cdadfd8cd8714aef86a4f4865a88936bb`.
- Simulator `v0.6.0`, contract `openquatt-modbus-opentherm-v2`, inclusief
  communicatie-watchdog uit simulator-PR #10; HP1 V2 old model, HP2 V2 new model.
- Automatische Power House-regeling, API-inputs voor kamer, setpoint, externe
  vraag en toestemming; ODU-flowmeter, circa 650 l/h; minimumflow 400 l/h.
  Water-in 30 °C, buiten 2 °C. Geen fysieke PT1000-test.
- Globale lablock en voorafsnapshot; writes via de bestaande REST-client en
  RequestGate. Bounded SSE-logcapture als aanvullende diagnose.
- Tegelijk actief: MQTT met acht niet-retained QoS1-inputs iedere 2 s,
  HTTP-telemetrie/API-inputs, web/SSE, Modbus en gesimuleerde OpenTherm-peers.
  Geen HA/native-API-client als gecontroleerde belasting ingezet.

## Gedrag

| Test | Waarneming | Resultaat |
|---|---|---|
| Baseline, geplande herstart | Beide ODUs vooraf 30/36 Hz; beide op 0 Hz bij sample +25,2 s, door stopwrites van OpenQuatt terwijl reads doorgaan. | Continuïteit ontbreekt in baseline |
| Duo, normale geplande herstart | 07:38:21 herstart; toestemming +14,6 s, comfortinputs +66 s. Beide bestaande cycli overgenomen; alle 41 samples tot +200,2 s blijven 55/48 Hz, eindstate CM2. | PASS |
| Verse invoer boven setpoint, externe vraag 0 | 07:42:02 herstart; verse kamer 23,05 °C bij setpoint 23 °C en externe vraag 0. RTC-geheugen hersteld, eerste berekening met positieve vraag; daarna normale afbouw tot basisvraag 0. Geen volledige compressorstop in 55 samples tot +270,2 s. | PASS |
| Overgang naar langer doorverwarmen | In dezelfde run op +200 s ingeschakeld; fase `extending`, basis 0 W en vloer circa 2,6–2,8 kW. Daarna draait HP1 verder op 30 Hz en schakelt HP2 uit. | PASS |
| Herstart vanuit langer doorverwarmen | 07:47:04 herstart met HP1 30 Hz, HP2 uit, basisvraag 0. Na verse inputs keert `extending` terug. Alle 67 samples tot +330,5 s houden HP1 op 30 Hz; twaalf extra samples tot +459 s bevestigen doorverwarmen na de 180 s-hold en de 300 s-minimumruntime vanaf overname. HP2 blijft uit. | PASS |
| Ontbrekende comfortinputs, vaste 120 s-deadline | 07:55:23 herstart; log meldt timeout na 120 s, eerste fysieke 0 Hz op +130,3 s. Late comfortinputs op +146,3 s herstellen het geheugen niet. Tot +410,2 s blijven beide units uit, ook nadat technische beschikbaarheid en de 240 s-minimum-off terug zijn. Hoogste HP1-watchdogteller 796, ruim onder 12.000. | PASS |

Tijdens de acquisitie van de geslaagde Duo-runs lopen de Modbus-readtellers
op terwijl de write-tellers gelijk blijven. De gemeten watchdogtellers blijven
ruim onder 12.000; stoppen op de eigen acquisitiedeadline mag dus niet worden
verward met aflopen van de ODU-watchdog. De sampler leest iedere circa 5 s;
readbacks en logs ondersteunen de conclusie, er is geen oscilloscoopmeting.

De testcase boven setpoint levert de nulvraag opnieuw aan na de herstart.
Voor de herstart stond de externe vraag nog op 6.000 W. Dit is bewijs voor
geheugenovername en afbouw op verse nulvraag, niet voor een langdurige fysieke
verwarmingscyclus die uitsluitend op comfortfeedback draaide. De hostregressie
combineert het RTC-roundtrip met de echte vraagberekening: leeg geheugen geeft
0 W, hersteld geheugen behoudt een positieve vraag; actuele waterbegrenzing
blijft leidend.

## Vastgelegde regressies

De HIL-test bracht fouten aan het licht die door losse policytests niet werden
gevangen. De uiteindelijke code controleert de fysieke bronreceipts van flow,
gebruikt de echte ESPHome-switchstate, verzamelt veiligheidsdata binnen 45 s
en wacht vervolgens op de bestaande verbindingsvrijgave van minimaal 60 s.
Overname blijft aan volledige technische beschikbaarheid gebonden.

Bij de eerste actuatorberekening na overname kon een oudere ticktijd één of
meer milliseconden vóór het adoptietijdstip liggen. Unsigned tijdrekening liet
de nieuwe beschermingshold dan onmiddellijk verlopen. De hold gebruikt nu de
actuele klok. De regressie voert de echte `apply_level_` uit voor beide HPs,
met adoptie op T+1, een nulrequest, klokomloop en een gelijktijdige low-flow-stop.
De oude klokquery faalt deze regressie; de correctie slaagt.

Power House neemt uitsluitend drie begrensde getallen eenmalig mee in RTC:
laatste berekende vraag, comfortgeheugen en het laatst beoordeelde setpoint.
Herstel vereist dezelfde bevestigde firmware, een gecontroleerde draaiende
cyclus en verse geldige invoer met hetzelfde setpoint. Geen oude Control Mode,
geen nieuwe NVS-sleutels en geen herstel bij koude start, crash of eerste OTA-boot.

## Geheugen en grenzen

Een vroege baseline/kandidaatvergelijking gebruikte dezelfde vijf
stackmetingen, MQTT-, HTTP/SSE-, Modbus- en OpenTherm-belasting. Kort gemeten
vensters, zonder koude power-cycle of echte HA-client:

| Minimum binnen venster | Baseline | Eerste kandidaat |
|---|---:|---:|
| Actuele interne heap | 69.423 B | 71.123 B |
| Minimum-sinds-boot | 39.828 B | 39.828 B |
| Grootste intern vrij block | 31.744 B | 31.744 B |
| Vrije PSRAM | 5.317.400 B | 5.317.144 B |
| Main-loop stack | 4.038 B | 4.038 B |
| MQTT-worker stack | 21.988 B | 21.984 B |
| MQTT-transport stack | 10.064 B | 10.068 B |
| TCPIP-stack | 1.716 B | 1.804 B |
| WiFi-stack | 2.752 B | 2.728 B |

Maximum gerapporteerde fragmentatie in die vensters: 55,63% versus 55,89%.
De uiteindelijke kandidaat voegt twee diagnostische sensors toe en is daarmee
geen identieke meetbuild. De per-HP-policy staat in de bestaande PSRAM-arena;
de RTC-marker is 60 bytes. De geïnstrumenteerde eindbuild gebruikt 218.095 B
statisch RAM. Deze cijfers bewijzen geen marge voor de zwaarste gelijktijdige
TLS-/native-API-/OTA-allocatie.

Over alle vier scenario's met de eindbuild was de kleinste actuele interne
heap 53.775 B; minimum-sinds-boot 39.828 B, grootste block 31.744 B, PSRAM
5.287.044 B en maximale fragmentatie 57,60%. Stackminima: main loop 3.990 B,
MQTT-worker 21.984 B, MQTT-transport 9.984 B, TCPIP 1.788 B, WiFi 2.656 B.
De eenmalige heapdip is gemeten tijdens CM98 in de deadline-run; de
allocatie-overlap is niet geïsoleerd. Deze langere herstartscenario's vormen
geen identiek baselinevenster. De dip en worst-case marge moeten dus nog
worden gekwalificeerd voordat geheugenveiligheid wordt vrijgegeven.

De P1-review voor volledige geheugenkwalificatie blijft open. Niet uitgevoerd:
fysieke koude power-cycle, echte HA/native-API-belasting, de volledige gelijke
baseline/eindkandidaatmatrix, fysieke ODU-timing, nieuwe defrost/oil-return-
interleavings en Modbus-antwoordverlies op deze eindbuild. De circa 120 s
ODU-timeout is een simulatormodel uit statische analyse van één firmwarebackup.
OTA is gebruikt om testfirmware te laden; doorverwarmen tijdens OTA wordt niet
geïmplementeerd of bewezen door deze PR.

## Controles en bewijs

- `bash scripts/run_host_regression_tests.sh`: 124 binaries geslaagd.
- `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`: 443 tests
  geslaagd; lokaal met de macOS SDK C++-includepaden beschikbaar.
- `npm run check:cpp-format`, `npm run check:docs` en `git diff --check` geslaagd.
- Geïnstrumenteerde `duo_hil.yaml`-build en OTA geslaagd.
- Configvalidatie is geldig, maar de bestaande NVS-budgetcontrole faalt:
  570/630 geschatte entries, 60 beschikbaar tegenover 100 vereist. Deze PR
  voegt geen opgeslagen sleutels toe.
- Complete diff tegen de baseline primair en onafhankelijk koud beoordeeld,
  inclusief boot/reset, queue/dispatch, storagefouten, HP-isolatie en klokgrenzen.

Lokale meetbestanden staan in `.tmp/hil/2026-10-16-warm-restart/`:
`baseline-restart`, `candidate-memory4-normal`,
`candidate-memory4-above-setpoint`, `candidate-memory4-extension` en
`candidate-memory4-deadline`. Ruwe snapshot, logs, firmware en tijdelijke
helpers blijven buiten Git; gedeelde rapportage bevat geen tokens of sleutels.

## Eindstate

Beide ODUs op 0 Hz en Standby; basisvraag en comfortgeheugen 0, run extension
`inactive`. De controller staat op Auto/Power House en CM98 (normale
vorstcirculatie bij buiten 2 °C), kamer 23,05 °C en setpoint 23 °C, externe
vraag 0, verwarmtoestemming aan. ODU-pompaansturing bij vorstcirculatie is 800;
water-in 30 °C en externe systeemflow blijven als gewone simulatorinstellingen
staan. Alle timeout/UART/no-flow/defrost/manual/frequency-freeze-injecties uit,
Drop every Nth ODU request 0 en alle ingestelde foutwoorden 0. De tijdelijke
MQTT-koppeling is uitgeschakeld en teruggelezen; publisher en broker gestopt.
Logcaptures afgesloten en globale lablock vrijgegeven. Gewone testinstellingen
en de HIL-firmware zijn niet hersteld volgens de HIL-werkwijze.
