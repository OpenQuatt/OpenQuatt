# Water Temperature Control

Deze pagina is bedoeld voor gebruikers die de stooklijnregeling van OpenQuatt beter willen begrijpen, zonder meteen in technische details te verdwijnen.

> Zoek je vooral de korte uitleg? Begin dan bij [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md).

## Wat is Water Temperature Control?

`Water Temperature Control` is de strategie die vooral denkt in gewenste aanvoertemperatuur.

De gedachte is:

- hoe koud is het buiten;
- welke aanvoertemperatuur hoort daar volgens de stooklijn bij;
- hoe ver zit de echte aanvoer daarvan af;
- hoeveel compressorvraag is nodig om dat doel te volgen.

Dit voelt vaak vertrouwder voor gebruikers die gewend zijn aan een klassieke stooklijn.

## Wanneer kies je dit?

Deze strategie past meestal goed als je:

- graag in een stooklijn denkt;
- vooral naar aanvoertemperatuur kijkt;
- een voorspelbare weersafhankelijke regeling wilt;
- liever minder directe kamercorrectie gebruikt dan bij `Power House`.

Wil je juist sterker op comfort en kamertemperatuur sturen, dan past [Power House](power-house.md) vaak beter.

## Hoe werkt het in gewone taal?

De regeling loopt in de praktijk in vier stappen.

### 1. De buitentemperatuur bepaalt het doel

OpenQuatt gebruikt de gekozen buitentemperatuur om een aanvoertemperatuurdoel te bepalen.

Belangrijk:

- als de buitentemperatuurbron niet klopt, klopt de hele stooklijn ook niet;
- een te lage buitenwaarde geeft vaak een te hoge gevraagde aanvoer;
- een te hoge buitenwaarde geeft vaak een te lage gevraagde aanvoer.

### 2. De stooklijn maakt een supply target

De stooklijn is opgebouwd uit een paar vaste buitentemperatuurpunten. Tussen die punten rekent OpenQuatt vloeiend door.

Belangrijke instellingen:

- `Curve Tsupply @ -20°C`
- `Curve Tsupply @ -10°C`
- `Curve Tsupply @ 0°C`
- `Curve Tsupply @ 5°C`
- `Curve Tsupply @ 10°C`
- `Curve Tsupply @ 15°C`
- `Curve Fallback Tsupply (No Outside Temp)`

Hiermee bepaal je dus vooral: welke aanvoer hoort bij welk weer?

### 3. De PID houdt de aanvoer op koers

Daarna vergelijkt OpenQuatt het doel met de gemeten aanvoer.

Belangrijke instellingen:

- `Heating Curve PID Kp`
- `Heating Curve PID Ki`
- `Heating Curve PID Kd`

In gewone taal:

- `Kp` bepaalt de directe reactie;
- `Ki` werkt blijvende afwijking weg;
- `Kd` dempt snelle schommelingen.

Voor de meeste installaties is `Kp` en `Ki` belangrijker dan `Kd`.

### 4. Begrenzing en laaglastgedrag houden het rustig

OpenQuatt laat de regeling niet zomaar hard naar nul vallen als de vraag klein wordt.

Praktisch merk je dan:

- minder pendelen rond lage vraag;
- rustigere overgang naar uit;
- minder vaak snel opnieuw starten.

Ook hier blijft `Maximum water temperature` belangrijk als rem en beveiliging.

## Wat doet de kamer nog in deze modus?

De kamer blijft wel meetellen, maar niet als hoofdregeling.

Praktisch:

- als de ruimte al duidelijk te warm wordt, kan OpenQuatt het stooklijndoel wat afremmen;
- als de ruimte iets te koud is, blijft de stooklijn zelf het hoofdmodel;
- deze strategie is dus minder direct kamergericht dan `Power House`.

Dat maakt `Water Temperature Control` vaak rustiger en voorspelbaarder, maar soms ook minder "comfortgestuurd".

## Warmtetoestemming: meestal de thermostaat

