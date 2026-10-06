# Web-app gebruiken

De OpenQuatt web-app is de lokale bedienings- en instellingenpagina van je OpenQuatt-module. Zodra de controller online is, is dit de eerste plek waar je naartoe gaat: open `http://openquatt.local` en rond Quick Start af. Home Assistant komt eventueel daarna.

## Wanneer gebruik je de web-app?

Gebruik de web-app voor alles wat direct op OpenQuatt zelf hoort:

- eerste ingebruikname via Quick Start;
- controleren of OpenQuatt online is en logisch meet;
- verwarmingsstrategie en flowregeling kiezen;
- koeling en dauwpuntbeveiliging instellen;
- firmware-updatekanaal en updates beheren;
- backup en restore van OpenQuatt-instellingen;
- web-login, API-beveiliging, logboek en herstarten.

De web-app blijft altijd de plek waar je OpenQuatt inricht, beheert en controleert als er iets niet klopt. Gebruik je Home Assistant, dan is dat daarnaast een prettige plek voor dagelijks meekijken, dashboards en automatisering.

## Beveiligde verbinding met Home Assistant

Home Assistant stelt bij de eerste koppeling automatisch de beveiligde ESPHome-verbinding in. Het koppelvenster is maximaal 10 minuten na het opstarten; een bestaande koppeling blijft behouden bij herstarten en updates. Je hoeft geen API-key te kopiëren en de web-app toont de sleutel nooit.

