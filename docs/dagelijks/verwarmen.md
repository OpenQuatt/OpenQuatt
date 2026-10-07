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

Power House berekent hoeveel warmte nodig is om je woning op temperatuur te houden. Met **Afkoeling onder gewenste temperatuur** kies je hoeveel graden de kamer mag afkoelen voordat Power House extra warmte vraagt. Voorbeeld: je wilt **21,0 °C** en stelt **0,2 °C afkoeling** in. Dan vraagt Power House extra warmte als de kamer onder **20,8 °C** komt. Met **0,4 °C afkoeling** gebeurt dat onder **20,6 °C**.

Met **Afremmen boven gewenste temperatuur** kies je wanneer Power House warmte gaat terugnemen. Je wilt bijvoorbeeld **20,0 °C** en stelt **0,3 °C** in: boven **20,3 °C** vraagt de regeling minder warmte. Met **1,0 °C** begint dat boven **21,0 °C**. Hoe verder de kamer boven die grens komt, hoe meer warmte wordt teruggenomen. De app toont de berekende temperatuur. Dit is geen gegarandeerde maximumtemperatuur.

Ook zonder extra opwarming kan de warmtepomp draaien om het warmteverlies van je woning te compenseren. Was de kamer langere tijd te koud, dan kan Power House al eerder extra warmte vragen en daarmee nog een tijdje doorgaan terwijl de kamer opwarmt. Beide comfortinstellingen werken ook als **Langer doorverwarmen** uitstaat. De warmtepomp stopt dan wanneer de regeling geen verwarming meer vraagt, rekening houdend met de minimale looptijd. De verdere afstelling staat onder **Geavanceerd**.

#### Langer doorverwarmen

Bij weinig warmtebehoefte kan een draaiende warmtepomp op laag vermogen blijven verwarmen. **Langer doorverwarmen** staat standaard uit en hoort alleen bij Power House. Je kiest een aparte stoptemperatuur en hoeveel de kamer daarna moet afkoelen. De lopende run mag ook bij warmte terugnemen door de comfortregeling doorgaan op laag vermogen. Een stilstaande warmtepomp wordt door deze schakelaar niet gestart.

Voorbeeld: je wilt **21,0 °C** en stelt bij **Stopgrens boven gewenste temperatuur** **0,7 °C** in. Power House vraagt de warmtepomp dan bij **21,7 °C** om te stoppen. Stel je bij **Afkoeling na stoppen** ook **0,7 °C** in, dan kan de warmtepomp bij **21,0 °C of lager** weer starten als je woning warmte nodig heeft. De app toont de temperaturen die uit jouw keuzes volgen.

De warmtepomp stopt later als hij zijn minimale looptijd nog moet afmaken. Daarna moet hij een minimale tijd uit blijven voordat hij weer kan starten. De radiatoren of vloer kunnen na de stop nog warmte afgeven. De instelling **Afkoeling onder gewenste temperatuur** begrenst hoe ver je de kamer laat afkoelen voordat opnieuw verwarmen mogelijk wordt. Zie [Langer doorverwarmen instellen](../web-app/instellingen.md#langer-doorverwarmen) voor voorbeelden en uitleg, en [Power House](../power-house.md) voor de technische werking.

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
