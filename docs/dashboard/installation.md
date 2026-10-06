# Dashboard installeren

Op deze pagina vind je de dashboardbestanden voor OpenQuatt in Home Assistant. Volg voor een nieuwe installatie deze volgorde:

1. Rond Quick Start af via `http://openquatt.local`.
2. [Koppel OpenQuatt via de ESPHome-integratie](koppelen.md).
3. Installeer de twee vereiste dashboardkaarten via HACS.
4. Importeer het dashboardbestand dat bij je opstelling past.

## Welk bestand kies je?

Kies het bestand dat past bij je opstelling en voorkeurstaal:

- [Single · Nederlands](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/single-nl.yaml)
- [Single · Engels](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/single-en.yaml)
- [Duo · Nederlands](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/duo-nl.yaml)
- [Duo · Engels](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/dashboards/duo-en.yaml)

Gebruik `duo` voor Duo en `single` voor Single. Kies daarna `nl` of `en`.

Open het gekozen bestand, kopieer de volledige inhoud en plak die later in de **Raw configuration editor** van Home Assistant.

## OpenQuatt via ESPHome toevoegen aan Home Assistant

Volg eerst [OpenQuatt koppelen aan Home Assistant](koppelen.md). Daar staan het koppelvenster van 10 minuten, automatische API-beveiliging en de instructie om pas na het aanmaken van de entiteiten een area toe te wijzen. Deze stap is ook nodig als je geen dashboard gebruikt.

## Vereiste dashboardkaarten installeren

De dashboards gebruiken twee custom dashboardkaarten die niet standaard in Home Assistant zitten:

| Dashboardkaart | Gebruikt voor |
| --- | --- |
| [Mini Graph Card](https://github.com/kalkih/mini-graph-card) | Compacte grafieken bij actuele meetwaarden |
| [ApexCharts Card](https://github.com/RomRider/apexcharts-card) | Uitgebreide temperatuur-, vermogen- en statusgrafieken |

Installeer beide kaarten bij voorkeur via [HACS](https://www.hacs.xyz/docs/use/download/download/):

1. Open **HACS** in de zijbalk van Home Assistant.
2. Zoek naar `Mini Graph Card`, open het resultaat en kies **Downloaden**.
3. Zoek naar `ApexCharts Card`, open het resultaat en kies **Downloaden**.
4. Herstart Home Assistant als HACS aangeeft dat dit nodig is.
5. Vernieuw de dashboardpagina volledig. Wis zo nodig de browsercache of herlaad de Home Assistant-app.

HACS registreert de JavaScript-resources normaal automatisch. Controleer bij problemen onder **Instellingen -> Dashboards -> menu met drie puntjes -> Resources** of deze modules aanwezig zijn:

```text
/hacsfiles/mini-graph-card/mini-graph-card-bundle.js
/hacsfiles/apexcharts-card/apexcharts-card.js
```

Zie je het menu **Resources** niet, schakel dan eerst **Geavanceerde modus** in via je Home Assistant-gebruikersprofiel.

## Dashboard importeren in Home Assistant

1. Open Home Assistant.
2. Ga naar **Instellingen -> Dashboards**.
3. Maak bij voorkeur een nieuw leeg dashboard aan of open een bestaand handmatig beheerd dashboard.
4. Open het dashboard en daarna het menu met de drie puntjes.
5. Kies **Raw configuration editor**.
6. Plak de inhoud van het gekozen YAML-bestand.
7. Sla op en laad het dashboard opnieuw.

## Bij importproblemen

- Controleer of je echt het juiste `single`- of `duo`-bestand hebt.
- Controleer of je de volledige YAML hebt geplakt.
- Controleer of de OpenQuatt-entiteiten al in Home Assistant bestaan.
- Controleer of de entity-ID's beginnen met `openquatt_` en niet met een area-prefix zoals `zolder_openquatt_`.
- Krijg je `Custom element doesn't exist: mini-graph-card` of `Custom element doesn't exist: apexcharts-card`, controleer dan of beide kaarten in HACS zijn gedownload en bij **Resources** staan.
- Zijn alleen de grafieken leeg, controleer dan onder **Ontwikkelaarstools -> Statussen** of de gebruikte `sensor.openquatt_...`-entiteiten bestaan en historie opbouwen.

### Area was al geselecteerd

Als Home Assistant de area al in de entity-ID's heeft verwerkt, zijn er twee herstelroutes:

1. Hernoem de betrokken entity-ID's in Home Assistant en verwijder alleen de area-prefix. Wijzig bijvoorbeeld `sensor.zolder_openquatt_flow` in `sensor.openquatt_flow`. De area zelf mag daarna toegewezen blijven.
2. Is dit nog een verse installatie zonder gebruikte historie of automatiseringen, verwijder OpenQuatt dan uit Home Assistant en voeg het opnieuw toe zonder area. Wacht tot de entiteiten bestaan en wijs daarna de area toe.

Pas niet de dashboard-YAML aan naar een specifieke area. Zo'n dashboard werkt dan alleen voor die ene Home Assistant-installatie.

## Optionele bronnen

De dashboardinstallatie is nu klaar. Voeg alleen packages toe als je eigen Home Assistant-sensoren wilt doorgeven aan OpenQuatt:

- [Eigen sensoren gebruiken](dynamic-sources.md)
- [Koelbronnen gebruiken](cooling.md)

Dashboardbestanden en packages worden onderhouden in [OpenQuatt/home-assistant-openquatt](https://github.com/OpenQuatt/home-assistant-openquatt). De koppel- en dashboardinstallatiehandleiding op deze site wordt bij de firmwaredocumentatie onderhouden.

## Optioneel: dynamische bronselectie via Home Assistant

Volg [Eigen sensoren gebruiken](dynamic-sources.md) voor installatie en configuratie van het optionele bronpakket.

## Optioneel: dynamische koelbronnen via Home Assistant

Volg [Koelbronnen gebruiken](cooling.md) voor installatie en configuratie van het optionele koelpakket.
