# Beslislog: contract en vragenmatrix

Dit document trekt het concept achter de `Beslislog` scherper. Het doel is niet om een
firmwarelog te tonen, maar om gebruikers en support te laten begrijpen:

- wat het systeem zag;
- welke keuze de controller maakte;
- waarom die keuze logisch was;
- wat het systeem daarna doet;
- of actie nodig is.

## Status en broncode

Dit is het functionele contract tussen de firmware-eventbuffer en de web-app. De
huidige eventnamen en serialisatie staan in
`components/openquatt_decision_log/`; de gebruikersweergave staat in
`openquatt/web/js/src/features/control-replay-*.js`. Controleer beide bij een
contractwijziging. De sectie met aanbevolen uitbreidingen is nadrukkelijk geen
toegezegde roadmap.

De controller blijft eigenaar van de control mode. De UI gebruikt `CM0`, `CM1`, `CM2`,
`CM3`, `CM4`, `CM5`, `CM98` en `CM100` alleen als context of kleine supportbadge.

## Kernprincipe

Log betekenisvolle systeemkeuzes, niet elke interne modewissel.

Voorbeelden:

- Wel tonen: `Warmtepomp gestart`, `Twee warmtepompen actief`, `Koeling begrensd`,
  `Vorstbescherming actief`, `CV ondersteunt`, `Start uitgesteld`.
- Niet als hoofdregel tonen: `CM2 actief`, `CM5 actief`, `CM98 actief`.
- Wel in details tonen: `Control mode: CM98`.

Alle tekst voor gebruikers hoort in de web-UI. Firmware levert codes, compacte waarden
en timing.

## Datamodel v1

De huidige firmware-aanpak is een eventbuffer. Dat is goed voor v1. De UI mag daaruit
periodes en samenvattingen afleiden.

```text
seq              uint32
uptime_s         uint32
epoch_s          derived when time is valid
event_type       uint8 enum
subject          uint8 enum
reason_code      uint8 enum
severity         uint8 enum
control_mode     uint8
from_state       uint8 enum
to_state         uint8 enum
value_a          int16 event-specific
value_b          int16 event-specific
threshold_a      int16 event-specific
duration_s       uint16 optional
flags            uint8 bitfield, optional
```

Belangrijk:

- `event_type` zegt wat gebeurde.
- `reason_code` zegt waarom dit de primaire reden was.
- `control_mode` is context.
- `duration_s` hoort op clear/stop-events wanneer de firmware de starttijd kent.
- UI groepeert herhaling en maakt `STATE_SPAN`-achtige weergave uit start/stop-paren.

## Event-types

### Historische context van verwarmstops

Nieuwe automatische verwarmstops gebruiken append-only reason-codes 75–90 uit
`components/openquatt_decision_log/OpenQuattHeatingStop.h`. Ze onderscheiden
Power House-vraag nul, laaglast, comfortstop en vastgehouden comfortstop;
stooklijn-waterstop, stooklijn-laaglast en duo→single; ongeldige regelinputs,
elektrische begrenzing, geen toegestane kandidaat en centrale stopbeveiligingen.
Een centrale pauze zonder vastgelegde specifieke oorzaak blijft generiek; bestaande
override-, vorst- en vraag-beëindigd-codes behouden hun wirewaarden.
Een optimalisatiekeuze betekent niet noodzakelijk dat de warmtevraag is gedaald.

Een `source_stop` met `FLAG_RAW_VALUE_VALID` krijgt direct ervoor drie
`heating_stop_context`-records, met dezelfde bron, reden en mode. De bestaande
24-byte flashrecords, versie en ringbuffer blijven ongewijzigd. `source_stop`
houdt `value_a` als vorige compressorstand. De context gebruikt:

| `flags` | `value_a` | `value_b` | `threshold_a` |
| --- | --- | --- | --- |
| 1: vermogen | Basisregelvraag W | Effectieve regelvraag W | Laatst gemeten HP-warmtevermogen vóór stop W |
| 2: kamer | Kamer bij beslissing °C × 100 | Herstartgrens °C × 100 | Stopvoorwaarde °C × 100 |
| 3: water | Aanvoer bij beslissing °C × 100 | Waterherstartgrens °C × 100 | Waterstopgrens °C × 100 |

