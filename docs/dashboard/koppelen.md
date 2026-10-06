# OpenQuatt koppelen aan Home Assistant

Rond eerst Quick Start af via `http://openquatt.local`. Home Assistant is optioneel: OpenQuatt werkt ook zelfstandig via de web-app. Na het koppelen kun je de entiteiten gebruiken voor monitoring en automatisering, met of zonder het OpenQuatt-dashboard.

## Koppel binnen 10 minuten na het opstarten

> [!IMPORTANT]
> Bij de eerste koppeling heeft Home Assistant maximaal 10 minuten na het opstarten om de beveiligde ESPHome-verbinding automatisch in te stellen. Je hoeft geen API-key te maken of te kopiëren; de web-app toont de sleutel nooit.
>
> Is OpenQuatt al langer ingeschakeld, bijvoorbeeld doordat Quick Start langer duurde? Zet de controller kort uit en weer aan en rond de koppeling binnen 10 minuten af. Doe dit één keer; meerdere snelle power-cycles achter elkaar kunnen een factory reset activeren.
>
> Een bestaande koppeling blijft behouden na een firmware-update, herstart en stroomonderbreking. Daarvoor geldt het koppelvenster niet. Het Wi-Fi-instelvenster staat hier los van.

## OpenQuatt via ESPHome toevoegen aan Home Assistant

OpenQuatt draait al op ESPHome-firmware. Je hebt alleen de ingebouwde **ESPHome-integratie** nodig; de ESPHome Device Builder-app is niet nodig om OpenQuatt te koppelen.

> [!IMPORTANT]
> Selecteer bij de eerste toevoeging nog geen Home Assistant-area. Het standaarddashboard verwacht entity-ID's zoals `sensor.openquatt_flow`. Een area bij de eerste toevoeging kan een prefix toevoegen, zoals `sensor.zolder_openquatt_flow`. Wacht tot de entiteiten zijn aangemaakt en wijs daarna pas een area toe.

1. Zorg dat OpenQuatt en Home Assistant op hetzelfde lokale netwerk bereikbaar zijn en dat het koppelvenster nog open is.
2. Open **Instellingen -> Apparaten & diensten** in Home Assistant.
3. Staat OpenQuatt bij **Ontdekt**, kies dan **Configureren**.
4. Verschijnt het apparaat niet, kies **Integratie toevoegen -> ESPHome**.
5. Vul `openquatt.local` of het IP-adres van OpenQuatt in. Laat de API-poort op `6053` staan.
6. Laat Home Assistant de beveiligde verbinding automatisch instellen. Vraagt Home Assistant toch om een API-encryptiesleutel? Ga naar [problemen met koppelen](#problemen-met-koppelen); de sleutel is niet uit de web-app te kopiëren.
7. Rond de configuratie af zonder een area te selecteren.
8. Controleer bij het OpenQuatt-apparaat of de entiteiten verschijnen en waarden ontvangen. Wijs daarna eventueel een area toe.
9. Controleer in de OpenQuatt web-app bij **Instellingen -> Systeem -> Toegang & Beveiliging** dat API-encryptie actief is.

## Problemen met koppelen

### OpenQuatt wordt niet gevonden

Controleer eerst of de web-app bereikbaar is. Voeg daarna ESPHome handmatig toe met het IP-adres. Automatische ontdekking en bereikbaarheid van de API zijn verschillende zaken; het ontbreken van een melding betekent niet dat de controller offline is.

### Het koppelvenster is verlopen

Zet de controller één keer kort uit en weer aan. Voeg hem binnen 10 minuten toe aan Home Assistant. Een gewone herstart wist een bestaande API-sleutel niet: deze stap helpt bij een eerste koppeling zonder opgeslagen sleutel, niet bij een onbekende bestaande sleutel.

### Home Assistant vraagt om een API-key of heeft een oude sleutel

Een bestaande sleutel wordt bij opstarten niet vervangen. Kun je niet koppelen doordat de sleutel onbekend is of niet overeenkomt? Volg [API-beveiliging resetten](../web-app/herstel.md#api-beveiliging-resetten). Daar staat hoe je de sleutel wist via de herstelpagina, of via de instellingen als je met een gebruikersnaam en wachtwoord op de webinterface bent ingelogd. Ook vind je daar uitleg over de eventuele melding in Home Assistant om de oude sleutel te verwijderen.

Na de reset en herstart heeft Home Assistant opnieuw 10 minuten om encryptie in te stellen. Controleer daarna dat API-encryptie actief is; alleen bereikbaarheid bewijst dit niet. Een factory reset is hiervoor niet nodig.

### Entiteiten hebben een area-prefix

Zie [Area was al geselecteerd](installation.md#area-was-al-geselecteerd). Je hoeft geen API-beveiliging te resetten om entity-ID's te herstellen.

## Verder

- [Dashboard installeren](installation.md): benodigde kaarten en Single/Duo-dashboard.
- [Eigen sensoren gebruiken](dynamic-sources.md): optionele bronnen vanuit Home Assistant.
- [Koelbronnen gebruiken](cooling.md): optionele dauwpuntbronnen.
- [Home Assistant-overzicht](README.md): alle vervolgstappen op één plek.
