# Heatpump Controller Q-edition aansluiten en in gebruik nemen

Van nieuwe controller naar een werkende OpenQuatt-installatie. De Heatpump Controller Q-edition (HCQ) wordt standaard geleverd met `Single` + `Wi-Fi` voorgeïnstalleerd. Je brengt hem eerst online; de web-app begeleidt je daarna bij de juiste configuratie voor jouw opstelling en netwerkverbinding.

## De hoofdroute in vier stappen

1. Maak de CiC en Quatt-buitenunit(s) spanningsloos, verplaats de kabels en voer de eindcontrole uit.
2. Voed de HCQ, schakel de Quatt-buitenunit(s) weer in en stel Wi-Fi in.
3. Open `openquatt.local` en controleer de basisverbinding.
4. Kies in **Quick Start → Configuratie en software-update** de juiste configuratie, laat de software controleren en rond Quick Start af.

Daarna werkt OpenQuatt zelfstandig via de web-app. Home Assistant is een optionele vervolgstap voor dashboards en automatisering.

## 1. Controller aansluiten

Schakel eerst de CiC uit en maak alle Quatt-buitenunits spanningsloos, bijvoorbeeld met de werkschakelaar. Maak daarna een foto van de complete aansluitstrook en label de kabels voordat je iets losmaakt.

> [!WARNING]
> Twijfel je over de bedrading of aansluitingen? Stop dan en laat dit door een vakbekwaam installateur uitvoeren.

De HCQ neemt de signaalkabels van de CiC over. Sluit dezelfde kabel nooit tegelijk op de CiC en de HCQ aan.

### Interactief aansluiten: één stap tegelijk

Gebruik de foto van je eigen CiC als uitgangspunt. De stappenhulp toont steeds één grote stap. Kies een stap bovenaan of gebruik **Vorige** en **Volgende**. Bij stap 4 laat je de ketelkabel nog op de CiC zitten. Vergelijk de aansluiting met de twee voorbeelden en kies OpenTherm of aan/uit. Daarna toont de hulp alleen het bijbehorende aansluitschema.

![Kabels stap voor stap verplaatsen van Quatt CiC naar Heatpump Controller Q-edition](assets/q-edition-kabels-stappen.svg)

### Welke kabel gaat waarheen?

Draadkleuren in de schema's en stappenhulp zijn illustratief. De klemmarkeringen en functies zijn altijd leidend.

| Van de CiC | Naar de HCQ | Zo sluit je aan |
|---|---|---|
| Buitenunit(s) · Modbus `A/G/B` | `M1` · `GND/A/B` | Let goed op de juiste volgorde: `A → A` (rood), `G → GND` (groen), `B → B` (blauw). |
| Kamerthermostaat · OpenTherm | `OTT` | Neem de twee aders over. |
| CV-ketel · OpenTherm | `OTB` | Neem de twee aders over. |
| CV-ketel · aan/uit | `R1` · `COM + NO` | Het CV-aan/uit-contact van de CiC zit onder een apart afdekkapje. Op R1 blijft de bovenste klem `NC` vrij; gebruik de middelste klem `COM` en de onderste klem `NO`. |
| Quatt flowmeter / PT1000 | `Q` | Steek de bestaande Quatt-sensorstekker over. |
| **Optioneel:** vrijgekomen buitenunit-Modbus `A/G/B` op de CiC | `M2` · `GND/A/B` | Gebruik een aparte RS485-kabel: `A → A`, `G → GND`, `B → B`. |

> [!NOTE]
> Bij de OpenTherm-verbindingen (`OTT` en `OTB`) en het aan/uit-contact (`R1`: `COM` + `NO`) maakt de polariteit of volgorde van de twee aders niet uit. Gebruik wel de genoemde aansluitklemmen.

> [!IMPORTANT]
> Kies voor de CV-ketel óf `OTB` óf `R1`; gebruik beide routes niet tegelijk.

> [!TIP]
> De verbinding tussen `M2` en de CiC is optioneel. Activeer daarna **CiC-compatibiliteit** onder **Instellingen → Bronnen / integraties** als de Quatt app via de CiC moet blijven meekijken. Deze functie staat standaard uit en geeft alleen OpenQuatt-data door; de CiC neemt de regeling niet over.

### M1, M2 en de optionele aansluitingen

