# Hardware-in-the-loop-tests

OpenQuatt gebruikt HIL-tests voor wijzigingen waarbij hosttests alleen niet
bewijzen dat controller, ODU, OpenTherm en timing samen veilig blijven. Een
volledige HIL-run is bedoeld voor gebundelde control-wijzigingen en
releasekandidaten, niet voor iedere kleine pull request.

## Onderhoudsmodel

HIL is de kleinste testlaag in OpenQuatt. Nieuwe regressies horen standaard in
host- of integratietests. Een hardwaretest wordt alleen toegevoegd wanneer echte
controller-, ODU-, OpenTherm-, reboot-, timing- of persistentie-eigenschappen
onderdeel zijn van het contract dat bewezen moet worden.

HIL-scenario's worden georganiseerd rond blijvende systeemcontracten, niet rond
issues of pull requests. Gebruik inhoudelijke domeinnamen zoals
`input-sources`, `communications`, `duo`, `defrost`, `boiler` en
`v2-performance`. Een issue of PR mag in commentaar of documentatie als
herkomst worden genoemd, maar wordt geen blijvende scenarionaam.

Voeg een nieuwe HIL-case alleen toe wanneer alle onderstaande punten gelden:

1. een host- of simulator-only test bewijst het relevante gedrag onvoldoende;
2. de case bewaakt een blijvend systeemcontract en niet alleen de historische
   vorm van één bug;
3. de setup, assertions en cleanup zijn deterministisch en automatisch;
4. herstel na mislukking loopt via de gedeelde snapshot/recovery-infrastructuur;
5. een bestaande case kan niet eenvoudiger worden uitgebreid om hetzelfde
   contract af te dekken.

Een scenariofile mag meerdere nauw verwante cases bevatten. Maak dus liever één
`defrost.mjs` met grens-, overlap- en completion-cases dan losse scripts per
incident. Gedeelde lifecycle-code hoort in `scripts/hil/`; scenariofiles bevatten
alleen domeinspecifieke voorbereiding en assertions.

Hardware-HIL is bovendien selectief. Een pull request draait de relevante stage(s)
plus een rooktest; `--stage all` is bedoeld voor brede control-wijzigingen en
releasekwalificatie. Het bestaan van een HIL-case betekent nadrukkelijk niet dat
hij bij iedere wijziging op hardware moet worden uitgevoerd.

De lokale regels voor scenariostructuur en review staan ook in
[`tests/hil/scenarios/README.md`](../../tests/hil/scenarios/README.md).

## Veiligheidscontract

De runner is standaard read-only. Een scenario dat instellingen wijzigt of
firmware uploadt vereist altijd `--apply`. Verder gelden deze grenzen:

- controller- en simulator-URL hebben bewust geen standaardwaarde;
- alle REST-verzoeken lopen serieel door één begrenzer;
- controllerstates worden per meetmoment in één read-only bulkverzoek opgehaald;
- tussen REST-writes zit standaard minimaal 1.500 ms en nooit minder dan
  1.000 ms;
- vóór de eerste mutatie wordt `.tmp/hil/<run>/snapshot.json` atomisch
  vastgelegd;
- testinstellingen worden vóór én na het terugplaatsen van normale firmware
  hersteld en teruggelezen;
- pending én actieve ODU-profielen en Modbus-adressen worden afzonderlijk
  vastgelegd; na de simulatorreboot worden ook tijdelijke instellingen en de
  oorspronkelijke pending configuratie opnieuw hersteld en gecontroleerd;
- de herstelde firmwareversie en config-hash moeten exact overeenkomen met de
  identiteit die vóór de test in het snapshot is opgeslagen;
- tijdens compileren en terugplaatsen van normale firmware blijft de controller
  geforceerd in CM0; de oorspronkelijke override wordt pas na profielcontrole
  teruggezet;
- de testfirmware moet het verwachte `HIL Test Profile` publiceren;
- het testfirmware bouwt op `duo_hil.yaml` en publiceert dus `openquatt-test`,
  nooit de productie-hostname `openquatt.local`;
- het profiel `Thermostat room setpoint` van de simulator moet 0..40 °C
  kunnen aannemen voordat `setpoint-validity` de afgewezen waarden injecteert;
