# Instellingen en meetwaarden

Deze pagina is bedoeld als praktische naslag. Niet om alles tegelijk te tunen, maar om sneller te zien welke instellingen en meetwaarden er echt toe doen.

## Begin met dit onderscheid

OpenQuatt heeft grofweg twee soorten instellingen:

### Runtime-instellingen

Dit zijn de instellingen die je tijdens normaal gebruik kunt aanpassen via Home Assistant of de webinterface.

Hier begin je bijna altijd mee als je iets wilt afstellen.

### Compile-time instellingen

Dit zijn firmware-instellingen die pas veranderen na opnieuw compileren en flashen.

Hier kom je meestal pas aan als:

- je hardware anders is;
- je een ontwikkelaar bent;
- of een runtime-instelling het probleem duidelijk niet kan oplossen.

Voor gewone gebruikers geldt dus meestal: eerst runtime, bijna nooit compile-time.

### Firmware-updates

Na een herstart wacht OpenQuatt met de eerste automatische firmwarecontrole tot
de netwerkverbinding vijf minuten stabiel is en er geen OTA-update actief is.
Daarna controleert de firmware iedere vier uur opnieuw. Dit uitstel is normaal
opstartgedrag en geen teken dat de updatefunctie niet werkt.

`Check Firmware Updates` voert wel direct een controle uit. Een echte
runtimewijziging van het firmwarekanaal of updatedoel controleert eveneens
direct; alleen de tijdens het opstarten herstelde keuze volgt de wachttijd.

Voor flowdiagnose zijn er wel twee compile-time constanten die je soms in discussies of issues terugziet:

- `oq_flow_mismatch_threshold_lph`
- `oq_flow_mismatch_hyst_lph`

Die bepalen wanneer OpenQuatt een flowafwijking serieus genoeg vindt. Voor normale gebruikers zijn dit zelden de eerste knoppen om aan te raken.

## Welke runtime-instellingen zijn het belangrijkst?

### 1. Basisbediening en begrenzing

Deze groep bepaalt hoeveel ruimte OpenQuatt krijgt:

- `OpenQuatt Enabled`
- `Manual Cooling Enable`
- `Cooling Enable Source`
- `Silent Mode Override`
- `Day max frequency`
- `Silent max frequency`
- `Electrical current limit`
- `Silent start time`
- `Silent end time`
- `CM Override`

Gebruik deze groep vooral om gedrag te begrenzen of te verklaren, niet om fijn te tunen.

`Electrical current limit` begrenst als `Maximale gezamenlijke netstroom` het gezamenlijke elektrische ingangsvermogen van de buitenunits. Standaard blijft de bestaande grens actief: 16 A voor Single en voor Duo V1/V1.5, 20 A voor Duo V2. Een hogere waarde is alleen mogelijk tot de absolute OpenQuatt-bovengrens, afgeleid van de gepubliceerde maximale stroom per buitenunit (2 × 10 A voor V1/V1.5, dus 20 A; 2 × 13 A voor V2, dus 26 A), en alleen wanneer beide buitenunits betrouwbaar als die familie zijn gedetecteerd. De officiële Quatt Duo-specificatie (16 A respectievelijk 20 A) blijft de standaard; gebruik daarboven vereist een daarvoor geschikte volledige elektrische installatie. Zonder bevestigde detectie blijft de installatieafhankelijke standaard (16 of 20 A) het plafond. Een hogere waarde toont direct een waarschuwing en vraagt een expliciete bevestiging. `Standaardwaarde herstellen` zet de actuele standaardwaarde opnieuw in. Power House gebruikt de grens voorspellend én via gemeten feedback; stooklijn en koelen alleen via gemeten feedback. Het is een softwarematige regelgrens en geen vervanging voor zekeringen, aardlekbeveiliging of load balancing; korte overschrijdingen door meetvertraging zijn niet volledig uit te sluiten.

### 2. Verwarmingsstrategie

Deze groep bepaalt hoe OpenQuatt verwarmingsvraag opbouwt.

Belangrijke keuze:

- `Heating Control Mode`

Daarna hangt het ervan af welke strategie je gebruikt.

Voor `Power House` zijn vooral belangrijk:

- `House cold temp`
- `Rated maximum house power`
- `Maximum heating outdoor temperature`
- `Power House temperature reaction`
- `Power House comfort below setpoint`
- `Power House comfort above setpoint`
- `Power House demand rise time`
- `Power House demand fall time`

Voor `Water Temperature Control` zijn vooral belangrijk:

- `Curve Tsupply @ -20/-10/0/5/10/15°C`
- `Curve Fallback Tsupply (No Outside Temp)`
- `Heating Curve PID Kp/Ki/Kd`

Voor beide strategieën blijft belangrijk:

