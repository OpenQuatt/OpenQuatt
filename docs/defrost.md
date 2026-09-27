# Defrostdiagnostiek

Onder **Instellingen buitenunit → Ontdooien** staan live metingen en een handmatige aanvraag per HP. De buitenunit blijft eigenaar van de ontdooicyclus. OpenQuatt stuurt geen eigen compressorfrequentie, ventilator of vierwegklep tijdens die cyclus.

De UI scheidt status en handmatige bediening, één methodekeuze met korte uitleg en ingeklapte **Technische metingen**. De keuze blijft een concept tot **Toepassen** en de aparte bevestiging; de uitgelezen methode blijft zichtbaar.

Het **i** naast **Ontdooimethode** licht de ondersteunde methoden van deze variant toe, inclusief de grenzen van de startvoorspelling. Interne ODU-kwalificatie blijft leidend; de uitleg toont geen gegarandeerde aftelling of universele drempels.

## Bediening

- Laad de actuele parameters uit de aangesloten ODU; voorbeeldwaarden zijn geen defaults.
- Een handmatige aanvraag vereist verse meetwaarden, een bekende ODU, ODU-gestuurde defrost, een draaiende compressor in verwarmen, geldige actuele flow en geen blokkerende incidenten of servicetaak. Bij Duo mag de andere HP niet bezig of onbekend zijn.
- Na bevestiging wordt eenmaal een ontdooiaanvraag verzonden. Een verloren antwoord veroorzaakt geen automatische herhaling. De terugmelding van de werkmodus bevestigt acceptatie; alleen de ontdooistatus niet.
- Alleen de ontdooimethode kan per HP worden gewijzigd. De mogelijkheden zijn variantafhankelijk: **V1 ondersteunt 0, 1 en 3; V1.5 en de bestaande V2-profielen behouden 0, 1, 3 en 4**. De gereserveerde modus is niet selecteerbaar. Vereist zijn verse identiteit/telemetrie, stilstaande compressor, geen actieve/aangevraagde defrost en geen incident of conflicterende serviceactie. Vóór schrijven wordt de actuele methode opnieuw gelezen; wijkt deze af van de getoonde waarde, dan eerst opnieuw laden. Er wordt precies één waarde geschreven en daarna teruggelezen; alleen een overeenkomende readback geldt als succes. Een onzeker antwoord wordt niet meteen herhaald of teruggedraaid.
- **Na herstart opnieuw toepassen** bewaart optioneel per HP alleen de methode en de bijbehorende ODU-identiteit in NVS. Hertoepassing wacht op verse identiteit, een stilstaande compressor en dezelfde veiligheidsvrijgave; een andere variant of control-board-identiteit blokkeert herstel. Opslagfouten worden gemeld en mogen niet als geslaagde toepassing worden getoond. Zonder deze optie wordt de bewaarde methode niet automatisch hersteld. Andere defrostparameters blijven alleen-lezen.
- V1 ondersteunt interval-/trendmethoden (waaronder methode 3), maar **niet** de V1.5 mode-4-regeling op `Ta − Tevap`. Een uitgelezen methode 4 op een V1 mag daarom niet als dezelfde methode worden geïnterpreteerd of opnieuw worden toegepast. De backend blokkeert dit ook buiten de UI om. Onbekende identiteiten kunnen niet schrijven.
- Tijdens wachten/ontdooien worden normale mode- en niveaucommando's vastgehouden. Veiligheidsstops blijven mogelijk.
- Een stopverzoek, ook `Force CM0`, wacht in CM1 met normale circulatie zolang de ODU of actuator nog actief is. De bestaande minimumlooptijd en defrosthold blijven gelden; CM0 volgt pas na stilstand. Veiligheidsstops worden hierdoor niet uitgesteld.
- Start de cyclus niet binnen 210 seconden, dan gaat de regeling pas na verse niet-actieve terugmelding verder. Na een actieve cyclus moeten beide terugmeldingen opnieuw niet-actief zijn. Vervolgens wordt de **actuele** gewenste werkmodus gestuurd, ook wanneer de warmtevraag inmiddels is verdwenen. Dit is geen bevestiging van fysieke uitvoering van die normale modewrite.
- Herstart of communicatieverlies wist meetzekerheid en lokale historie. Een nog actieve ODU-cyclus wordt opnieuw herkend. Incidentstops blijven ook bij herstart leidend.

De API gebruikt bestaande webauthenticatie, origincontrole en CSRF voor acties. HTTP en Modbus delen alleen een kleine gesynchroniseerde snapshot; regeling en Modbus-transities lopen op de hoofdloop. Het optionele NVS-profiel wijzigt geen EEPROM-persistentie in de buitenunit en start nooit een handmatige defrostcyclus.

