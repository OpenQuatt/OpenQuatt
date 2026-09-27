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

## Mono, Duo en communicatie

De domeinrunners gebruiken de gedeelde lock, REST-begrenzer, snapshot en
firmware-restore. Selecteer alleen het relevante domein en de relevante stage:

| Runner | Stage | Contract |
|---|---|---|
| `run-duo.mjs` | `start` | Vanuit idle starten met één beschikbare ODU; HP1 en HP2 worden afzonderlijk getest. CM2 zonder bevestigde compressorstart is geen PASS. |
| `run-duo.mjs` | `peer-loss` | Eén ODU valt tijdens verwarmen weg; de resterende ODU neemt verwarming over zonder CM4 of ketelstart. CM1 en normale restartguards zijn toegestaan vóór overname. Beide uitvalrichtingen worden getest. |
| `run-communications-mono.mjs` | `fallback` / `all` | Mono: HP1 weg, causale stop-timeout en CM4; herstel naar CM2 met bevestigde compressorfeedback. HP2 wordt niet onderdrukt. |
| `run-communications.mjs` | `fallback` | Beide ODU's weg: causale onbevestigde stop en CM4 met actieve keteltransportuitgang; na herstel gezonde ODU’s in CM2, compressorfeedback, ketel uit en causale permissie gewist. |

Alle runners hebben een read-only `smoke`. `all` betekent alleen alle stages
van die runner; het is geen volledige hardware-regressieset.

```bash
node scripts/hil/run-duo.mjs \
  --controller http://openquatt-test.local --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/control_regression_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage start --apply
```

Voor de fallback-/handbackcase gebruik je `run-communications.mjs` met
`--stage fallback` en dezelfde overige opties. Voor herstel gebruik je dezelfde
domeinrunner met `--restore-snapshot .tmp/hil/<run>/snapshot.json --apply` en de
controller-, simulator-, device- en restoreconfigopties.

De Duo-testoverlay hergebruikt de bestaande HIL-CIC-supplyfixture en observaties,
met marker `control-regression-v1`. Productieguardtijden blijven intact;
`Power House demand rise time` wordt tijdelijk op de normale minimuminstelling
van 2 minuten gezet. Tijdens linkherstel zijn CM1 en tijdelijke CM4 toegestaan zolang herstel nog
niet bevestigd is; directe CM4→CM2 is geldig. Na bevestigde CM2 worden terugval,
ketelactivatie en verlies van feedback gedurende 30 seconden afgewezen.
Een assertionfase kan tot tien minuten wachten; positieve
uitgangstoestanden worden daarna 30 seconden bewaakt. De API-vraag wordt serieel
ververst, zonder achtergrondtimer, en de geselecteerde inputs worden gecontroleerd.

Gemeenschappelijke precondities: Setup Complete en OpenQuatt Enabled staan aan, de aanvullende
warmtebron is aangesloten, run extension staat uit en response-/simulatiegates
staan aan. Duo vereist beide ODU's op adres 1/2 met V1.5/V2-flowondersteuning;
Mono vereist dit alleen voor HP1 op adres 1. Communications stelt eerst Force CM0 in en wacht op
bevestigde CM0 en gestopte ODU’s vóór transportselectie. De desktopketel gebruikt OpenTherm;
de voorbereiding selecteert dit transport en vereist een beschikbare link zonder
connection mismatch voordat de vraag wordt ingeschakeld.
Timeout-, exception-, reboot-, UART-fault- en frequency-freeze-injecties moeten
uit staan. De setup verifieert veilige CM0, supplyfixture en geldige flowtelemetrie;
0 l/h is normaal bij stilstand. Tijdens CM2 vereisen de verwarmingscases minstens
250 l/h geselecteerde flow en bevestigde compressorfeedback. Dit bewijst geen fysieke PT1000 of hydrauliek.

