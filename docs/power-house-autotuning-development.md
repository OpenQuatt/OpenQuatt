# Power House Auto-Tuning: implementatie en Adaptive-roadmap

## Status en uitgangspunt

Ontwikkelbasis: `dev` op `fd2e200c`, met GitHub vergeleken op 6 september 2026. PR #633, #625 en #634 zijn daarin gemerged; de [centrale webontwikkelafspraken](../openquatt/web/README.md) gelden voor de passieve learnerbediening. Ontwerpinput: *OpenQuatt Power House Autotuning Engineer Plan*, revisie 1.1, *Adaptive Power House* en de aanvullende *1R1C Design Note*. De 24 synthetische checks uit de overdracht zijn gereproduceerd. Ze bewijzen geen meetkwaliteit, firmwaregedrag of energiebesparing.

De implementatie bevat de scheiding van woninglijn en regelgrenzen, de passieve C++ batch-leerkern, een parallel 1R1C/RLS-model, kruisvalidatie, de meetadapter en CSV-replay. De live collector, geselecteerde-bronmetadata, één runtime-eigenaar, strikte PSRAM-opslag, een dubbel flashjournal en webbediening zijn nu aangesloten. Dit is experimentele testfirmware voor passief leren. De bestaande regeling blijft de handmatige Power House-instellingen gebruiken. Modelactivatie en automatisch toepassen zijn nog niet geïmplementeerd; de productfase met gecontroleerd toepassen is daarmee nog niet voltooid.

De feature blijft bestemd voor `heatpump_controller_q` en `waveshare`, Single en Duo. Listener en onbekende profielen krijgen geen learner. De gedeelde aanpassing aan de vraagberekening bewaart ook hun bestaande handmatige gedrag.
Alleen Q en Waveshare zetten `OQ_POWER_HOUSE_LEARNING_TARGET=1`. De learningheaders hebben daarvoor
een expliciete platformpoort: ESPHome neemt de hele include-directory op, dus vertrouwen op ongebruikte
inline functies alleen zou Listener niet daadwerkelijk van de leerkern uitsluiten. De flag en het profielpakket maken de collector en webcapability uitsluitend op deze profielen beschikbaar. Geen learnerpad schrijft regelparameters.

## Eigenaarschap en vermogensopbouw

`oq_house_model_logic.h` definieert twee onafhankelijke waarden:

- `HouseLine`: `heat_loss_w_per_k` en `zero_power_temp_c`.
- `PowerHouseEnvelope`: `request_max_w`, `demand_scale_w` en `slew_scale_w`.

De ongeclipte woninglijn is `max(0, H * (T0 - Tout))`. `Tc` blijft de representatiecoördinaat; `Pr = H * (T0 - Tc)`. Een geleerd ander lijnpunt verandert geen envelope. Handmatige legacy-invoer behoudt de oude betekenis: het ingestelde `Pr` vult alle drie envelopewaarden.

Het legacy-pad behoudt bewust de oude volgorde van delen, clippen en vermenigvuldigen. Eerst `H` uitrekenen kan bij halve demand-levels een andere afronding geven. De bevroren C++ baselinevergelijking controleert daarom behalve watts ook exact de discrete demand-uitvoer, flags en timestamps.

```text
HouseLine                  geselecteerde externe feedforward
    |                                     |
    +---------- bestaande bronkeuze -------+
                       |
          toekomstige tijdelijke P_adaptive = 0
                       |
             bestaande kamerfeedback
                       |
          request-cap, slew en waterlimit
                       |
         bestaande dispatch en supervisory
```

`DemandContributions` maakt `modelled_base_w`, `selected_feedforward_w`, `adaptive_w` en `room_feedback_w` afzonderlijk beschikbaar. `modelled_base_w` behoudt in het legacy-pad de historische basisclipping; `house_line_power_w()` geeft de ongeclipte structurele lijn. `adaptive_w` is in deze bouwstap altijd nul. Het samenvoegpunt staat vóór de bestaande regellimieten. Er bestaat geen adaptive instelling, toestand, actuatorpad of modelwrite.

De conceptuele som `P_base + P_adaptive + P_room` is geen vervanging voor bronkeuze, clipping of veiligheidslogica. Externe feedforward blijft zijn bestaande betekenis houden; een toekomstige adaptive laag krijgt daarvoor een expliciet uitsluitcontract. De learner krijgt geen toegang tot `ph_kp_w_per_k`, comfort memory, rise/fall-state, compressorlevels of dispatchkeuzes.