Voor `ph_low_load` bevat record 3 de laaglast-startgrens en stopgrens in W;
`value_a` is onbekend. `duration_s` op alle contextrecords is de leeftijd van de
oorspronkelijke beslissing bij het uitsturen van de stopopdracht, verzadigd op 65535 s.
`INT16_MIN` betekent onbekend; overige waarden verzadigen op ±32767 en worden in
de UI als grenswaarden weergegeven. Een basisregelvraag omvat reeds de geselecteerde
huis-/externe vraag en kamercorrectie; hij is niet de onbewerkte HouseLine.

De eerste stopbeslissing blijft behouden tijdens uitgestelde stops en wordt
ingetrokken zodra de strategie opnieuw warmte vraagt. Latere centrale beveiligingen
gaan voor en krijgen hun eigen context. Koelstops en handmatige bediening behouden
hun bestaande contract. De UI koppelt uitsluitend drie opeenvolgende contextrecords
met passende sequence, bron, reden, mode en leeftijd. Bij afgebroken historie of
tussenliggende events blijven de waarden onbekend. Contextrecords zijn niet zichtbaar
als losse beslissingen en verhogen geen stop-/starttellers. Herstartvoorwaarden zijn
uitleg bij een stop en genereren geen extra stoprecords.

Een verwarmstop gebruikt hierdoor vier archiefrecords in plaats van één. De vaste
capaciteit van 5120 records blijft gelden; bij veel gebeurtenissen kan de effectieve
historieduur korter worden dan de maximale zeven dagen. Er zijn twee vaste
strategiesnapshots van maximaal 24 bytes, zonder nieuwe heapallocaties.

### Huidige eventset

| Event type | Subject | Doel in UI | Bron |
| --- | --- | --- | --- |
| `source_start` | `HP1` / `HP2` | Warmtepomp gestart | thermal actuator |
| `source_stop` | `HP1` / `HP2` | Warmtepomp gestopt | thermal actuator |
| `topology_change` | `HP1` / `HP2` / `BOTH` | Een of twee warmtepompen actief | thermal actuator |
| `decision_hold` | `HP1` / `HP2` / `SYSTEM` | Start/wissel tijdelijk uitgesteld | thermal actuator |
| `decision_blocked` | `CV` / `SYSTEM` | Actie niet toegestaan | boiler/control guard |
| `defrost_seen_start` | `HP1` / `HP2` | Ontdooien gestart | thermal actuator |
| `defrost_seen_clear` | `HP1` / `HP2` | Ontdooien klaar | thermal actuator |
| `cooling_limited` | `COOLING` | Koeling begrensd of gestopt | cooling strategy |
| `cooling_released` | `COOLING` | Koeling weer vrijgegeven | cooling strategy |
| `sticky_pump_run` | `PUMP` | Pompbescherming uitgevoerd | supervisory |
| `boiler_assist_start` | `CV` | CV ondersteunt tijdelijk | boiler control |
| `boiler_assist_stop` | `CV` | CV-ondersteuning gestopt | boiler control |
| `attention_pattern` | `SYSTEM` / bron | Patroon vraagt aandacht | decision log |
| `frost_protection_start` | `SYSTEM` | Vorstbescherming actief | supervisory |
| `frost_protection_clear` | `SYSTEM` | Vorstbescherming gestopt | supervisory |
| `candidate_blocked` | `HP1` / `HP2` | Startaanvraag tijdelijk geblokkeerd | thermal actuator |
| `flow_hold_start` | `SYSTEM` | Voorloop, naloop of flow-hold gestart | supervisory |
| `flow_hold_clear` | `SYSTEM` | Voorloop, naloop of flow-hold klaar | supervisory |
| `incident_start` / `incident_clear` / `incident_acknowledged` | `HP1` / `HP2` | Levenscyclus van een HP-incident | incident manager |
| `hp_availability_change` | `HP1` / `HP2` | Beschikbaar, verdacht, offline, gestoord of herstellend | incident manager |
| `hp_start_confirmed` / `hp_stop_confirmed` | `HP1` / `HP2` | Fysieke terugmelding na een commando | incident manager |
| `control_mode_change` | `SYSTEM` | Betekenisvolle modeovergang, waaronder CM4 | supervisory |
| `boiler_fallback_start` / `boiler_fallback_stop` | `CV` | Ketel-only foutfallback gestart of gestopt | supervisory / boiler control |