Schema-4 domeinsnapshots bevatten ook Boiler connection, Q Flow Source, ketelassist-/fallbackkeuzes,
responsegates en simulatorwaterinvoer. Een verloren ACK of interrupt herstelt
alle betrokken gates; een herstelprobleem blijft een fout. Recovery zet de
responses terug aan vóór stopbevestiging en OTA. Oude schema-3 snapshots van
input-/performance-runs en eerdere domeinruns blijven geldig. Eerdere schema-3
domeinruns wijzigden Boiler connection niet; hun herstel laat dit veld ongemoeid.
Schema-4 snapshots zonder de oorspronkelijke transportkeuze worden afgewezen.
Oude schema-3/4 snapshots zonder beide manual-frequencyvelden blijven herstelbaar;
alleen vastgelegde waarden worden hersteld. Een gedeeltelijk frequentiepaar faalt.

Runrapporten bevatten begrensde, geselecteerde incidentobservaties, zonder
action-CSRF-token. De cases worden ook zonder hardware met fakes getest;
een fake-PASS of configvalidatie is geen hardware-PASS.

## Defrostregressie

`run-defrost.mjs` gebruikt hetzelfde herstelcontract en een afzonderlijk
`defrost`-snapshotdomein. De controller moet de defrost-API uit PR #742 of de
samengevoegde opvolger bevatten. De scenario-PR importeert die productiecode niet;
voeg de tests pas samen nadat deze afhankelijkheid beschikbaar is.

| Stage | Duurzaam contract |
|---|---|
| `flow` | Exact 249 l/h geselecteerde flow weigert de aanvraag zonder cyclusstart; exact 250 l/h accepteert één aanvraag en voltooit die cyclus. |
| `overlap` | Actieve, verse HP2-defrost blokkeert HP1 met `PEER_DEFROST_ACTIVE`; geen cyclusstart. |
| `cycle` | Eén aanvraag bereikt mode4/defrostbit, vierwegklep en bodemplaatverwarming, daarna mode2/bit clear en `COMPLETE`; started/completed stijgen exact één en aborted niet. |
| `all` | Alle vier contracten; acceptatie bij 250 en completion delen één cyclus. |

```bash
node scripts/hil/run-defrost.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/defrost_regression_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all --apply
```

Voor OTA of scenario-instellingen controleert de runner read-only de geïnstalleerde
simulatormarker `ODU Defrost Contract=manual-defrost-v1`, de tellers
`started`, `completed`, `aborted` in `ODU 1 defrost diagnostics`, beide controller-
statusendpoints en uitgeschakelde injectiebits. Een ontbrekende fixture is
**BLOCKED coverage**, geen hardware-PASS. Ook `--stage smoke` doet deze controle.
Een bestaande simulatorversie of broncode alleen bewijst geen geïnstalleerde
capability. Een controller zonder de API uit #742 voldoet evenmin.

De afzonderlijke overlay behoudt `openquatt-test`, de echte guard en het minimum
250 l/h. Alleen de geselecteerde-flowmeting kan expliciet op 249/250 gezet worden;
de rest van de productie-state-machine en timers blijft intact. De fixture start
altijd uit, heeft geen persistente enable en wordt ook bij fouten uitgezet. Dit
bewijst de grens van de guard, geen fysieke flowmeter of hydrauliek. Een HTTP-ACK
bewijst geen cyclus: controllerreadbacks en simulatortellers zijn vereist. De
versnelde 30s fixture bewijst geen echte ODU-algoritmen of achtminutentijdschalen.

Een trigger krijgt één same-origin POST met een vers CSRF-token; bij een verloren
ACK volgt geen retry. Tokens komen niet in observaties. Foutobservaties blijven in
het rapport. Cleanup probeert onafhankelijk zowel de peer-injectie als de
flowfixture uit te zetten en terug te lezen. Daarna wacht herstel begrensd op
CM0 en bevestigde stilstand; defrostownership en gewone minimumlooptijd mogen
CM0 tijdelijk uitstellen. De fysieke stopgate geldt ook bij afzonderlijk
`--restore-snapshot`-herstel vóór OTA. Mislukt dat, dan blijven firmwareherstel
fail-closed en de recoverylock behouden. Tellers en inactieve defrost blijven
ook tijdens verwarmingsherstel, de 30s hold en de laatste stopcontrole bewaakt.

## Ketelassist en permissies

`run-boiler.mjs` gebruikt `boiler-regression-v1` en een afzonderlijk schema-4
`boiler`-snapshotdomein. Het selecteert pas na gezonde CM0/stilstand OpenTherm.
De simulator moet Responses enabled en Automatic boiler model aan hebben;
Manual telemetry, DHW demand en Fault indication moeten uit staan. Preflight
controleert de bron-defined entiteiten en tellers vóór instellingen of OTA.

