# HIL-plan: HTTP-status en geweigerde mutaties

Dit plan bewaakt het blijvende HTTP-contract van onze eigen handlers. ESPHome
2026.9.0 en 2026.10.0b4 vertalen `send(202/403/410/429/503/504)` naar HTTP 500.
`OpenQuattHttpStatus.h` verzendt deze antwoorden rechtstreeks via ESP-IDF, zonder
responseobject of bodykopie op de heap. Het wijzigt geen authenticatie-, CSRF-,
recovery-, actuator- of opslagbeslissing. De bestaande core-overrides en reeds
rechtstreeks verzonden/chunked antwoorden vallen buiten deze fix.

## Voorbereiding

Gebruik het [gedeelde HIL-veiligheidscontract](hil-testing.md), de exclusieve
`scripts/hil/session.mjs`-lock en de HCQ-desktopopstelling met uitsluitend
gesimuleerde ODU- en OpenTherm-peers. Leg controller- en simulatoridentiteit,
exacte commits, ESPHome-versie, config-hash en SHA-256 van BIN/ELF vast. Bewaar
rapporten en lokale credentials alleen in genegeerde `.tmp/hil/<run>/`.

- Verifieer de testcontrolleridentiteit `openquatt-test` en simulatorcontract
  `openquatt-modbus-opentherm-v2` voor iedere mutatie/OTA.
- Begin met een verse simulatorboot. Forceer en verifieer CM0, schakel
  API-heating/cooling, manual cooling en MQTT uit en controleer beide gevraagde,
  toegepaste en door de simulator ontvangen compressorcommando's op nul.
- Leg relevante instellingen vast vóór de eerste wijziging. Laat actieve
  foutinjectie altijd uit en controleer de veilige eindtoestand ook bij falen.
- Eén firmwarebuild tegelijk, maximaal twee compilerjobs. Gebruik
  `configs/heatpump_controller_q/duo_hil.yaml` voor de testcontroller; dev gebruikt
  momenteel ESPHome 2026.9.0. Een b4-resultaat vereist afzonderlijk de migratiecode;
  meng de twee baselines niet in één voor/na-geheugenclaim.
- Minimaal 1.500 ms tussen POSTs; alle verzoeken serieel, één gewone dashboard-
  of statusread per meetmoment. Geen burst- of druktest.

Lokale checks:

```bash
npm run check:python
npm run check:cpp-format
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml
```

De `duo_wifi.yaml`-alias bestaat op actuele dev niet meer; `duo.yaml` is het
normale Q Duo WiFi-profiel. Firmwarebouw en OTA vinden alleen plaats na
reservering van de gezamenlijke build- en HIL-queue.

## Cases en bewijs

1. **Normaal verkeer:** controleer `/mqtt/status`, `/openquatt/logs/recent`,
   `/openquatt/learning/status` en `/energy/history`. Verwacht HTTP 200, volledig
   leesbare JSON waar de route JSON aanbiedt. `/energy/history` gebruikt het
   tekstprotocol `@schema|3`; valideer daar metadata en afsluitende newline.
   Chunked antwoorden moeten netjes eindigen en hun framing moet geldig zijn.
   Authenticeer met de bestaande lokale Digest-login, zonder credentials of
   CSRF-tokens in rapporten op te nemen.
2. **Ontbrekende/onjuiste CSRF:** stuur één lege POST naar elk van `/mqtt/save`,
   `/mqtt/input/save`, `/mqtt/input/retained/save`,
   `/openquatt/debug-recording/start` en `/openquatt/logs/clear`. Herhaal op één
   MQTT-route met een onjuiste token. Verwacht steeds HTTP **403**,
   `application/json`, `{"ok":false,"error":"forbidden"}`, een exact passende
   Content-Length, `Accept-Ranges: none`, `Access-Control-Allow-Origin: *` en
   `Cache-Control: no-store`. Een HTTP 500 is FAIL, ook als de JSON weigering klopt.
3. **Cross-origin:** gebruik eenmaal een geldige MQTT-CSRF-token met een
   afwijkende `Origin` bij `/mqtt/save`. Verwacht dezelfde 403. Vergelijk voor en
   na iedere MQTT-weigering broker/port, enabled, credential-aanwezigheid en alle
   input-policyvelden; een geweigerde request mag geen configuratie wijzigen.
   Recorder-start mag geen start veroorzaken; vergelijk de beschikbare stabiele
   status/counters. Controleer log-clear met fingerprints van oude entries
   (`seq`, `ts`, `level`, `tag`, `message`): een overlap van alleen `seq` bewijst
   geen inhoudsbehoud, omdat clear de sequenceteller kan terugzetten. Houd
   normale nieuwe logregels en ringbufferrotatie apart van een clear.
4. **Authenticatie:** een request zonder login moet nog steeds een 401 met
   Digest-challenge krijgen; een foutieve login mag geen mutatie uitvoeren.
   Gebruik een verse, niet-geauthenticeerde sessie om dit apart vast te stellen.
5. **Herbruikbare verbinding:** verstuur een geweigerde POST gevolgd door een
   gewone GET via dezelfde Digest-sessie. De GET moet een volledige, losse 200
   opleveren zonder overgebleven bodybytes of framingfout. Test daarnaast een
   normale clientdisconnect; de volgende nieuwe verbinding moet blijven werken.
6. **Geheugen/continuïteit:** vergelijk identieke baseline/kandidaatprofielen
   vanaf verse boot met dezelfde normale HA-, web-, API- en busbelasting. Leg
   actuele én minimale interne heap, grootste vrije block, fragmentatie, vrije
   PSRAM en loop-/HTTPD-stackwatermarks vast. Een cumulatief minimum is geen
   actuele vrije heap. Geen reset/stackfout, onbehandelde allocatiefout of
   onverklaarde regressie toestaan; ontbrekende stacktelemetrie expliciet noteren.
7. **Eindtoestand:** verifieer opnieuw CM0, uitgeschakelde grants/MQTT/manual
   cooling, nul compressorcommando's en uitgeschakelde simulatorfoutinjectie.
   Registreer firmware-identiteit en eindinstellingen en geef pas daarna de lock vrij.

Controleer ook een same-origin POST naar `/openquatt/debug-recording/start`
met geldige recorder-CSRF maar zonder `rolling=1`: verwacht HTTP **410**,
`{"ok":false,"error":"timed_recording_removed"}` en ongewijzigde recorderstatus.
Deze route weigert vóór enige recorderactie.

202, 429, 503 en 504 zijn niet allemaal deterministisch via normale
bediening op te roepen. De hosttest gebruikt de echte helper met een IDF-stub
voor alle zes wire-statusregels, lege/non-empty bodies en foutinjectie bij
status/type/ieder header/send. Het falende pad sluit de verbinding zonder
retry of vervolgactie. Een eerder ingediende mutatie kan al lopen of zijn
opgeslagen; een afgebroken response bewijst geen rollback. Dit is geen
HIL-bewijs voor echte PSRAM- of persistentiefouten:
claim die dekking alleen na gerichte, afzonderlijk beoordeelde firmwarefaults.
Geen kunstmatige requeststroom gebruiken om 429 of allocatiefalen te forceren.

## Rapportage

Rapporteer afzonderlijk functionele HTTP-resultaten, geweigerde mutaties,
authenticatie, framing en geheugen. Vermeld per niet-uitgevoerde case waarom
hij openstaat. Een statusfix-PASS is geen vrijgave van de ESPHome-upgrade of
bewijs dat de eerder gemeten lage HTTPD-stackmarge is opgelost.
