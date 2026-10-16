# Eigen watertemperaturen en gekoppelde Duo voorspellingen

De proef op basis van `dev` commit `708e25dd` werkt drie samenhangende verbeteringen
uit: een eigen temperatuur per ODU, kandidaatvoorspellingen voor twee ODU's in
serie en bewegingskosten op basis van Hz met afzonderlijke start- en stopevents.
Dit document bewaart de eerste HIL-onderzoeksresultaten. De doorontwikkeling voor
productie staat in [Performance Model Observer](performance-model-observer.md).
De temperatuur- en serieberekeningen beïnvloeden de actieve regeling niet.
De actieve Hz-vergelijking is afzonderlijk in `dev` gemergd via
[PR #854](https://github.com/OpenQuatt/OpenQuatt/pull/854) en ook opgenomen in
de gecombineerde proefbuild. Minimumlooptijden, minimumstilstand,
frequentiebeleid en topologiebeveiligingen blijven daarbij behouden.

## Temperatuur bij de actuele bedrijfstoestand

Voor het voorspellen van het actuele vermogen gebruikt iedere ODU zijn eigen
gekalibreerde wateruitlaattemperatuur en gemeten compressorfrequentie. Waarde en
ontvangsttijd komen uit hetzelfde oorspronkelijke Modbus-receipt. Een latere
template-publicatie of een vastgehouden geselecteerde waarde krijgt geen nieuwe
ontvangsttijd. De proef gebruikt expliciet HP1 voor buitenlucht en flow.

Dit voorkomt dat dezelfde systeemaanvoer als uitlaattemperatuur voor beide ODU's
wordt gebruikt. Het maakt de performance mapping fysisch consistenter, maar
verbetert niet automatisch de absolute modelkalibratie. De bestaande V2 mapping
blijft dezelfde voor `V2 old model` en `V2 new model`.

Een gemeten uitlaattemperatuur hoort bij de actuele bedrijfstoestand. Haar
ongewijzigd invullen voor alle mogelijke toekomstige frequenties negeert de
temperatuurverandering door de vermogensstap. Daarom zijn actuele diagnose en
kandidaatvoorspelling aparte functies.

## Kandidaatvoorspellingen voor serieschakeling

Voor een bevestigde serievolgorde HP1 → HP2 en gemeenschappelijke flow rekent de
proef met de volgende stationaire warmtebalans:

```text
K = flow_lph × 4186 / 3600                         [W/K]
Q1 = map(hz1, buitenlucht, Tout1)
Tout1 = Tin1 + Q1 / K
Tin2 = Tout1
Q2 = map(hz2, buitenlucht, Tout2)
Tout2 = Tin2 + Q2 / K
Qduo = Q1 + Q2
Pel_duo = Pel1 + Pel2
```

De rekenkern lost eerst HP1 op en gebruikt zijn berekende uitlaat als inlaat voor
HP2. Alleen HP1 → HP2 is een geldige installatie; HP2 vooraan wordt nu afgewezen
als hydraulische mismatch. Omgekeerde volgorde in historische proefresultaten
was uitsluitend een onderzoekscenario. Een kandidaat op 0 Hz voegt geen
compressorwarmte toe en laat het water in dit model onveranderd door. Pompwarmte,
leidingverlies, standbyverbruik en thermische opslag vallen buiten deze balans.
De factor veronderstelt water met circa 1 kg/l; glycol of een nauwkeuriger
temperatuurafhankelijke vloeistofbeschrijving vraagt een ander contract.

De begrensde bisectie gebruikt maximaal 24 iteraties en accepteert een restfout
van maximaal 1 W per ODU. Zowel de beginpunten als ieder bezocht tussenpunt
moeten door de bestaande mapping worden ondersteund. Dit is bewust conservatief:
een onbruikbaar begininterval kan een kandidaat afwijzen waarvoor een andere
zoekmethode wel een geldige oplossing zou vinden. Er komt geen extrapolatie bij.
Alleen een geldige volledige combinatie levert een bruikbaar totaalvermogen.

## Validiteit en grenzen

De proef weigert de serieberekening bij onbekende volgorde of onbevestigde
gemeenschappelijke flow. Hij controleert bovendien:

- online ODU's en geldige oorspronkelijke receipts;
- water- en frequentiereceipts maximaal 30 s oud, buitenlucht maximaal 30 min;
- maximaal 12 s tijdverschil binnen de gebruikte watergroep;
- flow tussen 300 en 3000 l/h en maximaal 0,5 °C verschil tussen de gemeten
  uitlaat van de eerste ODU en de inlaat van de tweede;
- verse en onderling uitgelijnde werkmodus-, defrost- en statusreceipts;
- geen defrost, vierwegklepomschakeling of olie-retour;
- kandidaatfrequenties van 0 t/m **90 Hz inclusief**, daarnaast het bestaande
  geldigheidsgebied van de performance mapping.

Deze onderzoeksgrenzen zijn geen nieuwe productiebeveiligingen. De controle van
statusbits bewijst nog geen stationaire meetperiode na starten of moduleren.
Daarvoor is vóór actieve toepassing een stabiliteitsvenster nodig. De bestaande
V1 mapping kan temperatuurwaarden aan de asgrens klemmen zonder dit als
`boundary_estimate` te markeren; de proef verandert dat gedrag niet.

Bij afwijzing verschijnt een expliciete reden, zoals `stale`, `time_skew`,
`hydraulic_mismatch`, `model_unavailable` of `no_convergence`. Een oud geldig
totaal wordt niet hergebruikt. Als een tweede ODU niet oplosbaar is, mag de
eventueel al berekende eerste ODU alleen als gedeeltelijke diagnose worden
gelezen; de volledige combinatie blijft onbruikbaar.

## Moduleren en schakelen afzonderlijk beschrijven

De proef ontleedt iedere overgang in:

- de som van absolute Hz-veranderingen van ODU's die blijven draaien;
- het aantal starts en stops;
- Single ↔ Duo als verandering van het aantal draaiende ODU's;
- een wisseling van de actieve Single-ODU als afzonderlijk herkenbare overgang.

Zo wordt 30 → 45 Hz een modulatie van 15 Hz. Een stop van 30 → 0 Hz wordt één
stop, zonder die gebeurtenis te verbergen in 30 Hz modulatieafstand. Dit blijft
ook betekenisvol bij ongelijke of aangepaste fysieke F-leveltabellen.

Er zijn nog geen nieuwe gewichten voor slijtage of Wh ingevoerd. Zulke gewichten
vragen gemeten overgangskosten. Voor een eerste actieve wijziging past Hz-afstand
op de plaats van de huidige levelafstand als late vergelijkingsstap. Behoud
daarbij de bestaande vermogensmarges, topology-hold en start-/stopbeveiligingen.
Een tweede topologieboete kan anders hetzelfde schakelgedrag dubbel bestraffen.

De afzonderlijke actieve wijziging gebruikt in beide Duo-routes de volgorde
starts, stops en modulatieafstand in Hz, op de bestaande late vergelijkingspositie.
Power House vergelijkt daarvoor nog steeds eerst soft-limitoverschrijding,
elektrisch vermogen en warmtevraagafwijking. De stooklijn behoudt haar bestaande
50 W-foutband en startvoorkeur. Onbekende frequentiemetadata maakt de Hz-afstand
onbruikbaar (`-1`); een onbekende referentie wordt niet als gratis modulatie
gewaardeerd. Bekende frequenties van uitgesloten huidige levels mogen nog als
referentie dienen. Geldigheid en frequentiebeveiliging van nieuwe kandidaten
blijven afzonderlijke voorwaarden.

Beide Single-routes behouden hun bestaande levelgedrag. In Power House Single
blijft ook `50.0f * candidate.level_moves` behouden; dit wordt niet 50 W per Hz.
De onafhankelijke review vond aanvankelijk een onbedoeld effect op Single
stooklijn door de gedeelde comparator. De gerepareerde comparator schakelt Hz
alleen expliciet voor Duo in; een nieuwe regressietest dekt de Single-keuze.

## Implementatie en herhalen

- `openquatt/includes/performance/oq_water_performance_shadow.h`: zuivere
  rekenkern, receipts, validiteit en overgangsontleding; zonder I/O of allocaties.
- `openquatt/includes/experimental/oq_water_performance_probe.h`: adapter naar
  de bestaande variantgebonden mappings en maximaal 512 bytes proefstate.
- `scripts/hil/configs/water_performance_duo.yaml`: uitsluitend HIL, gebaseerd
  op de Q Duo-testcontroller met hostname `openquatt-test`.
- `tests/host/oq_water_performance_shadow_test.cpp`: analytische referentie,
  originele mappings, tijdgrenzen, ontbrekende data en overgangen.
- `openquatt/includes/experimental/oq_water_performance_grid.h`: één combinatie
  per loopstap, met een gecachete upstream-oplossing per frequentie.
- `openquatt/includes/control/oq_compressor_movement_logic.h` en
  `tests/host/compressor_movement_dispatch_test.cpp`: actieve Hz-vergelijking en
  regressies voor beide Duo-routes, beide Single-routes en gewijzigde F-tabellen.

De nieuwe rasterjob bezit één onveranderlijke set receiptwaarden en originele
ontvangsttijden. Iedere stap actualiseert alleen de rekentijd voor de
versheidscontrole. Zowel succesvolle als afgewezen upstream-oplossingen worden
binnen die job hergebruikt; een nieuwe job wist de volledige cache. Ook de
oorspronkelijke werkmodus/statusgroep heeft een eigen geldigheidsdeadline, zodat
een langlopende job een eerder geldig statusoordeel niet onbeperkt kan gebruiken.
Een afgebroken of verlopen raster wordt niet als voltooid gepubliceerd.

De HIL-overlay verwerkt één kandidaat per 25 ms en stopt de job bij uitschakelen,
snapshotfout, veranderde hydraulische bevestiging of gewijzigd frequentiebeleid.
`grid_seq` identificeert het gebruikte observerframe. `grid_done` onderscheidt
een volledig resultaat van een lopende/afgebroken job; `grid_max_us` meet de
loopstap inclusief context- en policycontrole. `grid_us` is de totale actieve
rekentijd en `grid_elapsed_us` de doorlooptijd inclusief tussenliggende loops.
Beide observerstates samen zijn compile-time begrensd op 1024 bytes. Er wordt
geen resultatenmatrix opgeslagen en de job maakt geen heapallocaties.

De oorspronkelijke bisectie blijft de referentie. Hosttests vergelijken alle
combinaties van een gecontroleerd raster én het volledige raster van 198
combinaties uit de echte V2 old/new EEPROM-fixtures bij 7 en −7 °C en beide serievolgordes:
de gecachete variant levert dezelfde vermogens, uitlaattemperaturen en
afwijsredenen. Ook cancel, herstart, verlopen waterreceipts, de exacte
statusdeadline en ongeldige frequentieassen zijn afgedekt.

## Aansluiting op modelvalidatie

De bestaande performance-telemetrie verzamelt al eigen Tin/Tout en frequentie per
ODU, samen met flow en energieschattingen. Zij neemt iedere 10 s een sample,
vereist zes geldige samples per minuut en hanteert vijf minuten wachttijd na
start/defrost en één minuut na een levelwisseling. Dat is de bestaande route
voor praktijkgegevens; deze proef voegt geen externe verzending toe.

Voor de volgende vergelijking moeten per installatie en dag dezelfde minuten
worden gebruikt voor de gedeelde-temperatuurreferentie, de eigen-Tout-voorspelling
en, bij bevestigde hydrauliek, de serieberekening. Bewaar generatie, mapversie,
werkelijke Hz, beide Tin/Tout, flowbron, actieve ODU's en uitsluitreden. Een
nieuwe sessie na een onderbreking krijgt opnieuw een stabiliteitsvenster.

De huidige collector controleert sensorstatus op aanwezigheid en eindigheid;
die controle alleen bewijst geen oorspronkelijke Modbus-versheid. Het bestaande
payloadcontract heeft ook geen oorspronkelijke ontvangsttijden of gedeelde
systeemaanvoer. Daardoor kan een daaruit afgeleide export de nieuwe vergelijking
en het receiptcontract niet volledig reconstrueren. Voor een toekomstige
uitbreiding horen leeftijd/skew en de gedeelde referentie expliciet bij het
schema en de consentgebonden collector. Dat vraagt afstemming binnen
`Feat: modelvalidatie`, inclusief servercompatibiliteit. De historische CSV was
bij deze vervolguitwerking niet lokaal aanwezig; deze schemabeoordeling is op de
collectorbron gebaseerd.

Thermisch vermogen uit flow × temperatuurverschil is een waterzijdige schatting.
Elektrische energie in de bestaande telemetrie is een firmwareberekening en geen
onafhankelijke elektriciteitsmeter. Gebruik voor kalibratie meerdere onafhankelijke
installaties/dagen en onafhankelijke warmte-/kWh-controles; beoordeel de mapping
niet met haar eigen voorspelde vermogen als meetwaarheid. Leg een kandidaat
vast vóór toetsing op andere installaties en dagen. Geef simulatorregels een
expliciete uitsluiting. De laagfrequente V2-mapping blijft tot die toets ongewijzigd.

Compileer de overlay met ESPHome. Voor OTA en simulatorreboot gelden de
labafspraken. Gebruik twee V2-profielen, normale startvoorwaarden en een werkelijk
aangestuurde vaste 30/30 Hz-toestand. Leg daarna via de handmatige
simulatortelemetrie een gecontroleerde waterketen vast. De HIL-switches starten
uitgeschakeld; volgorde en gemeenschappelijke flow moeten expliciet worden
bevestigd. `HIL Water Shadow Report` bevat status, vermogens, balansfouten,
rekentijd, stackruimte en rasterresultaten.

De snapshotfouten veranderen uitsluitend de observerkopie. Controleer naast het
rapport ook de aangestuurde F-levels, echte frequenties en simulatorcounters.
Zet foutinjecties uit en controleer na afloop CM0 én werkelijk 0 Hz bij beide
compressoren. Gewone testinstellingen en testfirmware blijven volgens de huidige
labafspraak staan.

## HIL resultaten van 16 oktober 2026

De testcontroller draaide proefbuild `0x09ec6386` met profiel
`water-performance-shadow-v1`. HP1 simuleerde `V2 old model`, HP2 `V2 new model`.
Beide compressoren werden eerst via de normale CM100-bediening werkelijk op
F3 / 30 Hz aangestuurd. Daarna leverde de simulator gecontroleerde telemetrie
via het normale Modbus-pad. De nominale fixture was buitenlucht 7 °C, flow
800 l/h, Tin1/Tout1 = 30/33 °C en Tin2/Tout2 = 33/36 °C. De controller ontving
door registerkwantisatie 799,7 l/h. Kalibratie-offsets werden uitgelezen en bij
het instellen van de fixture verrekend.

| Proef | Resultaat |
| --- | --- |
| Eigen Tout bij actueel 30/30 Hz | HP1 3178,96 W, HP2 3052,07 W; gedeelde 35 °C-referentie 3094,37 W per ODU |
| Stationaire kandidaat 30/30 Hz | Tout1 33,401 °C, Tout2 36,653 °C, totaal 6186,44 W |
| Kandidaat 45/30 Hz | Tout1 34,902 °C; HP2 daalt van 3024,44 naar 2963,71 W doordat zijn inlaat warmer wordt |
| Omgekeerde serievolgorde | Unitresultaten wisselen overeenkomstig; totaal blijft gelijk in deze symmetrische fixture |
| Gemeten Tout wijzigen bij gelijke retour en flow | Actuele eigen voorspellingen wijzigen; stationaire kandidaat blijft gelijk |
| Onbekende volgorde, onbevestigde flow en hydraulische mismatch | Afgewezen met bijbehorende status; herstel slaagt |
| Ongeldige uitlaat, receipt-skew, transiënt, geen convergentie en verouderde kopie | Geen geldig totaal; herstel slaagt na iedere fout |
| Kandidaat 0/30 Hz | Eerste ODU passief; één stop, topology-change en 0 Hz modulatieafstand |
| Kandidaat 90/90 Hz bij −7 °C | Geldig, balansfouten 0,494 en 0,460 W |
| Kandidaat 90,1/90 Hz | `frequency_policy`; terug naar 90 Hz herstelt |
| Kandidaat 90/90 Hz bij 7 °C | `model_unavailable` door bestaande mapping; de 90 Hz-beleidsgrens zelf is inclusief |

De oorspronkelijke actuatorcommando's bleven F3 / Heating met 30 Hz demand voor
beide ODU's. Ook na de negatieve proeven en frequentiegrenzen was de gemeten
frequentie 30/30 Hz en bleef de hoogste geschreven simulatorstand `maxF=3`.
Simulatorcounters `drop`, `exc`, `bad_addr` en `bad_write` bleven 0.

Alle 123 hosttests, C++-formatcontrole, UBSan en firmwarecompilatie slaagden. Een
onafhankelijke koude review van de volledige proef leidde tot twee gerepareerde
bevindingen: het benchmarkframe moest dezelfde hydraulische bevestigingen
krijgen en statusvoorwaarden moesten rechtstreeks uit verse receipts komen.
Vijf situaties zijn daarnaast op de host nagerekend: maximaal 0,008 W en
0,0004 °C verschil met de ESP32. Dit verifieert de implementatie; de opgelegde
simulatortemperaturen zijn geen validatie van de fysieke modelnauwkeurigheid.

De stijl-, docs-, webcontract- en webbundlechecks slaagden. De productieconfiguratie
`configs/heatpump_controller_q/duo.yaml` valideert, maar de daaropvolgende
NVS-budgetcheck faalt: 570 van 630 bruikbare entries begroot, dus 60 beschikbaar
tegenover 100 vereist. Alle bestaande productie-YAML, componenten en de
budgetchecker zijn ten opzichte van `708e25dd` ongewijzigd; de enige nieuwe YAML
is de afzonderlijke HIL-overlay. Deze reservekwestie hoort daarom bij de
bestaande dev-basis en moet afzonderlijk worden opgelost vóór releasekwalificatie.
`duo_wifi.yaml` bestaat in deze dev-basis niet meer; `duo.yaml` is het huidige
gecombineerde Q Duo-entrypoint.

### Rekentijd en geheugen

Een geldige losse evaluatie kostte doorgaans ongeveer 1,4–1,7 ms; één vastgelegd
herstelmoment kostte 2,962 ms. Het actuele raster bevatte 198 toegestane fysieke
levelcombinaties, waarvan 144 een geldige volledige voorspelling opleverden.
De rasterberekening kostte **119,666 ms**. ESPHome logde daarbij
`web_server took a long time for an operation (120 ms), max is 50 ms`.
Een volledige bisectie per kandidaat mag daarom in deze vorm niet als één
synchrone taak aan de productieregeling worden toegevoegd.

De gemeten vrije loopstack na de proeven was 3990 bytes. Met dezelfde proefbuild
en draaiende compressoren waren de laatste observer-aan/uitmetingen gelijk:
84087 bytes interne vrije heap, 40960 bytes grootste vrije block en 5342260
bytes vrije PSRAM. De minimum-sinds-boot bleef 39828 bytes. Een eerdere opname
tijdens de run liet tijdelijk 78803 bytes vrij en een grootste block van 36864
bytes zien. Dit is geen productiegeheugenkwalificatie met gelijktijdige
HA/web/API/MQTT/OTA-belasting en ook geen vergelijking tussen twee firmwarebuilds.

In de aanloop stond één Modbus-response-timeout in de log; beide ODU's waren
daarna beschikbaar en alle resultaatasserties slaagden. Een voorbereidende
instelling gebruikte eerst een onjuiste Flow Control Mode-optienaam. Die write
had geen effect; `Flow Setpoint` is daarna ingesteld en teruggelezen.

De eindcontrole bevestigde CM0 en werkelijk 0 Hz voor beide compressoren.
Beide handmatige telemetrie-overrides en UART-foutinjectie stonden uit. De
observer stond uit met snapshotfout `None`. De proefbuild, V2-profielen,
bronselecties en gewone testinstellingen zijn blijven staan; Day max frequency
en Silent max frequency staan beide op 90 Hz.

## Vervolg-HIL: Hz-vergelijking en rasterjob

De vervolgproef draaide op dezelfde testcontroller met profiel
`water-performance-shadow-v2`, firmwarehash `0x8c484561`. Deze gecombineerde build
bevat de actieve Duo-Hz-vergelijking en de nieuwe rasterjob. Het thermische model
blijft observerend. De simulator behield de twee V2-profielen; de normale
minimumlooptijd en startvoorwaarden bleven actief.

De HIL-run herhaalde de eigen-Tout-, serie-, fout- en 90 Hz-proeven succesvol.
Vijf situaties kwamen opnieuw binnen 0,008 W en 0,0004 °C overeen met de native
hostberekening. De kandidaat 90/90 Hz was geldig bij −7 °C; 90,1 Hz werd geweigerd.
Bij 7 °C bleef het bestaande mappingmasker van toepassing. De echte compressor-
en demandfrequenties bleven tijdens deze observerproeven 30/30 Hz, met F3 als
hoogste geschreven level.

| Rasterproef | Max. stap | Actief rekenwerk | Doorlooptijd | Combinaties geldig/totaal |
| --- | --- | --- | --- | --- |
| Eerste volledige job | 2,157 ms | 197,692 ms | 4,985 s | 144/198 |
| Herstart na uitschakelen | 2,611 ms | 197,444 ms | 4,974 s | 144/198 |
| Herstart na snapshotfout | 2,023 ms | 197,634 ms | 4,970 s | 144/198 |
| Herstart na onbekende volgorde | 9,405 ms | 204,270 ms | 4,990 s | 144/198 |
| Herstart na gewijzigde frequentiegrens | 2,064 ms | 197,642 ms | 5,018 s | 144/198 |

Iedere volledige job gebruikte 173 unit-solves. De vier onderbroken jobs
publiceerden geen voltooid resultaat; herstel begon met een nieuwe cache en
leverde opnieuw alle 198 combinaties. Verlopen receipts en de exacte
statusdeadline zijn daarnaast op de host geïnjecteerd. Alle waarden en
afwijsredenen van het gecachete raster kwamen overeen met de oorspronkelijke
berekening, ook voor de volledige EEPROM-tabellen en omgekeerde serievolgorde.

De winst is **begrensde blokkering van de loop**: de langste gemeten stap was
9,405 ms, tegenover het eerdere synchrone blok van 119,666 ms. Het nieuwe totaal
is geen bewijs van minder CPU-werk. De nieuwe timer omvat ook runtimecapture en
policycontrole per stap, en deze rasterruns vonden bij −7 °C plaats; het eerdere
blok werd bij 7 °C gemeten. De doorlooptijd van circa 5 s maakt het voorlopig
geschikt als observer, zonder daarmee een actieve regelcadans te kwalificeren.
De begrensde SSE-opname bevatte geen warning/error; de resultaatasserties en
simulatorcounters vormen het primaire bewijs.

Alle 125 hosttests, gerichte strict-O2/UBSan-tests, C++-format-, stijl- en
docschecks en de firmwarecompilatie slaagden. Beide branches slagen ook
voor alle 443 Python-contracttests, met twee skips. De gedeelde Hz-helper en
regressie zijn in de broncodebudgetten opgenomen met behoud van de bestaande
marges en semantische assertions. De GitHub-CI van PR #854 is volledig groen,
inclusief productiecompilaties van Q Single en Q Duo. Onafhankelijke koude reviews
controleerden de actieve vergelijking en de nieuwe rasterjob. De regressie voor
Hz-afstand gebruikt monotone tabellen waarbij levelafstand én Hz-balans juist
de andere kandidaat bevoordelen. Daardoor bewijst de test de bedoelde prioriteit,
ook bij een anders genummerde 20-leveltabel. De HIL-run gebruikte CM100 met vaste
F3-commando's: autonome Power House-/stooklijnkeuzes zijn in deze run niet fysiek
tegen elkaar vergeleken. Die keuze is door de hostregressies getoetst.

Tijdens de vervolgproef bleef de minimum-sinds-boot 39828 bytes, het grootste
interne vrije block 40960 bytes en de vrije PSRAM bij draaiende compressoren
5342260 bytes. Vrije interne heap was vóór observerinschakeling 84979 bytes,
na de rasterproeven 83583 bytes en na uitschakeling 83819 bytes. De grootste-
blockfragmentatie was respectievelijk 51,80%, 50,99% en 51,13%; loopstackruimte
bleef 3990 bytes. Dit zijn momentopnamen met actieve HTTP/SSE-belasting. Ze
vervangen geen afzonderlijke baseline/candidate- of worst-case geheugentest.

De afzonderlijke read-only eindcontrole bevestigde firmware `0x8c484561`, CM0,
0/0 Hz, observer uit, snapshotfout `None`, beide telemetrie-overrides uit en
UART-foutinjectie uit. Beide frequentiegrenzen bleven 90 Hz. De bestaande
NVS-budgetkwestie wordt al behandeld in
[PR #847](https://github.com/OpenQuatt/OpenQuatt/pull/847); de productieconfiguratie
uit deze dev-basis heeft nog steeds 60 vrije entries tegenover 100 vereist.
Deze proef voegt geen persistente instellingen aan de productieconfiguratie toe.

De inspecteerbare lokale meetbestanden staan in
`.tmp/hil/2026-10-16T13-49-46-489Z-water-performance-grid-v2/`: `report.json`,
`measurements.json`, `events.jsonl`, `final-state.json` en de logstreamopname.
Deze labbestanden worden niet meegepubliceerd.

## Advies voor actieve toepassing

Herstel ook de bestaande NVS-budgetmarge voordat deze dev-basis als
releasekandidaat wordt gekwalificeerd. Voor de vermogensregeling is de volgende
volgorde passend:

1. **Hz-afstand als afzonderlijke kleine wijziging beoordelen.** De implementatie
   en regressies staan in [PR #854](https://github.com/OpenQuatt/OpenQuatt/pull/854)
   richting `dev` op `codex/duo-movement-hz`. Starts, stops en modulatie
   blijven gescheiden, met behoud van bestaande hysterese en tijdgrenzen.
   Houd deze review en uitrol afzonderlijk van de observerende temperatuur-
   en serieberekeningen; voor deze wijziging is geen nieuw thermisch model nodig.
2. **Eigen Tout eerst naast de bestaande voorspelling verzamelen.** Vergelijk
   residuals per ODU in stabiele frequentie-, buitenlucht- en temperatuurvakken.
   Gebruik originele ontvangstmomenten en onafhankelijke gemeten warmte en
   elektriciteit. Het modelvalidatieproject kan dezelfde receiptselectie en
   uitsluitredenen gebruiken. Een plausibeler temperatuurbron is geen reden om
   de laagfrequente V2-kalibratie zonder metingen aan te passen.
3. **Van volledig raster naar volledige beslissingen vergelijken.** De cache en
   verdeling over loopcycli zijn uitgevoerd en in HIL getoetst. Voeg nu eerst
   observerende oude/nieuwe kandidaatkeuzes toe. Onderzoek een directe oplossing
   per geldig lineair temperatuurinterval alleen als verdere versnelling nodig
   blijkt, met dezelfde maskers en een begrensde terugval. Vergelijk Pel,
   warmtevraagafwijking, minimumvermogen en Single/Duo-overgangen.
4. **Activeren na praktijkvalidatie.** Bevestig de echte hydraulische volgorde en
   gemeenschappelijke flow, voeg een gemeten stabiliteitsvenster na overgangen
   toe en toets de voorspelde temperatuursprong. Onbruikbare nieuwe
   voorspellingen moeten herkenbaar terugvallen op de bestaande veilige route.
   Kalibreer eventuele start- en stopkosten pas op gemeten overgangsenergie;
   verzin geen Wh- of slijtagegewichten.

De belangrijkste opbrengst is een samenhangend model voor temperatuur,
kandidaatvermogen en overgangstype. De HIL-run onderbouwt de rekenkundige en
ingangscontracten en maakt de benodigde rekentijdoptimalisatie concreet. Een
COP-winst of betere warmtevraagregeling vraagt metingen op een echte installatie.