- `Maximum water temperature`

Voor koeling zijn vooral belangrijk:

- `Cooling Enable Source`
- `Cooling schedule start time`
- `Cooling schedule end time`
- `Cooling Minimum Supply Temp`
- `Cooling Demand Max`
- `Cooling Restart Mode`
- `Cooling Restart Delta`
- `Cooling Minimum Off Time`
- `Cooling Room Request Required`
- `Cooling Request On Delta`
- `Cooling Request Off Delta`
- `Cooling Safety Margin`

Met `Cooling Restart Mode` kies je tussen herstart op watertemperatuur en herstart na een minimale uit-tijd. In de eerste modus bepaalt `Cooling Restart Delta` hoeveel de aanvoer na een waterzijdige stop moet opwarmen. In de tweede modus bepaalt `Cooling Minimum Off Time` hoe lang een werkelijk gestopte koelcyclus uit blijft; bij Duo blokkeert die tijd beide warmtepompen. Los daarvan bewaakt OpenQuatt altijd de vaste minimale uit-tijd per compressor (4 minuten). Een compressor start dus pas wanneer zowel de gekozen koelherstartvoorwaarde als zijn eigen minimale uit-tijd is vrijgegeven. De normale dauwpunt-, flow- en veiligheidsgrenzen blijven in beide modi actief.

Kies `Schedule` bij `Cooling Enable Source` om de koeltoestemming lokaal tot een dagelijks venster te beperken. Het begin is inbegrepen en het einde niet (`[start,end)`). Een begintijd die later is dan de eindtijd loopt over middernacht; gelijke tijden schakelen het venster uit. De standaard is daarom veilig `00:00-00:00`.

Het schema is een toestemmingsbron, geen aparte koelstrategie of veiligheidsoverride. `Cooling Room Request Required` staat standaard aan, zodat binnen het venster ook een geldige kamerkoelvraag nodig blijft. Alleen wanneer je die instelling bewust uitzet, wordt het actieve venster zelf de vraag. Dauwpunt-, minimale aanvoer-, flow- en overige beveiligingen blijven altijd gelden.

De controller beoordeelt het schema met zijn lokale, via SNTP gesynchroniseerde klok. Na een offline herstart is de bron ongeldig en blijft koeltoestemming via `Schedule` uit totdat de tijd geldig is; na synchronisatie loopt de lokale klok op de controller door. Bij het sluiten van het venster gebruikt OpenQuatt de normale gecontroleerde overgang. Een nog geldige minimale compressortijd kan de compressor kort voorbij de eindtijd laten lopen, tenzij een harde veiligheidsingreep direct stoppen vereist; daarna kan de pomp nog de normale postflow uitvoeren.

`Manual Cooling Enable` omzeilt de gekozen toestemmingsbron, ook `Schedule`, maar geen enkele koelbeveiliging en ook `OpenQuatt Enabled` niet. De stand wordt met `RESTORE_DEFAULT_OFF` opgeslagen: na een herstart keert een eerder opgeslagen ingeschakelde stand terug; uit is alleen de standaard wanneer nog geen stand is opgeslagen.

### 3. Duo en looptijdgedrag

Deze groep speelt vooral mee als je twee warmtepompen gebruikt of onrustig compressorgedrag ziet.

Belangrijke instellingen:

- `Minimum runtime`
- `Dual HP Enable Level`
- `Dual HP Enable Hold`
- `Dual HP Disable Hold`

Raak deze groep pas aan nadat de strategie zelf logisch voelt.

### 4. Flow en pomp

Deze groep bepaalt hoe de circulatiepomp wordt aangestuurd.

Belangrijke instellingen:

- `Flow Setpoint`
- `Cooling Flow Setpoint`
- `Flow Control Mode`
- `Manual iPWM`
- `Flow PI Kp`
- `Flow PI Ki`

`Flow Setpoint` geldt voor verwarmen en normaal automatisch bedrijf. `Cooling Flow Setpoint` geldt alleen tijdens koelen, zodat koeling een eigen hydraulisch werkpunt kan hebben zonder de verwarmingsflow te veranderen.

CM98 gebruikt een vaste pompregeling van iPWM 800. AUTO start met de laatst bekende goede iPWM voor verwarmen of koelen en valt bij een ongeldige waarde terug op iPWM 440.

Gebruik deze groep voorzichtig. Bij verkeerde bronwaarden of hydraulische problemen maak je hier snel meer ruis dan winst.

### 5. Bronselectie

Deze groep is vaak belangrijker dan gebruikers denken.

Belangrijke keuzes:

