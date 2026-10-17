# EEPROM opslag en hardwarevrijgave

De EEPROM-service bewaart bodemplaatinstellingen, de ontdooimethode en beide
frequentietabellen via een afzonderlijke ODU-commit. De
[gebruikersprocedure](../odu-eeprom.md) beschrijft de UI. De eerste implementatie
is experimenteel voor V2 `0x1037` / `0x0201`; V1, V1.5, V2 met andere firmware,
ontbrekend serienummer en onbekende indelingen kunnen niet schrijven.

## Eigenaars en opslag

- `components/openquatt_odu_eeprom/` bezit de HTTP-opdrachten, state machine,
  Modbus-client en het duurzame journaal. HTTP kopieert alleen een gesynchroniseerde
  status of zet een kleine opdracht klaar. Flash- en Modbustransities lopen op
  de ESPHome-hoofdloop. Een download zet tijdelijk een leesreservering op de
  onveranderlijke backup; zij kopieert geen groot record naar interne RAM.
- `oq_odu_eeprom_image.h` bezit bytecodering, CRC, toegestane registergroepen en
  kandidaatvalidatie. Bodemplaat gebruikt `3236–3238`, defrost alleen `3275`,
  frequenties `3000–3021` en de V2-uitbreiding `3050–3069`. In de uitbreiding
  staan eerst verwarmen F11–F20 en daarna koelen F11–F20.
- `oq_odu_eeprom_maintenance.h` deelt een kleine controlblokkering en een exact
  begrensde permit voor het ene uitgaande schrijf-frame. De actuele actuator
  levert per HP verse veiligheidsvoorwaarden aan. Gewone mode-/niveaucommando's,
  profielhertoepassing en concurrerende servicewrites kunnen deze blokkering
  niet passeren. Veiligheidsstops onderbreken de opdracht zonder retry.
- `odu-eeprom-write.js` toont de bestaande runtime-instellingen als voorbereiding,
  een volledige diff, expliciete bevestiging en de afzonderlijke coldcontrole.
  NL en EN gebruiken dezelfde API-waarden. Sluiten van de modal annuleert de
  opdracht niet; heropenen haalt de duurzame status opnieuw op.

De buffer voor twee records, één lees-image en één FC16-frame wordt uitsluitend
in PSRAM gereserveerd. Er is geen fallback naar interne RAM. Bij allocatiefalen
wordt via NVS-keymetadata alsnog vastgesteld of er een journaal bestaat; een
bestaand of onleesbaar journaal houdt de aansturing dan geblokkeerd.

De native NVS-namespace `oq_eeprom` gebruikt per HP twee alternatieve blobs
(`hp1a/hp1b`, `hp2a/hp2b`). Een versie-1-record van 2116 bytes bevat generatie,
fase, geselecteerde groepen, board-/firmware-/seriële identiteit, oorspronkelijke
backup, laatste bevestigde opgeslagen image, bronimage, kandidaat en checksum.
De oorspronkelijke backup blijft behouden na latere wijzigingen.

