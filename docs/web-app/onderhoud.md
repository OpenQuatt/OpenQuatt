# Updates en backups

Maak vóór een grotere wijziging een instellingenbackup. Gebruik voor normaal gebruik het stabiele kanaal `main`; `dev` is voor bewust testen.

- [Backup maken en terugzetten](#backup-en-restore)
- [Firmware bijwerken](#updates)
- [Gegevens delen en privacy](privacy.md)

### Zo controleer je dat het gelukt is

Wacht tot de web-app na de update weer bereikbaar is. Controleer de draaiende firmwareversie en het releasekanaal in de firmware-updatefunctie en bekijk of de meetwaarden worden ververst. Lukt dat niet, begin bij [Problemen oplossen](../problemen-oplossen.md).

## Systeemrecorder voor support

Voor het bewaren van gegevens bij een storing zie [Diagnose en logboeken](diagnose.md#systeemrecorder-voor-support).

## Backup en restore

Maak een backup voordat je grotere wijzigingen doet of voordat je een factory-update uitvoert.

De backup bevat de instellingen die de web-app beheert, inclusief de vier warmtepompoffsets en iedere geldige aanvoeroffset die per bron is opgeslagen. De MQTT-configuratie wordt ook meegenomen, maar het MQTT-wachtwoord nooit. Bij restore vergelijkt OpenQuatt de backup met de huidige installatie, zodat je verschillen kunt controleren voordat je ze terugzet.

Externe invoerwaarden die je live aanlevert, zoals een warmtevraag, een aanvoertarget of een kamertemperatuur via MQTT of de API, zijn geen instellingen en gaan niet mee in de backup. De gekozen bron blijft wel bewaard: na een restore staan `Externe warmtevraag (Power House)` en `Aanvoertarget (stooklijn)` weer op dezelfde bron, zonder dat er een verouderde vraag of target wordt teruggezet.

De kalibratiewaarden worden op dezelfde manier als de overige instellingen hersteld, vóór de opgeslagen aanvoerbron wordt geselecteerd. Kalibreer na restore opnieuw als de controller of een temperatuursensor fysiek is vervangen; een gewone bron- of CIC-URL-wijziging verwijdert een geldige kalibratie niet.

Een backup is vooral handig bij:

- nieuwe release testen;
- overstap naar een nieuw bordje;
- factory-bin update;
- terugzetten na experimenteren met instellingen.

### Zo controleer je dat het gelukt is

Controleer dat het backupbestand is gedownload en bewaar het op een herkenbare plek. Controleer na terugzetten de installatiekeuzes en actieve sensorbronnen in de web-app. Vul het MQTT-wachtwoord opnieuw in als je MQTT gebruikt. Bij afwijkingen zie [Bronnen en integraties](bronnen.md).

## Updates

De web-app toont update-informatie via de firmware-updatefunctie. Normaal volg je het stabiele kanaal.

Na het kiezen van `dev` kun je de aangeboden dev-build ook installeren wanneer deze dezelfde basisversie heeft als de draaiende main-release (bijvoorbeeld `v0.49.1` → `v0.49.1-dev.780`). De web-app bevestigt de kanaalwissel pas wanneer het device de bedoelde dev-build en het dev-kanaal meldt.

Gebruik een dev-kanaal alleen als je bewust test en weet dat de firmware nog kan veranderen. Voor releasegebruik is het stabiele kanaal de route.

Draait het device op een nieuwere dev-versie dan de laatste main-release, dan biedt de OTA-modal na het kiezen van `main` een expliciete downgrade aan. Controleer de getoonde doelversie en bevestig bewust dat je teruggaat naar oudere firmware. Maak zo nodig eerst een instellingenbackup: instellingen blijven lokaal opgeslagen, maar functies en instellingen die alleen in de dev-build bestaan, zijn na de downgrade mogelijk niet meer beschikbaar.

Bij de Heatpump Controller Q kun je de opstelling (`Single` of `Duo`) en netwerkverbinding kiezen. De huidige Q-firmware bevat Wi-Fi en Ethernet: een verbindingswissel verandert de netwerkvoorkeur, terwijl een wissel tussen Single en Duo andere firmware installeert. Bij oudere firmware kunnen verbindingsvarianten nog afzonderlijke updates vereisen. Controleer bij Ethernet eerst of de netwerkkabel is aangesloten en bij Duo of de tweede warmtepomp bij deze controller hoort.

Als de verbinding voor de firmwaredownload niet kan worden geopend, probeert OpenQuatt dit eenmaal automatisch opnieuw. Mislukt ook die poging of wordt de installatie afgebroken, dan stopt de voortgang en kun je de setupwissel opnieuw starten.


## Systeem

Hier vind je beheerfuncties:

- Quick Start opnieuw openen;
- opslag voor Diagnose, Beslislog en Energie;
- firmware-updates en updatekanaal;
- web-login en API-beveiliging;
- de keuze voor gebruiksstatistieken;
- backup en restore;
- systeemstatus;
- logboek;
- herstarten.

## Gebruiksstatistieken en privacy

Zie [Gegevens delen en privacy](privacy.md) voor wat wordt gedeeld en hoe je dit aan- of uitzet.

## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