Voor `boiler_fallback_start` en `boiler_fallback_stop` bevat `cm` de
actuele control mode, `value_a` de aanvoertemperatuur × 10 en `value_b`
of de keteluitvoer tijdens de rolwisseling fysiek continu bleef. Alleen
`control_mode_change` gebruikt `value_a` en `value_b` als vorige en
volgende control mode.

### V2: aanbevolen uitbreidingen

| Event type | Waarom toevoegen | Standaard zichtbaar? |
| --- | --- | --- |
| `runtime_balance_choice` | Uitleg waarom HP1 of HP2 gekozen is | Alleen bij startdetail |
| `cooling_request_start` / `cooling_request_stop` | Koelvraag los van HP-aan/uit begrijpen | Nee, context/grafiek |
| `service_mode_start` / `service_mode_stop` | CM100 supportcontext | Alleen support/service |
| `sensor_fallback_start` / `sensor_fallback_clear` | Uitleg bij fallbackwaarden | Ja bij limited/attention |

Mijn advies: voeg de resterende v2-events niet in een keer toe. De eerste slice is
`candidate_blocked` en `flow_hold_start/clear`, omdat die de meeste "waarom gebeurt
er niets?" vragen beantwoorden.

## Reason-codes

### Huidige codes

| Reason code | Betekenis voor gebruiker | Severity default |
| --- | --- | --- |
| `keep_current` | Huidige keuze blijft logisch | `normal` |
| `hold_active` | Wissel bewust uitgesteld | `limited` of `normal` |
| `defrost_hold` | Ontdooien rustig laten verlopen | `normal`/`limited` |
| `better_heat` | Twee pompen of extra vermogen past beter | `normal` |
| `soft_guard` | Veilige marge bewaakt | `limited` |
| `less_power` | Minder vermogen nodig | `normal` |
| `no_candidate` | Geen veilige beschikbare kandidaat | `limited` |
| `defrost_boost` | Andere bron helpt tijdens defrost | `normal` |
| `runtime_lead` | Draaiurenbalans kiest bron | `normal` |
| `single_topology` | Installatie heeft maar een bron | `normal` |
| `oil_return_hold` | Compressorherstel beschermen | `limited` |
| `min_rest_active` | Minimum rusttijd loopt nog | `limited` |
| `start_stop_rate_high` | Veel starts/stops gezien | `attention` |
| `sticky_protection` | Pompbescherming na stilstand | `normal` |
| `boiler_assist` | CV helpt of is geblokkeerd | `normal`/`limited` |
| `dew_stop` | Koeling stopt door dauwpuntmarge | `limited` |
| `cooling_limiter` | Koeling tijdelijk zachter | `limited` |
| `sensor_fallback` | Sensorwaarde fallback gebruikt | `attention` |
| `frost_protection` | Vorstbescherming door circulatie | `limited` |
| `flow_preflow` | Waterflow opbouwen voor start | `limited` |
| `flow_postflow` | Pomp draait kort na voor stop/standby | `normal` |
| `flow_too_low` | Waterflow te laag voor compressorstart | `limited` |
| `candidate_in_rest` | Kandidaat wacht op minimum rusttijd | `limited` |
| `candidate_in_defrost` | Kandidaat is bezig met defrost | `limited` |
| `candidate_unavailable` | Kandidaat technisch niet beschikbaar | `limited` |
| `hp_fault` | Bevestigde HP-storing | `fault` |
| `hp_protection` | HP-beveiliging begrenst of blokkeert | `limited` / `attention` |
| `hp_preheat` | HP wacht op voorverwarming | `limited` |
| `hp_link_loss` | HP-verbinding is na debounce verloren | `fault` |
| `hp_start_failed` | Start is niet op tijd bevestigd | `fault` |
| `hp_stop_unconfirmed` | Stop is nog niet veilig bevestigd | `fault` |
| `hp_recovery_wait` | Telemetrie of foutstatus moet stabiel herstellen | `limited` |
| `hp_recovered` | Warmtepomp is weer stabiel inzetbaar | `normal` |
| `boiler_fallback` | CV-ketel verwarmt tijdelijk zonder HP | `attention` |
| `fallback_blocked` | CM4 is gevraagd maar een veiligheidsvoorwaarde blokkeert | `fault` |

