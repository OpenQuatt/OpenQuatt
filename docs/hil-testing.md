# Hardware-in-the-loop-tests

OpenQuatt gebruikt HIL-tests voor wijzigingen waarbij hosttests alleen niet
bewijzen dat controller, ODU, OpenTherm en timing samen veilig blijven. Een
volledige HIL-run is bedoeld voor gebundelde control-wijzigingen en
releasekandidaten, niet voor iedere kleine pull request.

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
- tijdens compileren en terugplaatsen van normale firmware blijft de controller
  geforceerd in CM0; de oorspronkelijke override wordt pas na profielcontrole
  teruggezet;
- de testfirmware moet het verwachte `HIL Test Profile` publiceren;
- een muterende run vereist een normale restoreconfig en OTA-adres;
- snapshots en rapporten blijven onder het door Git genegeerde `.tmp/hil/`.

De runner bevat geen IP-adressen, wifi-gegevens, wachtwoorden of lokale
instellingenback-ups. Gebruik hem alleen wanneer bedrading, hydrauliek en de
simulator volgens de testerhandleiding veilig zijn aangesloten.

## Read-only rooktest

Controleer bereikbaarheid, firmware, heap en ODU-protocoldiagnostiek zonder
iets te wijzigen:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt.local \
  --simulator http://SIMULATOR-IP \
  --stage smoke
```

Het rapport bevat actuele en minimale interne heap, grootste vrije block,
fragmentatie, vrije PSRAM en beide ODU-diagnoseregels voor zover de entities
beschikbaar zijn.

## Volledige input-/bronselectietest

De testconfig is uitsluitend voor HIL en wordt niet als releaseprofiel gebouwd.
Hij gebruikt de normale Q Duo WiFi-firmware, met alleen deze kortere testtijden:

| Contract | Productie | HIL |
|---|---:|---:|
| API-input stale | 0/600/900/1.800 s | 30 s |
| Geselecteerde-input hold | 300 s | 10 s |
| HP minimum-off | 240 s | 10 s |

Start de gebundelde run met:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt.local \
  --test-config configs/hil/input_sources_fast_duo_wifi.yaml \
  --restore-config configs/heatpump_controller_q/duo_wifi.yaml \
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

Losse scenario's zijn beschikbaar als `inputs`, `enable-expiry`,
`active-switch` en `reboot-reset`. Ook een losse muterende run herstelt altijd
de normale firmware. De inputtest raakt alle zeven API-inputslots, inclusief
het dauwpunt. Met `--min-heap-min-free` en `--min-largest-block` kan een
vooraf afgesproken profielbudget als harde grens worden meegegeven. Zonder die
opties rapporteert de runner de waarden, maar noemt hij een geheugentest niet
automatisch releaseveilig.

## Afbreken en herstellen

Eén keer `Ctrl-C` vraagt recovery aan. Een actieve compile of OTA wordt eerst
afgerond; REST-wachtlussen stoppen bij hun volgende controle. Een tweede signaal
breekt direct af en kan daarom handmatig herstel nodig maken. Bij onvolledig
herstel blijft `.tmp/hil/input-sources.lock` staan
en toont de runner het snapshotpad. Tegelijk herstel door twee processen wordt
geblokkeerd; een achtergebleven recovery-processlock wordt alleen opgeruimd als
de vastgelegde PID niet meer actief is.

Herstel dan met exact dezelfde doelen:

```bash
node scripts/hil/run-input-sources.mjs \
  --controller http://openquatt.local \
  --simulator http://SIMULATOR-IP \
  --device openquatt.local \
  --restore-config configs/heatpump_controller_q/duo_wifi.yaml \
  --restore-snapshot .tmp/hil/RUN/snapshot.json \
  --apply
```

Gebruik `--settings-only` alleen wanneer normale firmware aantoonbaar al actief
is. De herstelopdracht weigert een snapshot voor andere controller- of
simulator-URL's. In deze modus moeten ook de firmwareversie en config-hash exact
overeenkomen met de identiteit in het snapshot.

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
```

De eerste opdracht test write-gating, requestbegrenzing, CLI-veiligheidsregels,
OTA-commandoconstructie en volledig instellingenherstel met fakes. De tweede
controleert of de testoverlay met de actuele firmwareconfig blijft compileren.
