# Verwarmen en koelen uitgelegd

Deze pagina legt OpenQuatt uit in gewone taal. Het doel is niet dat je alle interne logica kent, maar dat je snapt hoe het systeem denkt en welke keuzes voor jou het belangrijkst zijn.

## In een zin

OpenQuatt zit tussen je thermostaat en je warmtepomp:

- de thermostaat vraagt warmte of koeling;
- de warmtepomp maakt die warmte of koeling;
- OpenQuatt beslist hoe actief of terughoudend het systeem mag reageren;
- de web-app laat zien wat er gebeurt en waar je iets kunt aanpassen;
- Home Assistant kan daar optioneel een dashboard en automatisering aan toevoegen.

## Wat doet OpenQuatt precies?

OpenQuatt vervangt je thermostaat niet. Het is de laag die meetwaarden verzamelt, controleert welke bron bruikbaar is en de warmtepomp rustiger en slimmer laat reageren.

Praktisch betekent dat:

- OpenQuatt kijkt welke temperatuur- en flowwaarden het echt vertrouwt;
- het voorkomt dat het systeem te agressief reageert op kleine schommelingen;
- het houdt rekening met grenzen en beveiligingen;
- het maakt gedrag zichtbaar in de web-app en, als je die gebruikt, Home Assistant.

## Verwarmen: twee manieren van denken

OpenQuatt kent twee hoofdstrategieën voor verwarmen. Voor de meeste gebruikers is dit de belangrijkste keuze.

### 1. `Power House`

`Power House` denkt vooral vanuit het huis en het comfort.

In gewone taal:

- hoe koud is het buiten;
- hoe ver zit de kamer van het gewenste punt af;
- hoeveel warmte heeft het huis dan ongeveer nodig;
- hoe snel mag die warmtevraag oplopen of afnemen.

Deze strategie past vaak goed als je:

- vooral naar kamertemperatuur en comfort kijkt;
- wilt dat OpenQuatt meer zelf beslist;
- rustige, langere verwarmingsruns prettig vindt;
- bij `Duo` wilt dat OpenQuatt zelf de zuinigste geldige combinatie kiest.

Eenvoudig onthouden:

- `Power House` denkt eerst aan het huis, en pas daarna aan de warmtepomp.

Dit schema uit de webapp laat zien hoe de kamercorrectie in Power House rond het setpoint werkt:

![Kamercorrectie op Power House-huisvraag](assets/powerhouse-kamercorrectie.svg)

Onder de comfortband vraagt Power House extra warmte. Binnen de comfortband blijft de directe reactie vlakker. Boven de bovengrens start warme tegensturing.

### 2. Stooklijnregeling (`Water Temperature Control`)

Deze strategie denkt vooral vanuit de gewenste watertemperatuur.

In gewone taal:

- hoe koud is het buiten;
- welke aanvoertemperatuur hoort daar ongeveer bij;
- zit de echte aanvoer daaronder of daarboven;
- hoeveel extra warmtepompvraag is nodig om die aanvoer te volgen.

Deze strategie past vaak goed als je:

- gewend bent te werken met een stooklijn;
- de aanvoertemperatuur centraal wilt zetten;
- liever in watergedrag denkt dan in een huismodel;
- zelf duidelijk wilt bepalen welke aanvoertemperatuur bij welk weer past.

Eenvoudig onthouden:

- stooklijnregeling denkt eerst aan het water, en pas daarna aan het huis.

## Welke strategie moet ik kiezen?

Er is geen universeel beste keuze. Kies vooral de strategie die het best past bij hoe jij je systeem bekijkt.

Kies eerder `Power House` als:

- je comfort en kamertemperatuur het belangrijkst vindt;
- je zo min mogelijk in stooklijninstellingen wilt denken;
- je wilt dat OpenQuatt bij `Duo` veel zelf optimaliseert.

Kies eerder stooklijnregeling als:

- je gewend bent aan weersafhankelijke regeling;
- je graag met aanvoertemperaturen werkt;
- je liever een klassieke verwarmingsaanpak volgt.

Twijfel je? Begin dan met de strategie die het meest logisch voelt, en wissel niet te snel heen en weer. Eerst kijken hoe het systeem zich over langere tijd gedraagt is meestal verstandiger dan direct finetunen.

## Wat betekent koeling binnen OpenQuatt?

Koeling is niet simpelweg "verwarmen maar dan andersom". Bij koeling is vooral het risico op condens belangrijk.

Daarom werkt OpenQuatt bij koeling terughoudend:

- er moet echt een koelvraag zijn;
- de flow moet bruikbaar zijn;
- de minimale veilige watertemperatuur moet bewaakt worden;
- dauwpuntinformatie is normaal gesproken nodig.

Standaard gebruikt OpenQuatt de kamertemperatuur en het setpoint om vast te stellen of er echt koelvraag is. Een kleine marge voorkomt dat koeling steeds kort aan en uit schakelt rond het setpoint.

Bij koelvraag kijkt OpenQuatt vervolgens naar de watertemperatuur. De regeling start rustig, bouwt alleen op als dat nodig is en remt af of stopt wanneer de aanvoer dicht bij de veilige ondergrens komt.

Voor het opnieuw starten na een koelstop kun je kiezen tussen voldoende opwarming van het water en een vaste minimale uit-tijd. Die uit-tijd geldt bij Duo voor beide warmtepompen, zodat de tweede pomp niet direct de gestopte koelcyclus overneemt. Ook bij Single blijft de vaste minimale uit-tijd van de compressor (4 minuten) altijd gelden: OpenQuatt start pas wanneer alle relevante wachttijden en voorwaarden zijn vrijgegeven. De condens-, flow- en andere veiligheidsbewaking blijft altijd gelden.