## Passieve keten en meetcontract

```text
fysieke metingen + ontvangstbewijs + bronidentiteit + bedrijfsstatus
    -> strikte snapshotadapter
    -> tijdsgewogen segmenten
    -> gevalideerde dataset
    -> robuuste fit / latere onafhankelijke dagen / stabiliteit
    -> advies met redenen en meetcontext
```

Normale CM0/CM1-pauzes blijven onderdeel van dezelfde meetperiode als CM2. Een pompoploop of pompuitloop wordt met het gemeten watervermogen geïntegreerd; geldig nuldebiet telt mee als nulvermogen. De overgang naar stilstandsdebiet is geen bronwissel. Het actuele ketelcommando en de uitgangen moeten ook tijdens deze pauzes uit staan.

De kern gebruikt maximaal 128 records en 365 dagen historie: 64 recente records en 64 historische plaatsen verdeeld over vier temperatuurgebieden. De batchduur blijft vier uur. Het bestaande journalformaat blijft gelijk; journals met maximaal 64 records blijven leesbaar. Een volledig journal vraagt maximaal 7868 bytes en past in het bestaande 8 KiB-slot. De JSON-exportbuffer is 32 KiB. Teruggaan naar firmware met de oude limiet van 64 records kan een voller journal niet herstellen. De grotere capaciteit vereist nog hardwarekwalificatie van het geheugenbudget. Een ontbrekend essentieel interval maakt het lopende segment ongeschikt. Normale uitperioden en signed calorimetrie blijven onderdeel van de tijdsintegratie. De dataset leert uit gemeten warmte; `P_request`, compressorlevels en de eigen woninglijn zijn geen trainingslabels.

Eén eigenaar in de ESPHome-mainloop beheert records en fitworkspace in PSRAM. Tijdens een hervatbare fit blijft de recordarray onveranderlijk; append/prune annuleert eerst de fit. HTTP-callbacks krijgen uitsluitend een onder mutex gekopieerde JSON-cache. Een tweede gelijktijdige export krijgt HTTP 429; netwerk-I/O houdt de cachemutex niet vast.

De actuele geselecteerde waarden blijven leidend voor bronkeuze. De learner neemt de waarde en
geldigheid van die bestaande resolver over; hij voert geen tweede, afwijkende herkomstcontrole uit.
OpenTherm, CiC, Home Assistant, API input en MQTT kunnen daardoor allemaal learning-bronnen zijn
wanneer de regelaar ze geldig verklaart. Een routewijziging maakt wel een nieuwe meetcontext. Zie
[broncontract](power-house-learning-sources.md).

Passief leren introduceert geen hydraulisch installatieprofiel. De bestaande geselecteerde waarden
blijven de bron van waarheid. In een Q Duo-build gebruikt de calorimetrie intern HP1 gevolgd door HP2;
dat is geen gebruikerskeuze. CM0, CM1 en CM2 gebruiken het bestaande geen-ketelvraagcontract; R1-uit wordt nooit
als fysiek ketelbewijs behandeld. Het passieve model vraagt geen ingevoerde meetonzekerheid of grens
voor zon- en interne warmte; afwijkingen blijven zichtbaar in de residuals. Automatisch toepassen
bestaat nog niet. De toekomstige toepasfase moet meetkwaliteit en afwijkende warmtebronnen afzonderlijk
onderbouwen.

De getalsgrenzen voor stabiliteit, spreiding en verbetering zijn ontwikkelinstellingen. Een succesvolle fit is geen gekalibreerd betrouwbaarheidspercentage en geeft geen toestemming tot automatisch toepassen. Onvoldoende geschikte data is een geldige uitkomst.

## Parallel 1R1C/RLS vanaf fase 1

De batch-fit blijft eigenaar van het structurele voorstel `H` en `T0`. Daarnaast identificeert
`oq_ph_thermal_model_logic.h` de dynamische energiebalans:

```text
delta_Tin = (U / C) * integral(Tout - Tin, dt_hours)
          + (1 / C) * integral(Qhp, dt_hours)
U: W/K; C: Wh/K; Qhp: gemeten signed W
```

