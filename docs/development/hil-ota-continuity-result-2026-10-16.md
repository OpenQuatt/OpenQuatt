# HIL-vervolg: OTA en doorverwarmen, 16 oktober 2026

Gerichte vervolgtest voor [PR #852](https://github.com/OpenQuatt/OpenQuatt/pull/852)
op `openquatt-test` en de gecombineerde desktopsimulator. Tijden zijn UTC.
De [eerdere herstartmeting](hil-warm-restart-result-2026-10-16.md) betreft
`2eba4115-memory4`; die bewijst de latere OTA-uitbreiding niet.

## Verandering en veiligheidsgrenzen

- Eén bestaand NVS-overdrachtsrecord, versie 2, 76 B, op dezelfde sleutel
  `openquatt/oq_restart_v1`. Geen nieuwe sleutels, periodieke schrijfacties,
  CM-snapshot, worker-task of dynamische buffer.
- Per ODU een hervattingsbit; Power House neemt alleen de laatste basisvraag,
  het begrensde comfortgeheugen en het beoordeelde setpoint mee. Doorverwarmen
  bepaalt zijn fase opnieuw uit de bevestigde cyclus en actuele comfortgrens.
- Alleen een correct afgeronde OTA korter dan 45 s mag een draaiende cyclus
  overdragen. Exacte targetpartitie, imagehash, hardware, topologie en minimale
  uit-tijd moeten passen. Het record wordt duurzaam gewist vóór beschikbaarstelling.
- De nieuwe firmware wacht op normale imagebevestiging (`ESP_OTA_IMG_VALID`),
  verse readback, veiligheidsdata binnen 45 s, bestaande linkvrijgave en geldige
  inputs binnen totaal 120 s. Die bootdeadlines zijn onafhankelijk van de transfer.
- Tijdens web-OTA blijft de hoofdloop lopen; beschikbaarheid van verse reads
  blijft vereist. Een live guard kan de hold
  annuleren. Native en HTTP-download-OTA blokkeren de hoofdloop. De 45 s-grens
  begrenst daar uitsluitend overname, niet de duur van de upload. Een trage upload
  wordt niet afgebroken; de ODU kan uiteindelijk zijn eigen watchdog bereiken.
- De ODU-watchdog is volgens één statisch onderzochte backup nominaal 120 s
  (12.000 × 10 ms). Ook correcte Modbus-reads voeden hem. Dit is in de simulator
  gemodelleerd; fysieke stoptijd en andere ODU-firmware zijn niet bevestigd.
- Een uitsluitend blokkerend RTC-woord van 4 B onthoudt een mislukte consume.
  Daardoor kan ook een latere software-reset via recovery een koude/crash-boot
  niet tot geplande hervatting maken. Het woord geeft nooit toestemming.
  Opslagretries gebruiken één oorspronkelijke bootcontext; geen automatische reboot.
- OTA START/ERROR/ABORT wissen achtergebleven overdracht. Terminale fouten zijn
  aan de actieve transporttransactie gekoppeld. Bij wisfalen wordt de huidige
  bootpartitie teruggekozen en geverifieerd. Als zowel NVS als bootselectie falen,
  is duurzame uitsluiting van een oud record niet bewezen; verse bootgates blijven gelden.

## Opstelling en images

Q Duo HIL, ESPHome 2026.9.0; vijf stackmetingen, debug iedere 5 s en twee
bootdiagnoses. Simulator v0.6.0, contract `openquatt-modbus-opentherm-v2`,
HP1 V2 old model, HP2 V2 new model. Alle apparaten zijn gesimuleerde peers;
geen productieapparatuur. Regeling Auto/Power House, kamer/setpoint/toestemming
via API, water lokaal met ODU-terugval, minimumflow 400 L/h, minimumdraaitijd
300 s, minimale uit-tijd 240 s, doorverwarmen aan met stopmarge 0,7 °C.

| Image | Config-hash | OTA-bytes | SHA256 |
| --- | --- | ---: | --- |
| `ota-continuity-a4` | `0x11e7e5c6` | 2484352 | `4ffcfbe2c9a907371aaa3ebd28be2033ddb801e2f0eeb312f38bc3b235259626` |
| `ota-continuity-b4` | `0x01abe86b` | 2484352 | `2df7c4e72691afec6861b67579aac60a530780bd03c1f5d63a9f65fdb3ab6b68` |

De images verschillen alleen in hun HIL-revisie/buildmetadata. Daardoor is een
andere imagehash/partitie getest zonder de functionele configuratie te wijzigen.
Statisch RAM 218.135 B; de eerdere memory4-build gebruikte 218.095 B.
De vervolgimages A5/B5 voegen uitsluitend een scheduler-tick na iedere geslaagde
web-OTA-write toe, in plaats van alleen de bestaande yield per >16 KiB.
Dit laat HTTPD eerder tijd aan de control-loop; deadlines en ownership blijven gelijk.
De HIL gebruikt 1 ms per tick. Deze kleine transportdelta is onafhankelijk koud
beoordeeld en door de bestaande upload-/abort-/sessiehosttests gecontroleerd.

| Vervolgimage | Config-hash | OTA-bytes | SHA256 |
| --- | --- | ---: | --- |
| `ota-continuity-a5` | `0x4064ad07` | 2484352 | `3475c6711f028ea78b96d2f446300eeb612e1b0d4ee56f7b001d5edbca066dd0` |
| `ota-continuity-b5` | `0x35f8c64a` | 2484352 | `b5400d15915be7021182d4614aec528680c96ddb8de762675718e5cf373babe0` |

## Bevindingen en uitgevoerde scenario's

| Scenario | Waarneembaar resultaat | Oordeel |
| --- | --- | --- |
| Native OTA met oude memory4-build | Transfer 14,04 s; HP1 voor het eerst 0 Hz op +32,46 s, HP2 op +37,46 s. ODU-watchdogs bereikten geen timeout. | Probleem bevestigd |
| Eerste OTA-doorverwarmen, alleen HP2 actief | 23,1 °C bij setpoint 23 °C; basisvraag 0 W, 30 Hz. Overdrachtsbit 2 kwam aan, maar HP1 initialiseerde pomp R2015=1000. Geselecteerde flow viel van 650 naar 0 L/h; HP2 stopte terecht. | FAIL; gecorrigeerd |
| Gewone herstart met NVS-overdracht | A4; 37 samples tot +180,22 s: HP1 continu op 30 Hz, HP2 continu uit; opnieuw extending met basisvraag 0 W en voldoende HP2-flow. | PASS |
| Afgebroken webupload | Begrensde upload met curl-timeout 4 s; OTA_ABORT bevestigd, dezelfde A4-boot blijft actief met compressor 30 Hz en voldoende flow. Vastgelegde overnamegegevens verworpen; de volgende volledige upload is apart getest. | PASS |
| Web-OTA met Power House-geheugen, run extension uit | A4 → B4, verse kamer 23,1 °C/setpoint 23 °C/externe vraag 0 W. Tijdens flashen verouderden veiligheidsmetingen op de broncontroller; de overname van de lopende cyclus werd niet toegekend en beide ODU’s stopten. Geen watchdog-timeout. | Veilige terugval; continuïteit FAIL |
| Native OTA, Power House-geheugen zonder run extension | B4 → A5; 49 samples tot +240,42 s. Verse kamer 23,1 °C/setpoint 23 °C/externe vraag 0 W; vloer 0 W. HP1/HP2 continu op 30/52 Hz. Na boot positief 2499 W met geheugen 0,03153 °C gemeten, daarna normale afbouw naar 0 W. Watchdogmaxima 1605/2023. | PASS |
| Web-OTA na schedulerverbetering | A5 → B5; 48 samples tot +240,26 s: beide compressoren continu op 30/52 Hz. Overdrachtsmask 3 duurzaam opgeslagen, beide cycli overgenomen. Vloer niet nodig; verse warmtevraag 10 kW, flow 650 L/h. Watchdogmaxima 882/1200. | PASS |
| Trage web-OTA, grensoverschrijding | B5 → A5; gelogde transfer 69 s, mask 0 / conservative fallback. Beide ODU’s voor het eerst 0 Hz op +94,72 s; tot +180,59 s geen vroege herstart ondanks verse 10 kW-vraag. De normale opstart/minimale uit-tijd blijft actief. Watchdogmaxima 881/963. | PASS |
| Native OTA met gedeelde pomphold | B3 → A4; transfer 15,18 s. 97 samples tot +480,59 s: HP1 continu op 30 Hz, HP2 continu op 0 Hz, geselecteerde HP2-flow 650 L/h. Eindstatus extending, basisvraag 0 W, vloer 2803 W, comfortgeheugen 0. Voorbij de hold van 180 s én opnieuw begonnen minimale draaitijd van 300 s. Watchdogmaxima 2157/1652 tellingen, onder 12.000. | PASS |

De correctie bewaart R2010/R2015 van de stilstaande Duo-peer zolang de andere
ODU in acquisitie of een geldige OTA-hold zit. Peer-update verwerkt veiligheidsabort
of deadline vóór de guard; de deadline wordt niet verlengd. Compressorstop en
R2006 van de stilstaande peer blijven mogelijk. Beide richtingen zijn met echte
commandoruntime getest, inclusief enqueue, queued dispatch, toestemmingverlies
en 45 s-grens; Single blijft werken.

## Offline validatie en review

- `bash scripts/run_host_regression_tests.sh`: 125 binaries PASS.
- `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`: 447 tests,
  PASS, 2 overgeslagen; lokaal met macOS SDK C++-headers.
- Formatterings-, docs- en diffchecks: PASS.
- `python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml`:
  config geldig; NVS-budget FAIL, 571/630 geschatte entries, 59 beschikbaar bij
  100 vereist. Deze branch heeft geen `duo_wifi.yaml`; `duo.yaml` is de huidige
  Q Duo-entrypoint met WiFi/Ethernet-pakket.
- Beide finale geïnstrumenteerde images compileren; native en web-OTA zijn uitgevoerd.
- Onafhankelijke koude review van de volledige branchdiff en daarna de gedeelde
  pompcorrectie: geen resterende concrete bevindingen binnen bovenstaande grenzen.
  Hostchecks bevatten corruptie/legacyrecords, exacte image/configbinding, durable
  consume, open/commit/readback-falen, koude/crash/SW-retries, replay, klokomloop,
  transferdeadline, transactie-overlap en geheugenherstel met echte vraagberekening.

## Gedeeltelijke geheugentelemetrie

Native duurtest: minimale actuele interne heap 71.155 B, minimum-sinds-boot 39.828 B,
grootste intern block minimaal 31.744 B, PSRAM minimaal 5.317.016 B. Stackminima:
main 3.990 B, MQTT-transport 10.208 B, TCPIP 1.796 B, WiFi 2.728 B. MQTT-worker was
niet doorlopend beschikbaar als benoemde taak; dit is geen volledige MQTT-/HA-loadtest.
Op de finale A5/B5-runs werd minimaal 71.407 B actuele interne heap gemeten,
met hetzelfde minimum-sinds-boot van 39.828 B en grootste block minimaal 31.744 B.
Native geheugenproef: PSRAM minimaal 5.338.020 B, main 3.990 B, TCPIP 1.796 B,
WiFi 2.656 B; webproef MQTT-transport 10.220 B. De HTTP-interface is tijdens delen
van web-OTA niet bereikbaar; ontbrekende samples zijn geen gezonde heapmeting.

Een geslaagde OTA en deze waarden bewijzen geen marge voor de zwaarste gelijktijdige
allocaties; de volledige geheugen-P1 blijft open.

## Eindstatus

Laatste firmware `ota-continuity-a5` (`0x4064ad07`). Na de trage upload stonden
beide ODU’s stil binnen hun normale herstartblokkade; warmtevraag bleef bewust
actief (kamer 19 °C, setpoint 23 °C, externe vraag 10 kW, toestemming aan). Een latere
normale start na minimale uit-tijd is daarmee verwacht. Run extension staat uit
voor de afzonderlijke geheugenproef. Flow Source `Outdoor unit`, Outdoor Unit
Flow Mode `Flowmeter HP2`; gewone instellingen zijn niet teruggezet.
Alle foutinjecties zijn uit en de zes ODU-foutwoorden zijn nul. Logcaptures zijn
afgesloten en de lab-lock is vrijgegeven. De normale minimum-off-wachttijd is in
deze negatieve proef tot +180,59 s gevolgd, niet tot de latere normale start.

## Grenzen en lokale bewijsbestanden

Dit is functionele HIL-validatie. De P1 voor volledige geheugenkwalificatie blijft
open: geen fysieke koude power-cycle, gecontroleerde echte HA/native-API-belasting
of volledige identieke baseline/kandidaatmatrix onder gelijktijdige maximale load.
De eerste webbuild viel veilig terug bij verouderde data; de schedulerverbetering
is daarna afzonderlijk met een geslaagde web-OTA getest. HTTP-download-OTA en
fysieke defrost/oil-return-/transportfout-interleavings zijn in deze vervolgrun
niet uitgevoerd. Flash-opslagfouten zijn offline geïnjecteerd.

Lokale ruwe gegevens staan in `.tmp/hil/2026-10-16-ota-continuity/`, inclusief
`baseline-native-ota`, de gefaalde `native-run-extension` en
`native-run-extension-fixed`, `planned-run-extension-nvs`,
`native-power-house-memory`, `web-scheduler-yield` en `web-long-transfer`;
per scenario JSONL met controller- en simulatormetingen, begrensde SSE-logcapture en uploadlog. Firmware en snapshots blijven lokaal.
