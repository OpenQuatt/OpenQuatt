# Defrostdiagnostiek

Onder **Instellingen buitenunit → Ontdooien** staan live metingen en een handmatige aanvraag per HP. De buitenunit blijft eigenaar van de ontdooicyclus. OpenQuatt stuurt geen eigen compressorfrequentie, ventilator of vierwegklep tijdens die cyclus.

De UI scheidt status en bediening, **Instellingen** (methode met uitleg; alleen de defrostmethode is wijzigbaar) en ingeklapte **Technische metingen**.

## Bediening

- Laad de actuele parameters uit de aangesloten ODU; voorbeeldwaarden zijn geen defaults.
- Een handmatige aanvraag vereist verse meetwaarden, een bekende ODU, ODU-gestuurde defrost, een draaiende compressor in verwarmen, geldige actuele flow en geen blokkerende incidenten of servicetaak. Bij Duo mag de andere HP niet bezig of onbekend zijn.
- Na bevestiging wordt eenmaal `3999=4` verzonden. Een verloren antwoord veroorzaakt geen automatische herhaling. `working_mode=4` bevestigt acceptatie; alleen een defrost-bit niet.
- Alleen de ontdooimethode kan per HP tijdelijk worden gewijzigd. De mogelijkheden zijn variantafhankelijk: **V1 ondersteunt 0, 1 en 3; V1.5 en de bestaande V2-profielen behouden 0, 1, 3 en 4**. De gereserveerde modus is niet selecteerbaar. Vereist zijn verse identiteit/telemetrie, stilstaande compressor, geen actieve/aangevraagde defrost en geen incident of conflicterende serviceactie. Vóór schrijven wordt de actuele methode opnieuw gelezen; wijkt deze af van de getoonde waarde, dan eerst opnieuw laden. Er wordt precies één waarde geschreven en daarna teruggelezen; alleen een overeenkomende readback geldt als succes. Geen retry, rollback, NVS-profiel of automatische hertoepassing.
- De V1-firmware 1.25 heeft wel het legacy interval-/trendpad (waaronder methode 3), maar **niet** de V1.5 mode-4-regeling op `Ta − Tevap`. Een ruwe waarde 4 op een V1 mag daarom niet als dezelfde methode worden geïnterpreteerd of opnieuw worden geschreven. De backend blokkeert dit ook buiten de UI om. Onbekende identiteiten kunnen niet schrijven.
- Tijdens wachten/ontdooien worden normale mode- en niveaucommando's vastgehouden. Veiligheidsstops blijven mogelijk.
- Start de cyclus niet binnen 210 seconden, dan gaat de regeling pas na verse niet-actieve terugmelding verder. Na een actieve cyclus moeten beide terugmeldingen opnieuw niet-actief zijn. Vervolgens wordt de **actuele** gewenste werkmodus gestuurd, ook wanneer de warmtevraag inmiddels is verdwenen. Dit is geen bevestiging van fysieke uitvoering van die normale modewrite.
- Herstart of communicatieverlies wist meetzekerheid en lokale historie. Een nog actieve ODU-cyclus wordt opnieuw herkend. Incidentstops blijven ook bij herstart leidend.

De API gebruikt bestaande webauthenticatie, origincontrole en CSRF voor acties. HTTP en Modbus delen alleen een kleine gesynchroniseerde snapshot; regeling en Modbus-transities lopen op de hoofdloop. Er worden geen NVS-profielen of automatische EEPROM-persistentie toegevoegd; wijzigingen van de defrostmethode zijn tijdelijk en vragen een aparte productkeuze voor opslag/herstel.

## Telemetrie-ontwerpkeuze: vasthouden bij stale, stoppen bij langdurige blindheid

- Zolang mode-/defrostterugmeldingen vers zijn (30 s) volgt de regeling de ODU. Is de telemetrie stale maar is er geen bewezen defrost actief of aangevraagd, dan blijft het vorige compressorcommando staan; gewone demandwijzigingen wachten. Dit is bewust conservatief: een ongemerkt lopende ODU-cyclus mag niet door een normale write worden verstoord. Andere ODU-serviceoperaties kunnen de normale polling tijdelijk pauzeren; daarom is dit als contracttest vastgelegd.
- Pas na 90 seconden zonder geldige mode-/defrostmeting volgt een veiligheidsstop via `telemetry_fault`. Transportverlies zelf gaat elders sneller fail-closed; deze 30/90 s-keuze vervangt dat niet.

