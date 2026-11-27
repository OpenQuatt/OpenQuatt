# Ontdooien

De buitenunit bepaalt wanneer een ontdooicyclus begint en eindigt. In OpenQuatt kun je de cyclus per buitenunit volgen, een ondersteunde methode kiezen of een extra ontdooicyclus aanvragen.

## Ontdooimethoden

- [Modus 0 met wisselende wachttijd](#modus-0-met-wisselende-wachttijd)
- [Modus 1 met 45 minuten draaitijd](#modus-1-met-45-minuten-draaitijd)
- [Modus 3 met eerdere metingen](#modus-3-met-eerdere-metingen)
- [Modus 4 met temperatuurverschil](#modus-4-met-temperatuurverschil)

Modus 2 is gereserveerd en ontbreekt in de methodekeuze.

## Modus 0 met wisselende wachttijd

De buitenunit begint met een interval van 50 minuten compressordraaitijd en kan dit aanpassen binnen een bereik van 45 tot 120 minuten. De teller loopt op terwijl de compressor draait. Hoelang het ontdooien zelf duurde bepaalt het volgende interval. Duurt een ontdooibeurt bijvoorbeeld 2 minuten, dan gaat een interval van 50 naar 70 minuten.

| Duur van de vorige ontdooibeurt | Volgende wachttijd |
| --- | --- |
| 1 s t/m 3 min | 20 minuten erbij, maximaal 120 minuten |
| 3 min 1 s t/m 4 min 59 s | Blijft gelijk |
| 5 min t/m 7 min 59 s | 10 minuten eraf, minimaal 45 minuten |
| Vanaf 8 min | Terug naar 45 minuten |

Bij ongeveer −3 tot +3 °C buiten kan een vaste wachttijd van 45 minuten voorrang krijgen, afhankelijk van de instelling. Voor de volgende ontdooibeurt moet ook de verdamper koud genoeg zijn. OpenQuatt kan de actuele verdampergrens uitlezen en schat hoelang de verdamper lokaal aan die voorwaarde voldoet. De buitenunit houdt de gekozen wachttijd zelf bij.

Bij droog winterweer kan een langere wachttijd onnodige onderbrekingen voorkomen. Trekt er mist op rond het vriespunt, dan kan rijp ineens sneller aangroeien en kan de gekozen wachttijd te lang zijn. Ook de beschikbare warmte uit het cv-water beïnvloedt hoelang het ontdooien duurt.

## Modus 1 met 45 minuten draaitijd

Voor een gewone ontdooiing moet de compressor in totaal 45 minuten hebben gedraaid. De verdamper moet ook lang genoeg onder zijn temperatuurgrens blijven. Staat de compressor tussendoor stil, dan pauzeert de teller. Met 30 minuten draaien, 10 minuten stilstand en nog 15 minuten draaien bereikt de unit na 55 minuten kloktijd zijn 45 minuten draaitijd.

Neem een temperatuurgrens van −3 °C. Bij −2 °C blijft de buitenunit verwarmen. Zakt de verdamper naar −4 °C en blijft hij lang genoeg onder −3 °C, dan kan de buitenunit ontdooien. OpenQuatt gebruikt ongeveer 3 minuten als schatting voor de tijd onder de grens. De draaitijdteller loopt op tot 50 minuten. Ook daarna kan de buitenunit blijven verwarmen zolang de verdamper te warm is.

De eerste ontdooiing kan vóór 45 minuten beginnen als de verdamper lang genoeg kouder dan −7 °C blijft en de compressor minstens 5 minuten heeft gedraaid. Bij mist kan rijp de luchtstroom al beperken vóór de gewone draaitijd is bereikt. Bij droge vorst kan de verdamper lang koud blijven terwijl er weinig rijp zit.

## Modus 3 met eerdere metingen

Draait de ventilator vrijwel op het gevraagde toerental, dan kan de buitenunit 60 metingen van de omvormer verzamelen. Hij vergelijkt het gemiddelde met een eerder opgeslagen waarde voor dezelfde ventilatorstand. In de uitgelezen profielen telt een afwijking van minstens 400 naar boven of beneden mee bij een ontdooiaanvraag. Dat getal hoort bij de interne schaal van het signaal. Welke fysieke grootheid de waarde weergeeft, is onbekend.

In die profielen schakelt de buitenunit de vergelijking in bij 8 °C buiten of kouder en uit bij 14 °C of warmer. Tussen die grenzen houdt hij zijn vorige stand vast. Bij 11 °C kan de vergelijking dus actief zijn als het eerder 6 °C was, of uitstaan als het eerder 15 °C was. De buitenunit houdt ook bij hoelang de verdamper onder zijn temperatuurgrens blijft. Voor een ontdooiaanvraag moet de compressor minstens 5 minuten hebben gedraaid.

Op een vochtige ochtend rond het vriespunt kan rijp aangroeien terwijl de ventilator op dezelfde snelheid blijft draaien. De buitenunit kan in die periode veranderingen in de omvormerwaarde meewegen. Wisselt de ventilator vaak van stand, dan kan het langer duren voordat er genoeg vergelijkbare metingen zijn. OpenQuatt kan het toerental en de verdampertemperatuur uitlezen. De referentiewaarde blijft in de buitenunit.

## Modus 4 met temperatuurverschil

Tijdens het verwarmen neemt het verdampende koudemiddel warmte op uit de buitenlucht. De buitenunit trekt de verdampingstemperatuur (`Tevap`) af van de buitenluchttemperatuur (`Ta`). Dat verschil telt mee bij de beslissing om te ontdooien.

Bij +3 °C buiten en een Tevap van −6 °C is het verschil 9 K. Daalt Tevap naar −9 °C terwijl het buiten +3 °C blijft, dan loopt het verschil op naar 12 K. Daarmee wordt de verschilgrens voor +3 °C in beide profielen hieronder bereikt.

### Temperatuurgrenzen bij modus 4

Onder +9 °C buiten kiest de buitenunit de verschilgrens op basis van de buitentemperatuur. De tabel toont het minimale verschil uit twee uitgelezen buitenunits, één V1.5 en één V2. De instellingen op jouw buitenunit kunnen afwijken.

| Buitenlucht (Ta) | Quatt V1.5 | Quatt V2 |
| --- | ---: | ---: |
| 0 tot onder +9 °C | 12 K | 12 K |
| −5 tot onder 0 °C | 11 K | 11 K |
| −10 tot onder −5 °C | 11 K | 10 K |
| −15 tot onder −10 °C | 10 K | 10 K |
| −20 tot onder −15 °C | 9 K | 9 K |
| −23 tot onder −20 °C | 7 K | 6 K |
| Onder −23 °C | 6 K | 5 K |

Bij −8 °C buiten ligt de grens in deze profielen op 11 K voor de V1.5 en 10 K voor de V2. Tevap moet dan bij de V1.5 −19 °C of kouder zijn en bij de V2 −18 °C of kouder.

Van +9 °C tot onder +15 °C buiten geldt in deze twee profielen een vaste grens voor Tevap zelf. Tevap moet dan −4 °C of kouder zijn. Vanaf +15 °C geldt −1 °C of kouder. Bij Tevap onder ongeveer −29,4 °C kan een aparte temperatuurvoorwaarde gelden.

### Tijdsvoorwaarden bij modus 4

In de beschreven instellingen geldt gewoonlijk een tijdgrens van 45 minuten en moet de compressor minstens 5 minuten hebben gedraaid. Rond +7 °C buiten of kouder kan voor de eerste ontdooibeurt 25 minuten gelden. De teller voor de tijdgrens loopt tijdens compressorbedrijf op tot maximaal 180 minuten. De buitenunit controleert ook hoelang het temperatuurverschil groot genoeg blijft. Na het bereiken van de tijdgrens kan hij blijven verwarmen zolang de temperatuurvoorwaarden nog uitblijven.

Bij sommige buitenunits kan de tijdgrens ook na de eerste ontdooibeurt korter worden als het temperatuurverschil bij koud weer lang genoeg groot blijft. Bij lang compressorbedrijf kan tot ongeveer +12 °C buiten een aparte temperatuurvoorwaarde meespelen.

Bij vochtig weer rond het vriespunt kan rijp de warmteopname bemoeilijken. Een oplopend temperatuurverschil kan daarop wijzen. Een harder draaiende compressor of minder lucht langs de lamellen kan het verschil ook vergroten. Bij droge vorst kunnen beide temperaturen juist laag zijn terwijl het verschil klein blijft. Bij −6 °C buiten en een Tevap van −12 °C is dat bijvoorbeeld 6 K.

## V1 tegenover V1.5

Handmatig ontdooien en defrostmethode 4 zijn twee verschillende zaken. De handmatige actie vraagt één extra cyclus aan; die blijft ook voor V1 beschikbaar wanneer alle normale veiligheidsvoorwaarden kloppen. De methodekeuze bepaalt daarentegen hoe de buitenunit normale ontdooicycli kwalificeert. De beschikbare methoden verschillen per buitenunitvariant.

De V1.5 heeft aanvullende mode-4-instellingen voor het verschil tussen buitentemperatuur en verdampingstemperatuur. Die uitbreiding ontbreekt op V1. Ook wanneer de actieve methode op mode 0 staat, kan V1.5 eigen instellingen voor mode 4 hebben. Mode 4 op V1.5 gebruikt het eigen V1.5-profiel; er hoeft en mag geen V2-profiel naar de V1.5 worden gekopieerd.

Ook beide ondersteunde V2-varianten gebruiken de mode-4-instellingen van de aangesloten buitenunit. OpenQuatt leest de actuele waarden uit wanneer je de instellingen opvraagt.

## Bediening

Onder **Instellingen buitenunit → Ontdooien** zie je de cyclusstatus per buitenunit en kun je **Handmatig ontdooien** aanvragen. Kies **Instellingen uitlezen** om de huidige methode te laden. Het info-icoon naast **Ontdooimethode** geeft uitleg per ondersteunde methode. Je wijziging blijft een concept tot je **Toepassen** kiest en de wijziging bevestigt. De uitgelezen methode blijft zichtbaar.

Onder **Technische metingen** staan de buitenluchttemperatuur, de temperatuur van de verdamper, de verdampingstemperatuur van het koudemiddel en de compressorfrequentie. OpenQuatt toont ook de duur van de gemeten cyclus en, zodra een eerdere ontdooibeurt is waargenomen, de tijd sinds die beurt. De buitenunit houdt zijn eigen startvoorwaarden bij.

## Technische werking

OpenQuatt volgt de ontdooicyclus van de buitenunit. De buitenunit regelt daarbij zijn eigen compressor, ventilator en vierwegklep.

### Instellingen en ontdooiaanvragen

OpenQuatt leest de instellingen op aanvraag uit. De aanvullende instellingen voor modus 4 worden alleen gelezen bij buitenunits die deze methode ondersteunen. Met **Na herstart opnieuw toepassen** controleert OpenQuatt het bewaarde profiel zodra de identiteit en metingen na opstart of verbindingsherstel weer geldig zijn. Daarna leest OpenQuatt ongeveer eens per tien minuten alleen de gekozen ontdooimethode. Bij een afwijking leest OpenQuatt de instellingen opnieuw en gelden dezelfde voorwaarden voordat het iets terugschrijft.

- Laad de actuele parameters uit de aangesloten ODU; voorbeeldwaarden zijn geen defaults.
- Een handmatige aanvraag vereist verse meetwaarden, een bekende ODU, ODU-gestuurde defrost, een draaiende compressor in verwarmen, geldige actuele flow en geen blokkerende incidenten of servicetaak. Bij Duo mag de andere HP niet bezig of onbekend zijn.
- Na bevestiging wordt eenmaal een ontdooiaanvraag verzonden. Een verloren antwoord veroorzaakt geen automatische herhaling. De terugmelding van de werkmodus bevestigt acceptatie; alleen de ontdooistatus niet.
- Alleen de ontdooimethode kan per buitenunit worden gewijzigd. **V1 ondersteunt 0, 1 en 3. V1.5 en de ondersteunde V2-buitenunits ondersteunen ook 4.** Wijzigen vereist een bekende buitenunit, verse metingen en een stilstaande compressor. Een ontdooiing, incident of conflicterende serviceactie blokkeert de wijziging. OpenQuatt leest de actuele methode opnieuw uit vóór het schrijven. Wijkt deze af van de getoonde waarde, laad de instellingen dan opnieuw. Bij een wijziging schrijft OpenQuatt de methode eenmaal en bevestigt die met een overeenkomende teruglezing. Bij een onzekere uitkomst kun je de actuele methode opnieuw uitlezen.
- **Na herstart opnieuw toepassen** bewaart optioneel per HP alleen de methode en de bijbehorende ODU-identiteit in NVS. Hertoepassing wacht op verse identiteit, een stilstaande compressor en dezelfde veiligheidsvrijgave; een andere variant of control-board-identiteit blokkeert herstel. Opslagfouten worden gemeld en mogen niet als geslaagde toepassing worden getoond. Zonder deze optie wordt de bewaarde methode niet automatisch hersteld. Andere defrostparameters blijven alleen-lezen.
- Bij opslag wordt automatische hertoepassing eerst duurzaam geblokkeerd. Alleen een volledig bevestigde opslag met deze optie aan verleent opnieuw toestemming. Een fout ná die blokkering kan de oude toestemming na een herstart niet terugbrengen. Mislukt al de eerste opslag van de blokkering, dan kan OpenQuatt dit niet garanderen: de interface meldt dat de oude keuze na een herstart kan terugkomen. Sla de keuze opnieuw op en controleer de bevestiging. Oudere profielen zonder bevestigde toestemming worden na een upgrade niet automatisch toegepast; bevestig de optie opnieuw als je haar wilt blijven gebruiken.
- V1 ondersteunt interval-/trendmethoden (waaronder methode 3), maar **niet** de V1.5 mode-4-regeling op `Ta − Tevap`. Een uitgelezen methode 4 op een V1 mag daarom niet als dezelfde methode worden geïnterpreteerd of opnieuw worden toegepast. De backend blokkeert dit ook buiten de UI om. Onbekende identiteiten kunnen niet schrijven.
- Tijdens wachten/ontdooien worden normale mode- en niveaucommando's vastgehouden. Veiligheidsstops blijven mogelijk.
- Een stopverzoek, ook `Force CM0`, wacht in CM1 met normale circulatie zolang de ODU of actuator nog actief is. De bestaande minimumlooptijd en defrosthold blijven gelden; CM0 volgt pas na stilstand. Veiligheidsstops worden hierdoor niet uitgesteld.
- Als OpenQuatt binnen 210 seconden geen cyclus waarneemt, verloopt de aanvraag zodra beide terugmeldingen vers zijn en aangeven dat de unit niet ontdooit. Na een actieve cyclus moeten beide meldingen ook opnieuw aangeven dat de ontdooiing voorbij is. Daarna stuurt OpenQuatt de werkmodus die op dat moment bij de warmtevraag past.
- Herstart of communicatieverlies wist meetzekerheid en lokale historie. Een nog actieve ODU-cyclus wordt opnieuw herkend. Incidentstops blijven ook bij herstart leidend.

De API gebruikt bestaande webauthenticatie, origincontrole en CSRF voor acties. HTTP en Modbus delen alleen een kleine gesynchroniseerde snapshot; regeling en Modbus-transities lopen op de hoofdloop. Het optionele NVS-profiel wijzigt geen EEPROM-persistentie in de buitenunit en start nooit een handmatige defrostcyclus.

### Wegvallende telemetrie

- Zolang mode-/defrostterugmeldingen vers zijn (30 s) volgt de regeling de ODU. Is de telemetrie stale maar is er geen bewezen defrost actief of aangevraagd, dan blijft het vorige compressorcommando staan; gewone demandwijzigingen wachten. Dit is bewust conservatief: een ongemerkt lopende ODU-cyclus mag niet door een normale write worden verstoord. Andere ODU-serviceoperaties kunnen de normale polling tijdelijk pauzeren; daarom is dit als contracttest vastgelegd.
- Pas na 90 seconden zonder geldige mode-/defrostmeting volgt een veiligheidsstop via `telemetry_fault`. Transportverlies zelf gaat elders sneller fail-closed; deze 30/90 s-keuze vervangt dat niet.

Lokale looptijden beginnen bij waarneming en kunnen na een herstart korter zijn dan de werkelijke ODU-looptijd. De tijdschalen van de interne duurbegrenzingen zijn niet volledig bekend: de benodigde eindbevestiging blijft daarom onbekend. Een mogelijke maximale-duur-eindreden blijft een afleiding, nooit een bewezen oorzaak.

### Hardwarevalidatie

Voor het testen van de aansturing en het opslaan en opnieuw toepassen van de ontdooimethode gelden de [HIL-testaanpak](development/hil-testing.md) en het [HIL-plan voor ontdooiprofielen](development/hil-defrost-profiles.md).
