# Q NVS vergroten via OTA

De Q-firmware vergroot de bekende NVS-indeling van 24 naar 64 KiB bij boot.
Dit geeft 1.260 extra entries voor instellingen. Entitynamen, hun bestaande
opslagformaat, de twee applicatieslots en `openquatt_data` veranderen niet.
ESPHome core wordt niet aangepast; de eigen component gebruikt ESP-IDF-API's.

## Eén gewone firmware-update

HTTP OTA en native OTA downloaden alleen de applicatie. De nieuwe applicatie
controleert vóór ESPHome preferences of migratie nodig is. Bij een bekende
64 KiB-tabel start de applicatie zonder migratie-writes. Bij de bekende oude
tabel verplaatst en verifieert hij OTA-/PHY-metadata, bewaart de bestaande NVS,
maakt de uitbreiding leeg, schrijft de nieuwe tabel en herstart.

Gebruikers hoeven geen tussenrelease of losse partitietabel te installeren.
Latere releases behouden de component, zodat ook gebruikers die deze eerste
release overslaan rechtstreeks kunnen updaten. De nieuwe CSV wordt gebruikt
voor verse installaties; deze plaatsen meteen de 64 KiB-indeling.

De wijziging geldt alleen voor ESP32-S3 met 16 MiB flash, zonder secure boot
of flash-encryptie, en deze exacte ongeëncrypteerde tabellen:

| Partitie | Eerder: offset / grootte | Nieuw: offset / grootte |
| --- | --- | --- |
| `nvs` | `0x9000 / 0x6000` | `0x9000 / 0x10000` |
| `otadata` | `0xF000 / 0x2000` | `0x19000 / 0x2000` |
| `phy_init` | `0x11000 / 0x1000` | `0x1B000 / 0x1000` |
| `app0` | `0x20000 / 0x500000` | ongewijzigd |
| `app1` | `0x520000 / 0x500000` | ongewijzigd |
| `openquatt_data` | `0xA20000 / 0x5E0000` | ongewijzigd |

De journals staan in de ongebruikte sectoren `0x1C000` en `0x1D000`. Ze blijven
buiten NVS en krijgen geen ESPHome entities. CRC en readback controleren
metadata, NVS, de herstelkopie en elke afrondingswrite. Tijdens de migratie
zijn WiFi, preferences en de regeling nog niet gestart. Flash-readloops
yielden begrensd zodat de idle-task ook bij volledige Q-images kan draaien.
Een tijdelijke interne scratchbuffer van 4.096 bytes wordt vóór ESPHome vrijgegeven.
Daarnaast kan het SDK-beheer van zestien in plaats van zes NVS-pagina's de
runtime-RAM veranderen; de HCQ-geheugenmeting hieronder blijft noodzakelijk.

## Herstel en beperkingen

Bij starten vanuit `app1` kopieert de migratie de geverifieerde nieuwe image
naar de inactieve `app0`, voordat oude OTA-metadata wordt gewist. Die kopie
vervangt de vorige firmware. De image wordt VALID verklaard vóór volledige
applicatiegezondheid: automatische rollback naar de vorige versie is tijdens
deze eerste migratie niet beschikbaar. Normale boot-good blijft apart getest.

De primaire partitietabelwrite is niet atomair. Een onderbreking op dat moment
kan USB-herstel noodzakelijk maken. Dit risico is bewust geaccepteerd; een
fysieke stroomuitvalproef is geen voorwaarde voor deze PR. De hosttests met
onderbrekingen blijven behouden en claimen geen volledige power-loss recovery.

Onbekende tabellen, vreemde/beschadigde journals, gewijzigde NVS tijdens een
onvoltooide transactie, imagefouten of NVS-initialisatiefouten stoppen vóór
ESPHome. WiFi, OTA en de web-factory-reset starten dan niet; USB-herstel is
nodig. Voor fysieke tests wordt vooraf een gecontroleerde flashbackup gemaakt.

`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED` is nodig voor de tabelsector en
blijft als globale SDK-optie ook na startup actief. De migratieadapter heeft
een eigen adresallowlist; die geldt niet voor andere raw-flashgebruikers.
Bootloader, oorspronkelijke NVS, actieve app en `openquatt_data` vallen buiten
de schrijfallowlist. Dit blijft een expliciet reviewpunt.

Een downgrade via app-only OTA behoudt de grotere tabel. Een oude USB-
factoryflash kan zijn kleine tabel terugschrijven en is geen veilige manier
om de vergroting terug te draaien. Instellingcompatibiliteit tussen releases
blijft bovendien een afzonderlijk onderwerp.

## Validatie en HCQ-vervolg

Op de 16 MiB Waveshare zijn volledige Q-labimages via native én HTTP OTA vanaf
24 KiB getest, vanuit beide OTA-slots, inclusief een rechtstreeks overgeslagen
tussenrelease. Instellingen en 64 KiB bleven behouden; vervolgboots en updates
hadden nul migratie-writes. Gewone native app-OTA-downgrades naar herbouwde
v0.45.2 (ESPHome 2026.7.0/IDF 5.5.4) en v0.49.1 (2026.8.2/5.5.5), gevolgd door
terugkeer, behielden beide stookcurvewaarden en het regelprofiel. WiFi werkte.
Dit betreft oude bron met labaanpassingen, niet de oorspronkelijke release-assets.

De productintegratie wijkt af van die labbuilds: de MAC-gate is vervangen door
chip/layout/security-checks en flash-readloops yielden nu ook. Deze integratie
is nog niet op de HCQ-controller geflasht. Er komt geen Waveshare-buildprofiel.
De PR-builds met het label `test-firmware` leveren de bestaande Q-targets.

Vóór merge worden op de geïdentificeerde HCQ-testcontroller gecontroleerd:

- actuele flashgrootte/layout, herstelbackup en naam `openquatt-test`;
- de PR-image via HTTP OTA vanaf 24 KiB, vanuit beide slots;
- werkelijke NVS-stats, geselecteerde instellingen, verse boot-good, normale
  herstart en vervolgupdate met nul migratiewrites;
- interne heap current/minimum/largest block, PSRAM en stackwatermarks bij
  realistische gecombineerde HIL-/web-/API-/netwerkbelasting;
- compressor-minimum-off en restart-handoff na de extra migratieherstart.
  Een vroeg RTC-ticket kan daarbij conservatief vervallen;
- normale HCQ-simulatorcommunicatie en regeling.

De eerdere Waveshare-USB-debuguitval na een extra softwareherstart op `app1`
is nog niet verklaard. De geslaagde downgrade-rondjes lossen dat open punt niet op.

Reproduceerbare lokale checks:

```sh
python3 -m unittest scripts.tests.test_nvs_partition_migration
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml
python3 scripts/check_nvs_budget.py configs/heatpump_controller_q/duo.yaml
npm run check:cpp-format
```