### Aanbevolen extra codes

| Reason code | Nodig voor | Opmerking |
| --- | --- | --- |
| `cooling_room_cap` | Koeling zachter door kamer/setpoint | Alleen toevoegen met duidelijke supportweergave |
| `cooling_restart_wait` | Koeling wacht na stop | Voorkomt aan/uit-fladderen |
| `service_override` | CM100 of handmatige test | Support/service |

## Reason-prioriteit

Een event krijgt precies een primaire `reason_code`. Extra context mag via values,
flags of supportdetails mee. Dit voorkomt dat de UI drie halve redenen tegelijk noemt.

Aanbevolen prioriteit:

1. Veiligheid en bescherming  
   `dew_stop`, `frost_protection`, `flow_too_low`, `defrost_hold`,
   `oil_return_hold`, `sensor_fallback`, `hp_fault`, `hp_stop_unconfirmed`,
   `fallback_blocked`

2. Beschikbaarheid van kandidaat  
   `candidate_in_rest`, `candidate_in_defrost`, `candidate_unavailable`,
   `single_topology`, `no_candidate`, `hp_link_loss`, `hp_start_failed`,
   `hp_recovery_wait`

3. Vraag en capaciteit  
   `better_heat`, `less_power`, `boiler_assist`, `cooling_limiter`,
   `cooling_room_cap`

4. Rust en anti-fladderen  
   `hold_active`, `min_rest_active`, `cooling_restart_wait`

5. Balans en voorkeur  
   `runtime_lead`, `keep_current`

Voorbeeld: als HP2 niet start omdat HP2 nog rusttijd heeft en de warmtevraag hoog is,
is de primaire reden `candidate_in_rest` of voorlopig `min_rest_active`, niet
`better_heat`.

## Vragenmatrix

| Gebruikersvraag | Firmware event | Primaire reason | UI-antwoord |
| --- | --- | --- | --- |
| Waarom draait er een warmtepomp? | `source_start` | `runtime_lead` / `keep_current` | "De vraag past bij een warmtepomp. De regelaar koos de beschikbare bron die het beste past bij draaiuren en wachttijd." |
| Waarom juist HP1 of HP2? | `source_start` | `runtime_lead` | "HP1 en HP2 zijn gelijkwaardig. Deze bron had op dit moment de gunstigste draaiurenbalans en was vrij." |
| Waarom draaien beide warmtepompen? | `topology_change` to duo | `better_heat` | "De vraag bleef hoog. Twee warmtepompen leveren rustiger vermogen dan een pomp op hoge belasting." |
| Waarom stopt een warmtepomp? | `source_stop` / `topology_change` to single | `less_power` | "De gekozen broncombinatie gebruikt minder elektriciteit bij voldoende warmteproductie." |
| Waarom start de andere pomp nog niet? | `candidate_blocked` | `candidate_in_rest` / `candidate_unavailable` / `no_candidate` | "Er is vraag, maar starten is nog niet verstandig door rusttijd of bescherming." |
| Waarom blijft dezelfde pomp draaien? | `decision_hold` | `hold_active` | "Wisselen zou nu weinig voordeel geven en kan extra starts/stops veroorzaken." |
| Waarom ontdooit hij? | `defrost_seen_start` | `defrost_hold` | "De buitenunit ontdooit kort. Dit is normaal bij koud en vochtig weer." |
| Waarom wordt defrost niet los uitgeschreeuwd? | UI aggregate | `defrost_hold` | "Normale defrosts worden samengevat zolang duur en herstel normaal blijven." |
| Waarom koelt hij niet ondanks koelvraag? | `cooling_limited` | `dew_stop` | "Koelen komt te dicht bij het dauwpunt. Het systeem voorkomt condens." |
| Waarom koelt hij zachter? | `cooling_limited` | `cooling_limiter` | "Er is koelvraag, maar de veilige marge laat nu alleen een lager koelniveau toe." |
| Waarom komt CV erbij? | `boiler_assist_start` | `boiler_assist` | "De warmtepompen leveren de basis; CV vult tijdelijk aan zolang extra vermogen nodig is." |
| Waarom verwarmt alleen de CV-ketel? | `boiler_fallback_start` | `boiler_fallback` | "Geen warmtepomp is veilig beschikbaar. Na bevestigde stop en geldige beveiligingen neemt de CV-ketel tijdelijk over." |
| Waarom komt CV niet erbij? | `decision_blocked` | `boiler_assist` / `soft_guard` | "CV-assist was mogelijk, maar niet vrijgegeven of geblokkeerd door een voorwaarde." |
| Waarom start CM4 nog niet? | `decision_blocked` | `fallback_blocked` / `hp_stop_unconfirmed` | "Foutfallback wacht totdat de warmtepomp veilig gestopt is en alle installatiebeveiligingen geldig zijn." |
| Waarom draait er iets zonder comfortvraag? | `sticky_pump_run` | `sticky_protection` | "De pomp draait kort om vastzitten na lange stilstand te voorkomen." |
| Waarom draait er iets bij vorst? | `frost_protection_start` | `frost_protection` | "Het systeem laat water circuleren om bevriezing te voorkomen." |
| Is dit normaal? | severity + event type | n.v.t. | `normal` is verwacht gedrag, `limited` is beschermend, `attention` vraagt supportblik. |

