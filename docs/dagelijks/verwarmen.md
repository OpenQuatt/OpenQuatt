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

Bij **Instellingen → Verwarmen → Power House — comfort** vind je twee herkenbare onderdelen.

#### Op temperatuur houden

Power House blijft zelf bepalen hoeveel warmte je woning nodig heeft, ook bij de gewenste temperatuur. Met **Reageren op afkoeling** kies je hoe ver onder die temperatuur extra opwarming wordt gevraagd. Bijvoorbeeld: **21,0 °C** gewenst en **0,2 °C** geeft een normale koude grens van **20,8 °C**. Een grotere waarde legt die grens lager.

Gewone verwarming kan al eerder actief zijn; dit is geen simpele aan/uit-band. Na een langdurige temperatuurachterstand kan extra opwarming nog doorwerken. De specialistische afstelling staat onder **Geavanceerd**; voor dagelijks gebruik hoef je die niet aan te passen.

#### Langer doorverwarmen

Bij weinig warmtevraag kun je een lopende verwarmingsperiode op laag vermogen laten doorgaan. **Langer doorverwarmen** staat standaard uit en hoort alleen bij Power House. Je kiest een stopgrens boven de gewenste temperatuur en hoeveel afkoeling daarna nodig is voordat opnieuw verwarmen mogelijk wordt. Inschakelen start een stilstaande warmtepomp niet zelfstandig.

Voorbeeld: bij **21,0 °C** gewenst en **Stopgrens boven gewenste temperatuur +0,7 °C** wordt bij **21,7 °C** een stop aangevraagd. Met **Afkoeling vóór opnieuw verwarmen 0,7 °C** is vanaf **21,0 °C** opnieuw verwarmen mogelijk als er warmtevraag is. De app toont de temperaturen die uit jouw keuzes volgen.

Minimumlooptijd kan de stop uitstellen; wachttijden en beveiligingen kunnen de herstart uitstellen. Restwarmte kan de kamer na de stop verder opwarmen. De normale koude grens begrenst het extra wachten, ook bij veel ingestelde afkoeling. Zie [Langer doorverwarmen instellen](../web-app/instellingen.md#langer-doorverwarmen) voor de bediening en voorbeelden, en [Power House](../power-house.md) voor de technische werking.

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
