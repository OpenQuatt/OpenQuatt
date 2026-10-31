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

De scenario-overlays staan bij de runners in `scripts/hil/configs/`. Hun gedeelde
`base_duo.yaml` include neemt `duo_hil.yaml` over en past alleen de relatieve
bronpaden en het buildpad aan deze mapdiepte aan. De overlays mogen nooit
rechtstreeks `duo.yaml` includen: dat draagt de productie-identiteit en zou
een mDNS-conflict met een echte productiecontroller veroorzaken.

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
  --test-config scripts/hil/configs/input_sources_fast_duo.yaml \
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
  --config scripts/hil/configs/input_sources_fast_duo.yaml
python3 scripts/dev.py validate --config-only \
  --config configs/heatpump_controller_q/duo_hil.yaml
```

De eerste opdracht test write-gating, requestbegrenzing, CLI-veiligheidsregels,
OTA-commandoconstructie en volledig instellingenherstel met fakes. De twee
config-validaties controleren respectievelijk de testoverlay en de canonieke
HIL-entrypoint tegen de actuele firmwarecompositie. De Duo HIL-config wordt ook
automatisch in CI gevalideerd.

## Power House: langer doorverwarmen bij een hogere minimumstand

De gerichte stage `extension` gebruikt HP1 als V2 old en HP2 als V2 new.
Gewone frequentie-uitsluitingen kiezen de geteste pomp; bij de Duo-controle blijft
de tweede pomp beschikbaar maar uitgeschakeld. De simulator blijft dynamisch;
de echte Modbus-frequentie en werkmodus bewijzen de start en stop. De bestaande
CIC-fixture levert uitsluitend de geselecteerde aanvoertemperatuur.

```bash
node scripts/hil/run-run-extension.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config scripts/hil/configs/run_extension_duo.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage extension \
  --apply
```

De stage controleert inschakelen tijdens stilstand en doorverwarmen op F1 bij
13,5, 14,6 en 20 °C buiten met de gedeelde V2-new-kaart. Een gewone uitsluiting
van 20–26 Hz maakt F3 het minimum voor de eerdere stop, ook als de stilstaande
tweede pomp wel op F1 kan draaien. De stage controleert ook F1 met een berekende
uitlaattemperatuur vlak onder 49 °C, afkoel-/herstartgrenzen en echte warmtevraag
boven het minimumvermogen. Daarna wordt ook HP2 op F1 getest.
De gewone stand `Manual PWM` op 50 iPWM levert in dit lab circa 900 l/h;
de watergrensproef gebruikt 47,0 °C inlet en controleert circa 48,944 °C uitlaat.
De overlay `run-extension-v2-fast` verlaagt uitsluitend op HIL de runtimevloer
naar 120 seconden en de minimale stilstand naar 60 seconden. Het scenario stelt
180 seconden minimumlooptijd in zodat de kaartmetingen en de Duo-controle binnen
het bewaakte looptijdvenster vallen; beide timers worden bewaakt.
Na de bewezen minimumlooptijd gebruikt het scenario voor de vervolgruns de
HIL-vloer van 120 seconden om herhaling en herstel te versnellen.
Normale firmware behoudt 300 en 240 seconden. De gedeelde snapshot en recovery
bewaren ook de geraakte comfort-, extension-, warmup-, frequentie- en
simulatortemperatuurinstellingen. Nieuwe snapshots gebruiken schema 5. Herstel
blijft schema 3 en 4 accepteren en herstelt daar alleen de werkelijk opgeslagen velden.
Recovery wacht in veilige CM0-aanvraag op de minimumlooptijd en hydraulische
uitloop voordat OTA mag beginnen. Na herstel van de simulatorprofielen herstart
het scenario de normale controller en bevestigt het ook beide ODU-varianten.

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
  --test-config scripts/hil/configs/v2_performance_duo.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all \
  --apply
```

De `power`-stage controleert de V2-formule bij 230 V, 1,3 A en fanwaarde 200,
plus de afzonderlijke bijdragen van de V2-heatergroep (R2108 bit 2: 280 W voor
de parallel geschakelde bottom plate heater en drain pan heater van elk 140 W),
carterverwarming (bit 3: 33,62 W) en pompfeedback (bit 11). Defrost wordt bewust
als negatieve scheidingstest gebruikt: defrost alleen mag de 280 W van de
heatergroep niet toevoegen. De `performance`-stage controleert het
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