## Telemetrie-ontwerpkeuze: vasthouden bij stale, stoppen bij langdurige blindheid

- Zolang mode-/defrostterugmeldingen vers zijn (30 s) volgt de regeling de ODU. Is de telemetrie stale maar is er geen bewezen defrost actief of aangevraagd, dan blijft het vorige compressorcommando staan; gewone demandwijzigingen wachten. Dit is bewust conservatief: een ongemerkt lopende ODU-cyclus mag niet door een normale write worden verstoord. Andere ODU-serviceoperaties kunnen de normale polling tijdelijk pauzeren; daarom is dit als contracttest vastgelegd.
- Pas na 90 seconden zonder geldige mode-/defrostmeting volgt een veiligheidsstop via `telemetry_fault`. Transportverlies zelf gaat elders sneller fail-closed; deze 30/90 s-keuze vervangt dat niet.

## Interpretatie

OpenQuatt leest de actuele instellingen op aanvraag uit. Aanvullende instellingen voor methode 4 worden alleen gelezen voor varianten die deze methode ondersteunen. Er is geen periodieke scan van het buitenunitgeheugen.

- Mode 0: geselecteerde verdampertemperatuurdrempel en geschatte lokale kwalificatietijd. Het interne adaptieve interval en de uitzonderingen daarop zijn niet volledig bekend.
- Mode 1: verdampertemperatuurdrempel; geen exacte aftelling voor de nog onduidelijke 45-/50-minutenlogica.
- Mode 2: gereserveerd; geen instelbare strategie.
- Mode 3: beperkte diagnostiek, geen reconstructie van interne trendhistorie.
- Mode 4: parameterafhankelijke `Ta - Tevap`-drempel en normale interval-/looptijdparameters, alleen op de varianten die deze methode implementeren. V1.5 en beide onderzochte V2-varianten hebben hiervoor een gevuld eigen parameterprofiel; de waarden verschillen per variant en worden daarom live uitgelezen. Dit omvat niet alle interne startpaden.

Lokale looptijden beginnen bij waarneming en kunnen na een herstart korter zijn dan de werkelijke ODU-looptijd. De tijdschalen van de interne duurbegrenzingen zijn niet volledig bekend: de benodigde eindbevestiging blijft daarom onbekend. Een mogelijke maximale-duur-eindreden blijft een afleiding, nooit een bewezen oorzaak.

## V1 tegenover V1.5

Handmatig ontdooien en defrostmethode 4 zijn twee verschillende zaken. De handmatige actie vraagt één extra cyclus aan; die blijft ook voor V1 beschikbaar wanneer alle normale veiligheidsvoorwaarden kloppen. De methodekeuze bepaalt daarentegen hoe de buitenunit normale ontdooicycli kwalificeert. De beschikbare methoden verschillen per buitenunitvariant.

De V1.5 heeft aanvullende mode-4-instellingen voor het verschil tussen buitentemperatuur en verdampingstemperatuur. Die uitbreiding ontbreekt op V1. Ook wanneer de actieve methode op mode 0 staat, kan V1.5 eigen instellingen voor mode 4 hebben. Mode 4 op V1.5 gebruikt het eigen V1.5-profiel; er hoeft en mag geen V2-profiel naar de V1.5 worden gekopieerd.

Ook beide ondersteunde V2-varianten hebben een eigen mode-4-profiel en staan standaard op mode 4. Oudere en nieuwere V2-buitenunits gebruiken daarbij niet exact dezelfde Ta−Tevap-drempels. OpenQuatt leest daarom altijd de actuele instellingen van de aangesloten buitenunit en gebruikt geen generiek V2-profiel.

V1 heeft daarnaast niet de V1.5-opties voor afdruiptijd en ventilatorbijschakeling op hoge druk tijdens ontdooien; OpenQuatt presenteert die daarom niet als V1-capability.

## Nog te valideren op hardware

Voor vrijgave: normale en handmatige cyclus, vraag weg tijdens defrost, gemiste ACK, 210-seconden-timeout, communicatieverlies/herstel, reboot tijdens defrost, incidentstop, Duo-overlap en de tijdschalen. Na pompwijzigingen opnieuw vereist: CM0→forced-defrost, flow onder/boven de grens, demandverlies tijdens defrost, lost ACK, reconnect en Duo. Controleer daarbij interne heap, grootste vrije blok en stackmarges onder gelijktijdige web/API/HA/MQTT/Modbus-belasting. Een geslaagde compile is daarvoor geen vervanging.

Expertwijzigingen aan algoritme of drempels zijn conform fase 3 van issue #733 pas aan de orde na praktijkvalidatie. Temperatuurgrenzen, intervallen, maximale duur, compressorfrequenties en overige defrostparameters worden niet geschreven; de UI toont geen invoervelden hiervoor.
