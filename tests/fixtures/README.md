# ODU-referentiedata

`v2_old_amh6_eeprom.h` bevat de 512 ongewijzigde EEPROM-woorden en de voor generatieherkenning gebruikte ruwe identity-blokken uit een AMH6/V2-old-dump van 6 oktober 2026, 11:35:30 UTC. Bronformaat: `openquatt-odu-eeprom-v1`, schema 1, `runtime_eeprom_shadow`. Het serienummer en overige niet gebruikte identity-velden zijn weggelaten.

De dump was volledig, zonder waarschuwingen of retries. Alle high-bytes waren nul. CRC16/Modbus over de low-bytes van sheet 3000..3509 was `0xC618`, gelijk aan de opgeslagen CRC. De fixture bewaart de originele sheet 3000..3511 / Modbus 2999..3510, inclusief de twee CRC-woorden.

`oq_odu_v2_old_eeprom_test.cpp` gebruikt deze ruwe data voor generatieherkenning, AMH6-parsing, frequentietabellen, compressorstandbegrenzing, bodemplaatinstellingen en defrostdiagnostiek. De bestaande hosttestrunner neemt deze test automatisch mee:

```sh
./scripts/run_host_regression_tests.sh
```

Dit is een referentie van deze unit op dit meetmoment, geen bewijs dat iedere V2-old dezelfde instellingen heeft of dat alle waarden fabrieksdefaults zijn. De firmware blijft parameters van de aangesloten ODU lezen. De defrosttests bewaken de huidige diagnostische interpretatie; ze voorspellen geen autonome ODU-defroststart. De betekenis en eenheid van de minimumflowwaarde blijven buiten deze tests en deze wijziging.
