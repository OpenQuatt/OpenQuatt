# HIL-plan: doorverwarmen tussen opwarmstappen

Status: control-HIL uitgevoerd op 6 oktober 2026; aanvullende proeven worden
hieronder afzonderlijk verantwoord. Dit kwalificeert geen runtime-geheugenbudget
en sluit de bestaande geheugenbevinding niet.

## Aanleiding en lokaal bewijs

[Testerfeedback bij #784](https://github.com/OpenQuatt/OpenQuatt/issues/784#issuecomment-6019777848):
kamer 20,06 °C, tussendoel 20,16 °C, stop rond 20,11 °C en na doorstijgen een
nieuw tussendoel 20,27 °C. De opname gebruikt Single/Power House; warmup-status,
tussendoel en comfortinstellingen zijn niet opgenomen. Die instellingen zijn
daarom een verklaring op basis van code en getalvoorbeelden, geen gemeten feit.

De C++-regressie gebruikt 20,05859375 → 20,09765625 → 20,109375 → 20,16796875 °C,
stap 0,1 °C en comfortband 0,1 °C. Met gewone recovery ontstaan twee starts en één
tussentijdse stop; met warmup-recovery één start zonder tussentijdse stop.
Daarnaast worden 18 stap-/comfortcombinaties en de annuleringen getest.

Lokaal gecontroleerd op 6 oktober 2026: 108 hostexecutables, 379 Python-checks,
766 webtests, C++-format, docs-contract/strikte docs-impact en Q WiFi Duo config/NVS
geslaagd. De nieuwe simulatie slaagt ook met UBSan. De hostlayouts blijven gelijk:
heat-intent State 20 B, Input 40 B, Decision 28 B. Onafhankelijke koude review van
de complete PR-diff en de interruptie-/waterherstelgrenzen heeft geen resterende
codebevindingen. Dit zijn geen nieuwe firmware-heapmetingen. AddressSanitizer
startte lokaal niet door en is afgebroken; daarvoor is geen resultaat verkregen.
Q WiFi Duo en Single zijn volledig gecompileerd en via OTA op het testdevice
geverifieerd. Geen actuele visuele browserproef uitgevoerd.

```sh
bash scripts/run_host_regression_tests.sh
npm run check:cpp-format
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo_wifi.yaml
```

Dit is een replay van ingevoerde kamertemperaturen, geen model dat de warmteafgifte
van een woning voorspelt. De fixture gebruikt de echte lifecycle, inputadapter,
intent, demand en low-load helpers, met de bestaande PH-floor-glue nagebootst.
Dispatch, minimum on/off tijden en ODU-terugmelding zijn voor de kernproef ook op
HIL gecontroleerd. Een onderbroken run wegens toestemming, waterlimiet, defrost of een
andere guard is niet automatisch het oude tussendoelprobleem.

## Testopzet

- Gebruik `configs/heatpump_controller_q/duo_wifi.yaml` voor Jeroens standaardlab.
  Herhaal de kernproef op Single om de topologie van de tester te dekken.
- Controleer identiteit `openquatt-test`, simulatorcontract
  `openquatt-modbus-opentherm-v2`, bekabeling en lablock vóór elke wijziging.
  Geen productieapparaten gebruiken.
- Controller: `http://openquatt-test.local/` (zo nodig de geverifieerde test-IP).
  Simulator: `http://192.168.2.63/`. Gebruik de actieve PR-checkout en de actuele
  hcq-hil-skill/REST-reference; verzin geen endpoints.
- Leg bron-SHA, binary-SHA256, config-hash, simulatorversie/SHA, Single/Duo-profiel,
  alle benodigde instellingen en de oorspronkelijke firmware vast. Bouw met
  testnaam `openquatt-test`. Flash/OTA alleen met toestemming voor het testdevice.
- Stel Power House, automatische regeling, warmtetoestemming, geldige ODU- en
  waterwaarden en nul externe demand expliciet in. Zet run extension uit om de
  warmup-floor zelfstandig te kunnen beoordelen. Gebruik een huismodel en
  buitenwaarde waarbij de modelvraag nul is; lees die nul ook werkelijk terug.
- Gebruik de geverifieerde controller-API-inputs voor room, setpoint en buiten.
  Lees eerst de actuele bereiken/precisie en selecteer de betreffende API-bronnen.
  Schrijf na iedere wijziging teruglezend en controleer selected-value/freshness.
  Refresh room ruim binnen zijn 10min-geldigheid. De simulator-thermostaat heeft
  volgens de REST-reference een stap van 0,5 °C; vertrouw voor de 0,05 °C-grens
  niet op die entiteit zonder eerst de actuele precisie te verifiëren.
  `oq_api_ingress.yaml` heeft nu een room-inputstap van 0,1 °C. Verifieer dat een
  REST-write van 20,06 °C ongewijzigd in input én selected terechtkomt. Als de
  firmware die afrondt, gebruik uitsluitend voor deze proef een private overlay
  met room-inputstap 0,01 °C; documenteer de overlay/hash en kwalificeer daarnaast
  de gewone firmware met een invoerroute die de vereiste precisie doorgeeft.
- Begin één begrensde logstreamcapture en leg warmup-active, effectief tussendoel,
  werkelijk einddoel, kamertemperatuur, intent, PH-request, low-load latch, CM,
  requested/applied levels en echte compressorfeedback afzonderlijk vast.
  Een geslaagde write of alleen een logregel is geen PASS.

## Kernproef zonder versnelde klokken

1. Warmup aan, startgrens 1,5 °C, stap 0,1 °C, staptijd 45 min, comfort below 0,1 °C.
   Stel kamer 20,06 °C en doel 18 °C in. Laat een baseline vastleggen en verhoog
   vervolgens naar 22 °C. Bevestig actieve sessie en tussendoel circa 20,16 °C.
2. Wacht op echte compressorstart; registreer de bestaande bevestiging/preflow/
   minimum-off grenzen. Houd kamer 20,06 °C totdat de start vaststaat.
3. Breng kamer op 20,10 en daarna 20,11 °C. Houd elke waarde langer dan twee echte
   PH-cadences. Recovery/minimale vraag en compressor blijven actief, tenzij een
   onafhankelijk vastgelegde guard ingrijpt. Er mag geen comfortstop voor het
   tussendoel optreden.
4. Breng kamer op 20,17 °C. Bevestig dat het tussendoel naar circa 20,27 °C schuift
   zonder stop/start in de compressorfeedback. Herhaal met minstens drie stappen.
5. Breng de kamer eerst op 21,89 en daarna 21,90 °C. Bij de echte comfortgrens
   eindigt de sessie en verdwijnt de warmup-floor. Controleer de stopvraag en
   vervolgens de werkelijke stop, met de normale slew/minimumlooptijd/naloop.
   Een sessie-einde is niet hetzelfde als een onmiddellijke fysieke stop.

Verwacht: één gewone start en geen extra start bij alleen een tussendoelovergang.
Nul huisvraag en uitgeschakelde run extension zijn nodig om deze verwachting
zonder andere warmtevragen te beoordelen.

## Gerichte aanvullende proeven

- Herhaal stap 0,1 °C met comfort below 0,2 °C: start onder het tussendoel en
  eindig bij 21,8 °C; niet wachten totdat de stap groter dan de comfortband wordt.
- Begin met een reeds draaiende HP en start daarna warmup. Geen kunstmatige stop
  om warmup-recovery te verkrijgen. Bewaak ook een doelwijziging vlak vóór de
  lifecycle-tick; de volgorde is lokaal voor beide interleavings getest.
- Verlaag 22 naar 21,5 °C tijdens recovery, schakel warmup uit, wissel roombron
  en wissel regelmodus in afzonderlijke sessies. De warmup-floor verdwijnt;
  normale warmtevragen en veiligheid blijven afzonderlijk leidend.
- Trek warmtetoestemming in en maak room/setpoint stale. Controleer de werkelijke
  stop volgens bestaande minimumlooptijd en cadence; geen oude recovery hergebruiken.
- Laat de waterlimiet actief worden via de geverifieerde normale ODU/waterinputs.
  De warmup-floor mag die limit niet passeren. Geen API-supply-input verzinnen.
  Nadat de applied output werkelijk nul is geworden, mag herstel niet het oude
  warmup-runrecht hergebruiken: bevestig de nieuwe room-start en minimum-off gate.
- Herstart na een actieve sessie: instellingen blijven behouden, sessie niet;
  geen kunstmatige thermostaatverhoging of hervatting van de oude floor.
- Warmup uit: normale room-recovery stopt op de bestaande halve restartband.

Alleen de timeoutproef mag korter: zet `Tijd per stap` tijdelijk op de toegestane
5 min, houd de kamer onder het tussendoel en controleer 0,1 → 0,2 °C. Leg die
testinstelling vast; gebruik voor stop/start, permissie en interleavings echte
klokken. Er is geen reden om minimum on/off tijden of veiligheidsgrenzen te
versnellen. Verander de productdefaults niet.

## Resultaat vastleggen

Rapporteer per topologie en scenario PASS/FAIL met meetwaarden, tijdstippen,
applied levels en compressorfeedback; vermeld externe guards afzonderlijk.
Bewaar begrensde geredigeerde logs en meetreeksen met checksums. Noteer de
uiteindelijke controller-/simulatorinstellingen en firmware. Laat geen actieve
fault injection of ongebruikte fixture achter. Behandel de aparte heap/P1-proef
en de historische firmwarekwalificatie onafhankelijk van deze controlfix.

## Meetresultaten 6 oktober 2026

Kandidaatbron: `4830dece770b677f6888eef574dcc7ff3db3f09c`. Private overlays gebruiken
de gewone Q WiFi-configs, testnaam `openquatt-test` en uitsluitend een kortere
NVS-schrijfvertraging van 1 s. Geen versnelde regel-/veiligheidsklokken en geen
room-precisieoverlay. Nummer-REST toont 20,06 afgerond als 20,1, maar geselecteerde
sensor geeft de geschreven 20,06 ongewijzigd door; die geselecteerde waarde is
na iedere precieze write gecontroleerd.

| Profiel | Config-hash na OTA | OTA binary SHA256 |
| --- | --- | --- |
| Q WiFi Duo | `0x57f4228d` | `3709c7b93a4799709d3452c1fcac530bf9247ae9214a0dedb2d62bef502c61ad` |
| Q WiFi Single | `0xb3fe1313` | `c415626d5c491c7dc0dffaacac1eab7ed46fbb601c8a9edadcdd2029addc1867` |

Simulator: v0.5.0, contract `openquatt-modbus-opentherm-v2`, beide ODU's V1.5,
adressen 1/2. De exacte geflashte simulatorbron-SHA is niet blootgesteld; een
lokale checkout-SHA is daarvoor geen bewijs. Identiteit, contract en inactieve
servicestanden zijn vóór mutaties gecontroleerd; één exclusieve lablock is gebruikt.

Voor de kernproef: kamer/setpoint/buiten/CH via API, Power House, run extension
uit, buiten 19,6 °C en huisvraag aantoonbaar 0 W. Flow via ODU, 800 L/h gevraagd
en circa 797 L/h gemeten. Stap 0,1 °C, staptijd 45 min, startgrens 1,5 °C,
comfort below 0,1 °C, minimumlooptijd 300 s en minimum-uit-tijd 240 s. De behouden
fixturewaarde `Power House temperature reaction` bleek 0 W/K; de nulmodelproef
beoordeelt dus de warmup-floor zonder gewone PH-temperatuurfeedback.

| Proef | Resultaat | Waargenomen gedrag |
| --- | --- | --- |
| Duo: 20,10 / 20,11 bij tussendoel 20,16 | PASS | Request ≥ Pmin 2.527,63 W; HP1 applied 1, ODU1 mode 2. Frequentie 30 Hz in 45 aparte terugmeldingen over oude-stophold/eerste volgende stap; latere fasen alleen sampled applied/mode-terugmelding. |
| Duo: vasthouden op 20,11 | PASS | 41 meetpunten over 226 s; laatste meetpunt > 300 s na eerste bevestigde start. Minimumlooptijd maskeert de stop dus niet. |
| Duo: 20,17 → 20,28 → 20,39 | PASS | Tussendoel 20,27 → 20,38 → 20,49, geen compressorstop tussen stappen. |
| Single: dezelfde vijf fasen | PASS, kort | Circa 34 s per fase; HP1 applied 1 en ODU1 mode 2 blijven actief, request ≥ Pmin, huisvraag 0 W. De oude-grenshold kwam hier niet voorbij 300 s runleeftijd. |
| Duo: echte ondergrens 21,90 bij doel 22 | PASS, handoff | Sessie eindigt; tussendoel wordt 22 en de oude run stopt. Normale recovery kan op de inclusieve ondergrens opnieuw starten; geen claim van blijvende uitschakeling. |
| Duo: gewone recovery vrijgegeven | PASS | Op 21,96 verdwijnen request en latch; definitief stil na CH uit/CM0 en gewone minimumlooptijd. |
| Single: instellingen wijzigen tijdens bestaande sessie, daarna doel 24 | Continuïteitsproef mislukt | Annulering bij wijziging staptijd valt terug op 0 W/K-feedback/huisvraag 0: CM1-stop is al ingezet vóór nieuwe sessie. Niet als tussendoelregressie of schone running-entry-PASS geïnterpreteerd. |
| Single: reeds draaiende HP, daarna alleen doel 22 → 24 | PASS | Comfort below 0,2 °C/stap 0,1 °C/staptijd 5 min en reactie 3.000 W/K vooraf ingesteld. 58 meetpunten over circa 322 s: HP1 blijft actief, ODU1 mode 2; request zakt tot Pmin zonder stop. |
| Single: onbereikt tussendoel na 5 min | PASS | Kamer blijft 20,39 °C; tussendoel 20,49 → 20,59. Eerste nieuwe gepubliceerde meetwaarde 311 s na doelwrite, compressor blijft draaien. Dit is timeoutgroei, geen vaste opwarmsnelheid. |
| Single: waterlimiet tijdens warmup | PASS | Maximum water 60 → 25 °C bij geselecteerd water 32,24 °C: laatste meetpunten request 0, HP1 applied 0 en ODU1 mode 0, warmup nog actief. |
| Single: waterlimiet vrij, CH-toestemming uit | PASS | Waterlimiet terug op 60 °C, maar CH uit: 8 meetpunten request/output 0; geen warmup-floor door toestemming heen. |
| Single: toestemming terug tijdens minimum-uit-tijd | PASS, blokkering | 7 meetpunten fysiek uit, request 0/latch uit. Intent gaat van `none` naar nieuwe `room_demand`; geen oud runrecht. Geen volledige herstart na afloop van minimum-uit-tijd in deze fase gekwalificeerd. |
| Single: doel 24 → 23 / warmup uit | PASS, sessie | Sessie eindigt en effectief doel wordt respectievelijk 23/24. Met CH uit gecontroleerd; geen afzonderlijke compressorinterruptieproef. |
| Single: weer inschakelen | PASS, geen replay | Doel blijft 24; sessie blijft uit. Alleen een nieuwe echte verhoging 18 → 24 activeert haar opnieuw. |
| Single: roombron HA en terug naar API | PASS, ontbrekende invoer | HA-bron geeft `NA`: sessie annuleert; terugkeer naar verse API-waarde bij gelijk doel start haar niet opnieuw. Geen overdracht tussen twee verse bronnen gekwalificeerd. |
| Single: PH → heating curve → PH | PASS, sessie | Verse API-room blijft 20,39; sessie annuleert en keert niet terug bij gelijk doel. CH blijft uit. |
| Single: herstart met actieve sessie / CH uit | PASS, herstel | Uptime 0,700438 → 0,017189 h bewijst herstart. Enable/stap/staptijd/startgrens/comfort/PH-reactie/waterlimiet/minimumlooptijd blijven behouden. Sessie blijft uit, ook na opnieuw invoeren van kamer 20,39 en hetzelfde doel 24. Geen reboot met draaiende compressor gekwalificeerd. |

De mislukte aanvullende fase blijft in de meetreeks staan. Bounded SSE toont
17:40:00 UTC vraag 0 en CM1/postflow; om 17:40:30 is vraag terug maar postflow
kiest nog standby. ODU-stop om 17:41:03 gaat vooraf aan nieuwe CM2 om 17:41:05.
De HTTP-sensor `P_req` heeft een andere publicatiecadence dan de interne request;
gecachete Pmin-vraag tijdens die overgang is geen bewijs van een blijvende vraag.
De onafhankelijke analyse bevestigt dit onderscheid.

Waterbron bleef `HA input`. De waterproef verlaagt de bestaande grens tegenover
de daadwerkelijk geselecteerde waterwaarde; dit is geen PT1000-/ODU-sensorproef
of supply-temperature-API-test. Het waterlimietresultaat beoordeelt de downstream
guard, geen thermisch woningmodel. Defrost, een volledige freshness-timeout van
10 min en de 8-uursgrens zijn niet opnieuw op HIL uitgevoerd. Freshness, 8 uur,
annuleringen en waterinterruptie/herstel zijn wel in de C++-regressie getest.

Eindstand: opnieuw de verzegelde Duo-binary `0x57f4228d`, `Force CM0`, CH uit,
functie ingeschakeld en sessie inactief, doel 18 °C bij kamer
20,39 °C. Staptijd terug op 45 min, comfort below 0,1 °C, PH-reactie 3.000 W/K,
waterlimiet 60 °C. Beide applied levels, ODU-modes en compressorfrequenties nul;
servicefuncties, manual telemetry, force-no-flow en defrost uit. Flowbron ODU en
simulator external system pump flow blijven ingesteld. De runner meldde succesvolle
vrijgave, eindigde met code 0 en afwezigheid van de lablock is afzonderlijk bevestigd.

Bewijs: [545 geredigeerde meetpunten](hil-warmup-step-continuity-2026-10-06.csv)
en [identiteit, verdicts, herstart-/eindstand en SHA256-manifest](hil-warmup-step-continuity-2026-10-06.json).
CSV bevat ook de mislukte settingsproef en de startupwaarden; niet alleen de
geslaagde fasen. Timestamps zijn UTC. De ruwe bounded logstream en detailfeedback
blijven privé; het manifest verzegelt ze zonder netwerk-/installatiegegevens te publiceren.
Control- en bewijsreview zijn onafhankelijk uitgevoerd. Deze nominale regelproef
bevestigt geen heapreserve of historische crashoorzaak: P1 blijft open.