Schrijven vereist daadwerkelijk minimaal 64 KiB NVS en vóór iedere recordwrite
minimaal 100 beschikbare entries plus ruimte voor het nieuwe blobrecord. Dit
bouwt voort op [PR #847](https://github.com/OpenQuatt/OpenQuatt/pull/847), zonder
zelf partitietabellen te wijzigen. De budgetchecker telt maximaal 139 entries
voor Single en 277 voor Duo, inclusief namespace. De oude 24 KiB-configuratie
kan deze nieuwe records niet aanmaken.

`nvs_set_blob`, `nvs_commit`, opnieuw openen en volledige readback moeten alle
slagen. Een callback-ACK of een preferences-cache geldt niet als duurzame opslag.
Bij een afgebroken write blijft de vorige slotkopie beschikbaar; bij boot wint
de nieuwste generatie uitsluitend als beide slots geldig of leeg zijn. Een
niet-leeg corrupt of onleesbaar slot blokkeert altijd, ook naast een geldige
oudere baseline: het kan een onderbroken schrijfopdracht hebben bevat. Een verse
installatie begint zonder job of automatische write.

De factory-resetcounter heeft setupprioriteit `246`, na beide journaalladers
(`WIFI - 3 = 247`). Een pending, corrupt of onleesbaar journaal schakelt zijn
setup uit voordat de counter kan wissen. De resetknop vereist dat beide
geconfigureerde journaalchecks zijn afgerond en controleert die gate plus de
gedeelde blokkering ook opnieuw na zijn wachttijd. Na veilige afronding is de knop weer
beschikbaar; de bij boot geblokkeerde powercyclecounter wordt pas bij een volgende
veilige boot actief. Een volledige NVS-erase buiten deze firmware of de
initialisatie-/herstelpaden van ESP-IDF kan het journaal niet overleven; die
platformgrens vereist afzonderlijk herstelbewijs en wordt niet als veilige
annulering behandeld.

## Transactie en herstel

1. `BEGIN_BACKUP` reserveert de service voor beide HP's, controleert de exacte
   ODU-identiteit en bewaart `WAIT_BASELINE`. Daarna wordt de automatische
   hertoepassing van beide betrokken profielen duurzaam ingetrokken.
2. `CAPTURE` vereist een sinds de voorbereiding waargenomen disconnect én een
   expliciete bevestiging van een echte spanningsonderbreking. Identiteit,
   compressorstilstand en de volledige image worden opnieuw gelezen. Alleen
   geldige CRC en ondersteunde waarden leveren een duurzame `BASELINE` op. De
   automatische defrostbesturing wordt rechtstreeks uit `3274` (`1`) in de
   verse image gecontroleerd; de optioneel geladen editorcache is geen guard.
3. `PREPARE` bouwt uit die baseline en geselecteerde runtimevelden een kandidaat.
   Onbekende deltas worden geweigerd; bekende niet-geselecteerde deltas worden
   zichtbaar teruggezet. Koel- en verwarmingstabellen blijven binnen hun eigen
   oorspronkelijke minimum-/maximumfrequentie. `RESTORE` bereidt uitsluitend de
   lokaal bewaarde oorspronkelijke backup voor.
4. `COMMIT` is gebonden aan de getoonde generatie. Opnieuw gelezen identiteit en
   volledige bron moeten exact overeenkomen. Een verse `4003` moet vóór enige
   staging-write `0` zijn: een oude `4` kan geen nieuwe voltooiing bewijzen.
   Profielrevocatie en het `WRITING`-record moeten duurzaam bevestigd zijn vóór
   de eerste ODU-write.
5. Eén FC16-overdracht zet alle 512 bytes klaar in `2999–3510`, één byte per
   16-bits register, met hoge byte nul. De batches zijn `123+123+123+123+20`.
   Eerst volgt volledige teruglezing; alleen een exacte kandidaat met geldige
   CRC mag naar de commitfase.
6. De FC06-reeks is achtereenvolgens `3999=5`, `4001=0x0632`, `4002=3`,
   `4000=0x0102`. Daarna wordt `4003` gepolld. `2/6` zijn foutuitkomsten;
   `4` leidt naar volledige teruglezing en `WAIT_VERIFY`. De ODU-schrijfmodus
   wordt niet met een ongedocumenteerd commando verlaten. Gewone aansturing
   blijft geblokkeerd.
7. `VERIFY` vereist opnieuw een sinds de job waargenomen disconnect en de
   gebruikersbevestiging van een echte koude ODU-start. Exacte kandidaat levert
   `VERIFIED` op; exacte vorige baseline levert `NOT_PERSISTED` op. Beide worden
   duurzaam als nieuwe baseline bevestigd vóór vrijgave. Andere inhoud houdt
   de blokkering vast. De UI bewijst geen fysieke stroomonderbreking zelfstandig.

De CRC is CRC16-Modbus met startwaarde `0xFFFF` en polynoom `0xA001`, over
510 laagbytes. Byte 510 bevat CRC-low, byte 511 CRC-high. De fabrikant-CRC
`0xDECD` is geen universele CRC voor aangepaste instellingen.

Een fout na de eerste ODU-write bewaart `UNCERTAIN`. Verloren ACK, recovery,
incident, offlinegang en timeout veroorzaken geen automatische staging- of
commitretry. Bij herstart worden alleen status en blokkering hersteld. Zelfs een
oude duurzaam opgeslagen `WRITING` moet eerst opnieuw worden vastgesteld. Een
ongeldige of verloren slotwrite kan geen oude automatische toestemming herstellen.
Generaties lopen ook na annuleren en opnieuw voorbereiden door.

## Protocolbewijs en open vrijgavecriteria

De commitreeks, 512-byte-overdracht en statusinterpretatie komen uit de eerdere
statische analyse van CiC 4.15.0 `cic-modbus-odu`, met EEPROM-functies bij
`0xa1edc`, `0xa2124` en `0xa2308`. Die analyse is geen bewijs van uitvoering of
persistentie op een fysieke ODU. De strikte idle-0-voorwaarde is een aanvullende
veiligheidseis van deze implementatie en moet op de toegelaten firmware worden
bevestigd.

Voor vrijgave zijn vereist:

- [ ] V2-identiteit en de idle-0-, voltooiings-4- en fout-2/6-semantiek fysiek bevestigen.
- [ ] Betekenis van `4000/4002`, schrijfduur en veilig moment voor stroomonderbreking bevestigen.
- [ ] Eerst originele volledige backup en herstel op een geschikte test-ODU bewijzen.
- [ ] Bodemplaat, defrostmethode en beide frequentietabellen na echte spanningsuitval behouden.
- [ ] Geïnjecteerde onderbrekingen vóór/tijdens staging, commit en beide journalwrites testen.
- [ ] Verloren ACK, stale bron/status, dubbele requests, andere seriële identiteit en Duo-overlap testen.
- [ ] Verse installatie, herstart tijdens elke fase, OTA/recovery en onleesbare/volle NVS controleren.
- [ ] Werkelijke 64 KiB-NVS na #847 bevestigen en capaciteit bij volledige vulling toetsen.
- [ ] Baseline/kandidaat meten op interne vrije heap, minimum sinds boot, grootste blok,
      fragmentatie, PSRAM en relevante stack-watermarks onder gecombineerde belasting.
- [ ] UI op mobiel/desktop, licht/donker en NL/EN, inclusief productie-bundle en focusbehoud controleren.

Deze implementatie vergroot geen firmware-updateprotocol en biedt geen
EEPROM-bestandsupload. Een downgrade naar firmware die het pending journaal
negeert is tijdens een onafgeronde opdracht niet ondersteund. Wis het journaal
niet om een onzekere schrijfopdracht te omzeilen.

## Gerichte controles

```sh
python3 scripts/run_tests.py python --pattern 'test_odu_eeprom_runtime.py'
python3 scripts/run_tests.py python --pattern 'test_nvs_budget.py'
node --test openquatt/web/tests/odu-eeprom-write.test.mjs
npm run check:cpp-format
npm run check:web
npm run smoke:web
npm run check:docs
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml
```

De hostfixture compileert de productie-state-machine met een gescheiden
duurzame NVS-store en Modbus-ODU-shadow. Zij test onder meer geen fysieke opslag
ondanks succesvolle ACK/readback, verloren commit-ACK zonder retry,
herstart/herstel, oude voltooiingsstatus, stale bron, opslagfouten, identiteit en
PSRAM-uitval. Deze tests vervangen geen hardwarevrijgave.
