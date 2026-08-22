# Power House

Deze pagina is bedoeld voor gebruikers die `Power House` net iets beter willen begrijpen, zonder in interne regelcode te duiken.

> Zoek je vooral de korte uitleg? Begin dan bij [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md).

## Wat is Power House in gewone taal?

`Power House` stuurt niet eerst op een vaste aanvoertemperatuur, maar op de warmtevraag van het huis.

De gedachte is simpel:

- bij zacht weer heeft het huis minder warmte nodig;
- bij koud weer heeft het huis meer warmte nodig;
- als de kamer te koud wordt, vraagt `Power House` extra warmte;
- als de kamer te warm wordt, remt `Power House` juist af.

Daarom voelt deze strategie vaak meer als sturen op comfort dan als sturen op een klassieke stooklijn.

## Wanneer kies je dit?

`Power House` past meestal goed als je:

- vooral op kamertemperatuur en comfort wilt sturen;
- rustige, langere runs wilt;
- wilt dat OpenQuatt zelf slim omgaat met `Single` of `Duo`;
- minder wilt denken in "welke aanvoertemperatuur hoort vandaag bij het weer?".

Werk je liever direct met een stooklijn en aanvoertemperatuur, dan past [Water Temperature Control](water-temperature-control.md) vaak beter.

## Wat doet het systeem bij te koud, goed of te warm?

`Power House` werkt rond de ingestelde kamertemperatuur met een comfortband.

### Te koud

Onder de comfortband vraagt OpenQuatt extra warmte boven op de normale huisvraag.

Praktisch merk je dan:

- de warmtevraag loopt op;
- de compressorvraag blijft makkelijker actief;
- het systeem probeert de ruimte weer rustig terug richting setpoint te brengen.

### Bijna goed

Binnen de comfortband blijft de directe reactie juist rustiger.

Praktisch merk je dan:

- geen nerveus op- en afschakelen rond een paar tienden graad;
- minder kans op pendelen;
- een gelijkmatiger comfortgevoel.

### Te warm

Boven de bovengrens van de comfortband remt `Power House` de warmtevraag af.

Praktisch merk je dan:

- minder warmtevraag;
- sneller terugnemen als de ruimte wegdrijft;
- minder kans dat het systeem te lang blijft doorduwen.

## Waar kijkt Power House vooral naar?

De kern bestaat uit vijf onderdelen.

### 1. Het huismodel

OpenQuatt schat eerst hoeveel warmte het huis ongeveer nodig heeft op basis van buitentemperatuur.

Belangrijke instellingen:

- `House cold temp`
- `Maximum heating outdoor temperature`
- `Rated maximum house power`

Samen bepalen die hoe "zwaar" jouw woning aanvoelt voor de regeling.

### 2. Kamercorrectie

Daarna kijkt `Power House` of de kamer te koud, ongeveer goed of te warm is.

Belangrijke instellingen:

- `Power House comfort below setpoint`
- `Power House comfort above setpoint`
- `Power House temperature reaction`

Dit deel bepaalt vooral hoe fel of juist rustig de regeling op kamerafwijking reageert.

### 3. Reactiesnelheid

De berekende warmtevraag wordt niet abrupt doorgezet. OpenQuatt bouwt vraag bewust op en af.

Belangrijke instellingen:

- `Power House response profile`
- `Power House demand rise time`
- `Power House demand fall time`

Korter betekent sneller reageren. Langer betekent rustiger gedrag.

### 4. Begrenzing op water

Ook in `Power House` blijft de aanvoertemperatuur een veiligheid en rem.

Belangrijke instelling:

- `Maximum water temperature`

Als de aanvoer al hoog zit, neemt OpenQuatt de warmtevraag terug.

### 5. Verdeling naar compressor(en)

Pas daarna vertaalt OpenQuatt de warmtevraag naar compressorlevels.

Dat is belangrijk, want vreemd gedrag komt niet altijd uit het huismodel zelf. Soms zit het juist in:

- verkeerde bronkeuze;
- begrenzing op water;
- of de verdeling over één of twee warmtepompen.

## Externe warmtevraag (optioneel)

Standaard rekent `Power House` zelf uit hoeveel vermogen het huis nodig heeft, uit de buitentemperatuur en het huismodel. Heb je een eigen voorspelling die verder kijkt dan dat — bijvoorbeeld een model dat zonnewinst, wind of bewoningspatroon meeneemt — dan kun je die vraag rechtstreeks aan `Power House` doorgeven.

Zet daarvoor `Heat Demand Source` op `HA input` of `API input` en lever een waarde in watt. Staat de bron op `Disabled`, dan verandert er niets aan het gedrag dat je nu kent.

