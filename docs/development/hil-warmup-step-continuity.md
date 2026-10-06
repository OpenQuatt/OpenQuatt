# HIL-plan: doorverwarmen tussen opwarmstappen

Status: voorbereid, **niet uitgevoerd**. Het lab is bezet. Geen preflight,
controller-/simulatorrequests, flash of OTA voor deze wijziging uitgevoerd.
Dit plan kwalificeert geen firmware en sluit de bestaande geheugenbevinding niet.

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
Geen volledige kandidaat-firmwarecompile of actuele visuele browserproef uitgevoerd.

```sh
bash scripts/run_host_regression_tests.sh
npm run check:cpp-format
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo_wifi.yaml
```

Dit is een replay van ingevoerde kamertemperaturen, geen model dat de warmteafgifte
van een woning voorspelt. De fixture gebruikt de echte lifecycle, inputadapter,
intent, demand en low-load helpers, met de bestaande PH-floor-glue nagebootst.
Dispatch, minimum on/off tijden en de fysieke terugmelding moeten nog op HIL worden
bevestigd. Een onderbroken run wegens toestemming, waterlimiet, defrost of een
andere guard is niet automatisch het oude tussendoelprobleem.

## Voorbereiding zodra het lab vrij is

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
  testnaam `openquatt-test`. Flash/OTA pas tijdens de latere afgesproken HIL-run.
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
