# HCQ aansluitingen en technische I/O

Technische naslag voor ontwikkeling en diagnose van de Electropaultje Heatpump Controller Q-edition (HCQ). Dit overzicht beschrijft de aansluiting en GPIO-koppeling van de huidige OpenQuatt-firmware. Gebruik voor het veilig aansluiten altijd eerst de [aansluitgids voor de HCQ](q-edition.md).

> [!WARNING]
> Schakel de CiC, controller en buitenunit(s) spanningsloos voordat je kabels verplaatst, doormeet of soldeert. De aanpassing voor een alternatieve B10-flowmeter hieronder is een hardwaremodificatie. Controleer vóór het inschakelen de pinbezetting en alle verbindingen met een multimeter. Twijfel je over de bedrading, laat het werk dan door een vakbekwaam installateur uitvoeren.

## Externe aansluitingen

| Aansluiting | Functie in de huidige firmware | Signaal |
|---|---|---|
| `Q` | Lokale aanvoertemperatuur en, bij Quatt V1, flowmeting | PT1000 en flowmeter-puls |
| `R1` | CV-ketel via aan/uit | Potentiaalvrij relais voor een aan/uit-kamerthermostaatingang (bijv. RT, RT24 of TA): `COM` + `NO` |
| `R2` | Configureerbaar hulprelais, standaard uit | Potentiaalvrij wisselrelais: `NC` / `COM` / `NO` |
| `T` | Optionele lokale temperatuursensor | 1-Wire Dallas/DS18B20: `+3.3V`, `GND`, `DATA` |
| `OTT` | Kamerthermostaat | OpenTherm slave, twee aders |
| `OTB` | CV-ketel | OpenTherm master, twee aders; alleen aansluiten op de digitale OpenTherm-klemmen van de ketel |
| `M1` | Quatt-buitenunit(s) | RS485 Modbus: `GND` / `A` / `B` |
| `M2` | Quatt-app via CiC (optioneel) | RS485 Modbus: `GND` / `A` / `B` |
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
| Quatt-app via CiC | `M2` | UART met RS485 DE/RE | TX `GPIO45`, RX `GPIO39`, DE/RE `GPIO38` |
| Ethernet W5500 | RJ45 | SPI | MOSI `GPIO10`, MISO `GPIO11`, CLK `GPIO12`, CS `GPIO13`, INT `GPIO9` |
| Status-led geel | Front | GPIO-uitgang | `GPIO1` |
| Status-led rood | Front | GPIO-uitgang | `GPIO2` |
| Boot-/herstelknop | Front | GPIO-ingang met pull-up | `GPIO46` |

## Functie per interface

### Q: PT1000 en flowmeter

De `Q`-sensorstekker bevat de PT1000 voor de lokale aanvoertemperatuur en de flowmeter-puls van Quatt V1. De PT1000 gebruikt een MAX31865 met een referentieweerstand van 1500 Ω, een nominale weerstand van 1000 Ω en 2-draadsbedrading. De firmware leest hem alleen wanneer **Lokale aanvoertemperatuur** op `PT1000` staat.

De 6-polige Molex MX3.0-stekker van de `Q`-aansluiting heeft deze pinbezetting:

| Pin | Functie | Aansluiting |
|---|---|---|
| `1` | `+5V` | Rode voedingsdraad van de flowmeter |
| `2` | PT1000 | Eerste draad van de 2-draads PT1000 |
| `3` | Flowpuls | Collector van de BC547 bij gebruik van een alternatieve B10-flowmeter |
| `4` | `GND` | Zwarte draad van de flowmeter en emitter van de BC547 |
| `5` | PT1000 | Tweede draad van de 2-draads PT1000 |
| `6` | Niet gebruikt | Niet aansluiten |

De twee PT1000-draden op pin 2 en 5 mogen worden verwisseld. Bepaal de pinnummers aan de hand van de markeringen op de stekker; ga niet alleen af op links/rechts, omdat dit afhangt van de kijkrichting.

De pulslezer op `GPIO15` heeft een interne pull-up en een filter van 100 µs. Kies in de web-app onder **Instellingen → Bronnen / integraties → Sensorselectie → Flow → Lokale flowmeter** het aangesloten type. In Home Assistant en de standaard ESPHome-webinterface heet deze instelling **Controller Flow Meter**:

- **Huba Control 236** (standaard, normaliter door Quatt geïnstalleerd): behoudt de bestaande omrekening met 0,05 l/min per Hz en eventuele Huba-configuratieaanpassingen.
- **Custom**: gebruikt een zelf ingestelde, lineaire kalibratie in pulsen per liter voor bijvoorbeeld sensoren uit de ZJ-B10- en YF-B10-familie.

Een pulswaarde van nul blijft bij ieder profiel nul; de bestaande timeout van 5 seconden blijft behouden. Sensoren binnen dezelfde B10-productfamilie kunnen verschillende kalibraties hebben. Zoek daarom in de documentatie van de daadwerkelijk geleverde sensor hoeveel pulsen per liter deze afgeeft en neem geen waarde over op basis van alleen de modelnaam.

Bij **Custom** verschijnt de instelling **Pulsen per liter**. Neem deze waarde over uit de datasheet of bepaal hem door een bekend watervolume te meten: deel het getelde aantal pulsen door het werkelijk doorgestroomde aantal liters. De firmware rekent vervolgens lineair met `flow [l/h] = pulsen per minuut × 60 / pulsen per liter`. Waarden van 1 tot en met 10.000 pulsen per liter zijn instelbaar; de standaardwaarde is 476.