## UI-regels

### Actueel

Doel: wat gebeurt er nu en wat doet het systeem daarna?

Regels:

- Geen periodekeuzes tonen.
- Maximaal een hoofdreden tonen.
- Bij CM98: titel `Vorstbescherming actief`, kleine badge `CM98`.
- Bij CM5 met limiter: titel `Koeling veilig begrensd`.
- Bij sticky pump: titel `Pompbescherming actief`.
- Geen technische codes in de hoofdtekst, behalve compacte modebadge waar nuttig.

### Tijdlijn

Doel: chronologische uitleg.

Regels:

- Toon momenten, periodes en samenvattingen visueel verschillend.
- Normale herhaling groeperen.
- Defrost blijft eigen categorie, niet alleen bescherming.
- Details tonen `source`, `reason_code`, `control_mode`, values en duration.

### Grafieken

Doel: context, niet uitleg.

Regels:

- Lanes: warmtevraag/koelvraag, HP1, HP2, CV, ontdooien, bescherming.
- De gekozen tijd verbindt grafiek en geselecteerd event.
- Bij lege echte eventbuffer geen mockdata tonen op real device.

## Groepering

Groepering hoort in de UI, niet in firmware.

Aanbevolen regels:

- Defrosts groeperen per venster als:
  - eventcount >= 2;
  - duur binnen verwachte band;
  - geen attention/fault;
  - herstel-event aanwezig of duur afleidbaar.
- Koelbeperkingen groeperen als:
  - zelfde reason binnen korte periode terugkomt;
  - geen fault/attention;
  - limiter-level alleen stapgewijs verandert.
- Normale koelmodulatie met een uitgestuurd niveau boven nul hoort niet als
  `cooling_limited` in de beslislog. `capacity_cap`, `projected_floor` en andere
  limiterstappen blijven beschikbaar via live diagnostics en Trends. De
  beslislog registreert alleen het begin en einde van een volledige koelpauze.
- Start/stop aandacht tonen als:
  - teller boven drempel in 1u/24u;
  - apart `attention_pattern` event of bucketcounter.

## Scenario-checklist

Deze matrix is de acceptatietest voor het concept.