| Stage | Contract |
|---|---|
| `assist` | Duurzaam Power House-tekort bij 20000 W vraag promoveert CM2 naar CM3; na verlaging naar 4000 W en gewist tekort volgt CM2 met ketel uit. |
| `permissions` | Assist uit voorkomt ketelactivatie gedurende 450s gezond bedrijf met tekort; fallback uit voorkomt CM4/keteluitgang na causale stop-onzekerheid bij volledige ODU-linkuitval. |
| `all` | Alle vier cases; snapshot, response gates en permissies herstellen ook bij verloren ACK of interrupt. |

```bash
node scripts/hil/run-boiler.mjs \
  --controller http://openquatt-test.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt-test.local \
  --test-config configs/hil/boiler_regression_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_hil.yaml \
  --stage all --apply
```

Geen guardtimer wordt ingekort: 120s minimum CM2 plus 300s promote; 300s minimum
CM3 plus 120s demote. Het tekort moet werkelijk boven de bestaande ON-drempel
liggen en bij handback onder de OFF-drempel komen. De tests veranderen geen
threshold of rated boiler power om een PASS te maken. Reële flow en bevestigde
compressorfeedback blijven verplicht tijdens CM2/CM3.

Een controllercommando alleen volstaat niet. De simulator moet verse ontvangen
Master CH Enable, Simulated CH active en geldige masterstatus tonen, met een
nieuwe rising edge en doorlopende OpenTherm request count. Counterreset, stale
peer, boilerfallback, onverwachte uitgang of verloren compressorfeedback falen.
Tijdens verboden ketelbedrijf en handback moet de rising-edge-counter gelijk
blijven, zodat ook een korte CH-puls tussen metingen faalt. Een gewijzigde
API-vraag mag begrensd op sensorpublicatie wachten; andere inputs blijven geldig.
Bij demotie mag ketelvermogen al uit zijn terwijl de CM3-minimumtijd uitloopt;
CM2-handback moet 30s stabiel zijn met alle keteluitgangen uit. Cleanup probeert
beide permissies onafhankelijk en beide onderdrukte peers via de gedeelde helper.
Ook afzonderlijke recovery wacht vóór OTA op fysieke ODU-stilstand en ingetrokken
controller- én ontvangen keteluitgang. Bij ontbreken van bewijs blijft de lock.

Dit test het OpenTherm-controlcontract op desktop-HIL, geen echte ketel of
hydrauliek. Host-fakes bewijzen scenario/assertiegedrag; de vier nieuwe cases
vereisen afzonderlijk fysiek bewijs op deze overlay.

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

### Mono-communicatie

Gebruik voor Mono de aparte runner en firmware-identiteit:

```bash
node scripts/hil/run-communications-mono.mjs \
  --controller http://openquatt-test.local --simulator http://192.168.2.63 \
  --device openquatt-test.local \
  --test-config configs/hil/control_regression_mono_wifi.yaml \
  --restore-config configs/heatpump_controller_q/single_hil.yaml \
  --stage all --apply
```

De test vereist precies één geconfigureerde warmtepomp in de incident-API en
controleert alleen HP1-flow en het actieve HP1-profiel. Een Duo-snapshot faalt
vóór de scenario-instellingen veranderen. Alleen de HP1-response gate wordt
onderdrukt en in de scenario-cleanup hersteld. De vaste labsimulator heeft nog
steeds twee ODU-slots; de algemene snapshot/restore bewaart beide slots zodat
ook de oorspronkelijke labinstellingen gecontroleerd terugkomen. Herstel een
Mono-run met dezelfde Mono-runner (`--restore-snapshot`); een snapshot uit een
ander domein wordt geweigerd. Het restore-config moet exact overeenkomen met
de vooraf vastgelegde baselinefirmware, ook als de baseline een andere topologie
heeft. De runners compileren en toetsen dat vóór de testupload.

Ook de Mono-cases zijn voorlopig uitsluitend met fakes gevalideerd. Voer geen
nieuwe run of OTA uit terwijl een andere HIL-test het lab gebruikt.
