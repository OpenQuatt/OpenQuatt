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

Bij **Instellingen → Verwarmen → Power House** vind je de nieuwe uitlegstroom met verklarende tekstblokken tussen de instellingen.

### Vereenvoudigde Power House (nieuwe versie)

Sinds de herontwerp is Power House eenvoudiger geworden:

- **⚠️ Geen comfortband meer**: De regeling gebruikt nu direct de afwijking van het setpoint, `phKp` is altijd leidend
- **Traagheid als schakelaar**: De comfort memory (opbouw tot maximaal 0,2 °C) is nu in- en uitschakelbaar via **Traagheid**
- **⚠️ één herstarttemperatuur**: Geen onderscheid meer tussen warme en koude herstart

#### Hoofdinstellingen

De nieuwe Power House flow bevat:

1. **Woningmodel** (houseColdTemp, houseOutdoorMax, housePower) - Bepaalt het warmteverlies van je woning
2. **Responsprofiel** - Kies hoe rustig of direct Power House reageert
3. **Kamercorrectie (phKp)** - Hoe sterk de regeling bijstuurt bij afwijking van het setpoint
4. **Traagheid (phInertia)** - Integrerende regeling aan/uit, max 0,2 °C
5. **Stoptemperatuur (phStopTemperature)** - Temperatuur boven setpoint waarbij de warmtepomp stopt
6. **Herstarttemperatuur (phRestartTemperature)** - Temperatuur waarbij opnieuw verwarmen mogelijk is (moet ≥ 0,1 °C onder stoptemperatuur)
7. **Langer doorverwarmen (phRunExtension)** - Minimaal vermogen tot aan de stoptemperatuur

#### Validatie

- **Stoptemperatuur** moet ≥ 0 zijn
- **Herstarttemperatuur** moet tenminste 0,1 °C lager zijn dan de stoptemperatuur
- Herstarttemperatuur mag negatief zijn (bv. -0,5 voor herstart onder setpoint)

### Oude Power House instellingen (verouderd)

> ⚠️ **Opgelet**: De volgende instellingen zijn behouden voor backward compatibility maar worden niet meer actief gebruikt in de nieuwe regeling:
>
> - **Afkoeling onder gewenste temperatuur** (phComfortBelow) - Vervangen door Traagheid + directe setpoint regeling
> - **Afremmen boven gewenste temperatuur** (phComfortAbove) - Vervangen door phStopTemperature
> - **Stopgrens boven gewenste temperatuur** (phRunExtensionStopMargin) - Vervangen door phStopTemperature
> - **Afkoeling na stoppen** (phRunExtensionRestartCooldown) - Vervangen door phRestartTemperature

Zie [Power House technische documentatie](../power-house.md) voor de complete uitleg van de nieuwe regeling. Zie [Langer doorverwarmen instellen](../web-app/instellingen.md#langer-doorverwarmen) voor voorbeelden en uitleg, en [Power House](../power-house.md) voor de technische werking.

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


### Temperatuurbegrenzing en handbediening

De buitenunit voert zijn eigen temperatuurafhankelijke frequentiebegrenzing en beveiligingen uit volgens de ODU-configuratie. De gerealiseerde compressorfrequentie kan daardoor lager zijn dan de gevraagde stand. OpenQuatt kopieert deze ODU-plafonds niet naar een tweede temperatuur-envelop. Het bereik van een prestatietabel is geen zelfstandige bedrijfsgrens: boven 12 °C buitenlucht wordt V1/V1.5 niet door zo'n extra envelop geblokkeerd.

OpenQuatt behoudt zijn bestaande flow-, vorst-, waterovertemperatuur-, incident-, start/stop- en defrostbewaking. Dag-/stiltegrenzen en frequentie-uitsluitingen blijven bij automatisch bedrijf gelden. Prestatietabellen worden gebruikt voor vermogensschatting en standkeuze; ontbrekende modelgegevens kunnen de beschikbare modelkandidaten beperken.

Ook bij handbediening blijft een warmtepomp in Standby zolang de compressorvraag nul is of een startbewaking de vraag tegenhoudt. Het selecteren van Heating of Cooling geeft op zichzelf geen startvrijgave en wist geen verdiende minimale stilstandtijd.
