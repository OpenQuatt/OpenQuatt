# HCQ aansluitingen en technische I/O

Technische naslag voor ontwikkeling en diagnose van de Electropaultje Heatpump Controller Q-edition (HCQ). Dit overzicht beschrijft de aansluiting en GPIO-koppeling van de huidige OpenQuatt-firmware. Gebruik voor het veilig aansluiten altijd eerst de [aansluitgids voor de HCQ](q-edition.md).

> [!WARNING]
> Deze pagina is geen bedradingsinstructie. Schakel de CiC en buitenunit(s) spanningsloos voordat je kabels verplaatst. Twijfel je over de bedrading, laat het werk dan door een vakbekwaam installateur uitvoeren.

## Externe aansluitingen

| Aansluiting | Functie in de huidige firmware | Signaal |
|---|---|---|
| `Q` | Lokale aanvoertemperatuur en, bij Quatt V1, flowmeting | PT1000 en flowmeter-puls |
| `R1` | CV-ketel via aan/uit | Potentiaalvrij relais: `COM` + `NO` |
| `R2` | Configureerbaar hulprelais, standaard uit | Potentiaalvrij wisselrelais: `NC` / `COM` / `NO` |
| `T` | Optionele lokale temperatuursensor | 1-Wire Dallas/DS18B20: `+3.3V`, `GND`, `DATA` |
| `OTT` | Kamerthermostaat | OpenTherm slave, twee aders |
| `OTB` | CV-ketel | OpenTherm master, twee aders |
| `M1` | Quatt-buitenunit(s) | RS485 Modbus: `GND` / `A` / `B` |
| `M2` | Optionele CiC-compatibiliteit | RS485 Modbus: `GND` / `A` / `B` |
| Ethernet | Netwerk in Ethernet-builds | W5500 met RJ45 |
| USB | Voeding, Wi-Fi-provisioning, flashen en herstel | USB |

Gebruik voor de CV-ketel altijd precies één route: `OTB` of `R1`, nooit beide tegelijk. De polariteit van de twee aders op `OTT` en `OTB` maakt niet uit.

## GPIO-overzicht

| Functie | Aansluiting | ESP32-S3-interface | GPIO |
|---|---|---|---|
| PT1000 | `Q` | SPI3 via MAX31865 | MOSI `GPIO7`, MISO `GPIO4`, CLK `GPIO6`, CS `GPIO5` |
| Flowmeter-puls | `Q` | GPIO-ingang met pull-up | `GPIO15` |
| CV-ketelrelais | `R1` | GPIO-uitgang | `GPIO16` |
| Hulprelais | `R2` | GPIO-uitgang | `GPIO3` |
| DS18B20 | `T` | 1-Wire | `GPIO18` |
| OpenTherm thermostaat | `OTT` | slave: in/uit | in `GPIO21`, uit `GPIO14` |
| OpenTherm CV-ketel | `OTB` | master: in/uit | in `GPIO47`, uit `GPIO48` |
| Buitenunit-Modbus | `M1` | UART met RS485 DE/RE | TX `GPIO40`, RX `GPIO42`, DE/RE `GPIO41` |
| CiC-compatibiliteit | `M2` | UART met RS485 DE/RE | TX `GPIO45`, RX `GPIO39`, DE/RE `GPIO38` |
| Ethernet W5500 | RJ45 | SPI | MOSI `GPIO10`, MISO `GPIO11`, CLK `GPIO12`, CS `GPIO13`, INT `GPIO9` |
| Status-led geel | Front | GPIO-uitgang | `GPIO1` |
| Status-led rood | Front | GPIO-uitgang | `GPIO2` |
| Boot-/herstelknop | Front | GPIO-ingang met pull-up | `GPIO46` |

## Functie per interface

### Q: PT1000 en flowmeter

De `Q`-sensorstekker bevat de PT1000 voor de lokale aanvoertemperatuur en de flowmeter-puls van Quatt V1. De PT1000 gebruikt een MAX31865 met een referentieweerstand van 1500 Ω, een nominale weerstand van 1000 Ω en 2-draadsbedrading. De firmware leest hem alleen wanneer **Lokale aanvoertemperatuur** op `PT1000` staat.

