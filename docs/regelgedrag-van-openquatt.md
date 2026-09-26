# Regelgedrag van OpenQuatt

Deze pagina helpt je begrijpen waarom OpenQuatt tijdens gebruik soms anders reageert dan je op het eerste gezicht verwacht.

Het gaat hier niet om afstellen, maar om het herkennen van normaal systeemgedrag.

## Het hoofdidee

OpenQuatt beslist niet op basis van één knop of één meetwaarde. Het gedrag ontstaat uit drie lagen tegelijk:

1. `Control Mode`
2. `Heating Strategy`
3. `Flow Mode`

Daardoor kun je vreemd gedrag bijna nooit verklaren vanuit alleen een pompstand of alleen een verwarmingsstrategie.

## Wat betekenen de Control Modes?

| Mode | In gewone taal |
|---|---|
| `CM0` | rust of standby |
| `CM1` | korte tussenstap voor of na verwarmen |
| `CM2` | normaal verwarmen met warmtepomp |
| `CM3` | warmtepomp met ketelhulp |
| `CM4` | alleen de CV-ketel na een bevestigde warmtepompstoring |
| `CM98` | vorstbeveiliging |
| `CM100` | commissioning / servicedoel |

Voor gebruikers zijn vooral deze twee dingen belangrijk:

- OpenQuatt springt niet altijd direct van uit naar vol verwarmen;
- tussenstanden zijn vaak normaal beveiligd gedrag, geen storing.

## Waarom gaat het systeem niet altijd meteen naar verwarmen?

Dat gebeurt meestal om één van deze redenen:

- flow is nog niet logisch of nog te laag;
- de vraag is nog niet stabiel genoeg;
- een beveiliging of wachttijd is actief;
- er is een overgang bezig tussen systeemstanden.

Daardoor kan het soms voelen alsof OpenQuatt aarzelend reageert, terwijl het juist bewust voorkomt dat het systeem te snel schakelt.

## Wanneer komt de ketel erbij?

`CM3` betekent niet automatisch dat er iets mis is.

In gewone taal:

- OpenQuatt ziet dat de warmtepomp het tekort niet goed genoeg opvangt;
- dat tekort moet meestal duidelijk genoeg en lang genoeg aanwezig zijn;
- pas dan mag ketelhulp actief worden.

De ketel springt dus normaal niet op elk kort dipje direct bij.

## Hoe stuurt OpenQuatt een R1-ketel aan?

Bij een OpenTherm-ketel stuurt OpenQuatt de gevraagde keteltemperatuur mee en
moduleert de ketel zelf. Een R1-ketel kan alleen aan of uit. Daarom regelt
OpenQuatt die schakelaar alsnog rond de temperatuur die het systeem al heeft
berekend:

- het relais gaat aan zodra de gemeten aanvoertemperatuur meer dan 2,0 K onder
  het gevraagde doel ligt;
- het relais gaat weer uit zodra de aanvoer binnen 0,5 K van dat doel is
  teruggekomen;
- tussen die twee grenzen blijft de vorige stand staan, zodat het relais niet om
  elke graad schakelt.

Een bereikt doel is een gewone regelstop en volgt daarmee dezelfde
anti-cyclingregel als het einde van een warmtevraag: een ingestelde minimale
aantijd houdt het relais dan nog even aan. Alleen een beveiliging of het
verlies van de eigenaar schakelt meteen uit. De volgorde is dus: beveiliging,
eigenaar, anti-cycling, temperatuurregeling.

Dat is dezelfde opdracht die OpenTherm krijgt, alleen uitgevoerd via een
binaire uitgang. De marge is bewust asymmetrisch: een binaire brander mag wat
verder onder het doel wegzakken voordat hij opnieuw wordt ingeschakeld, en wordt
weer gestopt zodra de temperatuur is teruggekomen.

Alle bestaande beveiligingen blijven leidend. Bij te weinig flow, een te hoge
of harde watertemperatuur, een ontbrekende hulpbron of een ongeldige aanvraag
gaat het relais meteen uit, ook als het doel anders binnen bereik zou liggen.

Een bereikt doel is geen storing. De aanvraag blijft zichtbaar en de ketelvraag
blijft eigendom van de actieve stand, terwijl het relais uit blijft. In de
diagnosegegevens zie je dat als "requested boiler target temperature satisfied",
naast de doelregelstand van de R1-regeling. Zo is een bereikt doel te
onderscheiden van een ketel die door een beveiliging wordt tegengehouden.

`CM4` heeft een andere betekenis. Deze stand is alleen voor foutfallback.
OpenQuatt gaat pas naar `CM4` wanneer:

