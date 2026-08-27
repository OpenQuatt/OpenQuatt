# Andere modules installeren

Deze route is bedoeld voor een bestaande `Waveshare ESP32-S3-Relay-1CH` of `Electropaultje Heatpump Listener`. Voor deze modules kies en flash je zelf het exacte OpenQuatt-profiel. De pagina behandelt de software-installatie en geen hardware- of bedradingsinstructies.

Heb je een nieuwe Heatpump Controller Q-edition met OpenQuatt voorgeïnstalleerd? Volg dan de primaire route [Heatpump Controller Q-edition aansluiten en in gebruik nemen](q-edition.md). Zelf firmware flashen is daarbij normaal niet nodig.

> OpenQuatt is een open-sourceproject op best-effortbasis, zonder gegarandeerde responstijd of individuele ondersteuning. Voor gebruiksvragen en hulp bij diagnose kun je terecht in het [OpenQuatt Discord-kanaal](https://discord.com/channels/1176602554885492786/1464174190788874427). Een reproduceerbare bug meld je als [GitHub-issue](https://github.com/OpenQuatt/OpenQuatt/issues/new/choose).

## Installatieroute

1. Open de [OpenQuatt installer](https://openquatt.github.io/OpenQuatt/install/).
2. Kies `Single` of `Duo` en daarna exact jouw hardwareprofiel.
3. Sluit de module via USB aan en flash de firmware.
4. Stel Wi-Fi in.
5. Open `http://openquatt.local`.
6. Rond Quick Start af en controleer de live warmtepompgegevens.

Daarmee is OpenQuatt geïnstalleerd. Home Assistant en het dashboard zijn optionele vervolgstappen.

Gebruik [Handmatige installatie](handmatige-installatie.md) alleen als fallback.

## Wat zie je onderweg?

1. **Installer:** je kiest `Single` of `Duo` en jouw ESP-module. Kies exact de combinatie die bij je bestaande hardware past.
2. **ESP Web Tools:** de browser vraagt met welke USB-poort hij moet verbinden en installeert daarna de firmware. Laat het tabblad open totdat ook Wi-Fi is ingesteld.
3. **Web-app:** open `http://openquatt.local`, kies Quatt Hybrid V1, V1.5 of V2 en rond Quick Start af.
4. **Optioneel: Home Assistant:** voeg OpenQuatt pas na de lokale eindcontrole toe en importeer eventueel het juiste dashboard.

## Benodigdheden

- een bestaande Waveshare ESP32-S3-Relay-1CH of Electropaultje Heatpump Listener;
- een USB-kabel voor de eerste flash;
- een werkend Wi-Fi-netwerk;
- Chrome of Edge op desktop voor de web installer.

Home Assistant is optioneel voor OpenQuatt zelf en aanbevolen voor dashboards en automatisering.

## Kies het juiste profiel in de installer

Kies in de installer altijd exact de combinatie van je opstelling en hardware. Waveshare en Heatpump Listener zijn beschikbaar met limited/best-effort support; de Heatpump Controller Q-edition is de focus voor nieuwe ontwikkeling en support.

OpenQuatt ondersteunt Quatt Hybrid V1, V1.5 en V2. Die versie kies je na het flashen in de Quick Start van de web-app.

Je herkent de generatie aan het model en de sensoraansluiting:

| Keuze | Model en kenmerken |
|---|---|
| `V1` | Model `AMM4`: flowmeter bij de CV-ketel en vorstbeveiligingsklep buiten de buitenunit. Kies dit ook voor een gemengde V1/V1.5 Duo. |
| `V1.5` | Model `AMM4-V1.5`: flowmeter in de buitenunit; onder de CV-ketel zit alleen een kleine clip-on temperatuursensor. |
| `V2` | Model `AMH6` of `AMH6-2`: flowmeter in de buitenunit; onder de CV-ketel zit alleen een kleine clip-on temperatuursensor. |

Kies per hardware de volgende combinatie:

- **Heatpump Listener:** `Single` of `Duo` + `Heatpump Listener` + `Wi-Fi`;
- **Waveshare:** `Single` of `Duo` + `Waveshare` + `Wi-Fi`.

Kies `Single` voor één warmtepomp en `Duo` voor twee warmtepompen.

## Installatie via de web installer

> Let op: OpenQuatt kan gevolgen hebben voor Quatts commerciële garantie. Zie de [actuele Quatt-voorwaarden](https://www.quatt.io/algemene-voorwaarden); wettelijke rechten staan daar los van.

### Firmware flashen

1. Open de [OpenQuatt installer](https://openquatt.github.io/OpenQuatt/install/).
2. Kies de combinatie die past bij je opstelling en hardware.
3. Sluit het ESP32-bord via USB aan.
4. Flash de firmware.
5. Laat het browsertabblad open, zodat de Wi-Fi-configuratie direct daarna kan worden aangeboden.
6. Open na de eerste start `http://openquatt.local`.
7. Rond de Quick Start in de web-app af.

Praktisch voor een DS18B20: sluit die sensor bij voorkeur aan voordat OpenQuatt opstart. De 1-Wire sensor wordt tijdens het opstarten gedetecteerd; als je hem later aansluit, moet je het bord eerst herstarten voordat de sensor zichtbaar wordt.

Als de browserflow voor Wi-Fi niet werkt, start een Wi-Fi-build het OpenQuatt fallback access point:

- SSID: `OpenQuatt`
- wachtwoord: `openquatt`

## Eerste start: openquatt.local

De eerste plek na het flashen is de web-app:

```text
http://openquatt.local
```

Als die naam niet werkt, zoek dan het IP-adres van OpenQuatt in je router en open `http://<ip-adres>`.

De web-app toont Quick Start zolang de basisinstellingen nog niet zijn afgerond. Loop die eerst rustig door. Quick Start zet de belangrijkste installatiekeuzes klaar:

Bij Waveshare en Heatpump Listener ligt `Single` of `Duo` al vast in de firmware die je in de installer hebt gekozen. Quick Start toont daarom geen stap **Configuratie en software-update** en begint met:

1. Quatt Hybrid-versie: V1, V1.5 of V2;
2. flowmeting;
3. bron voor kamertemperatuur en kamer-setpoint;
4. ondersteuning door CV-ketel of boiler;
5. verwarmingsstrategie;
6. instellingen voor de gekozen strategie;
7. flowregeling en afstelling;
8. watertemperatuurbeveiliging;
9. stille uren en compressorlimieten;
10. controleren en afronden.

Op Heatpump Controller Q kies je bij de CV-ketel of boiler ook de fysieke
aansluiting: `Aan/uit (R1)` of `OpenTherm (OTB)`. Wanneer bij een R1-keuze
tijdens Quick Start toch een OpenTherm-ketel antwoordt, selecteert OpenQuatt
automatisch `OpenTherm (OTB)` en licht die keuze toe. Na afgeronde onboarding
wijzigt OpenQuatt de aansluiting niet meer automatisch: bij dezelfde afwijking
blijft R1 uit totdat je de aansluiting handmatig corrigeert.

Blijkt dat je `Single` of `Duo` verkeerd hebt gekozen? Flash dan via de installer het juiste profiel voordat je verdergaat. Deze modules kunnen de setup niet vanuit Quick Start wisselen.

## Wanneer is de installatie klaar?

De basisinstallatie is afgerond wanneer:

- `openquatt.local` stabiel bereikbaar is;
- Quick Start volledig is afgerond;
- de warmtepompgegevens worden bijgewerkt;
- aanvoertemperatuur, flow en buitentemperatuur aannemelijke waarden tonen.

Home Assistant is hiervoor niet nodig.

Zie voor de lokale web-app:

- [Web-app gebruiken](web-app.md)

## Optioneel: Home Assistant

Home Assistant ontdekt OpenQuatt meestal automatisch zodra beide op hetzelfde netwerk zitten. Open de melding bij **Instellingen -> Apparaten & diensten** en kies **Configureren**. Verschijnt er geen melding, kies dan **Integratie toevoegen -> ESPHome** en vul `openquatt.local` of het IP-adres van OpenQuatt in. Voer de ESPHome API-encryptiesleutel in als Home Assistant daarom vraagt.

Zie ook de officiële ESPHome-handleiding: [Connecting your device to Home Assistant](https://esphome.io/guides/getting_started_hassio/#connecting-your-device-to-home-assistant).

> [!IMPORTANT]
> Selecteer bij het toevoegen van OpenQuatt nog geen Home Assistant-area. Sinds Home Assistant 2026.6 kan de gekozen area tijdens de eerste aanmaak in de `entity_id` terechtkomen, bijvoorbeeld `sensor.zolder_openquatt_flow`. De meegeleverde dashboards verwachten `sensor.openquatt_...`. Wacht daarom tot alle OpenQuatt-entiteiten bestaan en ken pas daarna een area toe. Een latere area-toewijzing verandert bestaande entity-ID's niet.

Controleer pas na de lokale eindcontrole of het apparaat online en zichtbaar is in Home Assistant. Importeer vervolgens eventueel het juiste dashboard.

Zie voor het dashboard:

- [Dashboard installeren](dashboard/README.md)
- [Dashboard gebruiken](dashboardoverzicht.md)

## Bij problemen

### Het apparaat verschijnt niet in Home Assistant

- Controleer of Wi-Fi echt is ingesteld.
- Kijk of het apparaat nog op het fallback access point zit.
- Herstart het bord een keer.

### Ik zie geen warmtepompgegevens

- Controleer RS485-bekabeling.
- Controleer of het gekozen hardwareprofiel klopt.
- Controleer of je niet per ongeluk een `Single`-bestand gebruikt op een `Duo`-opstelling, of andersom.

### Waarden zijn zichtbaar, maar lijken onlogisch

Controleer in Home Assistant eerst welke bron is geselecteerd voor flow, buitentemperatuur, kamertemperatuur en setpoint. Kijk pas daarna naar afstelling of tuning.

### Een aangesloten DS18B20 verschijnt niet

- Controleer of de sensor op de juiste `ds18b20_pin` van het gekozen hardwareprofiel zit.
- Herstart OpenQuatt nadat je de sensor hebt aangesloten. Zonder reboot wordt een later aangesloten DS18B20 niet ontdekt.

### Het dashboard wil niet importeren

- Gebruik de ruwe YAML-editor in Home Assistant.
- Controleer of je het juiste dashboardbestand voor `Single` of `Duo` hebt gekozen.
- Controleer of de OpenQuatt-entity-ID's met `openquatt_` beginnen en niet met een area-prefix zoals `zolder_openquatt_`.

## Als de installer niet werkt

Gebruik dan pas de fallbackroute: [Handmatige installatie](handmatige-installatie.md).