- `Water Supply Source`
- `Flow Source`
- `Outside Temperature Source`
- `Room Temperature Source`
- `Room Setpoint Source`
- `Heating Enable Source`
- `Cooling Enable Source`
- `Cooling Dew Point Source`
- `External Heat Demand Source`

En indirect alles wat bepaalt waar buiten-, kamer- en waterwaarden vandaan komen.

#### Strategie-afhankelijke aanbevelingen

De betekenis van dezelfde bron verschilt per verwarmingsstrategie:

| Instelling | Power House | Water Temperature Control |
|---|---|---|
| Kamertemperatuur | Vereist / sterk aanbevolen | Aanbevolen (comfortcorrectie) |
| Kamer-setpoint | Vereist / sterk aanbevolen | Aanbevolen |
| Buitentemperatuur | Vereist | Vereist (fallback 40 °C) |
| Aanvoertemperatuur | Nodig voor begrenzing | Vereist als PID-proceswaarde |
| Flow | Vereist | Vereist |
| Warmtetoestemming (`Heating Enable Source`) | Meestal `Niet gebruiken` | Meestal externe thermostaat/zonevraag |
| Externe warmtevraag | Optioneel (`HA`/`API`) | Niet van toepassing |

Tijdens Quick Start vervangt een strategieswitch de warmtetoestemming automatisch: `Heating Enable Source = Niet gebruiken` bij `Power House` (OpenQuatt bepaalt zelf de vraag), of de eerder gekozen, gekoppelde en actieve thermostaatbron bij `Water Temperature Control` (`OT thermostat` op Q-edition, anders `CIC`/`HA input`). Een uitgeschakelde of niet-geconfigureerde bron wordt niet automatisch als harde gate gekozen. Buiten Quick Start overschrijft de web-app een bestaande keuze niet stil; daar verschijnt alleen een advies met een knop om het over te nemen. Afwijkende combinaties blijven bewust mogelijk (bijv. Power House met zone-gate, stooklijn volledig weersafhankelijk).

Voor `Outside Temperature Source` is `Auto` meestal de verstandigste keuze. OpenQuatt kiest dan zelf een geldige bron (normaliter de buitenunit) en blijft minder gevoelig voor een buitenmeting die tijdelijk niet betrouwbaar is.

Kies je expliciet `MQTT`, houd er dan rekening mee dat de MQTT-buitentemperatuur na een (her)start pas geldig is zodra OpenQuatt een nieuwe live publicatie ontvangt. Tot die tijd kan de regeling naar `CM98` (antivriescirculatie) gaan. De wachttijd hangt af van het publicatie-interval van de zender.

Voor `Heating Enable Source` betekent `Niet gebruiken` / `Disabled`: geen externe warmtetoestemming gebruiken; de actieve verwarmingsstrategie mag zelf warmtevraag opbouwen. Dit staat dus niet voor verwarming uitschakelen. Bij `Power House` is dit meestal gewenst; bij `Water Temperature Control` met kamerthermostaat is meestal de gekoppelde thermostaatbron gewenst (`OT thermostat`, `CIC` of `HA input`). Zie [Power House](power-house.md) en [Water Temperature Control](water-temperature-control.md).

Voor `Cooling Dew Point Source` is `Auto` meestal ook de veiligste keuze. OpenQuatt gebruikt dan de hoogste geldige dauwpuntwaarde van Home Assistant, API-invoer en MQTT. Kies `Home Assistant`, `API input` of `MQTT` alleen als je die bron expliciet wilt vereisen.

Voor `External Heat Demand Source` is `Disabled` de standaard, en voor de meeste installaties ook de juiste keuze. Kies je `HA input` of `API input`, dan neemt een externe voorspelling de vermogensschatting van het huismodel in `Power House` over. De rest van de regeling blijft ongewijzigd, en bij een wegvallende of verouderde bron valt `Power House` terug op het eigen huismodel. Zie [Power House](power-house.md).

De temperatuurkalibratie neemt ook de actieve aanvoertemperatuurbron mee. OpenQuatt bewaart daarvoor vier afzonderlijke offsets: voor de lokale PT1000, lokale DS18B20, CIC-feed en Home Assistant-invoer. Bij een bronwissel activeert OpenQuatt automatisch de eerder opgeslagen correctie voor die bron. De CIC-correctie blijft geldig na een gewijzigde feed-URL; na een andere Home Assistant-entiteit blijft die correctie uitgeschakeld totdat je de HA-invoer opnieuw kalibreert. Een tijdelijke automatische fallback naar de water-uitmeting van de warmtepomp gebruikt geen aanvoercorrectie en wist geen opgeslagen kalibratie.