### Koelen binnen een dagelijks tijdvenster

Wil je bijvoorbeeld alleen overdag koelen, zet dan onder **Instellingen → Koelen** het blok **Dagelijks koelvenster** aan en stel de start- en eindtijd in. Het tandwiel bij **Koeltoestemming** op het overzicht opent dezelfde instellingen. Inschakelen kiest intern `Schedule` als `Cooling Enable Source`; uitschakelen kiest `Disabled`. Het schema geeft alleen toestemming om te koelen. Standaard blijft `Cooling Room Request Required` aan en begint koeling dus pas als de kamertemperatuur daadwerkelijk om koeling vraagt. Zet je die instelling bewust uit, dan vormt een actief tijdvenster zelf de koelvraag. De dauwpunt-, water- en flowbeveiligingen en `OpenQuatt Enabled` blijven in beide gevallen leidend.

De starttijd hoort bij het venster, de eindtijd niet: `08:00-20:00` is actief vanaf 08:00 tot vlak voor 20:00. Een venster mag over middernacht lopen, bijvoorbeeld `20:00-07:00`. Zijn start en einde gelijk, dan staat het venster uit; de veilige standaard `00:00-00:00` activeert na een update dus niets onverwacht.

Het schema gebruikt de lokale klok van de controller. Na een herstart zonder geldige netwerktijd blijft de schematoestemming veilig uit. Zodra SNTP de tijd heeft gesynchroniseerd, loopt de lokale klok op de controller door en wordt het venster automatisch opnieuw beoordeeld.

Aan het einde van het venster trekt OpenQuatt de koeltoestemming gecontroleerd in. Een nog lopende minimale compressortijd kan de compressor kort na de eindtijd laten doorlopen; daarna kan de pomp voor de normale postflow actief blijven. Een harde veiligheidsingreep mag de minimale looptijd wel doorbreken.

Wil je de exacte koelinstellingen, marges en begrenzingen begrijpen of wijzigen? Gebruik dan de technische naslag [Instellingen en meetwaarden](instellingen-en-meetwaarden.md#koeling).

### Waarom is dauwpunt zo belangrijk?

Bij vloerkoeling of andere watergedragen koeling wil je voorkomen dat oppervlakken te koud worden en vocht uit de lucht erop condenseert.

Daarom kijkt OpenQuatt bij koeling niet alleen naar comfort, maar ook naar veiligheid:

- is de lucht in huis vochtig;
- wat is dan de veilige ondergrens voor de watertemperatuur;
- mag cooling op dit moment dus wel of niet vrijgegeven worden.

Een dauwpunt kan uit Home Assistant, API-invoer of MQTT komen. In de web-app kies je de bron. Bij `Auto` gebruikt OpenQuatt de hoogste geldige dauwpuntwaarde, omdat die voor koeling de veiligste ondergrens geeft. Een externe waarde moet regelmatig worden bijgewerkt; bij een verouderde of ontbrekende waarde valt OpenQuatt terug op een andere geldige bron of blokkeert het koelen. Zie [API inputbronnen](api-input.md) en [MQTT inputbronnen](mqtt.md) voor de technische geldigheidsduur.

### Wat doet `Manual Cooling Enable`?

Die schakelaar geeft extra handmatige toestemming en omzeilt daarmee de gekozen `Cooling Enable Source`, dus ook een gesloten of nog niet geldige `Schedule`. Met de standaardinstelling `Cooling Room Request Required` blijft nog steeds een normale koelvraag nodig. De schakelaar omzeilt nooit `OpenQuatt Enabled`, dauwpunt-, water- of flowbeveiligingen.

`Manual Cooling Enable` is geen automatisch aflopende override. De gebruikte herstelmodus `RESTORE_DEFAULT_OFF` betekent dat een opgeslagen stand na een herstart terugkomt; alleen zonder opgeslagen stand is de standaard uit. Zet de schakelaar daarom zelf weer uit wanneer de handmatige toestemming niet meer nodig is.

Kort gezegd:

- handmatig toestaan is niet hetzelfde als onbeperkt mogen koelen.

## `Single` en `Duo`

Bij `Single` is er een warmtepomp. Bij `Duo` zijn het er twee.

Voor de meeste gebruikers is vooral dit belangrijk:

- `Single` is eenvoudiger te volgen;
- `Duo` hoeft niet altijd beide units tegelijk hard te laten werken;
- rustige, langere runs zijn meestal prettiger dan snel op- en afschakelen.

Het precieze gedrag hangt af van de gekozen strategie:

- bij stooklijnregeling werkt OpenQuatt in de basis rustig op naar `Duo`;
- bij `Power House` kijkt OpenQuatt meer naar welke geldige combinatie het beste past en het zuinigst is.

## Wat hoef je niet meteen te doen?

Je hoeft niet direct:

- ingewikkelde parameterlijsten te leren;
- allerlei instellingen tegelijk te veranderen;
- elk klein verschil in het dashboard te willen verklaren.

Voor de meeste gebruikers is deze volgorde beter:

1. eerst zorgen dat de juiste bronnen gekozen zijn;
2. daarna kijken of het systeem logisch en rustig reageert;
3. pas daarna kleine wijzigingen proberen.

## Verder lezen

- Installatie- of beheerroute kiezen: [Kies je route](../README.md#kies-je-route)
- OpenQuatt lokaal bedienen: [Web-app gebruiken](web-app.md)
- Optioneel Home Assistant-dashboard: [Dashboard gebruiken](dashboardoverzicht.md)
- Problemen oplossen: [Problemen oplossen](problemen-oplossen.md)
- Technische verdieping: [Instellingen en meetwaarden](instellingen-en-meetwaarden.md), [Power House](power-house.md) en [Water Temperature Control](water-temperature-control.md)
