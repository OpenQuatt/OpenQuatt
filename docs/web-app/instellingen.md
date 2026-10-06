# Instellingen aanpassen

Open **Instellingen** in de web-app en kies de groep die bij je vraag past. Voor normaal comfortgebruik zijn vooral **Verwarmen** en **Koelen** relevant. Installatiekeuzes en servicetaken gebruik je bij de inrichting, een wijziging of gericht onderzoek.

## Instellingen

| Wat wil je aanpassen? | Groep |
| --- | --- |
| Buitenunits, flowregeling of aanvullende warmtebron | [Installatie](#installatie) |
| Verwarmingsstrategie, stooklijn of huismodel | [Verwarmen](#verwarmen) |
| Koelvenster, koelvraag of dauwpuntbeveiliging | [Koelen](#koelen) |
| Sensoren en externe invoer | [Bronnen / integraties](#bronnen-integraties) |
| Ontluchten, testen, kalibreren of buitenunitinstellingen | [Service](#service) |
| Updates, backup, opslag of toegang | [Systeem](#systeem) |

Verander één instelling tegelijk en controleer het effect. Bij onverklaarbaar gedrag begin je bij [Problemen oplossen](../problemen-oplossen.md).

### Installatie

Hier staan basiskeuzes zoals Quatt Hybrid-versie, flowregeling, een aanvullende warmtebron, stille uren, watergrenzen en compressorinstellingen.

#### Elektrische ingangsgrens

Bij `Elektrische ingangsgrens` stel je met `Maximale gezamenlijke netstroom` de gezamenlijke stroomgrens van de buitenunits in. De standaard blijft 16 A voor Single en Duo V1/V1.5 en 20 A voor Duo V2 (de officiële Quatt Duo-specificatie); de kaart toont het indicatieve vermogen bij 230 V als benadering. Hoger instellen kan tot de absolute OpenQuatt-bovengrens (20 respectievelijk 26 A, afgeleid van 2 × de gepubliceerde maximale stroom per buitenunit) en alleen bij betrouwbaar gedetecteerde buitenunits van dezelfde familie. Een waarde boven de standaard waarschuwt direct en vraagt een expliciete bevestiging met oude en nieuwe waarde; alleen een zwaardere installatieautomaat plaatsen is niet voldoende. `Standaardwaarde herstellen` zet de actuele standaardwaarde opnieuw in. Ook een backup met een grens boven de standaard vermeldt dit expliciet bij het herstellen. Power House houdt er vooraf en via gemeten vermogen rekening mee; stooklijn en koelen alleen via gemeten vermogen. Deze instelling is een softwarematige regelgrens, geen elektrische beveiliging; korte stroompieken boven de ingestelde waarde zijn niet volledig uit te sluiten.

#### Aanvullende warmtebron

Bij `Aanvullende warmtebron` leg je eerst vast of OpenQuatt een warmtebron fysiek kan aansturen. Daarna kies je afzonderlijk voor `Hybride verwarmen bij vermogenstekort` en `Overnemen wanneer de warmtepomp niet beschikbaar is`. Overname staat standaard uit. OpenQuatt schakelt pas over nadat de warmtepompen veilig zijn gestopt en flow, aanvoertemperatuur en aansturing geldig zijn. Een korte communicatiedip telt niet als uitval.

Tijdens Quick Start kan een gedetecteerde OpenTherm-ketel automatisch als **OpenTherm (OTB)** worden ingesteld. Na afronden blijft een onverwachte OpenTherm-ketel geblokkeerd totdat je de aansluiting corrigeert. Controleer daarom de getoonde aansluiting voordat je afrondt.

#### Opstarten met koud water

Bij een nieuwe warmtevraag controleert OpenQuatt na het starten van de circulatie de uitgaande watertemperatuur van iedere aangesloten warmtepomp. Onder `5 °C` blijven de compressoren uit; met `Overnemen wanneer de warmtepomp niet beschikbaar is` kan de aanvullende warmtebron het circuit eerst opwarmen. Vanaf `5 °C` mogen de warmtepompen starten. Met `Hybride verwarmen bij vermogenstekort` helpt de aanvullende warmtebron tot alle uitgaande temperaturen minimaal `12 °C` zijn. Zonder aangesloten of toegestane aanvullende warmtebron start de warmtepomp vanaf `5 °C` zelfstandig. De oude algemene startgrens van `18 °C` wordt niet gebruikt.

Gebruik dit deel vooral tijdens de eerste inrichting of als je installatie later verandert.

### Verwarmen

Hier kies en verfijn je de verwarmingsstrategie:

- `Power House`;
- `Stooklijnregeling`.

`Power House` probeert de warmtevraag van je woning te schatten. `Stooklijnregeling` stuurt op een aanvoerdoel op basis van de buitentemperatuur. Begin bij [Verwarmen en comfort](../dagelijks/verwarmen.md) als je nog niet zeker weet welke strategie bij je past.

#### Geleidelijk opwarmen na nachtverlaging

Met **Geleidelijk opwarmen** kan OpenQuatt na nachtverlaging of langere afwezigheid
in kleine stappen naar de gewenste kamertemperatuur werken. De gekozen thermostaat
of setpointbron bepaalt het einddoel; dit is niet specifiek voor Tado. Er komt geen
extra internettoegang bij. De schakelaar staat standaard uit en is ook bedienbaar
via Home Assistant en de API.

Open **Instellingen → Verwarmen → Geleidelijk opwarmen** en zet de functie aan
vóór de thermostaat vanuit de nachtstand omhoog gaat. De kamer moet nog onder de
comfortband van het einddoel liggen. Herstarten of een bron opnieuw verbinden
start geen nieuwe opwarmsessie.

Bij een startgrens van 1,5 °C begint geleidelijk opwarmen bijvoorbeeld wanneer
de thermostaat van 17 naar 20,5 °C gaat: een verhoging van 3,5 °C.
Staat de thermostaat al op 20,5 °C wanneer je de functie aanzet? Dan verwarmt
OpenQuatt gewoon verder naar 20,5 °C. Die opwarming wordt niet alsnog in kleine
stappen uitgevoerd. 20,5 °C is een voorbeeld, geen vaste startgrens.
De gewenste temperatuur is de instelling op je thermostaat; de gemeten
kamertemperatuur kan daarvan afwijken.

| Instelling | Standaard | Betekenis |
|---|---|---|
| Start bij verhoging groter dan | 1,5 °C | Thermostaat van 17 naar 20,5 °C: +3,5 °C, dus een start. Van 19 naar 20,5 °C: precies +1,5 °C, dus geen start. |
| Temperatuurstap | 0,1 °C | Bij 18,0 °C gemeten wordt het eerste tussendoel 18,1 °C. Zodra dat is bereikt, volgt de volgende stap direct. |
| Tijd per stap | 45 min | Is 18,1 °C na 45 minuten nog niet bereikt en meet de kamer nog 18,0 °C, dan groeit de stap naar 0,2 °C en wordt het tussendoel 18,2 °C. |

De maximale opwarmstap is vast 0,5 °C; na maximaal 8 uur neemt de normale regeling over.

Elk tussendoel blijft vast staan tot het bereikt is of de tijd per stap verloopt.
Na bereiken schuift het verder; het komt nooit boven de gewenste temperatuur.
Dit is dus geen vaste verhoging van 0,1 °C per 45 minuten: bij sneller bereiken
volgt de volgende stap eerder, bij te langzaam opwarmen groeit de stap.
De toegepaste opwarmstap bepaalt een nieuw tussendoel op basis van de kamertemperatuur.
Een bestaand tussendoel daalt niet bij afkoelen; de werkelijke afstand tot de kamer
kan daardoor groter worden dan de ingestelde maximale opwarmstap.
Binnen de comfortband neemt de normale regeling weer over. Je ziet hier ook
**Opwarmen in stappen**, de actuele kamertemperatuur, het tussendoel en het einddoel
van je thermostaat. Deze drie temperaturen worden met twee decimalen weergegeven.
Bijvoorbeeld: bij 18,06 °C gemeten is een tussendoel van 18,10 °C nog niet bereikt,
hoewel beide bij afronden op één decimaal 18,1 °C zouden lijken. Meer decimalen in
de weergave maken de sensor zelf niet nauwkeuriger en veranderen de regeling niet.

Een verlaging van de gewenste temperatuur, uitschakelen, ongeldige bronwaarden of
een wijziging van bronnen, instellingen of regelmodus beëindigt het opwarmen.
Een verdere verhoging tijdens opwarmen past het einddoel aan. Na beëindigen is een
nieuwe voldoende grote setpointverhoging nodig; de normale bron- en
veiligheidsvoorwaarden blijven gelden.
Bij CIC moeten kamertemperatuur en setpoint in het nieuwste feedantwoord staan.
Een oude waarde die nog zichtbaar is, houdt de opwarmsessie niet actief.

De eerste versie werkt alleen met Power House op basis van het huismodel.
Stooklijn, externe vermogensvragen, koelen en handbediening vallen erbuiten.
Een lager tussendoel vermindert de ruimtegebonden warmtevraag van Power House.
Warmtetoestemming, wachttijden, waterlimieten en beveiligingen blijven leidend.
Een tussendoel is geen stopgrens: tijdens opwarmen blijft de minimale warmtevraag
behouden tot de kamer binnen de comfortband van het echte einddoel komt.
Bijvoorbeeld: kamer 20,06 °C, stap 0,1 °C en einddoel 22 °C geven eerst een tussendoel
van 20,16 °C. De regeling laat de minimale warmtevraag niet al bij 20,11 °C los.
Bij bereiken van het tussendoel schuift dat verder zonder daarvoor een stop en
herstart te vragen. Met een comfortband van 0,1 °C eindigt het opwarmen bij 21,9 °C.
Ook als de comfortband groter is dan de stap, kan de warmtepomp na de normale
startbevestiging opwarmen. Een al draaiende warmtepomp hoeft daarvoor niet eerst
te stoppen. Het minimaal beschikbare warmtepompvermogen blijft de ondergrens;
een kleine temperatuurstap kan geen willekeurig laag vermogen afdwingen.
De functie kan al starten wanneer de thermostaat zijn doel verhoogt maar nog geen
warmtetoestemming geeft. De stap- en totaaltijd lopen ook gedurende die wachttijd.
De functie garandeert daarom geen lager verbruik of hogere COP; vergelijk comfort,
opwarmduur en energiegebruik in je eigen installatie.

#### Stooklijn en actueel aanvoerdoel

In de Nederlandse web-app heet de strategie **Stooklijnregeling**; de firmwareoptie
blijft `Water Temperature Control (heating curve)`. **Stooklijn instellen** bevat de
curvepunten. **Actueel aanvoerdoel** is het doel waar de regeling nu naartoe werkt;
**Gemeten aanvoertemperatuur** is de werkelijke meting.

Bij het aanvoerdoel staat de bron: de lokale stooklijn inclusief eventuele
kamercorrectie en begrenzing, de fallback bij ontbrekende buitentemperatuur, of een
bevestigd extern doel. Bij een extern doel wordt ook het lokale stooklijndoel
getoond. Het ingestelde maximum bij een lokaal doel is een grens, geen bevestiging
dat die grens op dat moment ingrijpt. De firmware levert kamercorrectie niet als
aparte meetwaarde aan; de web-app toont daarom geen berekende uitsplitsing ervan.
De stooklijn gebruikt een gefilterde buitentemperatuur, die tijdelijk kan afwijken
van de actuele buitenmeting.

#### Huismodel volgen en passief leren

In testfirmware met passief huismodelleren staat bij Power House op Heatpump Controller Q Single en Duo ook
`Huismodel volgen`. Deze functie verzamelt diagnostiek en schat woningparameters; zij past geen
regelinstellingen automatisch aan. Water staat vast; Single/Duo volgt uit de firmware en bij Duo
loopt het water in serie van HP1 naar HP2. Dit zijn geen instelbare keuzes. Technische meetgrenzen
en kalibratiebewijs horen niet bij deze bediening. Onbekende meetkwaliteit blijft een blokkade.
De leerfunctie neemt CM0, CM1 en CM2 mee en controleert de bestaande ketelaansturing; er is geen aparte keuze
voor een andere warmtebron. OpenTherm-telemetrie is hiervoor niet vereist; een actuele melding
van ketelactiviteit sluit de betreffende meting wel uit.

`Passief leren` staat standaard aan. De gekozen stand blijft na een herstart of
firmware-update behouden; bewust uitschakelen blijft dus uit. Bij de eerste update
vanaf de eerdere versie die altijd uit startte, wordt leren standaard ingeschakeld.
De leerfunctie heeft geen aparte
kalibratiebevestiging of meetgrensinstellingen; automatisch toepassen bestaat nog niet.
`Leerdata wissen` pauzeert het leren en wist uitsluitend de leerhistorie.
Bij een fysieke ombouw van Single naar Duo of andersom blijft de oude leerstand ook behouden.
Wis dan zelf de leerdata voordat je opnieuw gaat leren of de modelschattingen beoordeelt: de
waterzijdige meetopstelling is veranderd. Een gewone bronwissel vereist deze reset niet.
Bij ingeschakelde technische statistieken wordt alleen de aan/uit-stand van passief
leren gedeeld (`house_learning_enabled`), niet de leerdata of modelwaarden.
`Diagnostische leerdata downloaden` levert een lokale JSON-export. Voorlopige schattingen zijn nog
geen bruikbaar advies; zie [de ontwikkelstatus en testgrenzen](../power-house-autotuning-development.md).

#### Woninglijn en meetresultaten

De grafiek `Woninglijn en meetresultaten` vergelijkt de ingestelde woninglijn (blauw) met
de geaccepteerde stabiele meetperioden (punten). Een beschikbare geleerde woninglijn wordt
groen getoond; buiten het gemeten temperatuurbereik is deze gestippeld. Dit is een
doortrekking van het model, geen meting. De woninglijn toont de basiswarmtevraag, zonder
de tijdelijke kamercorrectie of vermogensbegrenzing.

De leerstatus, grafiek, meldingen en bediening volgen de gekozen app-taal (Nederlands of Engels).
Getallen en datums gebruiken de bijbehorende notatie; de JSON-export behoudt zijn vaste formaat.

Haal de meetpunten op met de knop bij de grafiek. Bij een meetpunt kun je datum, meetduur,
gemiddelde buitentemperatuur en warmtevermogen bekijken. Zonder voldoende gegevens blijft
de geleerde lijn weg; de grafiek verandert geen instellingen.

### Koelen

Hier staan de instellingen voor koeling en dauwpuntbeveiliging.

#### Dagelijks koelvenster

Het blok **Dagelijks koelvenster** onder **Instellingen → Koelen** combineert de aan/uit-schakelaar met de start- en eindtijd. Het tandwiel bij **Koeltoestemming** op het overzicht opent dezelfde bediening in een popup. Inschakelen komt technisch overeen met `Cooling Enable Source = Schedule`; uitschakelen kiest `Disabled`. De starttijd is inbegrepen en de eindtijd niet; een venster kan over middernacht lopen. Gelijke tijden betekenen uit, waardoor de standaard `00:00-00:00` na installatie of update geen koeltoestemming geeft.

Bij het koelvenster en stille uren kun je uren en minuten rustig na elkaar wijzigen. De tijd wordt opgeslagen zodra je het veld verlaat of op Enter drukt. Bij een schrijffout blijft je invoer staan om opnieuw te proberen.

#### Koelvraag en handmatige toestemming

Het schema geeft alleen toestemming. `Cooling Room Request Required` blijft standaard aan, zodat er binnen het venster nog steeds een kamerkoelvraag nodig is. Zet je die instelling bewust uit, dan geldt het actieve venster als koelvraag. In beide gevallen blijven `OpenQuatt Enabled` en alle dauwpunt-, water- en flowbeveiligingen van kracht. `Manual Cooling Enable` omzeilt alleen de gekozen toestemmingsbron; deze opgeslagen override kan na een herstart terugkomen en omzeilt nooit de veiligheidsbewaking.

#### Klok en stoppen aan het einde van het venster

Zie [Koelen](../dagelijks/koelen.md#wat-kun-je-verwachten) voor het gedrag van het tijdvenster en [Koelen: technische werking](../koelen-technisch.md) voor klokvoorwaarden en wachttijden.

#### Dauwpuntbeveiliging

Koeling is gevoeliger dan verwarming, omdat condensrisico een echte beperking is. Normaal gebruikt OpenQuatt een dauwpuntbron plus veiligheidsmarge. Zonder goede dauwpuntinformatie blijft koeling standaard geblokkeerd.

Bij `Dauwpuntsbenadering` gebruikt OpenQuatt een echte dauwpuntmeting zodra die beschikbaar is. Alleen als die meting ontbreekt, gebruikt OpenQuatt een conservatieve benadering op basis van buitentemperatuur, nachtminimum en kamertemperatuur.

Bij `Expliciet toestaan` gebruikt OpenQuatt geen dauwpuntgrens: ook een beschikbare dauwpuntmeting wordt dan genegeerd. Alleen de ingestelde minimale koel-aanvoer blijft gelden. Gebruik dit alleen als je de installatie zelf bewaakt en het condensrisico bewust accepteert.

#### Externe koelbronnen

Wil je dauwpuntbronnen uit Home Assistant gebruiken, volg dan de
[companion-handleiding voor dynamische koelbronnen](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/docs/cooling.md).
De web-app kiest daarna welke koelingsdauwpuntbron OpenQuatt gebruikt: `Auto`,
`Home Assistant`, `API input` of `MQTT`. In `Auto` gebruikt OpenQuatt de hoogste geldige
dauwpuntwaarde.

Wil je externe bronwaarden of toestemmingssignalen via MQTT aanleveren, configureer dan eerst de broker bij **Bronnen / integraties -> MQTT inputbronnen**. In **MQTT sensoren** kun je per topic zien wat OpenQuatt verwacht en ongebruikte topics uitzetten. Zie [MQTT inputbronnen](../mqtt.md) voor topics, payload en geldigheid. Zonder MQTT-broker kan hetzelfde via [API inputbronnen](../api-input.md).

### Bronnen / integraties

Zie [Bronnen en integraties](bronnen.md) voor sensorselectie, actieve bronnen en fallback.

### Service

Hier staan commissioning, tests, kalibratie en andere servicetaken. Gebruik deze groep alleen voor een gerichte controle of afstelling en volg de aanwijzingen in de web-app.

#### Ontluchten

**Ontluchten** draait in CM100 een pomp-only programma van 5 minuten met een rustige start, pomp-pulsen en stabilisatie. Tijdens een rustpuls mag de gemeten flow kort naar nul zakken. De routine stopt met een fout zodra 120 seconden aaneengesloten geen geldige flow van minstens 20 L/h is gedetecteerd. De gevraagde iPWM toont de opdracht van het programma, niet een bevestiging dat de pomp draait of water stroomt. Het resultaat onderscheidt **Mislukt** met foutreden van **Afgebroken**. Na een fout of afbreken keert de routine niet automatisch terug naar Auto; de optie voor terugkeer naar Auto geldt alleen bij normaal afronden.

#### Buitenunitinstellingen en ontdooien

Bij `Instellingen buitenunit` staat `Ontdooien` als derde rij. Het paneel toont per buitenunit de actuele status en cyclusduur. Onder `Instellingen` staan de huidige ontdooimethode met uitleg; `Instellingen uitlezen` leest deze uit de buitenunit. Alleen methoden die de herkende buitenunit daadwerkelijk ondersteunt kunnen tijdelijk worden gekozen na bevestiging van oud→nieuw: V1 biedt 0, 1 en 3; V1.5 en de bestaande V2-profielen bieden 0, 1, 3 en 4. De teruggelezen waarde geldt als bewijs. Sensorgegevens staan ingeklapt onder `Technische metingen`. Onbekende waarden blijven leeg; lokale tellers en mogelijke eindredenen zijn geen exacte weergave van de interne ODU-regeling. Zie [defrostdiagnostiek](../defrost.md) voor de beperkingen.

`Handmatig ontdooien` vraagt na bevestiging één cyclus aan bij een reeds verwarmende buitenunit met bewezen flow. De ODU beslist over acceptatie en uitvoering. OpenQuatt bewaart deze aanvraag niet en herhaalt haar niet automatisch. Veiligheidsstops houden voorrang; na afloop hervat de actuele regeling.

#### Ketelvermogen meten

De `Boiler power test` stabiliseert eerst de flow en meet daarna het afgegeven ketelvermogen. De test duurt meestal 5 tot 15 minuten. Een bruikbaar resultaat kan als voorstel voor `Boiler rated heat power` worden toegepast. Bij een aan/uit-ketel blijft de fysieke aansturing binair.

#### Temperatuursensoren kalibreren

De taak `Temperatuursensoren kalibreren` bepaalt naast de relatieve offsets van HP1/HP2 ook een offset voor de actieve aanvoertemperatuurbron. Het resultaat wordt pas actief na `Offsets toepassen`. OpenQuatt bewaart afzonderlijke aanvoercorrecties voor lokale PT1000, lokale DS18B20, CIC en Home Assistant en activeert bij een bronwissel automatisch de passende correctie. De CIC-correctie blijft geldig na een gewijzigde feed-URL; een andere Home Assistant-invoer vereist wel een nieuwe kalibratie. Een korte automatische fallback tijdens een bronstoring wordt ongecorrigeerd gebruikt en wist geen opgeslagen bronkalibratie.

#### Installatiebewaking

Onder `Installatiebewaking` zie je per warmtepomp actieve en herstellende incidenten, wat daarvan het effect op de regeling is en hoe OpenQuatt erop reageert. Herstelde gelatchte incidenten blijven zichtbaar totdat je de melding als gezien markeert. Als een storing volgens de warmtepomp een echte uit- en inschakeling van de buitenunit vereist, verschijnt een aparte knop waarmee je na uitvoering bevestigt dat de powercycle werkelijk is uitgevoerd. Het paneel toont daarnaast compressorstarts, hydraulische aandachtspunten en verbindingsstatussen. De alarmgrenzen voor compressorstarts zijn uitklapbaar en bedoeld voor incidentele aanpassing.

### Systeem

#### Toegang en beveiliging

Onder **Instellingen → Systeem → Toegang & Beveiliging** beheer je de gebruikersnaam en het wachtwoord van de OpenQuatt-webinterface en controleer je de beveiliging van de ESPHome-verbinding met Home Assistant.

Home Assistant stelt de API-encryptie bij de eerste koppeling automatisch in; je hoeft geen sleutel te kopiëren. Volg [OpenQuatt koppelen aan Home Assistant](../dashboard/koppelen.md) voor de koppelprocedure.

Ben je het wachtwoord vergeten of werkt de bestaande Home Assistant-koppeling niet meer door een onbekende sleutel? Gebruik [Herstelpagina gebruiken](herstel.md). Voor authenticatie van eigen HTTP-clients zie [API inputbronnen](../api-input.md#authenticatie).

#### Overig systeembeheer

Zie [Gegevens delen en privacy](privacy.md) voor de keuzes over gegevens delen, [Updates en backups](onderhoud.md) voor systeembeheer en gegevensopslag, en [Herstelpagina gebruiken](herstel.md) voor toegang en herstel.

## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