Bij `Water Temperature Control` bepaalt de buitentemperatuur via de curve het gewenste aanvoerdoel; de PID regelt daarna de werkelijke aanvoer naar dat doel. De kamer is een aanvullende correctie, niet de primaire vraagregelaar.

Daarom is bij een normale installatie met kamerthermostaat een externe warmtetoestemming juist logisch: de thermostaat/zone-regeling bepaalt *of* verwarming nodig is, OpenQuatt bepaalt met de stooklijn *hoe warm* het water moet zijn.

Tijdens Quick Start zet een strategieswitch naar `Water Temperature Control` de warmtetoestemming automatisch op de eerder gekozen, gekoppelde thermostaatbron. Dat is op de Q-edition normaal `Heating Enable Source = OT thermostat`; op andere hardware kan dit `CIC` of `HA input` zijn. Een uitgeschakelde of niet-geconfigureerde bron wordt niet automatisch als harde gate gekozen. `Niet gebruiken` blijft mogelijk voor bewust volledig weersafhankelijk bedrijf met permanent open afgiftesysteem.

`Heating Enable Source = Niet gebruiken` betekent: geen externe warmtetoestemming; de strategie mag zelf warmtevraag opbouwen zonder harde gate. Met `Niet gebruiken` kan de stooklijn dus ook verwarmen terwijl de kamer al boven setpoint is.

