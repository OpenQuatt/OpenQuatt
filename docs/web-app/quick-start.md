# Eerste keer instellen

## Eerste keer: Quick Start

Na de eerste installatie opent de web-app Quick Start zolang de basisinstallatie nog niet is afgerond.

Quick Start begint met de configuratiekeuze en software-update. Daarna volgen de configuratiestappen:

| Stap | Wat kies je? | Waarom? |
|---|---|---|
| `Configuratie en software-update` | `Single` of `Duo`, via `Wi-Fi` of `Ethernet` | Alleen op de HCQ; controleert de stabiele release. Als de gekozen build al actief is, kun je ook met de huidige software doorgaan zonder OTA. |
| `Kies je Quatt Hybrid` | V1, V1.5 of V2 | Selecteert de juiste basislogica voor jouw warmtepompgeneratie. |
| `Flowmeting configureren` | De juiste flowbron | Zorgt dat de regeling de juiste meting gebruikt. |
| `Thermostaatgegevens configureren` | Eén bron voor kamertemperatuur en setpoint | Voorkomt dat OpenQuatt waarden uit verschillende bronnen combineert. |
| `Aanvullende warmtebron` | Aansluiting (`R1` of `OTB`), hybride verwarmen en overname | Legt afzonderlijk vast of een warmtebron is aangesloten, of deze bij een vermogenstekort hybride mag meeverwarmen en of deze mag overnemen wanneer geen warmtepomp beschikbaar is. Op Q-hardware controleert OpenQuatt bij een R1-keuze tijdens het opstarten kort of toch een OpenTherm-ketel antwoordt. Tijdens Quick Start wordt een gedetecteerde OT-ketel automatisch als `OpenTherm (OTB)` ingesteld en wordt die keuze toegelicht. Na afgeronde onboarding blijft een onverwachte OT-ketel geblokkeerd totdat de aansluiting handmatig is gecorrigeerd. |
| `Kies de verwarmingsstrategie` | `Power House` of `Stooklijnregeling` | Bepaalt hoe OpenQuatt warmtevraag maakt en vervangt daarbij automatisch de warmtetoestemming (`Niet gebruiken` voor Power House; de eerder gekozen actieve thermostaatbron voor stooklijn). |
| `Werk de regeling uit` | Strategie-instellingen | Toont alleen de instellingen die bij de gekozen strategie horen. |
| `Flowregeling en afstelling` | Automatische flow of vaste pompstand | Bepaalt hoe OpenQuatt de waterdoorstroming regelt. |
| `Watertemperatuur beveiligen` | Maximale watertemperatuur | Laat OpenQuatt terugregelen voordat het water te warm wordt. |
| `Stille uren en niveaus` | Tijdvenster en compressorlimieten | Begrenst de compressor bijvoorbeeld 's nachts. |
| `Gebruiksstatistieken` | Wel of niet beperkte technische systeemstatus en feature-instellingen delen | Tijdens een nieuwe Quick Start staat delen standaard aan en kan het hier worden uitgezet. |
| `Prestatiemetingen` | Wel of niet stabiele verwarmingsmetingen delen voor validatie van het prestatiemodel | Tijdens een nieuwe Quick Start staat delen standaard uit en kan het hier worden aangezet. Na inschakelen worden maximaal 15 complete minuutrecords per bericht iedere 15 minuten vanaf deviceboot verstuurd; de planning volgt uptime en niet UTC-kwartiergrenzen. |
| `Bevestigen en afronden` | Je keuzes controleren | Markeert de basisconfiguratie als klaar. |

Je hoeft niet meteen perfecte waardes te kiezen. Het doel van Quick Start is een veilige, begrijpelijke basis. Fijnregelen kan later.

De installatie is klaar zodra Quick Start is afgerond, `openquatt.local` stabiel bereikbaar blijft en de belangrijkste warmtepompwaarden logisch worden bijgewerkt. Home Assistant en het dashboard zijn optionele vervolgstappen.


## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