Een aparte collector maakt volledig gedekte intervallen van standaard 30 minuten, met tijdgewogen
trapeziumintegratie en echte kamertemperaturen aan begin en eind. De kern accepteert intervallen van
15–60 minuten. Tijdgaten en generatiewissels mogen geen ongedekte energie of temperatuurverandering
overbruggen. Het dynamische bronpad gebruikt `SnapshotPurpose::THERMAL_DYNAMIC`: opwarming, afkoeling,
setpoint-herstel en comfortafwijkingen zijn daarin niet automatisch ongeschikt. Betrouwbare waarden,
verwarmingsmodus, geen ketelwarmte/defrost/begrenzing/service en alle fysieke meetvoorwaarden blijven
vereist. De batch-route houdt zijn strengere quasi-stationaire selectie.

De RLS gebruikt twee geschaalde parameters en een vaste `double`-covariantiematrix van 2×2, zonder
allocatie. De actieve `H` en een conservatieve configureerbare `C` zijn uitsluitend startwaarden.
Effectieve geldige observatie-uren, werkelijke excitatie van beide parameters, temperatuur- en vermogensspreiding,
numerieke gezondheid, pre-update voorspellingsfout, bias, meetcontext en recente dekking bepalen samen
de bruikbaarheid. Een stilstaand systeem of een toevallig passende startwaarde vormt geen geleerd
model. Ongeldige recente metingen schorten de beoordeling op; zij wijzigen de basisregeling niet.
Tijdsweging en forgetting gebruiken uren, zodat vier kwartierintervallen niet viermaal zoveel
duur-bewijs leveren als één uur. Residuals worden in K/h beoordeeld; de gekozen intervalduur mag een
foute temperatuurtrend niet verbergen. Bron- en configuratiewijzigingen onderbreken de lopende meetperiode, maar behouden de afgeronde leergegevens. Alleen de expliciete reset wist de leerstand.

Zon en interne warmte ontbreken als regressorterm in deze MVP. De 1R1C-fit beoordeelt daarom de
restfout, bias en spreiding van de gemeten intervallen. Die checks bewaken of het eenvoudige model
bruikbaar is, maar onderscheiden geen zon, bewonersgedrag of meetfout. Er is geen instelling,
replayargument, verplichte weersdienst of solar-input voor zulke oorzaken. Automatisch toepassen
blijft uit totdat de toekomstige toepasfase dat met onafhankelijke praktijkvalidatie kan onderbouwen.

`oq_ph_model_validation.h` vergelijkt `H_batch` en `U_rls` alleen bij passende source-/physical-/
controlgeneraties, actuele RLS-gegevens en voldoende overlappend buitentemperatuurbereik. De
ontwikkelgrens voor het relatieve verschil is configureerbaar (standaard 20%); modellen worden nooit
blind gemiddeld. `T0` blijft uit de batch-fit komen en wordt nooit gelijkgesteld aan `Tin`.
`model_disagreement` of onvoldoende RLS-onderbouwing houdt het gecombineerde advies tegen.
`auto_apply_allowed` blijft in deze bouwstap altijd `false`, ook als beide modellen overeenkomen.

De geleerde `C` blijft voorlopig. `rls_ready` betekent dat de numerieke fit en datadekking
voldoen, niet dat de fysieke warmteopslag betrouwbaar is geïdentificeerd. Het statusendpoint
meldt daarom expliciet `capacity_validated=false`. Afronding van kamertemperaturen en
ongemodelleerde warmte kunnen C vertekenen, ook bij een kleine voorspelfout.
`C * room_trend` blijft geschatte diagnostiek, maar keurt geen batchadvies meer af.
De eigen batchkwaliteit, U/H-consistentie, context en actualiteit blijven gecontroleerd.
De web-app noemt overeenstemming van warmteverlies en presenteert C nooit als gevalideerd.
Er is geen nieuwe estimator, instelling of journalmigratie nodig. Automatische toepassing blijft uit.

Beide methoden delen dezelfde sensoren en mogelijke systematische meetfouten. Hun overeenstemming
is aanvullende modelcontrole, geen statistisch onafhankelijk meetbewijs of garantie op besparing.
Engineeringdiagnostiek toont beide hellingen, `T0`, `C`, samples, spreiding, laatste update,
voorspellingsfout/bias en expliciete kwaliteitsredenen; er worden geen ongekalibreerde percentages
als confidence gepresenteerd.

