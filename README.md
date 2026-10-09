# OpenQuatt [![Discord](https://img.shields.io/badge/chat-Discord-44cc11)](https://discord.com/channels/1176602554885492786/1464174190788874427) [![Release](https://img.shields.io/github/v/release/OpenQuatt/OpenQuatt?label=release&color=007ec6)](https://github.com/OpenQuatt/OpenQuatt/releases/latest)

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/brand/openquatt-logo-horizontal-dark.svg" />
  <img src="docs/assets/brand/openquatt-logo-horizontal-light.svg" alt="OpenQuatt" width="420" />
</picture>

OpenQuatt is open-source ESPHome-firmware voor Quatt Hybrid `Single`- en `Duo`-installaties. Het project geeft je lokaal inzicht, lokale controle, een uitgebreide web-app en optionele Home Assistant-integratie bovenop de bestaande Quatt-hardware.

> [!IMPORTANT]
> OpenQuatt is momenteel een **open-sourceproject op best-effortbasis**. Ontwikkeling, documentatie en ondersteuning worden door de community geleverd op basis van beschikbare tijd; er is geen gegarandeerde responstijd of individuele ondersteuning.
>
> Voor gebruiksvragen en hulp bij diagnose kun je terecht in het [OpenQuatt Discord-kanaal](https://discord.com/channels/1176602554885492786/1464174190788874427). Een reproduceerbare bug meld je als [GitHub-issue](https://github.com/OpenQuatt/OpenQuatt/issues/new/choose).

> [!WARNING]
> OpenQuatt is community-firmware voor gebruikers met enig technisch inzicht. Gebruik het bewust en test wijzigingen stap voor stap.
>
> Gebruik van OpenQuatt kan gevolgen hebben voor Quatts commerciële garantie. Raadpleeg de [actuele Quatt-voorwaarden](https://www.quatt.io/algemene-voorwaarden); wettelijke rechten staan daar los van.

## Kies je route

Kies alleen de route die bij jouw huidige hardware en situatie hoort. Home Assistant is geen onderdeel van de basisinstallatie.

| Jouw situatie | Begin hier | Wat je gaat doen |
|---|---|---|
| Nieuwe Heatpump Controller Q-edition met OpenQuatt voorgeïnstalleerd | [Q-edition aansluiten en in gebruik nemen](docs/q-edition.md) | Controller aansluiten, online brengen en Quick Start afronden. Zelf firmware flashen is normaal niet nodig. |
| OpenQuatt draait al | [Overzicht van de web-app](docs/web-app.md) | Instellingen beheren, updaten, backups maken en diagnose uitvoeren via `openquatt.local`. |
| Eerst alleen rondkijken | [Web-app demo](https://openquatt.github.io/OpenQuatt/demo/) | De interface bekijken zonder hardware of wijzigingen aan je installatie. |

## Wat is OpenQuatt?

OpenQuatt is bedoeld voor gebruikers van een Quatt Hybrid `Single` of `Duo` die:

- meer inzicht willen in wat de installatie doet;
- lokale controle willen via een uitgebreide web-app;
- instellingen en verwarmingsstrategie zelf willen beheren;
- koeling willen kunnen gebruiken waar hun installatie dat ondersteunt;
- Home Assistant willen gebruiken voor dashboards en automatisering;
- de juiste installatie- of beheerroute voor hun hardware willen volgen.

OpenQuatt richt zich niet op Quatt All-Electric, Quatt Chill of koppeling met Quatt HomeBattery.

Je hoeft voor de eerste installatie niet eerst alle technische achtergronddocumenten te lezen. De routekiezer hierboven brengt je direct bij de juiste handleiding. Home Assistant is optioneel voor OpenQuatt zelf en aanbevolen voor dashboards en automatisering.

## Ondersteunde combinaties

OpenQuatt ondersteunt momenteel Quatt Hybrid `Single` en `Duo` (V1, V1.5 en V2).

De [`Electropaultje Heatpump Controller Q-edition`](https://electropaultje.nl/product/heatpump-controller-q-edition/) is de ondersteunde OpenQuatt-module. Deze hardware is speciaal voor OpenQuatt gemaakt en is de basis voor support en nieuwe ontwikkeling.

De Q-edition ondersteunt `Single` en `Duo`, met Wi-Fi en Ethernet in dezelfde firmware. Bestaande Waveshare- en Heatpump Listener-modules worden niet meer ondersteund; v0.50.0 is daarvoor de laatste firmwarerelease.

## Wanneer is de installatie klaar?

De basisinstallatie is afgerond wanneer:

- `http://openquatt.local` stabiel bereikbaar is;
- Quick Start volledig is afgerond;
- de warmtepompgegevens worden bijgewerkt;
- aanvoertemperatuur, flow en buitentemperatuur aannemelijke waarden tonen.

Home Assistant en het meegeleverde dashboard zijn optionele vervolgstappen. Ze zijn niet nodig om OpenQuatt zelf te installeren of via de web-app te gebruiken.

Gebruik de [installer](https://openquatt.github.io/OpenQuatt/install/) om Wi-Fi via USB in te stellen of als herstelroute voor een HCQ. Ga bij problemen naar [Problemen oplossen](docs/problemen-oplossen.md).

## Mogelijkheden

OpenQuatt biedt:

- lokale controle en inzicht zonder afhankelijk te zijn van een cloud-dashboard;
- een uitgebreide geïntegreerde web-app voor installatie, instellingen, updates, backup en beheer;
- `Power House` als aanbevolen verwarmingsstrategie voor normale ingebruikname;
- koeling als bewuste OpenQuatt-functie waar de installatie dat ondersteunt;
- optionele Home Assistant-integratie met dashboards;
- OpenTherm-thermostaat- en ketelkoppeling op de Heatpump Controller Q-edition.

## Beperkingen

Belangrijke beperkingen voor deze fase:

- OpenQuatt is bedoeld voor Quatt Hybrid `Single` en `Duo`, niet voor Quatt All-Electric, Quatt Chill of Quatt HomeBattery.
- Alleen de Heatpump Controller Q-edition wordt ondersteund.
- OpenTherm-aansturing van de ketel via `OTB` is alleen beschikbaar op de Heatpump Controller Q-edition.
- Normale hybride ketelondersteuning in de stooklijnregeling en handmatig ketel-only gebruik staan op de roadmap. Gerichte ketelovername bij warmtepompuitval is een afzonderlijke, optionele functie.
- OpenQuatt is geen officiële Quatt-helpdesk of individuele installatieservice.

## Roadmap

Compacte roadmap:

- CV-assist in Heating Curve;
- handmatig ketel-only gebruik;
- verdere uitbreiding van storingsafhandeling.

## Documentatie

Belangrijkste pagina's voor gebruikers:

- [Heatpump Controller Q-edition aansluiten en in gebruik nemen](docs/q-edition.md) voor de normale route met nieuwe OpenQuatt-hardware
- [Overzicht van de web-app](docs/web-app.md) voor Quick Start, instellingen, updates, backup en beveiliging
- [OpenQuatt Home Assistant](docs/dashboard/README.md) voor dashboards, optionele HA-packages en de companion-repository
- [Verwarmen en comfort](docs/dagelijks/verwarmen.md) voor de keuze tussen Power House en stooklijnregeling; zie ook [Koelen](docs/dagelijks/koelen.md)
- [MQTT inputbronnen](docs/mqtt.md) voor externe MQTT-bronwaarden zoals dauwpunt, buiten- en kamerwaarden en toestemmingssignalen
- [Problemen oplossen](docs/problemen-oplossen.md) voor diagnose zonder meteen te gaan tunen

Meer heb je voor normaal gebruik meestal niet nodig.

## Licentie

Dit project bevat een `LICENSE`-bestand in de root van de repository.

Built with [ESPHome](https://esphome.io/).