- geen warmtepomp meer veilig beschikbaar is;
- de fout of verbindingsuitval voldoende lang en meermaals is bevestigd;
- een bereikbare warmtepomp aantoonbaar is gestopt; bij bevestigd langdurig
  verbindingsverlies geldt na de stopwachttijd de hieronder beschreven
  fallbackroute;
- de normale flow-, temperatuur- en ketelbeveiligingen akkoord zijn;
- foutfallback door de gebruiker is ingeschakeld.

Die laatste opt-in staat los van de schakelaar voor normale `CM3`-ketelondersteuning.

Is de warmtepomp langdurig volledig onbereikbaar, dan kan de stop niet meer
via metingen worden bevestigd. Als die stoploosheid pas door dat
verbindingsverlies zelf is ontstaan, mag de ketel na de veilige stopwachttijd
alsnog als fallback draaien, mits alle overige beveiligingen hierboven
akkoord zijn. Zodra verse metingen weer een draaiende compressor tonen,
vervalt die toestemming direct. Een bereikbare warmtepomp die niet
aantoonbaar stopt, blijft de ketelfallback blokkeren — ook als de verbinding
daarna wegvalt.

Een kort wegvallend communicatiebericht leidt daarom niet direct tot stoppen of
ketelfallback. Bij een Duo-installatie blijft een gezonde warmtepomp eerst
beschikbaar; `CM4` is pas toegestaan als geen van beide warmtepompen veilig kan
verwarmen.

De overgang tussen `CM3` en `CM4` verandert alleen de rol van de al actieve
CV-ketel. Zolang dezelfde beveiligingen geldig blijven, schakelt OpenQuatt de
keteluitgang daarbij niet kort uit en weer aan.

`CM100` is iets anders: dat is een expliciete service- of commissioningstand voor metingen zoals het boilervermogentestje of flow-autotune. Daar hoort geen normale warmtevraaglogica bij.

## Waarom blijft het systeem soms juist te lang in een tussenstand?

Dat heeft vaak te maken met flow, bronkeuze of beveiliging.

Praktische oorzaken zijn vaak:

- onlogische of onstabiele flowmeting;
- verkeerde geselecteerde bron;
- minimum looptijd of wachttijd;
- begrenzing op totaal vermogen of aanvoertemperatuur.

Als je dat niet eerst controleert, lijkt het al snel alsof de strategie zelf fout zit.

## Welke rol speelt de verwarmingsstrategie?

De gekozen strategie bepaalt hoe warmtevraag wordt opgebouwd.

### Bij Power House

Dan kijkt OpenQuatt vooral naar:

- buitentemperatuur;
- kamerafwijking;
- geschatte warmtevraag van het huis.

Lees verder in [Power House](power-house.md).

### Bij Water Temperature Control

Dan kijkt OpenQuatt vooral naar:

- buitentemperatuur;
- stooklijn;
- gewenste en gemeten aanvoertemperatuur.

Lees verder in [Water Temperature Control](water-temperature-control.md).

## Welke rol speelt Flow Mode?

`Flow Mode` bepaalt hoe de pompregeling werkt, maar vervangt de rest van het systeem niet.

Belangrijk om te onthouden:

- een handmatige pompstand zet veiligheidslogica niet uit;
- `Flow Mode` verklaart dus niet alleen waarom het systeem wel of niet verwarmt;
- kijk altijd samen naar `Control Mode`, strategie en flow.

## Bronkeuze is vaak de echte oorzaak

Veel klachten blijken uiteindelijk geen regelprobleem, maar een bronprobleem.

Controleer daarom altijd eerst:

- `flow_rate_selected`
- `outside_temp_selected`
- `water_supply_temp_selected`
- `Room Temperature (Selected)`
- `Room Setpoint (Selected)`

Als deze bronnen niet kloppen, gaat OpenQuatt logisch reageren op verkeerde informatie.

## Snelle controle bij twijfel

1. Kijk welke `Control Mode` actief is.
2. Kijk welke `Heating Control Mode` actief is.
3. Controleer of de geselecteerde bronwaarden logisch zijn.
4. Kijk of flow en aanvoer zich normaal gedragen.
5. Pas daarna pas instellingen of strategie aan.

## Veelvoorkomende misverstanden

- Een tussenstand betekent niet automatisch een fout.
- `CM3` betekent niet automatisch dat de warmtepomp faalt.
- `CM4` is ketel-only foutfallback en geen normale ketelondersteuning.
- Een andere verwarmingsstrategie verandert niet alle beveiligingen eromheen.
- Een handmatige pompstand verklaart niet het hele systeemgedrag.

## Verder lezen

- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Power House](power-house.md)
- [Water Temperature Control](water-temperature-control.md)
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)
- [Problemen oplossen](problemen-oplossen.md)