De instellingenbackup bevat de vier warmtepompoffsets en iedere geldige brongebonden aanvoercorrectie. Voor Home Assistant bewaart de backup ook een anonieme bronfingerprint, zodat een offset niet aan een andere HA-entiteit wordt gekoppeld; de firmware reconstrueert de checksum zelf. Kalibreer opnieuw wanneer de controller of temperatuursensor fysiek is vervangen of wanneer je een andere Home Assistant-invoer gebruikt.

### 6. Hulprelais R2 (alleen Heatpump Controller Q-edition)

Deze groep bestaat alleen op hardware met een tweede relais (R2) en staat standaard uit.

Belangrijke instellingen:

- `Aux Relay Function`
- `Aux Relay Wait For Supply Temp`
- `Aux Relay Heating Start Temp`
- `Aux Relay Cooling Start Temp`
- `Aux Relay Temp Hysteresis`

Met `Aux Relay Function` volgt R2 de effectieve warmte- of koelvraag van OpenQuatt, bijvoorbeeld om een fancoil, pomp of klep mee te schakelen. Met de schakelaar `Aux Relay Wait For Supply Temp` wacht R2 daarnaast tot het aanvoerwater warm of koud genoeg is; de hysterese voorkomt snel aan/uit schakelen rond de grens. Kies `Externe bediening` om R2 zelf te schakelen via bijvoorbeeld Home Assistant of de REST-API; in de automatische functies worden externe schakelcommando's genegeerd. Zonder vraag, met de functie op `Niet gebruiken`, of zonder geldige aanvoertemperatuur (als de schakelaar aan staat) blijft R2 uit. De actuele toestand en reden zie je onder **Instellingen → Installatie → Hulprelais (R2)**.

## Welke meetwaarden wil je meestal zien?

### Voor comfort en strategie

Begin bijna altijd met:

- `Room Temperature (Selected)`
- `Room Setpoint (Selected)`
- `Outside Temperature (Selected)`
- `Heating Control Mode`

### Voor aanvoer en begrenzing

Controleer daarna:

- `Water Supply Temp (Selected)`
- `Water Supply Temperature Calibration Status`
- `Maximum water temperature`
- `Heating Curve Supply Target` als je stooklijn gebruikt

### Voor flow en systeemgedrag

Kijk bij twijfel ook naar:

- `flow_rate_selected`
- actieve `Control Mode`
- pompstand of flowregeling

### Voor energie en belasting

Alleen als het probleem daar lijkt te zitten:

- opgenomen vermogen;
- power cap;
- gedrag rond stille uren of begrenzing.

### Voor compressorpendelen

De diagnostische pendelwaarschuwingen zijn in Home Assistant standaard
uitgeschakeld. Schakel `Compressor cycling warning` in om één samengesteld
signaal te krijgen zodra minimaal één actuele pendelwaarschuwing actief is. De
oorzaak blijft na inschakelen van de bijbehorende detailentiteiten afzonderlijk
zichtbaar via:

- `Compressor cycling warning 2h`;
- `Compressor cycling warning 72h`;
- `Alternating compressor starts warning` bij een duo-installatie.

De signalen kunnen gelijktijdig actief zijn en worden weer inactief zodra de
bijbehorende actuele conditie is hersteld. Een eerder gedetecteerde maar alleen
nog gelatchte melding houdt `Compressor cycling warning` niet actief.

## Wanneer zit je waarschijnlijk in de verkeerde laag?

Gebruik deze vuistregels:

- kamercomfort niet goed -> kijk eerst naar strategie en bronkeuze;
- aanvoer volgt doel niet -> kijk eerst naar stooklijn of PID;
- systeem blijft hangen of schakelt vreemd -> kijk eerst naar `Control Mode`, flow en begrenzingen;
- probleem blijft terugkomen ondanks goede runtime-afstelling -> pas dan aan compile-time denken.

## Wat hoef je meestal niet meteen te veranderen?

Voor de meeste gebruikers zijn dit geen eerste knoppen:

- compile-time constanten;
- flow autotune-details;
- lage-level optimizer- of guardrailinstellingen;
- meerdere groepen instellingen tegelijk.

Als je te veel tegelijk wijzigt, wordt het juist moeilijker om te begrijpen wat er verandert.

## Veilige manier van aanpassen

1. Controleer eerst of de gebruikte meetwaarden logisch zijn.
2. Verander één groep tegelijk.
3. Beoordeel het effect over tijd en bij verschillend weer.
4. Pas pas daarna een tweede groep aan.
5. Ga alleen naar compile-time als runtime echt niet genoeg blijkt.

## Verder lezen

- Web-app gebruiken: [Web-app gebruiken](web-app.md)
- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Power House](power-house.md)
- [Water Temperature Control](water-temperature-control.md)
- [Regelgedrag van OpenQuatt](regelgedrag-van-openquatt.md)
- [Problemen oplossen](problemen-oplossen.md)