Volg [OpenQuatt koppelen aan Home Assistant](dashboard/koppelen.md) voor de volledige procedure, de statuscontrole en problemen met een verlopen koppelvenster of onbekende sleutel. Voor herstel vanuit de web-app of met de fysieke knop zie [API-beveiliging resetten](#api-beveiliging-resetten).

## Wat doe je waar?

| Plek | Gebruik je vooral voor |
|---|---|
| Q-edition-handleiding | Een voorgeïnstalleerde Heatpump Controller Q aansluiten, online brengen en juist configureren. |
| Installer | Wi-Fi op een nieuwe HCQ instellen of een HCQ herstellen. |
| Web-app | Quick Start, installatiekeuzes, instellingen, updates, backup en beveiliging. |
| Optioneel: Home Assistant | Dagelijks meekijken, dashboards, bronselectie en dynamische bronnen. |
| Optioneel: Homey | Dagelijks meekijken, flows en OpenQuatt voeden vanuit Homey-sensoren. |

Kies bij een eerste installatie eerst de passende route in het [projectoverzicht](../README.md#kies-je-route). Beide installatieroutes komen uit bij de web-app; Home Assistant is geen onderdeel van de basisinstallatie.

## Openen

Probeer eerst:

```text
http://openquatt.local
```

Lukt dat niet, zoek dan het IP-adres van OpenQuatt in je router of in Home Assistant en open:

```text
http://<ip-adres>
```

De web-app draait lokaal op je eigen netwerk. Je gebruikt dus geen cloudaccount en hoeft niets externs open te zetten.

Wil je de interface eerst rustig bekijken zonder echte hardware, open dan de [web-app demo op GitHub Pages](https://openquatt.github.io/OpenQuatt/demo/). Die gebruikt dezelfde look-and-feel in mockmodus.

## Taal kiezen

De web-app is volledig beschikbaar in het Nederlands en Engels. Open rechtsboven het paneel **Weergave en systeem** en kies onder **Taal / Language** voor `Nederlands` of `English`. De keuze wordt lokaal in de browser bewaard en blijft na herladen en een controllerherstart actief. Zonder opgeslagen keuze blijft Nederlands de standaardtaal.

De taalkeuze vertaalt de interface, meldingen, statussen en datum-/getalopmaak. Technische firmwarewaarden, entitynamen en API-/MQTT-waarden blijven ongewijzigd zodat koppelingen en backupbestanden compatibel blijven.

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
| `Gebruiksstatistieken` | Wel of niet beperkte technische systeemstatus en feature-instellingen delen | Tijdens een nieuwe Quick Start staat delen standaard aan…5617 tokens truncated… een andere Home Assistant-invoer vereist wel een nieuwe kalibratie. Een korte automatische fallback tijdens een bronstoring wordt ongecorrigeerd gebruikt en wist geen opgeslagen bronkalibratie.

Onder `Installatiebewaking` zie je per warmtepomp actieve en herstellende incidenten, wat daarvan het effect op de regeling is en hoe OpenQuatt erop reageert. Herstelde gelatchte incidenten blijven zichtbaar totdat je de melding als gezien markeert. Als een storing volgens de warmtepomp een echte uit- en inschakeling van de buitenunit vereist, verschijnt een aparte knop waarmee je na uitvoering bevestigt dat de powercycle werkelijk is uitgevoerd. Het paneel toont daarnaast compressorstarts, hydraulische aandachtspunten en verbindingsstatussen. De alarmgrenzen voor compressorstarts zijn uitklapbaar en bedoeld voor incidentele aanpassing.

### Systeem

Hier vind je beheerfuncties:

- Quick Start opnieuw openen;
- opslag voor Diagnose, Beslislog en Energie;
- firmware-updates en updatekanaal;
- web-login en API-beveiliging;
- de keuze voor gebruiksstatistieken;
- backup en restore;
- systeemstatus;
- logboek;
- herstarten.

#### Gebruiksstatistieken en privacy

Tijdens een nieuwe Quick Start staat delen standaard aan en verschijnt de opt-out vóór het afronden. De keuze wordt pas opgeslagen wanneer die stap werkelijk wordt geopend. Je kunt de keuze later wijzigen via **Instellingen → Systeem → Gebruiksstatistieken**. Zolang Quick Start niet is afgerond, wordt niets verzonden. Daarna, of wanneer je delen later zelf aanzet, verstuurt OpenQuatt vrijwel direct en vervolgens ongeveer elk uur één klein bericht naar de centrale OpenQuatt-loggingserver.

Een ontbrekende telemetrykeuze geldt nooit als toestemming. Bestaande installaties starten na de introductie van deze functie daarom met delen uit, ook wanneer hun oude Quick Start-status ontbreekt. Als correctie op de eerste telemetryversie wordt de oude opslagindeling eenmalig naar uit gemigreerd; het willekeurige installatie-ID blijft behouden. Ook wie delen in die korte eerste versie bewust had aangezet, moet het daardoor eenmalig opnieuw inschakelen. Nieuwe installaties krijgen de standaard-aan opt-out alleen wanneer ze de gebruiksstatistiekenstap van Quick Start werkelijk openen.

Het bericht bevat uitsluitend:

- een willekeurig installatie-ID;
- de Unix-timestamp van de momentopname in seconden, of `null` zolang de klok nog niet is gesynchroniseerd;
- uptime;
- firmwareversie en releasekanaal;
- hardwareprofiel en, als beschikbaar, hardwarerevisie;
- `Single` of `Duo` en `Wi-Fi` of `Ethernet`;
- `quatt_hybrid_generation_config`: `v1`, `v1_5` of `v2` volgens de ingestelde Quatt Hybrid-versie;
- `flow_source_config`: `cic`, `controller_local` of `outdoor_unit`, afgeleid uit de algemene en (bij Q) Q-specifieke flowselectie;
- `heating_strategy`: `power_house` of `heating_curve`;
- de gekozen regelbronnen in `room_temperature_source`, `room_setpoint_source`, `outside_temperature_source`, `heating_enable_source`, `cooling_enable_source`, `cooling_dew_point_source`, `external_heat_demand_source` en `heating_supply_target_source`, genormaliseerd naar vaste waarden zoals `auto`, `local`, `outdoor_unit`, `cic`, `opentherm`, `home_assistant`, `api_input`, `mqtt`, `cic_or_home_assistant`, `schedule`, `disabled` en `heating_curve`;
- vrij heapgeheugen, het minimum sinds de start, het grootste vrije heapblok en vrij PSRAM;
- maximale looptijd van de firmwareloop, ESP-chiptemperatuur en reden van de laatste herstart;
- vier Modbus-betrouwbaarheidstellers, rechtstreeks op de primaire ODU-bus geteld en onafhankelijk van het ingestelde logniveau: partial responses, parse failures, succesvol herstelde responses met voor- of naloopruis en offline-transities. Per bericht wordt de toename sinds de vorige succesvolle publicatie verstuurd;
- bij Wi-Fi: de signaalsterkte in dBm;
- of CiC JSON-feed inlezen, Quatt-app via CiC en de OpenTherm-thermostaatkoppeling aanstaan;
- `boiler_assist_enabled`: of CV-ketel-/boilerondersteuning aanstaat;
- `boiler_connection`: `on_off` voor de `R1`-aansluiting en `opentherm` voor OTB; firmware zonder OTB-keuze rapporteert automatisch `on_off`;
- of MQTT inputbronnen als geheel aanstaan;
- of RAM-trends, flashtrends, beslisloghistorie en lifetime-energiehistorie aanstaan; RAM-loghistorie wordt altijd als `true` gerapporteerd omdat die permanent actief is.

Een niet-ondersteunde functie, tijdelijk nog niet geïnitialiseerde keuze, onbekende keuze of niet-beschikbare sensor krijgt de waarde `null`; `false` betekent dat de functie beschikbaar maar uitgeschakeld is. Dit geldt ook afzonderlijk voor de nieuwe configuratievelden. `flow_source_config` is `null` zolang de benodigde flowselectie nog geen bekende toestand heeft. Zo is de Wi-Fi-signaalsterkte bij Ethernet `null`. `boiler_connection` is alleen `null` wanneer de OTB-select bestaat maar tijdelijk nog geen geldige toestand heeft, of een onbekende optie bevat.

Het bericht bevat nooit een MAC-adres, lokaal IP-adres, wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of andere inloggegevens. Ook MQTT-servergegevens, topics, ontvangen MQTT-waarden, ingestelde temperaturen of grenzen, verwarmingsmetingen, regelwaarden, Modbus-frames en gewone logregels gaan niet mee. Alleen de hierboven genoemde communicatiebetrouwbaarheidstellers worden gedeeld. De OpenQuatt-loggingserver ziet bij een netwerkverbinding technisch wel het bron-IP-adres, maar dit staat niet in de payload en OpenQuatt slaat het niet op. In de web-app staat onder **Welke gegevens worden gedeeld?** (in Quick Start **Wat gaat er mee?**) een eenmalige momentopname van de JSON-vorm. De vier Modbus-tellers worden rechtstreeks uit de ODU-bus gelezen bij de echte verzending en zijn bewust geen web-/Home Assistant-entiteiten; daarom staan alleen deze vier velden in de lokale preview op `null`. Het getoonde `message_id` en `timestamp_s` worden voor een echte verzending opnieuw bepaald; `reset_reason` is niet via de lokale web-API beschikbaar en staat in deze preview eveneens op `null`.

Wanneer delen voor het eerst actief wordt, maakt de controller met de hardware-randomgenerator een UUIDv4 aan en bewaart die lokaal. Een UUIDv4 heeft 122 willekeurige bits; zelfs bij één miljoen installaties is de kans op minstens één dubbel ID kleiner dan ongeveer `10^-25`. Dit ID blijft gelijk na een OTA-update en wanneer je delen tijdelijk uitzet. Je kunt het bekijken via **Instellingen → Systeem → Gebruiksstatistieken**. Een fabrieksreset maakt een nieuw ID. De keuze en het ID worden niet via een instellingenbackup naar een andere controller gekopieerd. Uitzetten stopt nieuwe berichten direct; er wordt geen wachtrij voor later opgeslagen. Na een mislukte verzending maakt iedere retry een verse momentopname, maar behoudt binnen dezelfde retryreeks het `message_id` zodat een verloren QoS 1-bevestiging kan worden gededupliceerd.

De statistiekenclient staat los van de configureerbare [MQTT inputbronnen](mqtt.md): hij publiceert alleen dit ene bericht, subscribed nergens op en schakelt ESPHome MQTT-discovery, entiteitspublicaties en logexport niet in. Het JSON-bericht wordt met QoS 1 en zonder retain gepubliceerd op `openquatt/devices/<installation-id>/telemetry`. De broker bewaart het daardoor niet als retained state voor later verbindende subscribers; de loggingserver slaat ieder ontvangen bericht zelf op. Een eerder door oude firmware retained opgeslagen payload wordt door een non-retained publicatie niet gewist en moet zo nodig eenmalig op de centrale broker worden verwijderd. Een build zonder geconfigureerde centrale loggingserver maakt ook wanneer delen aanstaat geen externe verbinding.

#### Systeemrecorder voor support

De Systeemrecorder bewaart continu recente systeemgegevens, dus je hoeft een opname niet vooraf te starten:

1. Open **Diagnostiek → Systeemrecorder**.
2. Kies het venster dat het probleem afdekt: laatste 15, 30 of 60 minuten, of alles wat beschikbaar is.
3. Download het diagnosebestand.
4. Voeg het gedownloade `.oqdebug.json`-bestand toe aan je Discord-vraag of GitHub-issue. Via **Open analyser** kun je het bestand zelf alvast bekijken op OpenHeatPumps; er wordt niets automatisch verzonden.

De opname wordt lokaal in het apparaatgeheugen opgeslagen en niets wordt automatisch verzonden. Deel het bestand alleen binnen het supportverzoek waarvoor je het hebt gemaakt.

Vanuit de OpenHeatPumps-analyser kun je met een deep link terug naar de Systeemrecorder-popup: `http://<device-ip>/?view=settings&section=system&modal=systeemrecorder` (kort: `http://<device-ip>/#systeemrecorder`). De popup opent dan automatisch.

## Backup en restore

Maak een backup voordat je grotere wijzigingen doet of voordat je een factory-update uitvoert.

De backup bevat de instellingen die de web-app beheert, inclusief de vier warmtepompoffsets en iedere geldige aanvoeroffset die per bron is opgeslagen. De MQTT-configuratie wordt ook meegenomen, maar het MQTT-wachtwoord nooit. Bij restore vergelijkt OpenQuatt de backup met de huidige installatie, zodat je verschillen kunt controleren voordat je ze terugzet.

Externe invoerwaarden die je live aanlevert, zoals een warmtevraag, een aanvoertarget of een kamertemperatuur via MQTT of de API, zijn geen instellingen en gaan niet mee in de backup. De gekozen bron blijft wel bewaard: na een restore staan `Externe warmtevraag (Power House)` en `Aanvoertarget (stooklijn)` weer op dezelfde bron, zonder dat er een verouderde vraag of target wordt teruggezet.

De kalibratiewaarden worden op dezelfde manier als de overige instellingen hersteld, vóór de opgeslagen aanvoerbron wordt geselecteerd. Kalibreer na restore opnieuw als de controller of een temperatuursensor fysiek is vervangen; een gewone bron- of CIC-URL-wijziging verwijdert een geldige kalibratie niet.

Een backup is vooral handig bij:

- nieuwe release testen;
- overstap naar een nieuw bordje;
- factory-bin update;
- terugzetten na experimenteren met instellingen.

## Updates

De web-app toont update-informatie via de firmware-updatefunctie. Normaal volg je het stabiele kanaal.

Na het kiezen van `dev` kun je de aangeboden dev-build ook installeren wanneer deze dezelfde basisversie heeft als de draaiende main-release (bijvoorbeeld `v0.49.1` → `v0.49.1-dev.780`). De web-app bevestigt de kanaalwissel pas wanneer het device de bedoelde dev-build en het dev-kanaal meldt.

Gebruik een dev-kanaal alleen als je bewust test en weet dat de firmware nog kan veranderen. Voor releasegebruik is het stabiele kanaal de route.

Draait het device op een nieuwere dev-versie dan de laatste main-release, dan biedt de OTA-modal na het kiezen van `main` een expliciete downgrade aan. Controleer de getoonde doelversie en bevestig bewust dat je teruggaat naar oudere firmware. Maak zo nodig eerst een instellingenbackup: instellingen blijven lokaal opgeslagen, maar functies en instellingen die alleen in de dev-build bestaan, zijn na de downgrade mogelijk niet meer beschikbaar.

Bij de Heatpump Controller Q kan Quick Start vóór de verdere configuratie direct wisselen tussen `Single Wi-Fi`, `Single Ethernet`, `Duo Wi-Fi` en `Duo Ethernet`. De OTA-modal kan later nog steeds de verbinding of opstelling afzonderlijk wisselen. Dit zijn geen gewone updates: de web-app installeert de firmware voor de gekozen setup. Controleer bij Ethernet eerst of de netwerkkabel is aangesloten en bij Duo of de tweede warmtepomp bij deze controller hoort.

Als de verbinding voor de firmwaredownload niet kan worden geopend, probeert OpenQuatt dit eenmaal automatisch opnieuw. Mislukt ook die poging of wordt de installatie afgebroken, dan stopt de voortgang en kun je de setupwissel opnieuw starten.

## Web-login en API-beveiliging

Onder `Instellingen -> Systeem -> Toegang & Beveiliging` kun je de web-login en ESPHome API-encryptie aanpassen.

Vanaf de ESPHome 2026.7-build gebruikt de web-login HTTP Digest-authenticatie. De browserlogin blijft hetzelfde, maar losse REST-clients moeten Digest ondersteunen en kunnen niet meer met Basic-authenticatie aanmelden.

### Web-login herstellen

Op de HeatPump Controller Q edition is de herstelknop de **linker van de twee knoppen**.

Houd de fysieke herstelknop **5 seconden** vast en laat hem los. Open daarna
`http://openquatt.local/recovery` of `http://<IP-adres>/recovery`.
Gebruik bij een aangepaste apparaatnaam de bijbehorende hostnaam, bijvoorbeeld
`http://openquatt-test.local/recovery` voor een testcontroller.
De herstelpagina is zonder bestaande web-login bereikbaar, maar herstelacties
worden pas beschikbaar nadat je de fysieke knop hebt bediend. Zonder actief
herstelvenster toont de pagina de instructie om de knop in te drukken.
Je hebt 10 minuten om een nieuwe gebruikersnaam en wachtwoord op te slaan.
Sluit herstel daarna af; pas dan, of na afloop van het venster, geldt de nieuwe login.
Bij een opslagfout blijft de bestaande runtime-login behouden en kun je opnieuw proberen.

De gewone webinterface, REST-acties en webstreams zijn tijdens herstel afgeschermd.
Een nog geopende gewone webpagina kan daardoor een browser-inlogvenster tonen.
Annuleer dat venster en open rechtstreeks `/recovery`; de gewone login geeft
tijdens herstel geen toegang tot de normale webinterface.
Herstel opent geen algemene onbeveiligde beheeromgeving en wist geen andere instellingen.
Iedereen op hetzelfde netwerk kan tijdens het fysiek geopende venster de beperkte
herstelpagina gebruiken: voer dit alleen op een vertrouwd netwerk uit.
Een knop die tijdens boot al ingedrukt is moet eerst worden losgelaten.
Met **Herstel afsluiten**, of automatisch na 10 minuten, sluit het herstelvenster.
Open daarna de gewone webinterface zonder `/recovery`. Heb je geen nieuwe login
opgeslagen, dan blijft de eerdere web-login of open toegang gelden.

### API-beveiliging resetten

Gebruik deze reset als Home Assistant niet meer kan verbinden doordat de opgeslagen API-sleutel onbekend is of niet overeenkomt. Hiermee wis je alleen de sleutel voor de ESPHome-verbinding. Je Wi-Fi-instellingen, overige instellingen en de gebruikersnaam en het wachtwoord van de webinterface blijven behouden.

**Via de herstelpagina**

1. Houd op de Heatpump Controller Q de linker knop **5 seconden** ingedrukt en laat hem los. Daarmee open je het herstelvenster van 10 minuten.
2. Open `http://openquatt.local/recovery` of `http://<IP-adres>/recovery`. Hiervoor hoef je niet in te loggen op de webinterface.
3. Kies **API-beveiliging resetten** en bevestig het wissen van de sleutel en het herstarten van de controller.
4. Wacht tot de controller opnieuw bereikbaar is. De herstelpagina wordt na deze herstart nog één keer beschikbaar. Sluit het herstel af wanneer je klaar bent.
5. Rond het opnieuw koppelen in Home Assistant binnen 10 minuten na de herstart af.

**Via de normale instellingenpagina**

Heb je een gebruikersnaam en wachtwoord ingesteld voor de OpenQuatt-webinterface en ben je daarmee ingelogd? Dan kun je ook **API-beveiliging resetten** kiezen onder **Instellingen -> Systeem -> Toegang & Beveiliging**. Bevestig de reset en rond na de herstart het opnieuw koppelen binnen 10 minuten af. Deze route opent de herstelpagina niet.

Is de webinterface zonder wachtwoord toegankelijk, of ben je het wachtwoord vergeten? Gebruik dan de fysieke herstelroute hierboven. Alleen toegang tot een onbeveiligde webinterface is niet voldoende om de API-sleutel te wissen.

**Opnieuw koppelen in Home Assistant**

De reset verbreekt alle ESPHome API-verbindingen. Home Assistant kan daarna automatisch een sleutel instellen; dat kan dezelfde sleutel zijn als voorheen.

Bij een bestaande koppeling kan Home Assistant melden dat het apparaat transportencryptie heeft uitgeschakeld en vragen de oude sleutel te verwijderen. Bevestig dit alleen als je zelf deze reset hebt gestart en het juiste apparaat wordt genoemd. Controleer na het koppelen in **Instellingen -> Systeem -> Toegang & Beveiliging** dat API-encryptie weer actief is.

Meldt OpenQuatt dat het wissen niet is gelukt? De controller herstart dan niet automatisch. Controleer de melding voordat je opnieuw probeert. Voor verdere koppelproblemen zie [OpenQuatt koppelen aan Home Assistant](dashboard/koppelen.md#problemen-met-koppelen).

### Wi-Fi opnieuw instellen

Kies met een ingestelde web-login **Connectiviteit → Wi-Fi wissen en herstarten**,
of gebruik de fysieke herstelpagina na 5 seconden indrukken. Zonder browser kan
het ook: houd de herstelknop **10 seconden** vast. Laat hem bij 5 seconden los
als je alleen web-login/API wilt herstellen. Een knop die bij opstart al vastzit
moet eerst losgelaten worden.

De reset wist alleen opgeslagen Wi-Fi-gegevens en fast-connect metadata.
Web-login, API-beveiliging, de verbindingsvoorkeur en overige instellingen blijven
behouden. Na succesvolle opslag herstart de controller. Verbind met het OpenQuatt
access point en stel Wi-Fi in; de AP blijft beschikbaar totdat de nieuwe gegevens
werken én zijn opgeslagen, ook bij Ethernetvoorkeur en na een stroomonderbreking.
Dit instelvenster staat los van het 10 minuten durende API-koppelvenster.
Targets zonder Wi-Fi tonen deze actie niet.

Na het wissen van Wi-Fi:

1. Verbind je telefoon of laptop met het Wi-Fi-netwerk **OpenQuatt**, wachtwoord **`openquatt`**.
2. Kies zo nodig om verbonden te blijven als je apparaat meldt dat dit netwerk geen internet heeft.
3. Open het instelscherm dat verschijnt, of ga naar **`http://192.168.4.1`** terwijl je met dit netwerk verbonden bent.
4. Kies je eigen Wi-Fi-netwerk en voer daar het bijbehorende wachtwoord in.
5. Verbind je telefoon of laptop weer met je eigen netwerk. Open de controller via zijn hostnaam of het IP-adres in je router; dit adres kan veranderd zijn.

Bij verkeerde Wi-Fi-gegevens blijft het instelnetwerk beschikbaar om ze te corrigeren.
Een reset vanuit fysieke recovery opent na de herstart opnieuw een herstelvenster;
gebruik `/recovery` om dit af te sluiten of wacht tot het verloopt. Een reset vanuit
normaal beheer opent dit venster niet. Controleer na terugkeer ook of de bestaande
Home Assistant-koppeling weer werkt. Wi-Fi wissen is geen factory reset.

Wijzigingen aan beveiliging kunnen een herstart nodig hebben. Bewaar nieuwe gegevens goed, want Home Assistant moet dezelfde API-sleutel gebruiken als API-encryptie actief is.

## Bij problemen

Als de web-app niet opent:

- controleer of OpenQuatt online is in je router;
- probeer het IP-adres in plaats van `openquatt.local`;
- controleer of je telefoon of laptop op hetzelfde netwerk zit;
- kijk of OpenQuatt nog op het fallback access point zit;
- herstart OpenQuatt als het apparaat wel online is maar de web-app niet reageert.

Als Quick Start niet verschijnt terwijl je nog niet klaar bent, open `Instellingen -> Systeem -> Quick Start` en reset de setupstatus.

Wil je OpenQuatt ook aan Home Assistant toevoegen? Ga dan optioneel verder met [OpenQuatt in Home Assistant](dashboard/README.md) en [Dashboard gebruiken](dashboardoverzicht.md). Gebruik je Homey Pro, kijk dan bij [OpenQuatt in Homey](homey.md).