| Scenario | Verwachte events | Verwachte UI |
| --- | --- | --- |
| Koude winterdag, continu warmtevraag | `source_start`, `topology_change`, herhaalde `defrost_seen_*` | Actueel duo/single logisch; Tijdlijn groepeert defrosts; Grafiek toont HP1/HP2 lanes |
| Duo wissel | `source_start`, `topology_change`, `source_stop` | Uitleg dat HP1/HP2 gelijkwaardig zijn en keuze op draaiuren/beschikbaarheid rust |
| Andere pomp start niet | `candidate_blocked` of `decision_hold` | "Start uitgesteld" met rusttijd/defrost/beschikbaarheid |
| Force CM98 | `frost_protection_start`, `frost_protection_clear` | Actueel en Tijdlijn tonen `Vorstbescherming` met `CM98` badge |
| Zomerdag zonder vraag | `sticky_pump_run` | "Pompbescherming uitgevoerd", geen HP-compressorstart suggereren |
| Koeling, dauwpuntstop | `cooling_limited` met `dew_stop` | "Koeling gestopt door dauwpunt", limited severity |
| Koeling, limiter zonder stop | `cooling_limited` met `cooling_limiter` | "Koeling begrensd", niveau/marge in details |
| CV assist start/stop | `boiler_assist_start`, `boiler_assist_stop` | CV als tijdelijke ondersteuning, niet als storing |
| CV assist geblokkeerd | `decision_blocked` | "CV-ketel niet vrijgegeven" of guardrail-uitleg |
| Kort communicatiegat | geen fault/fallback-event; eventueel availability `suspect` | Draaiende HP blijft eigenaar; geen stop, Duo-wissel of CM4 |
| Bevestigde HP-storing, Single | `incident_start`, `hp_availability_change`, `hp_stop_confirmed`, `boiler_fallback_start` | Incident, veilige stop en daarna ketel-only fallback zijn afzonderlijk verklaarbaar |
| Eén HP gestoord, Duo | incident voor getroffen HP; gezonde HP blijft kandidaat | Geen CM4 zolang een gezonde HP veilig kan verwarmen |
| CM3 naar CM4 | `control_mode_change`, `boiler_fallback_start` | Rol wijzigt van ondersteuning naar fallback; geen fysieke keteluitgangspuls |
| HP hersteld | `incident_clear`, availability recovering/available, `boiler_fallback_stop` | Herstelvenster is zichtbaar; CM4 eindigt pas na stabiel herstel |
| CM1 flow/pre/postflow | v2 `flow_hold_*` of `decision_hold` | Alleen zichtbaar als het een start/stop verklaart |

## Implementatiefasering

### Fase 1: contract harden

- Zet deze tabellen om naar comment/doc bij `OpenQuattDecisionLog.h`.
- Controleer eventnamen en reasoncodes op stabiliteit.
- Voeg geen nieuwe UI toe zolang de woorden niet kloppen.

### Fase 2: ontbrekende verklaringen

Aanbevolen volgorde:

1. Specifiekere cooling reasons: room cap, restart wait, projected floor.
2. Runtime-balance snapshot alleen in supportdetails.
3. Service/CM100 alleen als supportevent.

### Fase 3: aggregatie

- Defrostgroep in echte data.
- Cooling limitergroep in echte data.
- Starts/stops attention op bucketcounter.

### Fase 4: UX-reductie

- Mockdata verder isoleren naar dev.html.
- Actueel nog compacter maken.
- Supportdetails uitbreiden zonder hoofdscherm technischer te maken.

## Open keuzes

Mijn voorgestelde antwoorden staan erbij; alleen afwijken als we bewust een andere richting willen.

1. Moet CM1 standaard zichtbaar zijn?  
   Besluit: nee. Alleen tonen wanneer CM1 gedrag verklaart, zoals flow hold, preflow of postflow.

2. Noemen we de hulpbron `CV`, `CV-ketel` of `boiler`?  
   Besluit: UI `CV-ketel`, code mag `boiler` blijven.

3. Moet een handmatige Force CM98 anders heten dan echte vorstbescherming?  
   Besluit: nee in hoofdtekst. Force CM98 is testgedrag en wordt gewoon als `Vorstbescherming` getoond.

4. Moet defrost onder `Bescherming` vallen?  
   Voorstel: nee. Defrost blijft eigen eventcategorie, maar telt wel mee als normaal beschermend wintergedrag.

5. Is `limited` de juiste severity voor vorstbescherming?  
   Voorstel: ja. Het is geen storing, maar bescherming heeft tijdelijk voorrang op normale regeling.

6. Mogen we reason-codes nog hernummeren voor PR-merge?  
   Voorstel: liever nu stabiliseren. Na release alleen append-only uitbreiden.
