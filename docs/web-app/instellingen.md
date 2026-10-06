# Instellingen aanpassen

Open **Instellingen** in de web-app en kies de groep die bij je vraag past. Voor normaal comfortgebruik zijn vooral **Verwarmen** en **Koelen** relevant. Installatiekeuzes en servicetaken gebruik je bij de inrichting, een wijziging of gericht onderzoek.

## Instellingen

| Wat wil je aanpassen? | Groep |
| --- | --- |
| Buitenunits, flowregeling of aanvullende warmtebron | [Installatie](#installatie) |
| Verwarmingsstrategie, comfort, stooklijn of huismodel | [Verwarmen](#verwarmen) |
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

#### Power House comfortregeling

Bij **Instellingen → Verwarmen → Power House — comfort** staan twee onderdelen: **Op temperatuur houden** en **Langer doorverwarmen**. De gewone regeling bepaalt hoeveel warmte je woning nodig heeft; langer doorverwarmen is een optionele keuze voor een lopende verwarmingsperiode. De bediening is hetzelfde voor radiatoren, LT-radiatoren en vloerverwarming.

##### Op temperatuur houden

De **gewenste temperatuur** is de kamertemperatuur die je hebt ingesteld, bijvoorbeeld op je thermostaat of in Home Assistant.

Met **Afkoeling onder gewenste temperatuur** kies je hoeveel graden de kamer onder je gewenste temperatuur mag zakken voordat Power House extra warmte vraagt. Voorbeeld: je wilt **21,0 °C** en stelt hier **0,2 °C afkoeling** in. Power House vraagt dan extra warmte als de kamer onder **20,8 °C** komt. Stel je **0,4 °C afkoeling** in, dan gebeurt dat onder **20,6 °C**.

Met **Afremmen boven gewenste temperatuur** kies je hoeveel opwarming boven je gewenste temperatuur de regeling toestaat voordat ze warmte terugneemt. Voorbeeld: je wilt **20,0 °C** en stelt **0,3 °C** in. Boven **20,3 °C** vraagt Power House minder warmte. Met **1,0 °C** begint dat boven **21,0 °C**. Hoe verder de kamer boven deze grens komt, hoe meer warmte wordt teruggenomen. Het getal bepaalt geen vaste stoptemperatuur of gegarandeerde maximumtemperatuur.

De app toont **Warmte terugnemen boven** naast de gewenste temperatuur en de temperatuur voor extra warmte. Beide comfortinstellingen blijven beschikbaar als langer doorverwarmen uitstaat.

Ook zonder extra opwarming kan de warmtepomp draaien om het warmteverlies van je woning te compenseren. Was de kamer langere tijd te koud, dan kan Power House al eerder extra warmte vragen en daarmee nog een tijdje doorgaan terwijl de kamer opwarmt. De app toont de gewenste temperatuur en **Extra warmte bij afkoeling onder** direct onder het veld.

Met **Langer doorverwarmen uit** volgt stoppen uit de berekende warmtevraag. Power House kan bij de gewenste temperatuur blijven verwarmen om het warmteverlies van je woning te compenseren. Bij opwarming boven je ingestelde grens wordt die vraag kleiner. Als er geen verwarming meer wordt gevraagd, volgt een stop; de minimale looptijd kan die uitstellen.

#### Langer doorverwarmen

Dit blok staat binnen **Power House — comfort**. Bij weinig warmtebehoefte kan een draaiende warmtepomp op laag vermogen blijven verwarmen. Je kiest een aparte stoptemperatuur en hoeveel de kamer daarna moet afkoelen. Ook als de comfortregeling al warmte terugneemt, mag de lopende run op laag vermogen doorgaan tot deze stopgrens. Een stilstaande warmtepomp wordt door deze schakelaar niet gestart. De overgang naar laag vermogen hangt af van hoeveel warmte je woning nodig heeft; daarvoor geldt geen vaste kamertemperatuur.

1. Zet **Langer doorverwarmen** aan als je dit wilt gebruiken; standaard staat het uit.
2. Kies bij **Stopgrens boven gewenste temperatuur** hoeveel graden boven je gewenste temperatuur de stopgrens ligt: 0,1–1,0 °C, standaard +0,5 °C. Voorbeeld: je wilt **21,0 °C** en stelt **0,7 °C** in. De stopgrens wordt **21,7 °C**. Met **0,5 °C** wordt dat **21,5 °C**. Verander je deze instelling, kijk dan ook naar de berekende temperatuur voor opnieuw verwarmen: die kan meeveranderen.
3. Kies bij **Afkoeling na stoppen** hoeveel graden de kamer na de stop moet afkoelen: 0,1–3,0 °C, standaard 0,2 °C. Voorbeeld: de stopgrens is **21,7 °C** en je stelt **0,2 °C afkoeling** in. Opnieuw verwarmen is dan mogelijk bij **21,5 °C of lager**. Met **0,7 °C afkoeling** wordt dat **21,0 °C**. We rekenen vanaf de ingestelde stopgrens, ook als de kamer na het stoppen nog warmer wordt.

De twee getalvelden verschijnen alleen als langer doorverwarmen aanstaat. Daaronder staan **Stopgrens** en **Opnieuw verwarmen mogelijk bij**, berekend met je gewenste temperatuur. Tijdens bewerken staat er **Voorbeeld van je wijziging — nog niet bevestigd**; de huidige instellingen blijven daarnaast leesbaar. De informatieknoppen geven uitleg met getalvoorbeelden. De status toont wat er nu gebeurt, bijvoorbeeld **Wacht op afkoeling** of **Stop aangevraagd**.

De instelling **Afkoeling onder gewenste temperatuur** gaat voor als je na de stop heel ver wilt laten afkoelen. Voorbeeld: je wilt **21,0 °C** en hebt daar **0,2 °C afkoeling** ingesteld. Ook als je bij **Afkoeling na stoppen** een grotere afkoeling kiest, is opnieuw verwarmen uiterlijk bij **20,8 °C** mogelijk. De warmtepomp start pas als je woning warmte nodig heeft en hij lang genoeg uit is geweest. Daardoor kan de kamer in de praktijk wel kouder worden.

Voorbeelden bij een gewenste temperatuur van **21,0 °C**, een stopgrens van **21,7 °C** en **Afkoeling onder gewenste temperatuur** ingesteld op **0,2 °C**:

| Afkoeling na stoppen | Opnieuw verwarmen mogelijk bij | Wat betekent dit? |
| --- | --- | --- |
| 0,2 °C | 21,5 °C | Opnieuw verwarmen is mogelijk terwijl de kamer nog warmer is dan gewenst |
| 0,7 °C | 21,0 °C | Afkoelen tot de gewenste temperatuur |
| 0,9 °C | 20,8 °C | Afkoelen tot 0,2 °C onder de gewenste temperatuur |
| 1,2 °C | 20,8 °C | De instelling **Afkoeling onder gewenste temperatuur** gaat voor: je hoeft niet tot 20,5 °C te wachten |

Bij de stopgrens vraagt Power House de warmtepomp om te stoppen. De warmtepomp stopt later als hij zijn minimale looptijd nog moet afmaken. Ook daarna kunnen de radiatoren of vloer nog warmte afgeven. Bij opnieuw verwarmen bepaalt Power House weer hoeveel vermogen nodig is; de warmtepomp start dus niet altijd op minimumvermogen. De normale beveiligingen blijven gelden.

Uitschakelen van langer doorverwarmen laat de gewone verwarming actief. De instellingen blijven na een controllerherstart behouden. Zie [Power House: langer doorverwarmen](../power-house.md#langer-doorverwarmen-optioneel) voor de precieze werking, statussen en het gedrag na een herstart.

##### Geavanceerd: afstelling van de regeling

**Temperatuurreactie** en **Power House responsprofiel** met opbouw-/afbouwtijd staan onder geavanceerd. Je hoeft ze niet te openen om langer doorverwarmen in te stellen. De bestaande waarden blijven behouden.

Power House houdt automatisch rekening met eerdere afkoeling. Was de kamer langere tijd te koud, dan kan het tijdens het opwarmen nog extra warmte vragen. Dit herstel wordt begrensd door de ingestelde grens voor afremmen. Zie [de technische uitleg over kamercorrectie](../power-house.md#2-kamercorrectie).

**Bij deze update:** de bestaande instelling `Power House comfort above setpoint` heet in de app **Afremmen boven gewenste temperatuur** en bepaalt opnieuw het begin van warmte terugnemen. Voorheen begon de warme correctie bij het setpoint; nu begint die boven het setpoint plus deze instelling. Met de standaardwaarde 0,3 °C wordt dus later afgeremd. Opgeslagen waarden blijven behouden. Kijk na de update of de gekozen bovenmarge bij je gewenste comfort past.

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

Bij Power House op Heatpump Controller Q Single en Duo staat onderaan **Verwarmen**
het experimentele blok `Huismodel volgen`. Open `Leerstatus en meetgegevens` voor
voortgang, bronnen en schattingen. Passief leren verandert geen regelinstellingen.

De functie gebruikt dezelfde geselecteerde bronwaarden als de regeling en neemt
verwarmen, pompnaloop en verwarmingspauzes (CM0, CM1 en CM2) mee. Ontbrekende of
ongeldige metingen, ontdooien en ketelwarmte onderbreken een meetperiode.
OpenTherm-telemetrie is hiervoor niet vereist.

Er zijn twee soorten meetperioden, met elk een eigen teller:

- **Opwarmen en afkoelen:** perioden van 30 minuten, ook als de kamertemperatuur
  verandert. Het eenvoudige huismodel (1R1C) schat warmteverlies en warmteopslag.
- **Woninglijn uit stabiele perioden:** perioden van vier uur waarin de kamer en
  het setpoint weinig veranderen. Het gemiddelde warmtevermogen en de
  buitentemperatuur leveren meetpunten voor de woninglijn. Korte compressorpauses
  tellen mee; vier uur continu compressorbedrijf is niet vereist.

Een onderbroken stabiele periode begint opnieuw bij de volgende geldige meting.
Eerder opgeslagen meetperioden en modelschattingen blijven behouden. De laatste
onderbreking of afwijzing blijft zichtbaar totdat een nieuwe stabiele periode is
opgeslagen; deze melding wordt niet over een herstart bewaard.
Veel korte meetperioden betekenen dus niet automatisch dat er ook meetpunten voor
de woninglijn zijn. Beide schattingen zijn voorlopig totdat er voldoende geschikte
gegevens en onafhankelijke controles zijn.

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
de geaccepteerde stabiele vieruursperioden (punten). De korte perioden voor
opwarmen en afkoelen zijn geen punten in deze grafiek. Een beschikbare geleerde woninglijn wordt
groen getoond; buiten het gemeten temperatuurbereik is deze gestippeld. Dit is een
doortrekking van het model, geen meting. De woninglijn toont de basiswarmtevraag, zonder
de tijdelijke kamercorrectie of vermogensbegrenzing.

De leerstatus, grafiek, meldingen en bediening volgen de gekozen app-taal (Nederlands of Engels).
Getallen en datums gebruiken de bijbehorende notatie; de JSON-export behoudt zijn vaste formaat.

Haal de meetpunten op met de knop bij de grafiek. Bij een meetpunt kun je datum, meetduur,
gemiddelde buitentemperatuur en warmtevermogen bekijken. Zonder voldoende gegevens blijft
de geleerde lijn weg; de grafiek verandert geen instellingen. `H` beschrijft hoeveel
extra vermogen per graad kouder nodig is. De `Verwarmingsgrens (T₀)` is het geschatte
nulpunt van deze lijn, geen schakelinstelling die de verwarming aan- of uitzet.

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
