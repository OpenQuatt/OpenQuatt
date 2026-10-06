# Verwarmen en comfort

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

![Kamercorrectie op Power House-huisvraag](../assets/powerhouse-kamercorrectie.svg)

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

## Geleidelijk opwarmen na nachtverlaging

Bij `Power House` kun je na nachtverlaging of langere afwezigheid geleidelijk
opwarmen. De functie staat standaard uit. Je thermostaat blijft de gewenste
eindtemperatuur bepalen; dit werkt ook met andere thermostaten dan Tado.

Zet de functie aan vóór je de thermostaat hoger zet. Bijvoorbeeld: van 17 naar
20,5 °C is een verhoging van 3,5 °C en overschrijdt de standaardstartgrens van
1,5 °C. Bij een gemeten kamertemperatuur van 18 °C wordt het eerste tussendoel
18,1 °C. Zodra de kamer dat bereikt, volgt de volgende stap. Wordt het tussendoel
na 45 minuten nog niet gehaald, dan wordt de stap groter.

Alleen inschakelen terwijl de thermostaat al op 20,5 °C staat, zet de bestaande
opwarming niet alsnog om in stappen. Binnen de comfortband van het einddoel neemt
de normale regeling weer over. Opwarmen kan langer duren; een energiebesparing
is niet gegarandeerd. De stooklijn gebruikt deze functie niet.

Zie [Geleidelijk opwarmen instellen](../web-app/instellingen.md#geleidelijk-opwarmen-na-nachtverlaging)
voor de bediening, instellingen en voorbeelden.

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


## Bediening en verder lezen

Zie [Instellingen aanpassen](../web-app/instellingen.md) voor de bediening in de web-app en [Dagelijkse controle](controleren.md) voor normaal gebruik.