- **M1** is de primaire Modbuspoort voor de Quatt-buitenunit(s). Deze verbinding is nodig voor de normale regeling.
- **M2** is de optionele Modbuspoort voor CiC-compatibiliteit. Verbind M2 alleen met de vrijgekomen Modbuspoort van de CiC als de Quatt app moet blijven meekijken.
- **R2** is een tweede potentiaalvrij wisselrelais met `NC`, `COM` en `NO`. R2 kan optioneel als hulprelais worden ingesteld via **Instellingen → Installatie → Hulprelais (R2)**, bijvoorbeeld om een fancoil, pomp of klep te laten volgen op de warmte- of koelvraag van OpenQuatt. Standaard staat deze functie uit en blijft R2 onbekrachtigd. Sluit apparatuur die moet inschakelen bij een actief relais aan op `COM` + `NO`; heb je geen hulpuitgang nodig, laat deze aansluiting dan vrij.
- **T** is een 1-Wire-aansluiting voor een optionele Dallas/DS18B20-temperatuursensor: `+3.3V`, `GND` en `DATA`.

### Aansluitingen op de HCQ

Onderstaand referentiebeeld toont de fysieke positie en functie van alle aansluitingen. De aansluitingen staan op de behuizing aangeduid als `Q`, `R1`, `R2`, `T`, `M1`, `M2`, `OTT` en `OTB`.

[![Referentiebeeld met alle aansluitingen van de Heatpump Controller Q-edition](assets/hcq-aansluitingen-referentie.png)](assets/hcq-aansluitingen-referentie.png)

[Open het referentiebeeld op volledige grootte](assets/hcq-aansluitingen-referentie.png)

Voor de technische functie per aansluiting en de GPIO-koppeling zie [HCQ aansluitingen en technische I/O](hcq-io-overzicht.md).

### Na het overzetten

Controleer in de laatste aansluitstap nog eenmaal `M1`, de gekozen ketelroute, de eventuele `M2`-verbinding en alle stekkers. Sluit daarna de USB-voeding aan op de USB-poort van de HCQ. Schakel vervolgens de Quatt-buitenunit(s) weer in, bijvoorbeeld met de werkschakelaar.

Laat de USB-poort bereikbaar. Je hebt deze later ook nodig voor Wi-Fi provisioning, een firmwarewissel of herstel.

## 2. Wi-Fi instellen

Een Wi-Fi-build biedt twee routes. Op een computer is provisioning via USB meestal het handigst. Op een telefoon of tablet staat de route via het OpenQuatt access point daarom als eerste.

Wil je de HCQ uiteindelijk via Ethernet gebruiken? Breng de geleverde `Single` + `Wi-Fi`-build ook dan eerst via deze stap online en sluit de netwerkkabel aan. In Quick Start kies je daarna de juiste `Single`- of `Duo`-Ethernetsetup; de web-app installeert dan de bijbehorende firmware.

### Route A: via USB

Deze route schrijft alleen de Wi-Fi-gegevens naar de controller en flasht geen nieuwe firmware.

1. Sluit de HCQ met een USB-datakabel aan op je computer.
2. Open de provisioningtool met de knop hieronder.
3. Klik op **Configureer Wi-Fi** en kies de USB-poort van de controller.
4. Vul de netwerknaam en het wachtwoord in.
5. Wacht tot de controller verbinding heeft en open daarna `http://openquatt.local`.