Wat die externe waarde wel en niet doet:

- Hij vervangt alleen de vermogensschatting uit het huismodel.
- De kamercorrectie, de begrenzing op `Rated maximum house power`, de reactiesnelheid, de waterbegrenzing en de vorstbeveiliging blijven van OpenQuatt zelf.
- Valt de bron weg, wordt hij te oud of stuurt hij een onbruikbare waarde, dan gaat `Power House` terug naar het eigen huismodel. Niet naar nul.

Daardoor kan een externe vraag de kamer niet boven het setpoint duwen: zodra de kamer warmer wordt dan gevraagd, trekt de kamercorrectie er weer vanaf. Wil je warmte in de tijd verschuiven, bijvoorbeeld voorverwarmen bij veel zon, dan is het kamer-setpoint daarvoor de juiste knop en niet de warmtevraag.

Kijk bij twijfel naar `Power House – demand source`. Die staat op `external` zolang de externe vraag echt gebruikt wordt, en op `model` zodra `Power House` op het eigen huismodel terugvalt.

## Welke instellingen zijn voor de meeste gebruikers het belangrijkst?

Als je `Power House` wilt afstellen, begin dan bijna altijd hier:

1. `Rated maximum house power`
2. `House cold temp`
3. `Maximum heating outdoor temperature`
4. `Power House temperature reaction`
5. `Power House comfort below setpoint`

Daarna eventueel:

6. `Power House comfort above setpoint`
7. `Power House demand rise time`
8. `Power House demand fall time`
9. `Maximum water temperature`

Verander liever niet meerdere van deze groepen tegelijk.

## Wat merk je bij Single en Duo?

### Single

Bij `Single` is het gedrag het makkelijkst te begrijpen:

- OpenQuatt berekent warmtevraag;
- vertaalt die naar een compressorlevel;
- en houdt daarna rekening met minimum looptijd en rustiger laaglastgedrag.

### Duo

Bij `Duo` kiest OpenQuatt niet automatisch altijd voor één warmtepomp of altijd voor twee.

In gewone taal:

- OpenQuatt kijkt welke combinaties logisch en toegestaan zijn;
- vergelijkt welke combinatie de vraag goed dekt;
- en probeert onnodig wisselen tussen `Single` en `Duo` te vermijden.

Voor de meeste gebruikers is vooral dit belangrijk:

- `Duo` hoeft dus niet altijd "meer vermogen" te betekenen;
- twee warmtepompen tegelijk zijn niet per definitie beter;
- het systeem probeert een praktische en zuinige keuze te maken.

## Wat hoef je meestal niet aan te raken?

Voor de meeste gebruikers zijn dit geen eerste afstelknoppen:

- interne optimizer-details rond `Duo`;
- laaglastdrempels die vooral bij technische diagnose helpen;
- compile-time constanten in de firmware;
- details rond runtime lead, ownerwissels en defrostgedrag.

Die informatie is nuttig bij echte analyse, maar zelden de eerste oplossing voor comfortklachten.

## Handige meetwaarden om naar te kijken

Als `Power House` niet logisch lijkt te reageren, kijk dan eerst naar:

- `Outside Temperature (Selected)`
- `Room Temperature (Selected)`
- `Room Setpoint (Selected)`
- `Water Supply Temp (Selected)`
- de actieve `Heating Control Mode`
- `Power House – demand source` en `Heat Demand (Selected)` als je een externe warmtevraag gebruikt

Controleer daarna pas of de afstelling zelf te scherp of te slap is.

## Veilige volgorde van afstellen

1. Controleer eerst of de gekozen bronnen kloppen.
2. Kijk of het huismodel logisch voelt bij zacht en koud weer.
3. Pas daarna de kamercorrectie aan.
4. Maak pas als laatste het gedrag sneller of trager.
5. Verander steeds maar één groep tegelijk en kijk minstens een tijdje naar het effect.

## Veelvoorkomende misverstanden

- `Power House` is geen gewone kamerthermostaat met een simpele PID.
- Meer reactie is niet automatisch beter; dat kan ook onrust geven.
- `Rise time` en `fall time` zijn niet de echte opwarmtijd van het huis, maar de reactiesnelheid van de regeling.
- `Duo` kiest niet blind altijd voor twee warmtepompen.

## Verder lezen

- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Water Temperature Control](water-temperature-control.md)
- [Regelgedrag van OpenQuatt](regelgedrag-van-openquatt.md)
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)
- [Problemen oplossen](problemen-oplossen.md)
