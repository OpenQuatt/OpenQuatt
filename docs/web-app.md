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

OpenQuatt beveiligt de verbinding met Home Assistant automatisch. Bij de eerste koppeling maakt Home Assistant een geheime sleutel aan en bewaart die veilig samen met het apparaat. Je hoeft deze sleutel niet zelf te maken, te kopiëren of in OpenQuatt in te voeren.

Alleen bij de eerste koppeling staat de koppelmogelijkheid na een opstart maximaal 10 minuten open. Is Home Assistant dan nog niet beschikbaar, dan worden nieuwe koppelpogingen geweigerd. Het apparaat schakelt niet terug naar een onbeveiligde verbinding. Zet het apparaat kort uit en weer aan om opnieuw te proberen. Een bestaande koppeling blijft behouden na een firmware-update, herstart en stroomonderbreking; de timer speelt daarna geen rol meer.

In `Instellingen → Toegang & Beveiliging` zie je:

- `Actief`: de verbinding met Home Assistant is beveiligd;
- `Wacht op koppeling`: open Home Assistant om dit apparaat toe te voegen;
- `Niet beschikbaar`: de koppeltijd is verlopen of de status kan tijdelijk niet worden opgehaald.

De web-app toont deze geheime sleutel nooit. Er is geen HTTPS op de lokale webinterface. Als de sleutel op het apparaat onbekend is, kun je API-beveiliging resetten via een web-login of de fysieke herstelpagina; daarna kan Home Assistant opnieuw koppelen. Een oude OpenQuatt-sleutel wordt niet automatisch overgenomen. Home Assistant kan tijdens opnieuw koppelen dezelfde sleutel terugzetten; verwijder een ongewenste oude sleutel daarom ook daar.

Bij de migratie geldt deze matrix:

| Device | Home Assistant | Gedrag |
|---|---|---|
| Native key aanwezig | Key bekend | Key behouden; OTA en startup wissen hem nooit. |
| Alleen oude OpenQuatt-key aanwezig | Geen native key | Oude key wordt genegeerd; Home Assistant provisiont een nieuwe native key. |
| Geen key | Geen key | Home Assistant provisiont automatisch binnen de provisioning window. |
| Geen key | Stale key | Eenmalig opnieuw koppelen of de stale key in Home Assistant verwijderen. |
| Native key aanwezig | Key onbekend | Reset API-beveiliging via een web-login of de fysieke [herstelpagina](#herstelomgeving); daarna opnieuw koppelen. OpenQuatt neemt de onbekende sleutel niet over. |

De oude OpenQuatt-preference wordt bij deze firmwareversie niet gewist, maar ook niet meer gelezen of toegepast. Dat houdt rollback mogelijk zonder een tweede bron van waarheid te activeren.

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
| `Kies de verwarmingsstrategie` | `Power House` of `Water Temperature Control` | Bepaalt hoe OpenQuatt warmtevraag maakt en vervangt daarbij automatisch de warmtetoestemming (`Niet gebruiken` voor Power House; de eerder gekozen actieve thermostaatbron voor stooklijn). |
| `Werk de regeling uit` | Strategie-instellingen | Toont alleen de instellingen die bij de gekozen strategie horen. |
| `Flowregeling en afstelling` | Automatische flow of vaste pompstand | Bepaalt hoe OpenQuatt de waterdoorstroming regelt. |
| `Watertemperatuur beveiligen` | Maximale watertemperatuur | Laat OpenQuatt terugregelen voordat het water te warm wordt. |
| `Stille uren en niveaus` | Tijdvenster en compressorlimieten | Begrenst de compressor bijvoorbeeld 's nachts. |
| `Gebruiksstatistieken` | Wel of niet beperkte technische systeemstatus en feature-instellingen delen | Tijdens een nieuwe Quick Start staat delen standaard aan en kan het hier worden uitgezet. |
| `Prestatiemetingen` | Wel of niet stabiele verwarmingsmetingen delen voor validatie van het prestatiemodel | Tijdens een nieuwe Quick Start staat delen standaard uit en kan het hier worden aangezet. Na inschakelen worden maximaal 15 complete minuutrecords per bericht iedere 15 minuten vanaf deviceboot verstuurd; de planning volgt uptime en niet UTC-kwartiergrenzen. |
| `Bevestigen en afronden` | Je keuzes controleren | Markeert de basisconfiguratie als klaar. |

Je hoeft niet meteen perfecte waardes te kiezen. Het doel van Quick Start is een veilige, begrijpelijke basis. Fijnregelen kan later.

De installatie is klaar zodra Quick Start is afgerond, `openquatt.local` stabiel bereikbaar blijft en de belangrijkste warmtepompwaarden logisch worden bijgewerkt. Home Assistant en het dashboard zijn optionele vervolgstappen.

## Hoofdschermen

De web-app heeft zes hoofdschermen.

| Scherm | Gebruik |
|---|---|
| `Overzicht` | Live zien wat OpenQuatt nu doet en of de belangrijkste waarden logisch zijn. |
| `Energie` | Vermogen, energie, COP en EER bekijken. |
| `Resultaten` | Opgeslagen energie- en resultaathistorie over een langere periode bekijken. |
| `Beslislog` | Terugzien welke regelbeslissingen OpenQuatt nam en waarom. Deze functie is nog beta. |
| `Diagnose` | Live waarden en korte trendhistorie naast elkaar bekijken om gedrag te onderzoeken. |
| `Instellingen` | OpenQuatt configureren, bijwerken en beheren. |

Voor dagelijks kijken is `Overzicht` meestal genoeg. Ga pas naar `Instellingen` als je bewust iets wilt veranderen.

## Overzicht

Begin hier als je wilt weten of alles normaal oogt.

Let vooral op:

- OpenQuatt is online;
- Quatt-data wordt ververst;
- flow, aanvoertemperatuur, buitentemperatuur en kamertemperatuur zijn geloofwaardig;
- er is geen onverwachte override actief;
- de gekozen strategie past bij wat je in huis verwacht.

Zie je hier al vreemde waarden, ga dan niet meteen tunen. Controleer eerst de bronkeuze onder **Instellingen → Bronnen / integraties → Sensorselectie** en, als je Home Assistant gebruikt, de aangeleverde Home Assistant-bronnen.

## Resultaten

`Resultaten` bundelt opgeslagen resultaten en historie. Gebruik dit scherm om prestaties over een langere periode te vergelijken. Voor een snelle diagnose van het actuele regelgedrag is `Diagnose` geschikter.

## Diagnose

`Diagnose` combineert actuele waarden met korte historie. Dat helpt bij vragen zoals:

- loopt de aanvoertemperatuur rustig op;
- blijft de flow stabiel;
- schakelt het systeem vaak;
- reageert de regeling logisch op setpoint en kamertemperatuur.

Gebruik bij een probleem dat je opnieuw kunt veroorzaken ook het **Logboek**. Nieuwe regels verschijnen daar live; valt de verbinding kort weg, dan vult OpenQuatt de gemiste recente regels weer aan. Het logboek is vluchtige diagnose-informatie: bewaar voor support daarnaast altijd een Systeemrecorder-diagnosebestand.

Via `Instellingen → Systeem → Gegevens bewaren` beheer je welke historie OpenQuatt bewaart. OpenQuatt maakt daarbij onderscheid tussen twee soorten geheugen:

- **PSRAM (tijdelijk, vluchtig)** — snelle opslag voor recente diagnosegegevens en RAM-logs. Deze historie is direct beschikbaar zolang de controller online is en verdwijnt na een herstart.
- **Flash-partitie `openquatt_data` (persistent)** — blijft bewaard na een herstart of update. Hier staan energie-dagtotalen (standaard aan, 180 dagen uurdetail), beslislog (standaard aan, maximaal 7 dagen, per uur gebundeld naar flash) en diagnosehistorie (standaard aan, maximaal 30 dagen).

Tijdelijke PSRAM-historie is op alle ondersteunde profielen standaard aan en wordt niet als aparte keuze in Quick Start getoond; ontbrekende PSRAM wijst op een hardware- of profielprobleem. Persistente flash-historie kun je per domein (Diagnose / Beslislog / Energie) aan of uit zetten onder Gegevens bewaren. Zet je een flash-optie uit, dan blijft bestaande flashhistorie gewoon staan — OpenQuatt stopt alleen met nieuw wegschrijven. Met `Nu opslaan` kun je vóór een herstart of update alvast een extra opslagmoment forceren. De technische opslagdetails tonen voor diagnosehistorie ook de langste volledige opslagactie, sector-erase, flashwrite en index-update sinds de laatste start.

## Beslislog

`Beslislog` laat zien welke regelkeuze OpenQuatt maakte en welke signalen daarbij meespeelden. Gebruik dit scherm vooral om een onverwachte omschakeling of begrenzing te verklaren. De functie is nog beta; combineer de uitleg daarom met de actuele waarden in `Diagnose`.

## Energie

`Energie` geeft inzicht in vermogen en rendement. Gebruik dit vooral om richting te krijgen, niet als gecertificeerde energiemeter.

Voorbeelden:

- elektrisch vermogen van de warmtepomp;
- thermisch vermogen;
- COP bij verwarmen;
- EER bij koelen, als koeling actief en ondersteund is;
- dag- en totaalwaarden wanneer die entiteiten beschikbaar zijn.

## Instellingen

Onder `Instellingen` staan de onderdelen bewust gescheiden. Het idee is: eerst de gewone installatie-instellingen, daarna pas de scherpere gereedschappen.

### Installatie

Hier staan basiskeuzes zoals Quatt Hybrid-versie, flowregeling, een aanvullende warmtebron, stille uren, watergrenzen en compressorinstellingen.

Bij `Elektrische ingangsgrens` stel je met `Maximale gezamenlijke netstroom` de gezamenlijke stroomgrens van de buitenunits in. De standaard blijft 16 A voor Single en Duo V1/V1.5 en 20 A voor Duo V2 (de officiële Quatt Duo-specificatie); de kaart toont het indicatieve vermogen bij 230 V als benadering. Hoger instellen kan tot de absolute OpenQuatt-bovengrens (20 respectievelijk 26 A, afgeleid van 2 × de gepubliceerde maximale stroom per buitenunit) en alleen bij betrouwbaar gedetecteerde buitenunits van dezelfde familie. Een waarde boven de standaard waarschuwt direct en vraagt een expliciete bevestiging met oude en nieuwe waarde; alleen een zwaardere installatieautomaat plaatsen is niet voldoende. `Standaardwaarde herstellen` zet de actuele standaardwaarde opnieuw in. Ook een backup met een grens boven de standaard vermeldt dit expliciet bij het herstellen. Power House houdt er vooraf en via gemeten vermogen rekening mee; stooklijn en koelen alleen via gemeten vermogen. Deze instelling is een softwarematige regelgrens, geen elektrische beveiliging; korte stroompieken boven de ingestelde waarde zijn niet volledig uit te sluiten.

Bij `Aanvullende warmtebron` leg je eerst vast of OpenQuatt een warmtebron fysiek kan aansturen. Daarna kies je afzonderlijk voor `Hybride verwarmen bij vermogenstekort` en `Overnemen wanneer de warmtepomp niet beschikbaar is`. Overname staat standaard uit. OpenQuatt schakelt pas over nadat de warmtepompen veilig zijn gestopt en flow, aanvoertemperatuur en aansturing geldig zijn. Een korte communicatiedip telt niet als uitval.

Bij een nieuwe warmtevraag controleert OpenQuatt na het starten van de circulatie de uitgaande watertemperatuur van iedere aangesloten warmtepomp. Onder `5 °C` blijven de compressoren uit; met `Overnemen wanneer de warmtepomp niet beschikbaar is` kan de aanvullende warmtebron het circuit eerst opwarmen. Vanaf `5 °C` mogen de warmtepompen starten. Met `Hybride verwarmen bij vermogenstekort` helpt de aanvullende warmtebron tot alle uitgaande temperaturen minimaal `12 °C` zijn. Zonder aangesloten of toegestane aanvullende warmtebron start de warmtepomp vanaf `5 °C` zelfstandig. De oude algemene startgrens van `18 °C` wordt niet gebruikt.

Gebruik dit deel vooral tijdens de eerste inrichting of als je installatie later verandert.

### Verwarmen

Hier kies en verfijn je de verwarmingsstrategie:

- `Power House`;
- `Water Temperature Control`.

`Power House` probeert de warmtevraag van je woning te schatten. `Water Temperature Control` werkt meer als een stooklijnregeling. Begin bij [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md) als je nog niet zeker weet welke strategie bij je past.

### Koelen

Hier staan de instellingen voor koeling en dauwpuntbeveiliging.

Het blok **Dagelijks koelvenster** onder **Instellingen → Koelen** combineert de aan/uit-schakelaar met de start- en eindtijd. Het tandwiel bij **Koeltoestemming** op het overzicht opent dezelfde bediening in een popup. Inschakelen komt technisch overeen met `Cooling Enable Source = Schedule`; uitschakelen kiest `Disabled`. De starttijd is inbegrepen en de eindtijd niet; een venster kan over middernacht lopen. Gelijke tijden betekenen uit, waardoor de standaard `00:00-00:00` na installatie of update geen koeltoestemming geeft.

Bij het koelvenster en stille uren kun je uren en minuten rustig na elkaar wijzigen. De tijd wordt opgeslagen zodra je het veld verlaat of op Enter drukt. Bij een schrijffout blijft je invoer staan om opnieuw te proberen.

Het schema geeft alleen toestemming. `Cooling Room Request Required` blijft standaard aan, zodat er binnen het venster nog steeds een kamerkoelvraag nodig is. Zet je die instelling bewust uit, dan geldt het actieve venster als koelvraag. In beide gevallen blijven `OpenQuatt Enabled` en alle dauwpunt-, water- en flowbeveiligingen van kracht. `Manual Cooling Enable` omzeilt alleen de gekozen toestemmingsbron; deze opgeslagen override kan na een herstart terugkomen en omzeilt nooit de veiligheidsbewaking.

Voor het schema gebruikt OpenQuatt zijn via SNTP gesynchroniseerde lokale klok. Na een herstart zonder geldige netwerktijd blijft de schematoestemming uit totdat synchronisatie lukt. Bij het bereiken van de eindtijd stopt OpenQuatt gecontroleerd: een nog lopende minimale compressortijd kan de compressor kort laten doorlopen en daarna kan de pomp nog de normale postflow uitvoeren.

Koeling is gevoeliger dan verwarming, omdat condensrisico een echte beperking is. Normaal gebruikt OpenQuatt een dauwpuntbron plus veiligheidsmarge. Zonder goede dauwpuntinformatie blijft koeling standaard geblokkeerd.

Bij `Dauwpuntsbenadering` gebruikt OpenQuatt een echte dauwpuntmeting zodra die beschikbaar is. Alleen als die meting ontbreekt, gebruikt OpenQuatt een conservatieve benadering op basis van buitentemperatuur, nachtminimum en kamertemperatuur.

Bij `Expliciet toestaan` gebruikt OpenQuatt geen dauwpuntgrens: ook een beschikbare dauwpuntmeting wordt dan genegeerd. Alleen de ingestelde minimale koel-aanvoer blijft gelden. Gebruik dit alleen als je de installatie zelf bewaakt en het condensrisico bewust accepteert.

Wil je dauwpuntbronnen uit Home Assistant gebruiken, volg dan de
[companion-handleiding voor dynamische koelbronnen](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/docs/cooling.md).
De web-app kiest daarna welke koelingsdauwpuntbron OpenQuatt gebruikt: `Auto`,
`Home Assistant`, `API input` of `MQTT`. In `Auto` gebruikt OpenQuatt de hoogste geldige
dauwpuntwaarde.

Wil je externe bronwaarden of toestemmingssignalen via MQTT aanleveren, configureer dan eerst de broker bij **Bronnen / integraties -> MQTT inputbronnen**. In **MQTT sensoren** kun je per topic zien wat OpenQuatt verwacht en ongebruikte topics uitzetten. Zie [MQTT inputbronnen](mqtt.md) voor topics, payload en geldigheid. Zonder MQTT-broker kan hetzelfde via [API inputbronnen](api-input.md).

### Bronnen / integraties

Hier beheer je de directe gegevensbronnen en integraties:

- `OpenTherm`: zet de lokale OpenTherm-thermostaatkoppeling aan of uit;
- `CiC JSON-feed inlezen`: haalt gegevens uit de CiC op via je lokale netwerk; open `Adres aanpassen` onder deze schakelaar om het feed-adres in te stellen;
- `MQTT inputbronnen`: configureer een broker voor externe MQTT-bronwaarden zoals dauwpunt, buiten- en kamerwaarden, het aanvoertarget en toestemmingssignalen, en zet ongebruikte topics uit;
- `API inputbronnen`: lever dezelfde externe bronwaarden via lokale HTTP-endpoints aan;
- `Quatt-app via CiC`: geeft alleen buitenunitgegevens via de Modbusverbinding op M2 door aan de CiC, zodat de Quatt-app kan meekijken.

#### CiC: kies de functie die je echt nodig hebt

De CiC is de originele Quatt-controller. Je kunt hem op twee manieren blijven gebruiken:

| Als je dit wilt | Schakel in | Wat gebeurt er? | Niet nodig voor |
|---|---|---|---|
| CiC-waarden als bron gebruiken | `CiC JSON-feed inlezen` | OpenQuatt leest de lokale JSON-feed van de CiC. Daaruit kunnen onder meer setpoint, kamerwaarden, aanvoertemperatuur en flow beschikbaar komen. | De Quatt-app behouden. |
| Buitenunitgegevens in de Quatt-app blijven bekijken | `Quatt-app via CiC` | OpenQuatt geeft via Modbus op M2 alleen buitenunitgegevens door aan de CiC. Thermostaatgegevens gaan niet mee. | CiC-waarden als bron gebruiken. |

Je kunt één functie inschakelen, beide combineren, of beide uit laten. Gebruik je geen CiC meer, laat beide schakelaars uit.

Voor **CiC JSON-feed inlezen** open je **Adres aanpassen** en vul je het lokale feed-adres van je CiC in, bijvoorbeeld `http://<ip-adres>:<poort>/beta/feed/data.json`. Zet deze schakelaar alleen aan als je ook werkelijk één of meer CiC-bronnen kiest onder **Sensorselectie**. De infoknop naast iedere verbinding geeft extra uitleg.

Voor **Quatt-app via CiC** verbind je `M2` met een aparte RS485-kabel met de vrijgekomen Modbuspoort van de CiC. Dit is alleen beschikbaar op de Heatpump Controller Q. Deze Modbusverbinding geeft uitsluitend buitenunitgegevens door; thermostaatgegevens zoals kamertemperatuur en kamer-setpoint gaan niet naar de CiC. OpenQuatt blijft de warmtepomp regelen; besturingscommando's via deze M2-koppeling worden niet overgenomen. De CiC heeft zijn eigen voeding en netwerkverbinding nodig om gegevens aan Quatt door te geven. Deze functie heette eerder **CiC-compatibiliteit**. Zie voor de aansluiting [Q-edition aansluiten](q-edition.md#welke-kabel-gaat-waarheen).

Onder `Sensorselectie` in dezelfde groep kies je per signaal welke bron OpenQuatt gebruikt. Naast de kaarten voor buiten-, kamer- en aanvoerwaarden staat daar `Externe warmtevraag (Power House)`: een optionele externe vermogensvraag, alleen voor de Power House-strategie, standaard op `Niet gebruiken`. Zet je die op Home Assistant of API-invoer, dan vervangt jouw waarde uitsluitend de vermogensschatting van het huismodel; de kaart laat zien of Power House die externe waarde daadwerkelijk gebruikt of is teruggevallen op het model. Zie [Power House](power-house.md).

Daarnaast staat er `Aanvoertarget (stooklijn)`: een optionele externe aanvoertemperatuur, alleen voor de stooklijnregeling, standaard op `Stooklijn`. Zet je die op OpenTherm-thermostaat, Home Assistant, API-invoer of MQTT, dan vervangt jouw waarde uitsluitend het berekende stooklijntarget; de kaart laat zien of de regeling dat externe target daadwerkelijk gebruikt of is teruggevallen op de stooklijn. Zie [Water Temperature Control](water-temperature-control.md#extern-aanvoertarget-optioneel).

Voor `Warmtetoestemming` (`Heating Enable Source`) betekent `Niet gebruiken`: geen externe gate; de strategie bepaalt zelf of warmte nodig is. Tijdens Quick Start vervangt een strategieswitch deze keuze automatisch door `Niet gebruiken` voor `Power House`, of door de gekoppelde en actieve thermostaatbron voor `Water Temperature Control`. Buiten Quick Start toont `Instellingen → Verwarmen` alleen een advies met knop en wordt de instelling niet stil overschreven. Afwijkende combinaties (zone-regeling, volledig weersafhankelijk) blijven mogelijk. De buitentemperatuur staat normaliter op `Auto` en gebruikt de buitenunit.

Dezelfde groep toont compacte diagnostiek voor OpenTherm en CIC, zoals linkstatus, JSON-feedstatus, kamertemperatuur, setpoint, flow en waterdruk wanneer de firmware die signalen exposeert.

Laat dit met rust zolang OpenQuatt logisch werkt. Verander liever een instelling per keer en kijk daarna wat het systeem doet.

### Service

Hier staan commissioning, tests, kalibratie en andere servicetaken. Gebruik deze groep alleen voor een gerichte controle of afstelling en volg de aanwijzingen in de web-app.

De `Boiler power test` stabiliseert eerst de flow en meet daarna het afgegeven ketelvermogen. De test duurt meestal 5 tot 15 minuten. Een bruikbaar resultaat kan als voorstel voor `Boiler rated heat power` worden toegepast. Bij een aan/uit-ketel blijft de fysieke aansturing binair.

De taak `Temperatuursensoren kalibreren` bepaalt naast de relatieve offsets van HP1/HP2 ook een offset voor de actieve aanvoertemperatuurbron. Het resultaat wordt pas actief na `Offsets toepassen`. OpenQuatt bewaart afzonderlijke aanvoercorrecties voor lokale PT1000, lokale DS18B20, CIC en Home Assistant en activeert bij een bronwissel automatisch de passende correctie. De CIC-correctie blijft geldig na een gewijzigde feed-URL; een andere Home Assistant-invoer vereist wel een nieuwe kalibratie. Een korte automatische fallback tijdens een bronstoring wordt ongecorrigeerd gebruikt en wist geen opgeslagen bronkalibratie.

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

Kies **API-beveiliging resetten** bij Toegang & Beveiliging (web-login vereist),
of op de fysieke herstelpagina. Bevestig het wissen en herstarten.
Dit wist uitsluitend de native API-sleutel, niet je web-login of Wi-Fi-instellingen.
Alle API-clients worden losgekoppeld. Bij een opslagfout wordt niet herstart;
controleer de melding voordat je opnieuw probeert.

Na de herstart heeft Home Assistant 10 minuten om de beveiligde verbinding
opnieuw in te stellen. Home Assistant kan daarbij dezelfde sleutel terugzetten.
Een fysieke reset keert eenmalig terug naar herstel; een reset met web-login niet.

Bij een bestaande koppeling kan Home Assistant melden dat het apparaat transportencryptie
heeft uitgeschakeld en vragen de oude sleutel te verwijderen. Bevestig dit alleen als
je zelf deze reset hebt gestart en het juiste apparaat wordt genoemd. Home Assistant
kan daarna opnieuw encryptie instellen. Controleer bij **Toegang & Beveiliging** dat
API-encryptie weer actief is; alleen bereikbaarheid bewijst dit niet.

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

Wil je OpenQuatt ook aan Home Assistant toevoegen? Ga dan optioneel verder met [Dashboard installeren](dashboard/README.md) en [Dashboard gebruiken](dashboardoverzicht.md). Gebruik je Homey Pro, kijk dan bij [OpenQuatt in Homey](homey.md).