De pulslezer op `GPIO15` heeft een interne pull-up en een filter van 100 µs. Kies in de web-app onder **Instellingen → Bronnen / integraties → Sensorselectie → Flow → Lokale flowmeter** het aangesloten type. In Home Assistant en de standaard ESPHome-webinterface heet deze instelling **Controller Flow Meter**:

- **Huba Control** (standaard): behoudt de bestaande omrekening met 0,05 l/min per Hz en eventuele Huba-configuratieaanpassingen.
- **ZJ-B10**: voor het [TinyTronics-model](https://www.tinytronics.nl/nl/sensoren/vloeistof/yf-b10-water-flow-sensor-messing-g1), met 7,9 Hz per l/min. De firmware deelt het aantal pulsen per minuut door 7,9 om l/h te berekenen. Een pulswaarde van nul blijft nul; de bestaande timeout van 5 seconden blijft behouden.

De keuze blijft bewaard na een herstart. De bestaande middeling over 10 seconden blijft actief; wacht na wisselen tot de meting is bijgewerkt. De keuze past alleen de lokale pulsmeting aan. Bij V1.5 en V2 gebruikt OpenQuatt normaal de flowmeting uit de buitenunit; de keuze is instelbaar via **Q Flow Source** (`Auto`, `Local` of `Outdoor unit`). Kies `Local` om de aangesloten controller-flowmeter expliciet te gebruiken.

### R1 en R2: relais

`R1` is de aan/uit-route voor de CV-ketel. Gebruik hiervoor alleen `COM` en `NO`; `NC` blijft vrij.

`R2` is een afzonderlijk hulprelais. Het is standaard uitgeschakeld en kan in de web-app worden ingesteld voor bijvoorbeeld een fancoil, pomp of klep. De beschikbare klemmen zijn `NC`, `COM` en `NO`.

### T: DS18B20

`T` is de 1-Wire-bus voor een optionele Dallas/DS18B20. Kies deze bron alleen als de lokale aanvoertemperatuur op `DS18B20` staat.

### OTT en OTB: OpenTherm

Op `OTT` gedraagt de HCQ zich als OpenTherm-slave tegenover de kamerthermostaat. `OTB` is de OpenTherm-masterroute naar de CV-ketel en is op de huidige Q-edition-firmware beschikbaar. Tijdens Quick Start kan de firmware een geldige OpenTherm-ketelverbinding op `OTB` detecteren en die route kiezen; zonder geldige link blijft ketelaansturing geblokkeerd.

### M1 en M2: Modbus

`M1` is de primaire RS485-poort voor de buitenunit(s). De HCQ is hier Modbus-master met 19200 baud, 8E1 en DE/RE op `GPIO41`.

`M2` is de optionele tweede RS485-poort voor CiC-compatibiliteit. Hier is de HCQ Modbus-server met 19200 baud, 8E1 en DE/RE op `GPIO38`. Na het aansluiten moet **CiC-compatibiliteit** in de web-app worden ingeschakeld als de Quatt-app via de CiC moet blijven meekijken.

### Ethernet, leds en herstelknop

In een Ethernet-build gebruikt de W5500 de SPI-pinnen in de tabel. In een Wi-Fi-build zet de firmware diezelfde W5500 in power-down; Ethernet en Wi-Fi zijn afzonderlijke firmware-builds.

De gele led brandt wanneer de actieve netwerkverbinding verbonden is. De rode led signaleert een actieve firmware-, sensor-, OpenTherm-, warmtepomp- of veiligheidsfout. Houd de herstelknop vijf seconden ingedrukt om een tijdelijk herstelvenster voor de web-login te openen.

## Bronnen in de firmware

De GPIO-koppeling staat in `openquatt/profiles/heatpump_controller_q.yaml`. De functie per aansluiting volgt uit de daarin opgenomen packages, waaronder de OpenTherm-, CiC-compatibiliteits-, status-led- en hulprelaisconfiguratie. Werk deze pagina mee bij wanneer die hardwareconfiguratie wijzigt.