De ontwerpnotitie verwijst naar
[`Appesteijn/stooklijn` thermal_model.py](https://github.com/Appesteijn/stooklijn/blob/462ef61f03b445e4ebeb34057410b7f3d6951482/custom_components/quatt_stooklijn/analysis/thermal_model.py).
Die implementatie is als technische referentie bekeken. OpenQuatt implementeert de energiebalans
zelf in C++; de MPC-output, horizon, forecasts en aanvoertemperatuursturing worden niet overgenomen.
De batch-ankering van `U` bij weinig warmtevraag is evenmin bruikbaar als kruisvalidatiebewijs.
De aanvullende RLS heeft geen pad naar `P_request`; `P_adaptive = 0` blijft gelden.

De firmware bewaart batchrecords én de 1R1C-leerstand in hetzelfde A/B-journal:
U/C-coëfficiënten, covariance, informatie voor kwaliteitsbeoordeling en sampletellers.
Er zijn twee slots van 8 KiB; schema 5 voegt 152 bytes toe aan het recordformaat.
Schrijven gebeurt alleen bij nieuwe leerdata, maximaal eenmaal per uur. Daardoor
kan een onverwachte herstart maximaal ongeveer een uur nog niet opgeslagen
leerwerk verliezen. Er is geen extra write in het OTA-pad.

Na herstart worden parameters en afgeronde 1R1C-perioden hersteld. Onvoltooide
meetintervallen en bootlokale tijdstempels worden niet hersteld; een nieuw geldig
interval is nodig voordat de 1R1C-beoordeling weer actueel kan zijn. Een bron- of
kalibratiewijziging onderbreekt alleen lopende intervallen en herbeoordeelt de fit;
het model en de afgeronde records blijven behouden. Ook een lange verwarmingspauze
wist het model niet. Wie na een fysieke wijziging opnieuw wil beginnen, gebruikt
`Leerdata wissen`. De bestaande begrensde recordselectie en maximale bewaartermijn
van 365 dagen blijven gelden.

Een nieuwe write raakt alleen het inactieve slot en wordt teruggelezen. Een
onderbroken write laat het vorige geldige slot beschikbaar. Opslagfalen stopt
verdere writes voor die boot; leren in RAM en de verwarmingsregeling blijven
werken. Er zijn geen extra NVS-transacties of automatische herstelpogingen.
Wissen heeft één pad: beide slots wissen en controleren, of een zichtbare fout
melden. Bij een mislukte reset kan na reboot oude historie terugkomen; de UI mag
daarom alleen na `cleared` succes melden.

Herstel vereist geldige UTC. Bestaande schema-4-batchrecords blijven leesbaar,
ook na een bronwissel; daarin stond nog geen 1R1C-checkpoint. Oudere onbekende
recordformaten of beschadigde inhoud worden niet als meetdata geïnterpreteerd.
De schema/algoritmecontrole blijft bestaan, maar de bronkeuze is geen voorwaarde
voor het behouden van historie. Een gewoon firmware- of webupdate wist geen data.
Automatisch toepassen blijft altijd uit.

Het vermogen gebruikt de ΔT per warmtepomp. Een verschil tussen HP1-uit en HP2-in
blokkeert leren niet: sensoren kunnen onderling afwijken. Controle/ijking van de
watertemperatuursensoren via het servicemenu kan de meetnauwkeurigheid verbeteren.

## Uitvoerbare replay

Firmware en replay gebruiken `tick_passive_runtime()` voor beide modellen en dezelfde fit-entrypoints. De CLI parseert CSV en schrijft resultaten; hij heeft geen eigen collector- of contextresetloop. Bouw bijvoorbeeld:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. scripts/power_house_learning_replay.cpp -o /tmp/power_house_learning_replay
/tmp/power_house_learning_replay --active-h 250 --active-t0 16 --reference-room-c 20 --reference-setpoint-c 20 snapshots.csv
```

De waarden in dit commando zijn synthetische voorbeelden. Gebruik de echte handmatige/actieve lijn en vastgelegde kamercontext voor een echte replay.

Het CSV-contract bevat deze header, in deze volgorde:

```csv
monotonic_ms,epoch_s,source_generation,physical_context_generation,control_generation,invalid_reasons,room_c,setpoint_c,outside_c,heat_to_water_w,mean_water_c
```

De replay leest dit historische CSV-formaat voor compatibiliteit, maar accepteert alleen rijen waarin de
drie revisionkolommen gelijk en niet nul zijn. In firmware en nieuw JSON-export bestaat uitsluitend
`context_revision`.

Temperaturen zijn in °C en vermogens in W; tijd is monotone milliseconden en UTC-seconden. Generatie 0 betekent onbekend. Voor historische data accepteert de CLI `--now-epoch` als expliciete analysetijd, zodat de bewaartermijn causaal kan worden gereproduceerd. Iedere rij vertegenwoordigt een snapshot met gecontroleerde provenance. Alleen een CSV-getal of `invalid_reasons=0` vormt geen bewijs dat de onderliggende bron actueel was. Een gewone HA-historie-export voldoet niet automatisch aan dit contract.

De CLI rapporteert JSON met aantallen, uitsluitredenen en advies/fitresultaten. `batch_status` en
`batch_advice_ready` beschrijven de structurele fit; `status`, `model_validation_status` en `advice_ready`
beschrijven de gecombineerde beoordeling. RLS-diagnostiek omvat `u_rls`, `c_rls_wh_per_k`,
`rls_readiness_reasons`, samples, spreiding, laatste update en pre-update residuals. Zonder geldige
RLS-updates zijn `u_rls` en `c_rls_wh_per_k` `null`, niet de startwaarden.
`model_difference_fraction` is een relatief verschil, geen confidencepercentage.
Ongeldige invoer is een procesfout; onvoldoende data of onvoldoende modelverbetering is een normale
analyse-uitkomst met `advice_ready=false`. Een oude CSV-rij met `invalid_reasons` wordt niet achteraf
geschikt gemaakt voor de dynamische route. De CLI past geen instellingen toe en bewaart geen model
op een controller.

De fitvelden `holdout_*_signed_bias_w` gebruiken **voorspeld minus gemeten**; positief betekent overvoorspelling. De toekomstige diagnostische prediction error hieronder gebruikt expliciet de omgekeerde richting. Onbezette temperatuurklassen worden als `null` geëxporteerd, niet als nulbias.

## Diagnostiek voor de toekomstige adaptive laag

De firmwarecollector bewaart naast de trainingsrecords een ring van 60 diagnostische samples (ongeveer tien minuten). De GET-export bevat expliciete validity, tijdsduur en bron-/control-generation. De replay-CSV blijft een afzonderlijk contract voor modelanalyse. De diagnostiek omvat:

| Waarde | Betekenis en herkomst |
| --- | --- |
| `P_base` | Actief structureel model op dezelfde control-tick; onderscheid ongeclipt model en geselecteerde feedforward |
| Werkelijk thermisch vermogen | Signed warmte over de bewezen meetgrens |
| `P_request` | Werkelijk begrensde wattvraag die naar dispatch gaat |
| Kamerfout | Gemeten kamertemperatuur versus gekozen setpoint; comfort memory afzonderlijk herkenbaar |
| Buitentemperatuur | Bestaande geselecteerde bronwaarde |
| Prediction error | Tijdgewogen `gemeten warmte - P_base`, met dekking en gemiddelde absolute fout |
| Richting en duur | Positieve/negatieve afwijkingsduur; onderbreken bij tijdgaten of contextwissel |
| Compressoractiviteit | Gemeten activiteit per unit, afzonderlijk van gevraagd/applied level |
| Ketelbijstook | Waargenomen status en bewijsniveau; een relaiscommando is geen gemeten warmte |
| Limiting states | Water-, elektrische en frequentiebegrenzing, tekort, service en incidenten |

Verzamel diagnostiek ook tijdens perioden die voor modeltraining afvallen. Anders verdwijnen slechte comfort- of begrenzingsperioden uit de evaluatie. Onbekende waarden hebben geen nulbetekenis. Dit observatiecontract introduceert geen adaptive aansturing en kan ook geen verborgen zon-, wind- of ventilatieoorzaak bewijzen.

## Vervolgstappen en vrijgavepoorten

| Stap | Resultaat | Voorwaarde vóór afsluiten |
| --- | --- | --- |
| 1. Fundament en passieve replay | Model/envelope, bronadapter, batch-fit, 1R1C/RLS, kruisvalidatie, regressies | Onafhankelijke review en hosttests; oude regeluitvoer behouden |
| 2. Bronmetadata en firmwarecollector | Werkelijke source-route/timestamps, Q/Waveshare-allowlist, opt-in, diagnostiek | Alle ondersteunde entry paths gecontroleerd; ontbrekend bewijs faalt gesloten; Listener bevat geen learner |
| 3. Persistente dataset en adviesbediening | Strikte PSRAM, begrensde export, A/B-journal en capability-aware UI volgens PR #633 | Reboot, allocatiefalen, CRC/schema, powercut, factory reset, upgrade/downgrade en archiefgrenzen getest |
| 4. Gecontroleerd toepassen | Eén modelowner, revisions, trial, bevestiging en rollback | Batch én 1R1C-kwaliteitscontrole, meetkwaliteit, echte winterreplay, HIL en comfortpoorten geslaagd |
| 5. Adaptive Power House | Tijdelijke power-bias met eigen confidence, limieten en decay | Aparte gesloten-lusvalidatie en aantoonbare meerwaarde uit diagnostiek |

Fase 1 van het product omvat stappen 1–4. Fase 2 is Adaptive Power House. Onderzoek naar `Kp` of reactieprofielen uit het oorspronkelijke plan blijft een afzonderlijk werkpakket; dit is niet automatisch onderdeel van de adaptive power-bias.

Voor latere activatie geldt: een complete, gevalideerde trial-intent met bevestigd herstelmodel moet aantoonbaar persistent zijn **vóór** de control-task de kandidaat activeert. Verlies van betrouwbare monitoring tijdens een trial moet herstel naar het bevestigde model geven. Een herhaalde request mag geen extra activatie of flashcyclus veroorzaken. Handmatige wijzigingen krijgen voorrang en mogen niet als een half samengestelde configuratie gecommit worden. Coalescing alleen bewijst geen volledige batch.

Bij toekomstige modelactivatie worden handmatige basis, actief en laatst bevestigd structureel model apart opgeslagen. Het huidige journal bevat alleen passieve meetrecords. Een toekomstige adaptive bias valt bij reboot standaard terug naar nul. Een tijdelijke afwijking wordt alleen na onafhankelijke, langdurige validatie input voor een nieuw structureel model.

Begrens toekomstige adaptive bias in beide richtingen en in verandering per tijdseenheid. Laat hem vervallen bij slechte metingen, storingen, overrides of ongeldige modus. Hij mag nooit de envelope of bestaande veiligheidsgrenzen verhogen. De kernrepresentatie van `HouseLine` en de dispatcharchitectuur blijven daarbij bruikbaar.

De hardwarepoort omvat Q Single/Duo en Waveshare Single/Duo: vier builds, met Q Wi-Fi/Ethernet samen zes runtimecases. Meet interne heap, grootste vrije blok, minimum-sinds-boot, stack-watermarks en timing onder gecombineerde belasting. Desktoptests of succesvolle configuratievalidatie vervangen deze metingen niet. Het 16 KiB learnerjournal ligt direct na het crasharchief binnen `openquatt_data`; bestaande archiefoffsets blijven behouden.

## Bediening en validatiestatus

Op Q en Waveshare staat onder Instellingen → Verwarmen → Power House **Passief leren**. Daar staan
opt-in, leerstatus, een handmatig te laden grafiek, export en wissen. De gewone geselecteerde bronwaarden blijven leidend; er is geen extra
formulier voor hydrauliek, warmtebronnen of meetgrenzen. Opt-in start na reboot uit. De reguliere
Power House-instellingen blijven leidend; automatisch toepassen is altijd uit.

Eén mainloop-leerkern beheert verzamelen, pauzeren, resetten, herstellen en beide modellen. De
bestaande bronselectie levert de werkelijk gekozen route, waarde en validity. Eén contextrevision
onderbreekt alleen lopende meetintervallen bij relevante wijzigingen, ook A→B→A tussen twee ticks.
De drie generatiekolommen blijven uitsluitend in het historische replay-CSV voor formaatcompatibiliteit;
firmware, records en JSON-export gebruiken `context_revision`.

De HTTP-component publiceert gesynchroniseerde snapshots in PSRAM. HTTP-callbacks lezen nooit
veranderende learnerstate. Status en export gebruiken bestaande webauthenticatie:

- `GET /openquatt/learning/status`: H/T0, U/C, voortgang, redenen en runtime-diagnostiek.
- `GET /openquatt/learning/export`: maximaal 128 batchrecords en 60 diagnostische rijen.

CM0, CM1 en CM2 gebruiken het bestaande geen-ketelvraagcontract; ontbrekende OpenTherm-telemetrie is geen extra
voorwaarde. Een actuele fysieke ketel-activiteitsmelding sluit de meting uit. Bronkwaliteit en het
gedrag van de residuals blijven nog praktijkwerk. Er zijn nog geen gevalideerde wintermodellen of
aangetoonde besparingen.

Een blijvende runtimeblokkade, bijvoorbeeld na teruglopende UTC, krijgt de reden
`runtime_blocked`. De web-app vraagt dan om de regelaar te herstarten en passief leren
opnieuw in te schakelen. Pauzeren heft deze blokkade niet op; opgeslagen leerdata
blijft bij herstart behouden.

### Historische smoke-test van 7 september 2026

De onderstaande resultaten horen bij de toenmalige vereenvoudigde build. Zij zijn
geen kwalificatie van de latere 128-recordcapaciteit, het schema-5-checkpoint of de
grafiek. Actuele vrijgavechecks en resterende hardwarepoorten moeten afzonderlijk
voor de uiteindelijke firmware worden vastgelegd.

De Q Duo-build is via OTA geplaatst als `Sep 7 2026 11:37:32 ph-passive-1`. API-kamer en API-setpoint
zijn tijdens CM2 als geldige geselecteerde learning-bronnen gezien; daarbij is ten minste één dynamische
learning-sample vastgelegd. Daarna zijn beide thermostaatbronnen teruggezet op `OT thermostat`, de
simulatorpomp is uitgezet en de controller stond in CM0. De toenmalige HIL-smoke meldde 39828 B minimum
interne heap, een grootste blok van 57344 B, 4480 B loopstackmarge en 46832 B learner-PSRAM. Dit is een
functionele smoke-test, geen kwalificatie onder belasting.

Deze vereenvoudiging is gecontroleerd met 83 C++-hosttests, 239 Python-contracttests en 457 webtests.
C++-format, docschecks, webbuild, smokecheck en controle van de gegenereerde assets slagen. De volledige
Q Duo Wi-Fi-build slaagt: 205815 bytes statisch RAM en 2261295 bytes applicatie-image. Die build bevat
ook de nieuwe bodemplaatinstellingen en herstartafhandeling uit `dev`; het verschil met een eerdere
firmwarebuild is daarom geen zuivere meting van deze vereenvoudiging.

De opslagtests injecteren erase-, partiële write-, verloren acknowledgement- en leesfouten en
controleren herstel na gesimuleerde reboot. De complete diff is afzonderlijk koud gereviewd; resetbevestiging,
bronwissels tussen leerticks en de previewreset zijn daarbij gecorrigeerd en getest. De synchrone fit
wordt pas overwogen na een meting op ESP32-S3; tot die tijd blijft de begrensde fit behouden.

Een schone webbuild van de toenmalige `dev` met dezelfde toolchain gaf 913958 bytes raw / 261411 bytes
gzip JavaScript. Deze feature geeft 924774 / 264797 bytes: +10816 raw en +3386 gzip (+1,30%), binnen
de relatieve gzipgrens van 4608 bytes. Het raw-budget houdt dezelfde 6000 bytes extra ruimte als de
eerdere featureversie, nu bovenop `dev`: 919000 → 925000. CSS blijft raw gelijk (195001 bytes).
De nieuwe previewreset was geautomatiseerd gecontroleerd; de browsermatrix en Safari/iOS waren
nog niet afgevinkt. De OTA-smoke-test bevestigt de nieuwe bronketen en één dynamische learning-sample.

De config-only projectwrapper heeft drie bestaande stijlmeldingen in `configs/hil/input_sources_fast_duo_wifi.yaml`
en `openquatt/oq_common.yaml`. Directe ESPHome-validatie blijft beschikbaar. Echte stroomonderbrekingen,
herstel op de controller en geheugen-/timingbelasting zijn nog niet op de vereenvoudigde build getest.
