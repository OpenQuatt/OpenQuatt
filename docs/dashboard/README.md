# OpenQuatt in Home Assistant

Gebruik Home Assistant om OpenQuatt te volgen, het dashboard aan je opstelling aan te passen en optionele dynamische bronnen te koppelen.

## Begin hier

1. [Koppel OpenQuatt aan Home Assistant](koppelen.md). Hier staan het **koppelvenster van 10 minuten**, automatische API-beveiliging en herstel bij koppelproblemen.
2. [Installeer het dashboard](installation.md), als je het meegeleverde dashboard wilt gebruiken. Kies Single of Duo en Nederlands of Engels.
3. [Gebruik het dashboard](dashboard.md) voor dagelijkse controle en diagnose.
4. Voeg alleen wanneer nodig [eigen sensoren](dynamic-sources.md) of [koelbronnen](cooling.md) toe.

De ESPHome-integratie werkt ook zonder dashboard, HACS of optionele packages. Voor alleen de koppeling heb je geen ESPHome Device Builder-app nodig.

## Kies je dashboard

Kies een dashboard dat past bij je opstelling en taal:

- [Single · Nederlands](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/single-nl.yaml)
- [Single · Engels](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/single-en.yaml)
- [Duo · Nederlands](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/duo-nl.yaml)
- [Duo · Engels](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/duo-en.yaml)

De dashboardbestanden en Home Assistant-packages hebben een eigen releasecyclus. De actuele bron staat in [OpenQuatt/home-assistant-openquatt](https://github.com/OpenQuatt/home-assistant-openquatt). Deze pagina helpt je kiezen en gebruiken; haal YAML en packages altijd uit die repository.
