# Eerste keer instellen

## Eerste keer: Quick Start

Na de eerste installatie opent de web-app Quick Start zolang de basisinstallatie nog niet is afgerond.

Quick Start begint met de configuratiekeuze en software-update. Daarna volgen de configuratiestappen:

| Stap | Wat kies je? | Waarom? |
|---|---|---|
| `Configuratie en software-update` | `Single` of `Duo`, via `Wi-Fi` of `Ethernet` | Controleert de software voor jouw opstelling. |
| `Kies je Quatt Hybrid` | V1, V1.5 of V2 | Selecteert de juiste basislogica voor jouw warmtepompgeneratie. |
| `Flowmeting configureren` | De juiste flowbron | Zorgt dat de regeling de juiste meting gebruikt. |
| `Thermostaatgegevens configureren` | Eén bron voor kamertemperatuur en setpoint | Voorkomt dat OpenQuatt waarden uit verschillende bronnen combineert. |
| `Aanvullende warmtebron` | Aansluiting (`R1` of `OTB`), hybride verwarmen en overname | Bepaalt aansluiting, hybride ondersteuning en overname bij uitval. Zie [Aanvullende warmtebron](instellingen.md#aanvullende-warmtebron). |
| `Kies de verwarmingsstrategie` | `Power House` of `Stooklijnregeling` | Bepaalt hoe OpenQuatt de warmtevraag regelt. Zie [Verwarmen en comfort](../dagelijks/verwarmen.md). |
| `Werk de regeling uit` | Strategie-instellingen | Toont alleen de instellingen die bij de gekozen strategie horen. |
| `Flowregeling en afstelling` | Automatische flow of vaste pompstand | Bepaalt hoe OpenQuatt de waterdoorstroming regelt. |
| `Watertemperatuur beveiligen` | Maximale watertemperatuur | Laat OpenQuatt terugregelen voordat het water te warm wordt. |
| `Stille uren en niveaus` | Tijdvenster en compressorlimieten | Begrenst de compressor bijvoorbeeld 's nachts. |
| `Gebruiksstatistieken` | Wel of niet beperkte technische systeemstatus en feature-instellingen delen | Standaard aan bij een nieuwe Quick Start; je kunt dit hier uitzetten. Zie [Gegevens delen en privacy](privacy.md). |
| `Prestatiemetingen` | Wel of niet stabiele verwarmingsmetingen delen voor validatie van het prestatiemodel | Standaard uit. Zie [Gegevens delen en privacy](privacy.md). |
| `Bevestigen en afronden` | Je keuzes controleren | Markeert de basisconfiguratie als klaar. |

Je hoeft niet meteen perfecte waardes te kiezen. Het doel van Quick Start is een veilige, begrijpelijke basis. Fijnregelen kan later.

## Zo controleer je dat het gelukt is

De installatie is klaar zodra Quick Start is afgerond, `openquatt.local` stabiel bereikbaar blijft en de belangrijkste warmtepompwaarden logisch worden bijgewerkt. Home Assistant en het dashboard zijn optionele vervolgstappen.


Controleer op **Overzicht** ook of kamertemperatuur, gewenste temperatuur en flow geloofwaardig zijn. Bij ontbrekende of vreemde waarden zie [Bronnen en integraties](bronnen.md); bij andere afwijkingen zie [Problemen oplossen](../problemen-oplossen.md).

## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
