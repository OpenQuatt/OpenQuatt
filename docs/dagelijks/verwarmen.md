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

## Hoe kiest OpenQuatt een compressorstand?

Bij automatisch verwarmen bekijkt OpenQuatt de frequenties uit de ingelezen standentabel van iedere warmtepomp. Het performancemodel schat bij die frequenties en de buiten- en watertemperatuur hoeveel warmte de unit levert. De regeling kiest daaruit een passende stand; bij `Duo` beoordeelt zij de combinatie van beide units binnen de bestaande inschakel- en wisselvoorwaarden.

Zo kunnen extra standen tussen de bestaande meetpunten worden gebruikt. Automatisch verwarmen blijft voorlopig begrensd op **90 Hz**. Je ingestelde dag-/stiltegrens en uitgesloten frequenties kunnen die keuze verder beperken. Standen zonder geldige tabel of bruikbare performancegegevens zijn geen nieuwe startkandidaten. Bestaande beveiligingen en wachttijden blijven gelden.

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


### Temperatuurbegrenzing

Ook bij handbediening blijft een warmtepomp in Standby zolang de compressorvraag nul is of een startbewaking de vraag tegenhoudt. Het selecteren van Heating of Cooling geeft op zichzelf geen startvrijgave en wist geen verdiende minimale stilstandtijd.

OpenQuatt begrenst normale verwarming ook per warmtepomp op de actuele lokale buitenlucht en de gekalibreerde waterinlaat plus **2,5 °C**. Deze grens gebruikt de lokale meetontvangst zonder extra CiC-filter of vertraging. Dit geldt eveneens voor handmatige verwarming. Het fysieke standnummer wordt via de ingelezen frequentietabel beoordeeld; de centrale cv-aanvoer en een buitenwaarde uit Home Assistant bepalen deze veiligheidsgrens niet.

Voor het V1-profiel geldt boven 33 °C modelwater maximaal 79 Hz. Voor V2 geldt maximaal 90 Hz tot en met 37,5 °C, 72 Hz tot en met 52,5 °C en daarboven 55 Hz. De buitenluchtmaskers kunnen verder beperken; 20 en 26 Hz zijn bij V2 alleen toegestaan van 5 tot 14 °C. Tussenfrequenties vereisen geldige omliggende meetpunten.

Bij normale verwarming buiten een actieve defrosthold wordt een ongeldig geworden stand meteen verlaagd of gestopt. Een gezonde, door de ODU beheerde defrostcyclus behoudt zijn bestaande hold; een technische veiligheidsstop houdt voorrang. Tabelpunten komen pas terug wanneer de temperatuur meer dan 0,20 °C aan de geldige zijde van hun opgeslagen grens ligt. De waterplafonds zelf hebben geen extra hysterese. Een ruimere grens schakelt de compressor niet automatisch hoger.

Dit is een conservatief profiel op basis van CiC 4.2.0, geen volledige kopie van alle CiC-bedrijfsmodi. Buiten de bewezen dekking worden geen verwarmingsstanden toegestaan: V1 buitenlucht −15 tot 12 °C en modelwater maximaal 55 °C; V2 buitenlucht −15 tot 44,99 °C en modelwater maximaal 54,99 °C. Lage modelwatertemperaturen mogen uitsluitend tijdens de bestaande vrijgegeven koude-startsessie tegen het onderste tabelsteunpunt worden beoordeeld. Ontbrekende, ongeldige of meer dan 60 seconden oude lokale metingen blokkeren verwarming; pomp-, vorst-, incident- en defrostbewaking blijven verantwoordelijk voor de bedrijfsvolgorde.

Per HP zijn het temperatuurprofiel, modelwater, waterfrequentieplafond, het toegestane F-levelmasker en de begrenzingsredenen beschikbaar als diagnostische entities. Bit `n` van het masker betekent dat `Fn` temperatuurtechnisch toegestaan is; dag-/stiltegrenzen en andere bewaking kunnen die stand alsnog weigeren. Verouderde actieve Modbus-opdrachten worden ook vóór verzending en retries geweigerd. Een al verzonden transactie kan niet worden teruggenomen.