[Configureer Wi-Fi via USB](install/index.html#wifi-provision-panel)

Gebruik deze route ook als alleen de netwerknaam of het Wi-Fi-wachtwoord is gewijzigd.

### Route B: via het OpenQuatt access point

Kan de controller geen verbinding maken met het ingestelde Wi-Fi-netwerk, dan start een Wi-Fi-build een eigen access point met captive portal:

- netwerknaam: `OpenQuatt`;
- wachtwoord: `openquatt`.

1. Open de Wi-Fi-instellingen van je telefoon, tablet of computer.
2. Verbind met het netwerk `OpenQuatt`.
3. Vul het wachtwoord `openquatt` in.
4. Wacht tot de captive portal opent en kies daar je eigen Wi-Fi-netwerk.
5. Vul het Wi-Fi-wachtwoord in en laat de controller verbinden.
6. Verbind je telefoon of computer weer met je normale netwerk.
7. Open `http://openquatt.local`.

Verschijnt de captive portal niet? Blijf verbonden met `OpenQuatt` en open handmatig [http://192.168.4.1/](http://192.168.4.1/) in je browser.

> [!NOTE]
> Het access point is een tijdelijke configuratieroute en niet bedoeld als normale netwerkverbinding. Bij Ethernet is deze route niet beschikbaar.

## 3. OpenQuatt voor het eerst openen

Open na de netwerkverbinding:

```text
http://openquatt.local
```

Werkt deze naam niet, zoek dan het IP-adres van OpenQuatt in je router en open `http://<ip-adres>`.

Controleer voordat je verdergaat:

- de controller blijft online;
- de firmwareversie wordt getoond;
- ten minste de basisgegevens van de eerste warmtepomp worden bijgewerkt.

Gebruik je een Duo-opstelling, of wijkt de gewenste setup af van de voorgeïnstalleerde `Single` + `Wi-Fi`-build? Controleer de tweede warmtepomp en alle meetwaarden pas volledig nadat je in de volgende stap de juiste setup hebt gekozen.

Zie [Web-app gebruiken](web-app.md) voor bediening, updates, backups en beveiliging.

## 4. Quick Start afronden

Quick Start verschijnt zolang de basisinstellingen nog niet zijn afgerond. De eerste stap heet in de web-app **Configuratie en software-update**. De gemarkeerde kaart toont welke configuratie nu actief is; bij levering is dat normaal `Single · Wi-Fi`.

Kies hier direct de combinatie die bij je installatie hoort:

- `Single · Wi-Fi` of `Single · Ethernet` voor één warmtepomp;
- `Duo · Wi-Fi` of `Duo · Ethernet` voor twee warmtepompen.

Sluit bij Ethernet eerst de netwerkkabel aan. Kies alleen `Duo` als de installatie daadwerkelijk twee warmtepompen heeft.

Na bevestiging controleert OpenQuatt de nieuwste stabiele release voor de gekozen configuratie. Alleen als de softwareversie of configuratie afwijkt, installeert OpenQuatt de juiste stabiele release en start de controller opnieuw op. Een aanwezige dev- of testbuild wordt daarmee vervangen. Zijn versie en configuratie al correct, dan gaat Quick Start zonder OTA verder. **Bestaande OpenQuatt-instellingen blijven bij een software-update of configuratiewissel behouden.** Je hoeft bij de eerste ingebruikname dus niet via **Instellingen → Systeem → Updates** te wisselen. Open na een herstart zo nodig opnieuw `http://openquatt.local` en ga verder met Quick Start.

Daarna geef je aan welke Quatt Hybrid en installatie je hebt. V1, V1.5 en V2 beschrijven de generatie van de warmtepomp en staan los van de keuze voor `Single` of `Duo`.

- Kies `V1` bij model `AMM4`: flowmeter bij de CV-ketel en vorstbeveiligingsklep buiten de buitenunit. Dit geldt ook voor een gemengde V1/V1.5 Duo.
- Kies `V1.5` bij model `AMM4-V1.5`: flowmeter in de buitenunit en onder de CV-ketel alleen een kleine clip-on temperatuursensor.
- Kies `V2` bij model `AMH6` of `AMH6-2`: flowmeter in de buitenunit en onder de CV-ketel alleen een kleine clip-on temperatuursensor.

Volg de route die de web-app voor jouw installatie toont. De basisstappen zijn:

1. **Configuratie en software-update:** `Single` of `Duo` en Wi-Fi of Ethernet; daarna controleert OpenQuatt de stabiele main-release en installeert deze alleen als dat nodig is.
2. **Kies je Quatt Hybrid:** V1, V1.5 of V2.
3. **Flowmeting configureren:** controleer en activeer de juiste flowbron.
4. **Thermostaatgegevens configureren:** kies waar kamertemperatuur en kamer-setpoint vandaan komen.
5. **CV-ketel of boiler:** geef aan of OpenQuatt deze als ondersteuning mag gebruiken.
6. **Kies de verwarmingsstrategie:** kies hoe OpenQuatt de verwarming regelt.
7. **Werk de regeling uit:** stel Power House of de stooklijn verder in.
8. **Flowregeling en afstelling:** leg vast hoe de pomp geregeld moet worden en welke waarden daarbij horen.
9. **Watertemperatuur beveiligen:** controleer de normale bovengrens en de tripgrens.
10. **Stille uren en niveaus:** stel het stille venster en de compressorlimieten voor dag en nacht in.
11. **Gebruiksstatistieken:** controleer of OpenQuatt beperkte technische systeemstatus, aan/uit-statussen van functies en configuratiekeuzes zoals Quatt Hybrid-versie, verwarmingsstrategie, flowbron en regelbronnen mag delen; tijdens een nieuwe Quick Start staat dit standaard aan en kan het hier worden uitgezet. Gemeten of ingestelde temperaturen, wifi-gegevens, gebruikersnamen en wachtwoorden worden nooit meegestuurd.
12. **Bevestigen en afronden:** controleer je keuzes en markeer Quick Start als voltooid.

## Je installatie is klaar wanneer

- `openquatt.local` stabiel bereikbaar is;
- Quick Start volledig is afgerond;
- de warmtepompgegevens worden bijgewerkt;
- aanvoertemperatuur, flow en buitentemperatuur aannemelijke waarden tonen.

Vanaf dit punt kun je OpenQuatt zelfstandig via de web-app gebruiken. Home Assistant en het dashboard zijn optionele vervolgstappen.

## Configuratie later wijzigen

Heb je Quick Start al afgerond en verandert de installatie later, dan kun je dezelfde firmwarewissel alsnog via de web-app starten. Maak voor de zekerheid eerst een backup; de bestaande OpenQuatt-instellingen blijven tijdens de update of wissel behouden.

Open in de web-app **Instellingen → Systeem** en kies bij **Updates** voor **Openen**. Onder **Geavanceerd** vind je, wanneer beschikbaar, **Opstelling wisselen** en **Verbinding wisselen**.

- Gebruik **Opstelling wisselen** om tussen `Single` en `Duo` te wisselen.
- Gebruik **Verbinding wisselen** om tussen Wi-Fi en Ethernet te wisselen. Sluit vóór een wissel naar Ethernet de netwerkkabel aan.
- Wijzig V1, V1.5 of V2 onder **Instellingen → Installatie**; daarvoor is geen firmwarewissel nodig.
- Gebruik voor alleen een andere Wi-Fi-netwerknaam of een ander wachtwoord opnieuw **Configureer Wi-Fi via USB** of het OpenQuatt access point.

> [!IMPORTANT]
> Wi-Fi en Ethernet blijven aparte firmware-builds. Een Ethernet-build heeft geen Wi-Fi fallback of captive portal. De web-app voert zo'n wissel daarom uit als firmware-update en toont vooraf de bijbehorende controle.

## Optioneel: toevoegen aan Home Assistant

Home Assistant is optioneel voor OpenQuatt zelf en aanbevolen voor dashboards en automatisering. Zodra OpenQuatt en Home Assistant op hetzelfde netwerk zitten, wordt het ESPHome-apparaat meestal automatisch gevonden.

Open de melding bij **Instellingen → Apparaten & diensten** en kies **Configureren**. Verschijnt er geen melding, kies dan **Integratie toevoegen → ESPHome** en vul `openquatt.local` of het IP-adres van OpenQuatt in.

Gebruik de bestaande handleidingen voor de vervolgstappen:

- [Het juiste Single- of Duo-dashboard installeren](dashboard/README.md)
- [Het dashboard gebruiken](dashboardoverzicht.md)

Selecteer bij de eerste toevoeging nog geen Home Assistant-area. Wacht tot de OpenQuatt-entiteiten zijn aangemaakt en ken daarna pas een area toe.

## Als het niet lukt

- **Geen USB-poort zichtbaar:** controleer of je een USB-datakabel gebruikt en probeer een andere USB-poort.
- **Instabiel of onverklaarbaar gedrag:** een USB-voedingsadapter van onvoldoende kwaliteit of vermogen kan vreemde storingen veroorzaken. Probeer een andere, betrouwbare voedingsadapter.
- **Geen captive portal:** controleer dat je met `OpenQuatt` bent verbonden en dat je geen Ethernet-build gebruikt.
- **`openquatt.local` opent niet:** zoek het IP-adres in je router.
- **Geen warmtepompdata:** controleer voeding, communicatiebedrading en of `Single` of `Duo` klopt.
- **Niet gevonden in Home Assistant:** controleer eerst of de web-app lokaal bereikbaar is.

Ga voor verdere diagnose naar [Problemen oplossen](problemen-oplossen.md). Gebruik [Handmatige installatie](handmatige-installatie.md) alleen als de normale installer niet werkt.