Buiten Quick Start toont `Instellingen → Verwarmen` bij een afwijkende keuze alleen een advies (`Aanbevolen instelling` / `Controleer configuratie`) en wordt de instelling niet stil overschreven. Voor de volledige matrix zie [Instellingen en meetwaarden](instellingen-en-meetwaarden.md#5-bronselectie).

## Extern aanvoertarget (optioneel)

Standaard bepaalt de stooklijn het aanvoerdoel uit de buitentemperatuur, met een kleine kamertrim erbovenop. Laat je dat doel liever door een externe regelaar bepalen — bijvoorbeeld een OpenTherm-thermostaat die zelf al kamerafwijking in `TSet` verwerkt, een buffervatregeling in Home Assistant of een MPC-optimalisatie — dan kun je dat target rechtstreeks doorgeven.

Zet daarvoor `Heating Supply Target Source` op `OT thermostat`, `HA input`, `API input` of `MQTT` en lever een waarde in graden Celsius. Staat de bron op `Heating curve`, dan verandert er niets aan het gedrag dat je nu kent.

Voor `HA input` luistert OpenQuatt naar twee vaste entiteiten in Home Assistant:

- `sensor.openquatt_ext_heating_supply_target` — het gewenste aanvoerdoel in graden Celsius;
- `binary_sensor.openquatt_ext_heating_supply_target_valid` — moet `on` staan, anders negeert OpenQuatt de waarde en valt hij terug op de stooklijn.

Die tweede entiteit is jouw eigen geldigheidsschakelaar: zet hem `off` zodra je berekening verouderd of onbetrouwbaar is. Uitzetten werkt direct: een eerder vastgehouden waarde wordt meteen ingetrokken en de stooklijn neemt het weer over, zonder overbruggingsvenster. Alleen waarden binnen 20…70 °C tellen mee. Voor `API input` en `MQTT` stuur je de waarde naar een lokaal endpoint of topic; zie [API inputbronnen](api-input.md) en [MQTT inputbronnen](mqtt.md). Die wegen hebben geen aparte geldigheidsentiteit maar een eigen vervaltijd van 15 minuten. Voor `OT thermostat` gebruikt OpenQuatt de al ontvangen `OT - Control Setpoint` (`TSet`), met een eigen versheidsbewaking: zonder recent `TSet`-bericht valt de regeling terug op de stooklijn. Ook hier telt alleen een `TSet` binnen 20…70 °C mee: `TSet=0` (thermostaat zonder warmtevraag) of een andere waarde buiten bereik valt terug op de stooklijn in plaats van als extern minimumtarget te gelden.

Wat die externe waarde wel en niet doet:

- Hij vervangt alleen het berekende stooklijntarget, inclusief kamertrim. De trim wordt bij een externe target niet nogmaals toegepast, anders zou de correctie dubbel tellen.
- `Maximum water temperature`, de water-temperatuurlimieten en trips, de PID, de demandlogica en de compressor- en Duo-dispatch blijven van OpenQuatt zelf.
- Valt de bron weg, wordt hij te oud of stuurt hij een onbruikbare waarde, dan gaat de regeling terug naar de eigen stooklijn. Niet naar nul of uit.

Dit staat los van `Heating Enable`: die bepaalt *of* er verwarmd mag worden, het aanvoertarget bepaalt *hoe warm* het water moet zijn. Voorbeeld buffervat: onder 38 °C buffer­temperatuur `Heating Enable` aan met een aanvoertarget van 42 °C, boven 40 °C `Heating Enable` uit.

Kijk bij twijfel naar `Heating Supply – target source`. Die staat op `external` zolang het externe target echt gebruikt wordt, en op `curve` zodra de stooklijn weer rekent. `Heating Curve Supply Target` blijft altijd het lokale stooklijnreferentie­doel tonen, ook terwijl een extern target actief is.

## Welke instellingen zijn voor de meeste gebruikers het belangrijkst?

Als je deze strategie afstelt, begin dan bijna altijd hier:

1. de zes stooklijnpunten
2. `Curve Fallback Tsupply (No Outside Temp)`
3. `Heating Curve PID Kp`
4. `Heating Curve PID Ki`
5. `Maximum water temperature`

Bij `Duo` daarna eventueel:

6. `Minimum runtime`
7. `Dual HP Enable Level`
8. `Dual HP Enable Hold`
9. `Dual HP Disable Hold`

## Wat merk je bij Single en Duo?

### Single

Bij `Single` is het gedrag vrij rechtlijnig:

- stooklijn bepaalt het doel;
- PID stuurt de aanvoer;
- de compressor volgt die vraag binnen de normale beveiligingen.

### Duo

Bij `Duo` werkt deze strategie eenvoudiger dan bij `Power House`.

Praktisch:

- OpenQuatt begint normaal met één warmtepomp;
- de tweede unit komt pas bij als de vraag hoog genoeg en lang genoeg aanwezig blijft;
- er zit dus bewust wachttijd en hysterese in.

Dat voorkomt onrustig schakelen tussen één en twee warmtepompen.

## Wat hoef je meestal niet meteen aan te raken?

Voor de meeste gebruikers zijn dit geen eerste knoppen:

- ingewikkelde lage-vraag faselogica;
- alle details van `HEAT`, `COAST` en `OFF`;
- een hoge `Kd` om kleine problemen "weg te dempen";
- meerdere stooklijnpunten en PID-waarden tegelijk.

Begin bijna altijd met de stooklijn zelf. Pas daarna komt PID.

## Handige meetwaarden om naar te kijken

Als deze strategie niet logisch voelt, kijk dan eerst naar:

- `Outside Temperature (Selected)`
- `Water Supply Temp (Selected)`
- `Heating Curve Supply Target`
- `Heating Supply Target (Selected)`
- `Heating Supply – target source`
- de actieve `Heating Control Mode`

Controleer daarna pas of de PID of `Duo`-grenzen te scherp staan.

## Veilige volgorde van afstellen

1. Controleer eerst of de gekozen buitentemperatuur en aanvoerbron kloppen.
2. Maak daarna de stooklijn logisch.
3. Pas daarna pas `Kp` en `Ki` aan.
4. Kijk bij `Duo` pas als laatste naar de inschakel- en wachttijden.
5. Verander steeds maar één groep tegelijk.

## Veelvoorkomende misverstanden

- Een goede stooklijn maakt PID niet overbodig.
- Meer `Kp` is niet automatisch beter.
- `Duo` kiest hier niet dezelfde logica als in `Power House`.
- Een rustige `COAST`-fase betekent niet automatisch dat er iets mis is.

## Verder lezen

- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Power House](power-house.md)
- [Regelgedrag van OpenQuatt](regelgedrag-van-openquatt.md)
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)
- [Problemen oplossen](problemen-oplossen.md)