- de simulator moet vóór iedere mutatie exact contract
  `openquatt-modbus-opentherm-v2` publiceren;
- een muterende run vereist een normale restoreconfig en OTA-adres;
- snapshots en rapporten blijven onder het door Git genegeerde `.tmp/hil/`.

De afzonderlijke `duo_hil.yaml`-testcontroller en gecombineerde simulator
gebruiken `preferences.flash_write_interval: 1s`. De runner wacht daarna twee
seconden en verifieert alle herstelwaarden nogmaals read-only. Persistente
controllerinstellingen en simulatorprofielen/-adressen zijn dan naar flash
geschreven. Handmatige registerwaarden zijn bewust alleen voor de huidige boot;
hun overrides, no-flow en defrost moeten vóór de run uit staan en starten na een
reboot altijd veilig uit. Deze instelling verandert de productie-entrypoints niet.

De runner bevat geen IP-adressen, wifi-gegevens, wachtwoorden of lokale
instellingenback-ups. Gebruik hem alleen wanneer bedrading, hydrauliek en de
simulator volgens de testerhandleiding veilig zijn aangesloten.
De zelfstandige simulatorbron en testerhandleiding staan in
[`OpenQuatt-Simulator`](https://github.com/OpenQuatt/OpenQuatt-Simulator). Gebruik
voor dit contract minimaal simulatorrelease `v0.4.0`.

## Testcontroller-identiteit

Het HIL-testfirmware bouwt altijd op
`configs/heatpump_controller_q/duo_hil.yaml`. Dat entrypoint pint
`device_name` op `openquatt-test` en een eigen projectidentiteit, zodat een
HIL-run nooit de productie-hostname `openquatt.local` claimt. Gebruik daarom
`openquatt-test.local` als `--device` en `duo_hil.yaml` als `--restore-config`.

De HIL-overlays includen daarom `duo_hil.yaml` en nooit rechtstreeks `duo.yaml`: dat
entrypoint draagt de productie-identiteit en zou op een netwerk met een echte
productiecontroller een mDNS-conflict veroorzaken. `configs/hil/` mag alleen
naar `duo_hil.yaml` (of een shim daarop zoals `duo_wifi_hil.yaml`) verwijzen.

## Read-only rooktest

Controleer bereikbaarheid, firmware, heap en ODU-protocoldiagnostiek zonder
iets te wijzigen:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --stage smoke
```

Het rapport bevat actuele en minimale interne heap, grootste vrije block,
fragmentatie, vrije PSRAM en beide ODU-diagnoseregels voor zover de entities
beschikbaar zijn. Ook simulatorcontract en -versie worden vastgelegd.

## Volledige input-/bronselectietest

De testconfig is uitsluitend voor HIL en wordt niet als releaseprofiel gebouwd.
Hij gebruikt de normale unified Q Duo-firmware, met alleen deze kortere testtijden:

| Contract | Productie | HIL |
|---|---:|---:|
| API-input stale | 0/600/900/1.800 s | 45 s |
| Geselecteerde-input hold | 300 s | 10 s |
| HP minimum-off | 240 s | 10 s |

Start de gebundelde run met:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/input_sources_fast_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all \
  --apply
```

De volgorde is vast:

1. bereikbaarheid en nulmeting controleren;
2. alle geraakte controller- en simulatorinstellingen opslaan;
3. testfirmware compileren en via ESPHome-OTA plaatsen;
4. profielmarker controleren en de gekozen scenario's uitvoeren;
5. heap- en protocolresultaten vastleggen;
6. instellingen herstellen;
7. normale firmware via OTA terugplaatsen;
8. instellingen na reboot opnieuw herstellen en verifiëren.

Losse scenario's zijn beschikbaar als `setpoint-validity`, `inputs`,
`enable-expiry`, `active-switch` en `reboot-reset`. Ook een losse muterende run
herstelt altijd de normale firmware. De inputtest raakt alle zeven
API-inputslots, inclusief het dauwpunt. Met `--min-heap-min-free` en
`--min-largest-block` kan een vooraf afgesproken profielbudget als harde grens
worden meegegeven. Zonder die opties rapporteert de runner de waarden, maar
noemt hij een geheugentest niet automatisch releaseveilig.

## Generieke lokale inputfixtures

Gebruik daarvoor de lokale fixture-runner, na afronding en herstel van de eerste
run. Het voorbeeldadres is het LAN-adres van de labdesktop; pas het aan als dat
adres verandert. Preflight weigert een adres dat niet bij de desktop hoort en
weigert controller- en simulatoradressen.

```bash
OQ_HIL_FIXTURE_HOST=192.168.2.103 node scripts/hil/run-source-transports.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/source_transports_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage transports \
  --apply
```

Deze proef gebruikt een kleine lokale CIC-HTTP-feed en een MQTT 3.1.1 QoS0-fixture.
Zij accepteren alleen de testcontroller. De CIC-proef controleert geldige
ontvangst, kleine temperatuurwijzigingen en publicatievertraging, ontbrekende kamer-/setpointvelden
en herstel van currentness. De MQTT-proef controleert echte subscriptions,
ontvangen kamer-/setpointwaarden en een bronwissel heen en terug.
Dit test geen productie-CIC of externe broker.

De runner bewaart en herstelt de oorspronkelijke CIC-URL, brokerinstellingen en
geraakte inputflags in het private snapshot. Hij leest geen MQTT-wachtwoord en
controleert bij herstel dat `password_set` gelijk blijft. Een lege oorspronkelijke
broker met een opgeslagen wachtwoord wordt vooraf geweigerd. De fixture vereist
een baseline zonder opgeslagen brokerwachtwoord: ook een niet-lege broker met
`password_set=true` wordt vóór mutatie geweigerd, omdat clearing niet herstelbaar is
en de fixture uitsluitend anonieme CONNECT accepteert. Gebruik voor recovery dezelfde runner, inclusief
`--stage transports`, met de herstelopties hieronder.

De transportproef blijft geforceerd in CM0 en heeft geen warmup-entities nodig.
De aparte overlay toont de actuele room/setpoint-currentness en source generation.
Source-hosttests controleren ontbrekende CIC-velden en de publicatietolerantie;
warmup-hosttests bewijzen stoppen/herstel voor generiek ongeldige input.
De native-HA-fixture (`scripts/hil/native-ha-fixture.mjs`) is gedeelde
infrastructuur voor versleutelde ESPHome-inputproeven; hij bewijst geen volledige
Home Assistant-installatie. Gebruik alleen afzonderlijke labfirmware met een
tijdelijke RAM-sleutel en private `OQ_HIL_NATIVE_API_KEY`.

## Gecontroleerd aanwarmen na nachtverlaging

Deze runner gebruikt de normale productietimers en uitsluitend de benoemde
testcontroller. De overlay voegt alleen een profielmarker toe.

```bash
node scripts/hil/run-controlled-warmup.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/controlled_warmup_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all \
  --apply
```

`boundaries` controleert de startgrens, vaste tussenstappen, afkoelen,
setpointwijzigingen, bronwissels, handmatige bediening en het uitsluiten van de stooklijn.
`curve` controleert dat overschakelen naar de stooklijn de sessie beëindigt.
`timing` wacht twee echte stappen van vijf minuten; `stale` wacht op het vervallen
van een API-kamertemperatuur na tien minuten. `restart` controleert opgeslagen
instellingen en het vervallen van de lopende sessie na een echte reboot.
`duration` controleert de vaste achtuurgrens. Reken voor `all` op ruim
acht en een half uur, plus compilatie, OTA en herstel. De instellingen en firmware
worden ook na een losse stage hersteld. Na de herstel-OTA worden de instellingen
opnieuw gezet en teruggelezen; dat is geen tweede persistentieproef na reboot.
De stage `restart` bewijst afzonderlijk dat de testinstellingen een reboot overleven.
`timers` combineert `timing`, `stale`, `restart` en `duration` in één run.
`short-timers` combineert alleen `timing`, `stale` en `restart`, bijvoorbeeld
wanneer de duurgrens afzonderlijk wordt onderzocht.

De featureproef gebruikt API-kamerinput voor de grenzen en OpenTherm voor lange
timerproeven. Transportcontracten worden apart door source-HIL bewezen. Een
optionele gedeelde native-API-fixture levert de bench-aanvoertemperatuur voor de
Power House-integratieproef via versleuteld ESPHome-verkeer. Daarvoor is afzonderlijke
labfirmware met een tijdelijke RAM-sleutel nodig, plus een privé aangeleverde
`OQ_HIL_NATIVE_API_KEY`. De runner wijzigt de opgeslagen API-sleutel niet.
Sleutels en de afgeleide labconfig horen uitsluitend in genegeerde lokale
bestanden. Zonder deze fixture omvat de run geen HA-transporttest en slaat `all`
de stage `regulation` expliciet over. Een losse `regulation` wordt zonder testsleutel
vóór wijzigingen geweigerd. Deze stage vergelijkt de werkelijke Power House-vraag
en controleert warmtetoestemming, actuatoruitgangen en de overgang naar koelen.
De koelproef schakelt tijdelijk `Cooling Room Request Required` uit, zodat
een afzonderlijk koelverzoek bij de nagebootste lage kamertemperatuur wordt
toegestaan. De oorspronkelijke instelling wordt meegenomen in het herstel.
`regulation-tail` herhaalt afzonderlijk de comfortband- en koelproef. Voor de
comfortbandproef moeten standby, nul toegepaste niveaus en een vrijwel nul
vermogensvraag eerst daadwerkelijk zijn bereikt.
Ook met de fixture is dit geen proef van een volledige Home Assistant-installatie.
MQTT en CIC vereisen afzonderlijke transportproeven.

Gebruik voor recovery de runner die het snapshot maakte, met `--restore-snapshot`,
`--restore-config`, `--device` en `--apply`, zoals hieronder beschreven.
Een warmup-snapshot bevat tevens de oorspronkelijke vier instellingen,
broninstellingen en simulatorwarmtevraag. Een ontbrekend of ongeldig blok wordt
afgewezen voordat het herstel instellingen terugzet.
Nieuwe extra instellingen gebruiken schema 2. Bij een ouder schema 1 zonder
`coolingRoomRequired` laat herstel die voorheen ongemoeide instelling staan.

## OpenTherm kamer-setpoint: `setpoint-validity`

`--stage setpoint-validity` test end-to-end dat een semantisch onbruikbaar
OpenTherm `TrSet` wel transportmatig binnenkomt en vers blijft, maar niet als
`Room Setpoint (Selected)` wordt doorgegeven. De productievaliditeit is
5..35 °C; de grenswaarden zelf worden geaccepteerd.

De stage gebruikt de echte OpenTherm-thermostaatsimulator en controleert:

- basislijn `TrSet = 21 °C` geeft `Room Setpoint (Selected) = 21 °C`;
- `Room Temperature = 0 °C` blijft numeriek geldig, want de 5..35 °C-grens
  geldt alleen voor het setpoint;
- `TrSet = 0.0 °C` en `4.5 °C` komen binnen terwijl de OT-link gezond blijft,
  maar `Room Setpoint (Selected)` wordt unavailable;
- `TrSet = 5.0 °C` en `35.0 °C` worden geaccepteerd en `35.5 °C` afgewezen;
- herstel naar `21.0 °C` werkt direct.

Voorwaarde: de simulator moet `Thermostat room setpoint` over 0..40 °C kunnen
zetten. Dat is ruimer dan het productiecontract en komt uit
[OpenQuatt-Simulator#2](https://github.com/OpenQuatt/OpenQuatt-Simulator/pull/2).
Controleer vooraf de actieve range:

```bash
curl -fsS 'http://SIMULATOR-IP/number/Thermostat%20room%20setpoint?detail=all'
```

Staat er `min_value 5.0` en `max_value 35.0`, dan kan deze stage de afgewezen
waarden niet injeceren. Flash dan een simulatorversie met PR #2 of nieuwer.

## Afbreken en herstellen

Eén keer `Ctrl-C` vraagt recovery aan. Een actieve compile of OTA wordt eerst
afgerond; REST-wachtlussen stoppen bij hun volgende controle. Een tweede signaal
breekt direct af en kan daarom handmatig herstel nodig maken. Bij onvolledig
herstel blijft de persistente globale lablock
`~/Library/Application Support/OpenQuatt/HIL/locks/hcq-desktop-lab/owner.json`
staan en toont de runner
het snapshotpad. Alle URL-aliases, outputmappen, worktrees, normale runs en
recovery's delen deze ene lock. Alleen recovery voor dezelfde runmap mag een
lock van een aantoonbaar gestopt proces overnemen; release controleert het
unieke owner-token en kan nooit de lock van een andere run verwijderen.
De lock overleeft een hostreboot; `OQ_HIL_LOCK_ROOT` is alleen bedoeld voor
geïsoleerde harness-tests, niet voor normale labruns.

Herstel dan met exact dezelfde doelen:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --restore-snapshot .tmp/hil/RUN/snapshot.json \
  --apply
```

Gebruik `--settings-only` alleen wanneer normale firmware aantoonbaar al actief
is. De herstelopdracht weigert een snapshot voor andere controller- of
simulator-URL's of voor een andere scenariorunner. Gebruik voor een issue-667
snapshot daarom `scripts/hil/run-v2-performance.mjs`, niet de generieke
input-source-runner. In deze modus moeten ook de firmwareversie en config-hash
exact overeenkomen met de identiteit in het snapshot.

Een herstelpoging overschrijft het oorspronkelijke rapport niet, maar schrijft
een afzonderlijk `recovery-<tijdstip>.json` in dezelfde runmap.

Kortstondige numerieke API-inputs en API-enablewaarden worden bij herstel bewust
niet teruggezet; enablewaarden worden expliciet uitgeschakeld. Dat voorkomt dat
een eerder testcommando na een afgebroken run opnieuw toestemming of vraag geeft
voor verwarmen of koelen. De oorspronkelijke bronselecties worden wel hersteld,
waarna de normale integratie nieuwe API-waarden moet aanleveren.

## Lokale validatie zonder hardware

```bash
npm run check:hil
python3 scripts/dev.py validate --config-only \
  --config configs/hil/input_sources_fast_duo_wifi.yaml
python3 scripts/dev.py validate --config-only \
  --config configs/heatpump_controller_q/duo_hil.yaml
```

De eerste opdracht test write-gating, requestbegrenzing, CLI-veiligheidsregels,
OTA-commandoconstructie en volledig instellingenherstel met fakes. De twee
config-validaties controleren respectievelijk de testoverlay en de canonieke
HIL-entrypoint tegen de actuele firmwarecompositie. De Duo HIL-config wordt ook
automatisch in CI gevalideerd.

## Issue #667: V2 performance en Power Input

Dit afzonderlijke scenario gebruikt het volledige V2-model van simulator
`v0.4.0`. HP1 identificeert zich als `V2 old model`, HP2 als `V2 new model`.
Handmatige registertelemetrie maakt de vermogensvector deterministisch zonder
het gesimuleerde thermische model als test-orakel te gebruiken.

```bash
node scripts/hil/run-v2-performance.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/issue_667_v2_performance_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all \
  --apply
```

De `power`-stage controleert de V2-formule bij 230 V, 1,3 A en fanwaarde 200,
plus de afzonderlijke bijdragen van bodemplaatverwarming (R2108 bit 2),
carterverwarming (bit 3) en pompfeedback (bit 11). Defrost wordt bewust als
negatieve scheidingstest gebruikt: defrost alleen mag de 140 W van de
bodemplaatverwarming niet toevoegen. De `performance`-stage controleert het
exacte kaartpunt A=12,6 °C, wateraanvoer=22,5 °C en 20 Hz (Pth=3072,6185 W),
en daarnaast een veldfixture van circa 1020 l/h en ΔT=2,03 K.

De runner neemt ook de pending en actieve simulatorprofielen en Modbus-adressen,
plus alle handmatige telemetry-instellingen, op in `snapshot.json`. Na afloop
wordt de oorspronkelijke actieve configuratie onder `Force CM0` teruggezet en
gecontroleerd. Daarna worden de oorspronkelijke pending configuratie en alle
tijdelijke instellingen hersteld en teruggelezen. Faultinjectie, waaronder
UART-parityinjectie, maakt geen deel uit van dit scenario.

Herstel een afgebroken issue-667-run met dezelfde scenariorunner:

```bash
node scripts/hil/run-v2-performance.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --restore-snapshot .tmp/hil/RUN/snapshot.json \
  --apply
```
