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

De productintegratie gebruikt chip/layout/security-checks en begrensde
flash-readloops. Op 15 oktober 2026 is deze integratie op de geïdentificeerde
HCQ-testcontroller getest met de bestaande Q Duo-release van PR #847:
`v0.55.0-pr.847.2867+473afc3`, broncommit `473afc3`, OTA-image 2.484.128 bytes,
SHA-256 `252ad59a1c44564d6c633ee21f0cd4a7471e58a04830ee121d9eeb50f41eb013`.
Er is geen Waveshare-buildprofiel of speciale HIL-firmware toegevoegd.

De controller downloadde manifest en applicatie zelf via HTTPS van GitHub.
USB werd gebruikt voor de volledige gecontroleerde 16 MiB-herstelbackup,
testfixtures en vroege bootlogs. De oorspronkelijke indeling was 24 KiB.
Voor de tweede route is dezelfde eigen backup teruggezet, met de bewezen
oude applicatie ook in `app1` en gecontroleerde OTA-selectie daarvan.
De volledige fixture is op het apparaat met een digest en prefix-readback
geverifieerd. Dit is testvoorbereiding, geen onderbroken-OTA-herstelproef.

| HCQ-proef | Resultaat |
| --- | --- |
| Oude `app0` → HTTP OTA → nieuwe `app1` | Migratie: 9.757 writes / 623 gewiste sectoren; daarna 0 / 0 |
| Oude `app1` → HTTP OTA → nieuwe `app0` | Migratie: 53 writes / 16 gewiste sectoren; daarna 0 / 0 |
| Gewone herstart op 64 KiB | Verse boot-good, 0 migratiewrites / 0 gewiste sectoren |
| Vervolg-HTTP-OTA met dezelfde GitHub-image naar het andere slot | Verse boot-good, 0 / 0 |
| Drie persistente testwaarden | 45,5 °C, 66,5 °C en profiel `Comfort` behouden |
| Werkelijk NVS-gebruik na migratie | 500 gebruikt, 1.516 vrij, 1.390 beschikbaar, 2.016 totaal |
| Gewone restart-handoff na migratie | 219 / 220 s off-credit voor HP1 / HP2 teruggelezen |

De nieuwe tabel en beide applicatie-images zijn na de eerste route ook via
USB teruggelezen/gecontroleerd. Voor deze proef was de standaardhostnaam
`openquatt` expliciet toegestaan; de test gebruikte het gecontroleerde vaste
IP en apparaatidentiteit. Normale API-warmtevraag leidde tot bevestigde
compressorwerking van HP1 en daarna een waargenomen Duo-overname door HP2.

De startup-guard is afzonderlijk getest met een kleine USB-metadatafixture:
alleen `0x8000..0x20000` is uit de oorspronkelijke backup hersteld, vóór de
ongewijzigde, eerder via GitHub HTTP OTA verkregen kandidaat op `app0`.
Bootloader, applicaties en `openquatt_data` zijn daarbij niet herschreven.
Na de nieuwe migratie en extra herstart vroeg de normale CM100-bediening
HP2 `Heating` / F3 aan. Dezelfde aanvraag bleef staan tot bevestigde start.
De beslislog registreerde een actieve HP2-startupblokkering met grens 240 s
en maximaal 150 s resterend over beide HP's. In 27 samples bleef HP2 werkelijk
stil. Vrijgave en `source_start` volgden op uptime 268 s; `hp_start_confirmed`
op 277 s. Dit bewijst de startupblokkering en daaropvolgende start, geen exacte
fysieke 240-secondenmeting. De fixture is geen extra HTTP-OTA-proef.
De service is daarna beëindigd; beide HP's stonden bevestigd stil, CM0 was
actief, API-warmtevraag stond uit en tijdelijke frequentie-exclusies waren hersteld.
Tijdens dezelfde boot groeiden de OpenTherm-tellers zonder nieuwe invalid/
timeoutframes; beide ODU-links waren gezond en hun verzoekentellers groeiden.

Vóór merge blijft de gematchte geheugenvergelijking open. Tijdens gemeten
HIL-/web-/API-belasting was de interne minimumheap 39.828 bytes en het grootste
vrije blok 31.744–45.056 bytes. Deze waarden bewijzen nog geen piekmarge.
De eerdere v0.55.0-baseline bevatte andere HIL-aanpassingen en is daarom geen
gematchte vergelijking. Current/minimum/largest block, PSRAM en relevante
stackwatermarks moeten met dezelfde bron/configuratie worden vergeleken bij
gecombineerde piekbelasting, inclusief de benodigde netwerkclients.

De eerdere Waveshare-USB-debuguitval na een extra softwareherstart op `app1`
is nog niet verklaard. De geslaagde downgrade-rondjes lossen dat open punt niet op.

Reproduceerbare lokale checks:

```sh
python3 -m unittest scripts.tests.test_nvs_partition_migration
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml
python3 scripts/check_nvs_budget.py configs/heatpump_controller_q/duo.yaml
npm run check:cpp-format
```