De keuze blijft bewaard na een herstart. De bestaande middeling over 10 seconden blijft actief; wacht na wisselen tot de meting is bijgewerkt. De keuze past alleen de lokale pulsmeting aan. Bij V1.5 en V2 gebruikt OpenQuatt normaal de flowmeting uit de buitenunit; de keuze is instelbaar via **Q Flow Source** (`Auto`, `Local` of `Outdoor unit`). Kies `Local` om de aangesloten controller-flowmeter expliciet te gebruiken.

#### Alternatieve B10-flowmeter aansluiten

Een alternatieve B10-flowmeter is niet rechtstreeks plug-and-play op de `Q`-aansluiting. Plaats een BC547 NPN-transistor en een weerstand van 4,7 kΩ in de signaalkabel. De transistor maakt van het 5V-signaal van de flowmeter een door de controller opgetrokken pulsingang. De pulsfrequentie blijft daarbij gelijk.

Benodigd:

- BC547 NPN-transistor;
- weerstand van 4,7 kΩ;
- geschikte draad, soldeerverbindingen en krimpkous of vergelijkbare isolatie;
- 6-polige Molex MX3.0-stekker voor de `Q`-aansluiting.

Sluit de draden als volgt aan:

1. Verbind de rode `+5V`-draad van de flowmeter rechtstreeks met pin 1.
2. Verbind de gele signaaldraad via de weerstand van 4,7 kΩ met de basis (`B`) van de BC547.
3. Verbind de emitter (`E`) van de BC547 met de zwarte `GND`-draad en met pin 4.
4. Verbind de collector (`C`) van de BC547 met de flowpulsingang op pin 3.
5. Verbind de twee draden van de PT1000 met pin 2 en pin 5. De volgorde maakt niet uit.
6. Laat pin 6 vrij.

Controleer de `C`-, `B`- en `E`-aansluitingen aan de hand van de datasheet van jouw BC547. De pootvolgorde kan per behuizing of fabrikant verschillen.

![Bedradingsschema B10-flowmeter naar de Q-stekker](assets/zj-b10-q-aansluiting.svg)

Isoleer na het doormeten iedere soldeerverbinding afzonderlijk. Schakel de controller daarna in, kies **Custom** als lokale flowmeter, vul de juiste waarde in bij **Pulsen per liter** en controleer de gemeten flow terwijl de circulatiepomp draait.

### R1 en R2: relais

`R1` is de aan/uit-route voor de CV-ketel. Gebruik hiervoor alleen `COM` en `NO`; `NC` blijft vrij.

`R2` is een afzonderlijk hulprelais. Het is standaard uitgeschakeld en kan in de web-app worden ingesteld voor bijvoorbeeld een fancoil, pomp of klep. De beschikbare klemmen zijn `NC`, `COM` en `NO`.

### T: DS18B20

`T` is de 1-Wire-bus voor een optionele Dallas/DS18B20. Kies deze bron alleen als de lokale aanvoertemperatuur op `DS18B20` staat.

### OTT en OTB: OpenTherm

Op `OTT` gedraagt de HCQ zich als OpenTherm-slave tegenover de kamerthermostaat. `OTB` is de OpenTherm-masterroute naar de CV-ketel en is op de huidige Q-edition-firmware beschikbaar. Tijdens Quick Start kan de firmware een geldige OpenTherm-ketelverbinding op `OTB` detecteren en die route kiezen; zonder geldige link blijft ketelaansturing geblokkeerd.

### M1 en M2: Modbus

`M1` is de primaire RS485-poort voor de buitenunit(s). De HCQ is hier Modbus-master met 19200 baud, 8E1 en DE/RE op `GPIO41`.

`M2` is de optionele tweede RS485-poort voor **Quatt-app via CiC**. Hier is de HCQ Modbus-server met 19200 baud, 8E1 en DE/RE op `GPIO38`. De CiC leest via deze koppeling alleen buitenunitgegevens; thermostaatgegevens gaan niet naar de CiC. Schakel na het aansluiten **Quatt-app via CiC** in als de Quatt-app via de CiC moet blijven meekijken. Dit staat los van **CiC JSON-feed inlezen**.

### Ethernet, leds en herstelknop

In een Ethernet-build gebruikt de W5500 de SPI-pinnen in de tabel. In een Wi-Fi-build zet de firmware diezelfde W5500 in power-down; Ethernet en Wi-Fi zijn afzonderlijke firmware-builds.

De gele led brandt wanneer de actieve netwerkverbinding verbonden is. De rode led signaleert een actieve firmware-, sensor-, OpenTherm-, warmtepomp- of veiligheidsfout. Houd de herstelknop vijf seconden ingedrukt om een tijdelijk herstelvenster voor de web-login te openen.

## Bronnen in de firmware

De GPIO-koppeling staat in `openquatt/profiles/heatpump_controller_q.yaml`. De functie per aansluiting volgt uit de daarin opgenomen packages, waaronder de OpenTherm-, CiC-compatibiliteits-, status-led- en hulprelaisconfiguratie. Werk deze pagina mee bij wanneer die hardwareconfiguratie wijzigt.
