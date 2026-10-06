# ODU-referentiedata

Iedere fixture bevat 512 EEPROM-woorden en de voor generatieherkenning gebruikte ruwe identity-blokken. Bronformaat: `openquatt-odu-eeprom-v1`, schema 1, `runtime_eeprom_shadow`. Serienummers en niet gebruikte extended identity-velden zijn weggelaten. De oorspronkelijke uploads zijn niet gewijzigd.

| Fixture | Capturetijd volgens export (UTC) | CRC baseline | Verwerking |
|---|---|---|---|
| `v1_eeprom.h` | 2026-08-17 19:32:35 | `0x2A5D` | Cooling F1-F10 hersteld; overige woorden en opgeslagen CRC ongewijzigd |
| `v1_5_eeprom.h` | 2026-10-06 12:33:47 | `0xB191` | Alle EEPROM-woorden ongewijzigd |
| `v2_old_amh6_eeprom.h` | 2026-10-06 11:35:30 | `0xC618` | Bestaande fixture, alle EEPROM-woorden ongewijzigd |
| `v2_new_amh6_eeprom.h` | 2026-08-17 14:25:10 | `0xDECD` | Alle EEPROM-woorden ongewijzigd |

Alle dumps waren volledig en alle high-bytes nul. De fixtures bewaren sheet 3000..3511 / Modbus 2999..3510, inclusief de twee originele CRC-woorden. CRC16/Modbus over de low-bytes van sheet 3000..3509 komt voor iedere baseline overeen met de opgeslagen CRC.

## V1-baselinecorrectie

Bron: `eeprom_V1(2).json`. De oorspronkelijke runtime-shadow had berekende CRC `0x5013`, opgeslagen CRC `0x2A5D`, één retry en de waarschuwingen `extended_metadata_unavailable` en `crc_mismatch`. De extended metadata was niet beschikbaar; de V1-test gebruikt alleen de core-identiteit.

De bodemplaatwaarden waren al `mode 1 / 4 °C / 3 K` (Modbus 3236..3238: `1,34,3`). Die zijn niet aangepast. De afwijking zat in de koelfrequentietabel:

| Cooling F0-F10 | Frequenties (Hz) |
|---|---|
| Aangeleverde runtime-shadow | `0,20,22,24,26,28,30,30,30,30,30` |
| Herstelde referentietabel | `0,30,36,42,47,52,56,61,66,71,74` |

Alleen Modbus 3001..3010 (F1-F10) is hersteld naar de al bekende V1/V1.5-referentietabel. Daarmee wordt de berekende CRC exact `0x2A5D`. De opgeslagen CRC is niet opnieuw berekend of vervangen om een match te forceren. `CAPTURED_COOLING_WORDS` bewaart de oorspronkelijke tabel; de test reconstrueert daarmee alle 512 originele runtime-woorden en controleert opnieuw de mismatch `0x5013` tegenover `0x2A5D`.

## Aanvullende captures en duplicaten

- V1.5-baseline: `openquatt-hp1-odu-eeprom-2026-10-06T12-33-57-345Z.json`, zonder waarschuwingen of retries. De aanvullende `eeprom_V1-5(2).json` (2026-08-26 07:15:58 UTC) heeft alleen acht gewijzigde cooling-woorden: F0-F10 `0,26,28,30,32,34,36,38,40,71,74`. De berekende CRC is `0x9DD5`, opgeslagen CRC `0xB191`, met één retry en `runtime_shadow_differs_from_stored_eeprom`. De test bewaart deze tabel als tweede reproduceerbare runtime-shadow, naast de ongewijzigde baseline.
- V2-old: `Pasted text(20261006-113659).txt`, zonder waarschuwingen of retries. De latere aanlevering `eeprom_V2_oud model(4).json` heeft exact dezelfde 512 EEPROM-woorden; die krijgt geen dubbele fixture.
- V2-new: `eeprom_V2_nieuw model(2).json`, zonder waarschuwingen of retries. `eeprom_V2(2).json` is inhoudelijk hetzelfde exportbestand en krijgt geen dubbele fixture.

## Gedeelde regressietest

`oq_odu_eeprom_reference_test.cpp` gebruikt één testopzet voor de vier varianten: CRC, generatieherkenning, ontbrekende customer metadata versus AMH6, fysieke frequentietabellen, model-/physical-levelmapping, F10/F20-begrenzing, bodemplaat en defrostdiagnostiek. V1 mag niet de niet-nulle woorden op het V2-extension-adres als F11-F20 verwerken. De twee aangepaste runtime-koeltabellen blijven geldige parsertestgevallen, inclusief hun oorspronkelijke CRC-mismatch.

De mode-4-drempels worden afzonderlijk bewaakt voor V1.5 (`12,11,11,10,9,7,6 K`), V2-old (`12,11,10,10,9,6,5 K`) en V2-new (`8,11,9,9,8,7,6 K`). V1/V1.5 zijn in modus 0 vastgelegd; voor V1.5 test een expliciete moduswijziging daarnaast de eigen mode-4-tabel. Dat is geen wijziging van de baseline. V1 levert geen mode-4-diagnostiek.

De bestaande hosttestrunner neemt de test automatisch mee:

```sh
./scripts/run_host_regression_tests.sh
```

Dit zijn referenties van afzonderlijke units op afzonderlijke meetmomenten, geen universele fabriekstabellen. De firmware blijft parameters van de aangesloten ODU lezen. De defrosttests bewaken de huidige diagnostische interpretatie; ze voorspellen geen autonome ODU-defroststart en bewijzen geen onbekende tijdseenheden. De betekenis en eenheid van de minimumflowwaarde blijven buiten deze tests en deze wijziging.