## Interpretatie

Temperatuurparameters worden in de backend omgerekend met `raw - 30`; mode-4 deltadrempels met `30 - raw`. PDU-adressen zijn `Pxxx + 2999`. Op aanvraag worden de gemeenschappelijke blokken `3270..3280`, `3307..3315` en `3336..3341` gelezen. Het mode-4-blok `3414..3427` wordt alleen gelezen voor varianten die deze uitbreiding ondersteunen; op V1 wordt het niet als defrostconfiguratie geïnterpreteerd. Geen periodieke EEPROM-scan.

- Mode 0: geselecteerde verdampertemperatuurdrempel en geschatte lokale kwalificatietijd. Intern adaptief interval blijft onbekend, mede vanwege de onvoldoende vastgestelde P342-uitzondering.
- Mode 1: verdampertemperatuurdrempel; geen exacte aftelling voor de nog onduidelijke 45-/50-minutenlogica.
- Mode 2: gereserveerd; geen instelbare strategie.
- Mode 3: beperkte diagnostiek, geen reconstructie van interne trendhistorie.
- Mode 4: parameterafhankelijke `Ta - Tevap`-drempel en normale interval-/looptijdparameters, alleen op de varianten die deze methode implementeren. V1.5 en beide onderzochte V2-varianten hebben hiervoor een gevuld eigen parameterprofiel; de waarden verschillen per variant en worden daarom live uitgelezen. Dit omvat niet alle interne startpaden.

Lokale looptijden beginnen bij waarneming en kunnen na een herstart korter zijn dan de werkelijke ODU-looptijd. Tijdseenheden voor P340/P427/P428 zijn niet bewezen: de benodigde eindbevestiging blijft daarom onbekend. Een mogelijke maximale-duur-eindreden blijft een afleiding, nooit een bewezen oorzaak.

## V1 tegenover V1.5

Handmatig ontdooien en defrostmethode 4 zijn twee verschillende zaken. De handmatige serviceactie gebruikt werkmodus `3999=4`; die blijft ook voor V1 beschikbaar wanneer alle normale veiligheidsvoorwaarden kloppen. De methodekeuze hierboven schrijft daarentegen parameter `P276` / PDU `3275`. Juist bij die parameter verschilt de betekenis per firmwarevariant.

De V1.5 heeft aanvullende mode-4-parameters voor het verschil tussen buitentemperatuur en verdampingstemperatuur. Die uitbreiding ontbreekt in de onderzochte V1 1.25. Belangrijk: op de onderzochte V1.5 is dit mode-4-parameterblok **daadwerkelijk ingevuld**, ook al staat de actieve defrostmethode daar op mode 0. Mode 4 op V1.5 gebruikt dus het eigen V1.5-profiel; er hoeft en mag geen V2-profiel naar de V1.5 worden gekopieerd.

Ook beide onderzochte V2-varianten hebben een ingevuld mode-4-profiel en staan standaard op mode 4. De oude V2 (firmware 1.34 / board 0x0E37) en nieuwe V2 (firmware 2.1 / board 0x1037) gebruiken daarbij niet exact dezelfde Ta−Tevap-drempels. OpenQuatt leest daarom altijd de actuele parameters van de aangesloten buitenunit en hardcodeert geen generiek V2-profiel.

V1 heeft daarnaast niet de V1.5-opties voor afdruiptijd en ventilatorbijschakeling op hoge druk tijdens ontdooien; OpenQuatt presenteert die daarom niet als V1-capability.

## Nog te valideren op hardware

Voor vrijgave: normale en handmatige cyclus, vraag weg tijdens defrost, gemiste ACK, 210-seconden-timeout, communicatieverlies/herstel, reboot tijdens defrost, incidentstop, Duo-overlap en de tijdschalen. Na pompwijzigingen opnieuw vereist: CM0→forced-defrost, flow onder/boven de grens, demandverlies tijdens defrost, lost ACK, reconnect en Duo. Controleer daarbij interne heap, grootste vrije blok en stackmarges onder gelijktijdige web/API/HA/MQTT/Modbus-belasting. Een geslaagde compile is daarvoor geen vervanging.

Expertwijzigingen aan algoritme of drempels zijn conform fase 3 van issue #733 pas aan de orde na praktijkvalidatie. Temperatuurgrenzen, intervallen, maximale duur, compressorfrequenties en overige defrostparameters worden niet geschreven; de UI toont geen invoervelden hiervoor.
