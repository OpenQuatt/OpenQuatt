# Power House Auto-Tuning: implementatie en Adaptive-roadmap

## Status en uitgangspunt

Ontwikkelbasis: `dev` op `b4eb58c37034970f402dac5bde652611aca8a939`, met GitHub vergeleken op 6 september 2026. PR #633 is daarin gemerged; de [centrale webontwikkelafspraken](../openquatt/web/README.md) gelden voor de passieve learnerbediening. Ontwerpinput: *OpenQuatt Power House Autotuning Engineer Plan*, revisie 1.1, *Adaptive Power House* en de aanvullende *1R1C Design Note*. De 24 synthetische checks uit de overdracht zijn gereproduceerd. Ze bewijzen geen meetkwaliteit, firmwaregedrag of energiebesparing.

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

De kern gebruikt maximaal 64 records en 42 dagen historie. Een ontbrekend essentieel interval maakt het lopende segment ongeschikt. Normale uitperioden en signed calorimetrie blijven onderdeel van de tijdsintegratie. De dataset leert uit gemeten warmte; `P_request`, compressorlevels en de eigen woninglijn zijn geen trainingslabels.

Eén eigenaar in de ESPHome-mainloop beheert records en fitworkspace in PSRAM. Tijdens een hervatbare fit blijft de recordarray onveranderlijk; append/prune annuleert eerst de fit. HTTP-callbacks krijgen uitsluitend een onder mutex gekopieerde JSON-cache. Een tweede gelijktijdige export krijgt HTTP 429; netwerk-I/O houdt de cachemutex niet vast.

De actuele geselecteerde waarden blijven leidend voor bronkeuze. De resolvers leveren nu de werkelijk gekozen route, raw ontvangsttijd, exacte configuratiegeneratie en eventuele hold/synthesized-status. Fysieke aggregaten behouden beide ontvangstbewijzen en de toegepaste operator; onbewijsbare HA/API/MQTT-provenance blijft geblokkeerd. Een callback op een `*_selected`-republish geldt niet als nieuwe ontvangst. Zie [brononderzoek en integratiecontract](power-house-learning-sources.md).

De eerste ontwikkelcasus is door de gebruiker bevestigd als Duo in serie HP1 → HP2, zonder buffer of bypass, met water en geen andere warmtebronnen tijdens CM2. De verklaring geldt uitsluitend in CM2; R1-uit wordt nooit als fysiek ketelbewijs gebruikt. Flowkalibratie, calorimetrische onzekerheid en een grens aan zon/interne warmte zijn hiermee nog niet bewezen. De meetadapter neemt die eigenschappen uitsluitend als expliciet bevestigd contract aan. Ontbrekend bewijs levert geen nulvermogen of geschikt leersample op.

De getalsgrenzen voor stabiliteit, spreiding, meetonzekerheid en verbetering zijn ontwikkelinstellingen. Een succesvolle fit is geen gekalibreerd betrouwbaarheidspercentage en geeft geen toestemming tot automatisch toepassen. Onvoldoende geschikte data is een geldige uitkomst.

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
foute temperatuurtrend niet verbergen. Configuratiewijzigingen wissen het oude RLS-bewijs.

Zon en interne warmte ontbreken als regressorterm in deze MVP. Zonder expliciet onderbouwde grens
aan die ongemodelleerde winst mogen numerieke parameters worden geschat, maar blijft de RLS-
kwaliteitsvoorwaarde onvoldoende. De replay kan die **analyseaanname** krijgen via
`--rls-max-unmodeled-gain-w`; de standaard is onbekend. Een argument of een kleine residual bewijst
niet dat de werkelijke zon/interne winst zo klein was. De beleidsgrenzen zijn ontwikkelwaarden die
praktijkvalidatie vereisen. Er bestaat geen verplichte weersdienst of solar-input.

`oq_ph_model_validation.h` vergelijkt `H_batch` en `U_rls` alleen bij passende source-/physical-/
controlgeneraties, actuele RLS-gegevens en voldoende overlappend buitentemperatuurbereik. De
ontwikkelgrens voor het relatieve verschil is configureerbaar (standaard 20%); modellen worden nooit
blind gemiddeld. `T0` blijft uit de batch-fit komen en wordt nooit gelijkgesteld aan `Tin`.
`model_disagreement` of onvoldoende RLS-onderbouwing houdt het gecombineerde advies tegen.
`auto_apply_allowed` blijft in deze bouwstap altijd `false`, ook als beide modellen overeenkomen.

De geleerde `C` controleert ook quasi-stationariteit: `C * room_trend` schat per batch-record het
vermogen dat in warmteopslag verdwijnt of daaruit vrijkomt. Als het maximum over training én holdout
de ontwikkelgrens van 250 W of 10% van het gemeten warmtevermogen overschrijdt, wordt het gecombineerde
advies geblokkeerd met `thermal_storage_not_stationary`. Beide grenzen zijn configureerbaar; dit is
een modelmatige extra controle, geen aparte meting van opgeslagen energie.

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

De pure checkpointfunctie valideert schema, inhoud en configuratie en herstelt bij corruptie naar
startwaarden. De firmware bewaart uitsluitend batchrecords in het journal; RLS en adviesreadiness
worden na reboot opnieuw opgebouwd. Het journal heeft twee slots van 8 KiB, expliciete serialisatie,
schema/CRC, exacte contextvergelijking en maximaal 42 dagen historie. Een duurzame dirty-markering
gaat vooraf aan wijzigingen; writes en NVS-commits worden teruggelezen. Herstel vereist bekende UTC
en dezelfde firmwarebuild/context. Dit conservatieve testbeleid verwerpt historie bij firmwarewissel.
Powercutbestendigheid op echte hardware blijft een afzonderlijke vrijgavepoort.

## Uitvoerbare replay

De replay compileert dezelfde C++-headers als de hosttests; er is geen tweede Python-implementatie van de fit. Bouw bijvoorbeeld:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. scripts/power_house_learning_replay.cpp -o /tmp/power_house_learning_replay
/tmp/power_house_learning_replay --active-h 250 --active-t0 16 --reference-room-c 20 --reference-setpoint-c 20 snapshots.csv
```

De waarden in dit commando zijn synthetische voorbeelden. Gebruik de echte handmatige/actieve lijn en vastgelegde kamercontext voor een echte replay.

Het CSV-contract bevat deze header, in deze volgorde:

```csv
monotonic_ms,epoch_s,source_generation,physical_context_generation,control_generation,invalid_reasons,room_c,setpoint_c,outside_c,heat_to_water_w,heat_uncertainty_w,mean_water_c
```

Temperaturen zijn in °C, vermogens en onzekerheid in W; tijd is monotone milliseconden en UTC-seconden. Generatie 0 betekent onbekend. Voor historische data accepteert de CLI `--now-epoch` als expliciete analysetijd, zodat de 42-dagengrens causaal kan worden gereproduceerd. Iedere rij vertegenwoordigt een snapshot met gecontroleerde provenance. Alleen een CSV-getal of `invalid_reasons=0` vormt geen bewijs dat de onderliggende bron actueel was. Een gewone HA-historie-export voldoet niet automatisch aan dit contract.

De CLI rapporteert JSON met aantallen, uitsluitredenen en advies/fitresultaten. `batch_status` en
`batch_advice_ready` beschrijven de structurele fit; `status`, `model_validation_status` en `advice_ready`
beschrijven de gecombineerde beoordeling. RLS-diagnostiek omvat `u_rls`, `c_rls_wh_per_k`,
`rls_readiness_reasons`, samples, spreiding, laatste update en pre-update residuals. De onbekende
gainaanname is `null`; zonder geldige RLS-updates zijn `u_rls` en `c_rls_wh_per_k` ook `null`, niet de
startwaarden. `model_difference_fraction` is een relatief verschil, geen confidencepercentage.
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
| Werkelijk thermisch vermogen | Signed warmte over de bewezen meetgrens, inclusief meetonzekerheid |
| `P_request` | Werkelijk begrensde wattvraag die naar dispatch gaat |
| Kamerfout | Gemeten kamertemperatuur versus gekozen setpoint; comfort memory afzonderlijk herkenbaar |
| Buitentemperatuur | Geselecteerde fysieke bron met ontvangstbewijs |
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
| 3. Persistente dataset en adviesbediening | Strikte PSRAM, begrensde export, journal, begrensde RLS-checkpoints en capability-aware UI volgens PR #633 | Reboot, allocatiefalen, CRC/schema, powercut, factory reset, upgrade/downgrade en archiefgrenzen getest |
| 4. Gecontroleerd toepassen | Eén modelowner, revisions, trial, bevestiging en rollback | Batch én 1R1C-kwaliteitscontrole, meetkwaliteit, echte winterreplay, HIL en comfortpoorten geslaagd |
| 5. Adaptive Power House | Tijdelijke power-bias met eigen confidence, limieten en decay | Aparte gesloten-lusvalidatie en aantoonbare meerwaarde uit diagnostiek |

Fase 1 van het product omvat stappen 1–4. Fase 2 is Adaptive Power House. Onderzoek naar `Kp` of reactieprofielen uit het oorspronkelijke plan blijft een afzonderlijk werkpakket; dit is niet automatisch onderdeel van de adaptive power-bias.

Voor latere activatie geldt: een complete, gevalideerde trial-intent met bevestigd herstelmodel moet aantoonbaar persistent zijn **vóór** de control-task de kandidaat activeert. Verlies van betrouwbare monitoring tijdens een trial moet herstel naar het bevestigde model geven. Een herhaalde request mag geen extra activatie of flashcyclus veroorzaken. Handmatige wijzigingen krijgen voorrang en mogen niet als een half samengestelde configuratie gecommit worden. Coalescing alleen bewijst geen volledige batch.

Het journal houdt handmatige basis, actief en laatst bevestigd structureel model en metadata apart. Een toekomstige adaptive bias valt bij reboot standaard terug naar nul. Een tijdelijke afwijking wordt alleen na onafhankelijke, langdurige validatie input voor een nieuw structureel model.

Begrens toekomstige adaptive bias in beide richtingen en in verandering per tijdseenheid. Laat hem vervallen bij slechte metingen, storingen, overrides of ongeldige modus. Hij mag nooit de envelope of bestaande veiligheidsgrenzen verhogen. De kernrepresentatie van `HouseLine` en de dispatcharchitectuur blijven daarbij bruikbaar.

De hardwarepoort omvat Q Single/Duo en Waveshare Single/Duo: vier builds, met Q Wi-Fi/Ethernet samen zes runtimecases. Meet interne heap, grootste vrije blok, minimum-sinds-boot, stack-watermarks en timing onder gecombineerde belasting. Desktoptests of succesvolle configuratievalidatie vervangen deze metingen niet. Het 16 KiB learnerjournal ligt direct na het crasharchief binnen `openquatt_data`; bestaande archiefoffsets blijven behouden.

## Verificatie van bouwstap 1

Gecontroleerd op 6 september 2026:

- `bash scripts/run_host_regression_tests.sh`: 69 hosttests geslaagd, inclusief de bevroren legacyvergelijking en de volledige synthetische seriële-Duo-keten van ruwe metingen tot advies.
- Gerichte Python-contracten voor Power House, strategy, thermal request en de replay-CLI: 15 tests geslaagd. De 7 CLI-tests dekken ook corrupte invoer, NUL-bytes, ongeldige actieve modellen en cohortwissels.
- UBSan op de nieuwe leerkern, bronadapter en volledige ketentest: geslaagd. ASan kon in de lokale omgeving niet betrouwbaar worden uitgevoerd; er wordt geen ASan-resultaat geclaimd.
- `npm run check:cpp-format`, `npm run check:docs` en `npm run build:web`: geslaagd. De webbuild was nodig voor de niet-ingecheckte assets bij configuratievalidatie; webbroncode is niet gewijzigd.
- Rechtstreekse `esphome config` met `esphome==2026.8.2`: Q Duo via `configs/heatpump_controller_q/duo_wifi.yaml`, Q Single, Waveshare Single/Duo en Listener Duo geslaagd. Dit zijn configuratiechecks, geen featurebuilds of hardwarekwalificatie.
- De integrale `scripts/dev.py validate --config-only` stopt vóór de configuratiecheck op bestaande stijlmeldingen in `configs/hil/input_sources_fast_duo_wifi.yaml:26` en `openquatt/oq_common.yaml:723,840`. Deze ongewijzigde baselinebestanden zijn buiten deze bouwstap gehouden.

De aparte koude review omvatte alle gewijzigde vraagpaden, numerieke gelijkwaardigheid, bronkwaliteit en tijdgaten, bron-/contextwissels, dataset- en parsergrenzen, holdoutlekken en onterecht positieve adviezen. Gevonden problemen zijn opgelost en met negatieve tests gecontroleerd. De leerkern blijft passief en allocation-free; de host-sizeguard is geen ESP32-geheugenmeting.

## Verificatie van de 1R1C- en bronmetadatastap

Gecontroleerd op 6 september 2026, boven op de eerste bouwstap:

- Alle 75 C++-hostregressies geslaagd. Gerichte UBSan-controles op RLS, thermische aggregatie,
  modelvalidatie, batch-fit, volledige bronketen en generatie-eigenaarschap zijn groen.
- 18 gerichte Python-tests voor replay, raw receipt-wiring en profieluitsluiting geslaagd.
  De volledige synthetische replay leert `H/U = 200 W/K` en `C = 6000 Wh/K`; zonder onderbouwde
  gainaanname, bij veroudering of bij teruglopende generaties volgt geen gecombineerd advies.
- C++-formatcontrole (255 bestanden), documentatiecontracten en `git diff --check` geslaagd.
- Volledige ESPHome 2026.8.2-testcompile van `configs/heatpump_controller_q/duo_wifi.yaml` geslaagd.
  De compile controleert ook de passieve leerkern op de Xtensa-toolchain. De regels blijven handmatig;
  er is geen collector- of apply-runtime geactiveerd en er is niets geflasht.
- Configuratiechecks van Q Single/Duo, Waveshare Single/Duo en Listener Duo geslaagd. Compilerprobes
  bewijzen bovendien dat de learningtypen ontbreken zonder de expliciete firmwareprofielpoort.
- De aparte koude reviews omvatten echte ontvangst versus republish/afwijzing, offline en ACK-correlatie,
  64-bits ontvangsttijd, ownership en late events, numerieke gezondheid, tijdnormalisatie, checkpoint-
  herstel, onterecht positieve kruisvalidatie en temperatuurtrend versus warmteopslag. Bevindingen zijn
  opgelost en met gerichte regressies gecontroleerd.

De ontvangstmetadata heeft kleine, vaste runtimeopslag. Een geslaagde compile bewijst geen interne-
heapmarge of gedrag onder gecombineerde belasting. HIL en vergelijking met een identieke baseline
blijven vereist vóór firmwarevrijgave. De RLS-checkpoint is nog een gevalideerd geheugenobject,
geen binaire flashrepresentatie; raw structdumps zijn geen ondersteund opslagformaat. Een restore
levert geen recente meetvaliditeit op. De hieronder beschreven firmwarestap voegt rebootownership,
selected-bronbinding, collector, PSRAM-opslag, journal en bediening toe. Echte winterreplay,
powercutproeven en comfort-/energiebesparingsvalidatie blijven open. Automatisch toepassen en
rollback volgen pas na die verificatie.

## Passieve firmware en bediening

Onder Instellingen → Verwarmen → Power House staat **Passief leren** op Q en Waveshare.
Het vaste installatiecontract is water; Single/Duo komt uit het firmwareprofiel en Duo gebruikt
altijd serie HP1 → HP2. Hiervoor bestaan geen selects meer. Ook de ketelbijdrage wordt automatisch
uit de bestaande regeling en ketelkoppeling afgeleid. Calorimetrische bevestiging, vermogensonzekerheid, maximumflow, junction-tolerantie
en de analysegrens voor ongemodelleerde warmte blijven interne engineeringconfiguratie en
kwaliteitsvoorwaarden; ze worden niet als invulvelden in de web-app aangeboden.
Een onbekende voorwaarde blijft zichtbaar als blokkade. Numerieke RLS-schattingen zijn voorlopig
tot de afzonderlijke kwaliteitscontroles slagen. `auto_apply_allowed` is altijd `false`.

`Power House Passive Learning` en `Power House Learning Calorimetry Confirmed` starten na elke reboot
uit. Andere meetcontractinstellingen herstellen via de bestaande ESPHome-instellingenopslag.
Leerdata wissen pauzeert de collector, wist uitsluitend de twee learnerslots en verifieert de erase;
het wijzigt geen Power House-basisinstellingen of andere archieven. Zonder PSRAM start de collector
niet; er is geen fallback naar interne DRAM.

De bestaande webauthenticatie beschermt beide read-only endpoints:

- `GET /openquatt/learning/status`: status, bronroutes, batch- en RLS-uitkomsten, expliciete
  blokkeerredenen en interne geheugen-/timingdiagnostiek.
- `GET /openquatt/learning/export`: begrensde dataset en diagnostische ring met kolomdefinities.

Voor de eerste installatie is bevestigd dat er tijdens CM2 geen andere warmtebron actief is.
De extra learnerselect is verwijderd; de actuele CM2-ketelvoorwaarden worden automatisch gecontroleerd.
Serie HP1 → HP2 en water volgen voortaan vast uit het installatiecontract. Calorimetrische bevestiging en onzekerheidsbudget worden
niet ingevuld om een blokkade kunstmatig te omzeilen. Het testen van de passieve keten kan beginnen
terwijl de meetkwaliteit nog onvoldoende is; een bruikbaar model vereist echte geschikte verwarmingsdata.

Voor een productvrijgave blijven koude starts, stroomonderbreking tijdens flashwrites, gelijktijdige
HA/web/API/MQTT/Modbus/OpenTherm/OTA-belasting en identieke baseline/candidate-geheugenmetingen op alle
ondersteunde profielen nodig. Een Q Duo-testcompile of één OTA-run vervangt die HIL-matrix niet.

## Verificatie van de passieve firmwarestap

Gecontroleerd op 6 september 2026:

- 81 C++-hostregressies en 236 Python-contracttests geslaagd; de gerichte receipt/profile/replayset
  bevat 18 geslaagde checks. Gerichte UBSan-controles op journal en passieve runtime zijn groen.
- Koude review en negatieve regressies omvatten bronwissels, ingetrokken fysieke receipts,
  CM2 → CM0 → CM2 zonder verlies van geldige historie, verkeerde strategy-owner, onbekende
  OpenTherm-status, monotone generaties, tijdgaten en onterecht positieve modelreadiness.
- Journaltests dekken corruptie, truncatie, schema/count/context, toekomst/veroudering, onderbroken
  slotwrites, gelijke sequences en behoud van bestaande state bij geweigerd herstel. De firmware
  verifieert NVS met close/reopen en flash met readback; fysieke powercuts zijn nog niet getest.
- De exporttest gebruikt dezelfde begrensde JSON-writer en kolomconstanten als de firmware. De
  conservatieve maximale export met 64 records en 60 diagnostische rijen is 24462 bytes binnen 24 KiB.
  Overloop publiceert een korte foutstatus, nooit een eerdere positieve uitkomst.
- 436 webtests, `npm run smoke:web`, bundle `--check`, C++-format en documentatiechecks zijn groen.
  Het raw-JS-budget stijgt van 905000 naar 911000 bytes voor de nieuwe lokale status/bediening;
  de bundel na de pre-PR-webreview is 910637 bytes raw en 260549 bytes gzip. Een schone build van
  `origin/dev` op dezelfde toolchain geeft 899821 bytes raw en 257173 bytes gzip. De gzipgroei is
  3376 bytes (1,313%), binnen de relatieve grens van 4608 bytes. Het absolute gzipbudget blijft
  behouden; het raw-budget heeft nog 363 bytes ruimte.
- Chrome: preview én productiebundels, desktop licht en mobiel 390 px donker, geen horizontale
  overflow of consolefouten; invoerfocus/draft behouden tijdens live verversing, JSON-download en
  reset naar nul records gecontroleerd. Safari/iOS zijn niet getest. De in-app browser kon niet
  starten door een lokale codesignfout; Chrome was de beschikbare fallback.
- ESPHome-configuraties Q Single/Duo, Waveshare Single/Duo en Listener Duo zijn groen. De volledige
  firmwarebuild wordt voor Q Duo Wi-Fi uitgevoerd; andere profielen hebben in deze stap geen
  volledige C++-compile of hardwarekwalificatie gekregen.

## Eerste Q Duo-hardwaretest

De testfirmware is via native ESPHome OTA naar de bevestigde Q Duo-controller op `openquatt.local`
gestuurd. De build is herkenbaar aan `ph-passive-1` met compiletijd in de statusendpoint.
Alle 133 bestaande, beschikbare bedieningsinstellingen zijn na de eerste OTA teruggelezen en gelijk
gebleven. De nieuwe instellingen staan op `Series HP1 to HP2`, `Water` en `No other heat in CM2`.
Calorimetrische bevestiging, onzekerheidsbudget en gainaanname blijven onbekend.

Status en export geven geldige JSON met `auto_apply_allowed=false`. Twaalf read-only requests met
twee gelijktijdige clients slaagden tijdens een open web-app. De runtime gebruikt 56112 bytes PSRAM;
de aparte endpointbuffers gebruiken samen 53248 bytes PSRAM. Tijdens deze lichte belasting werd
104155 bytes vrije interne heap, een minimum-sinds-boot van 39828 bytes, een grootste intern blok
van 47104 bytes en 3648 bytes mainloop-stackmarge gemeten. De maximale gemeten learnertick was
20004 microseconden, inclusief eerste initialisatie. Dit is geen worst-case kwalificatie.

De eerdere firmware gaf in standby 108435 bytes vrije interne heap, 39828 bytes minimum en een
grootste intern blok van 63488 bytes. De oude firmware en kandidaat hadden geen identieke bootduur
en webbelasting; deze waarden bewijzen daarom geen volledige baseline/candidate-geheugenkwalificatie.
Er trad bij deze test geen zichtbare allocatiefout of herstart op.

De echte learnerreset zet opt-in direct uit en levert een lege export; de latere status bevestigt
`journal_status=cleared` en nul records. Bestaande instellingen en archieven zijn geen resetdoel.
In CM0 zijn er terecht geen trainingsrecords: de beperkte geen-externe-warmteverklaring geldt alleen
in CM2 en de calorimetrie is nog niet onderbouwd. De test bewijst bediening en blokkades, geen geleerd
wintermodel of energiebesparing.

De definitieve OTA-build is live geverifieerd als `Sep  6 2026 12:21:07 ph-passive-1`.
SHA-256 van de geüploade OTA-image:
`9a2f368bc63e3160903f046e7d31cc962fed832fe4e7eefebd327e9eba67843a`.
Na deze herstart bleven de drie bevestigde installatiekeuzes behouden en stonden zowel passieve
opt-in als calorimetrische bevestiging uit. Daarna is alleen passieve opt-in weer ingeschakeld.
De eerste status van deze boot gaf 104719 bytes vrije interne heap, 59392 bytes grootste intern
blok, 39828 bytes minimum, 3552 bytes stackmarge en 8965 microseconden maximale learnertick.
Automatisch toepassen bleef in alle gecontroleerde antwoorden `false`.

## Vereenvoudiging van de bediening

Water en de seriële Duo-volgorde HP1 → HP2 zijn vaste projecteigenschappen. De firmware verwijdert
de eerdere hydrauliek- en vloeistofselects; achtergebleven NVS-waarden worden niet gelezen. De
nieuwe buildcontext voorkomt dat een oudere leercontext alsnog actief wordt. Single krijgt de
Single-meetgrens; Listener krijgt nog steeds geen learner.

De normale webbediening bevat geen formulier voor meetonzekerheid, maximumflow, junction-tolerantie,
gainaanname of een calorimetriecheckbox. Die waarden worden hiermee niet automatisch bevestigd.
De bestaande firmware-entities voor engineering blijven beschikbaar; een onderbouwde serviceflow
voor meetnauwkeurigheid is nog vervolgwerk. De bestaande waterkalibratie bewijst uitsluitend
temperatuuroffsets en geldt niet als volledige flow-/warmtekalibratie.

Deze vereenvoudiging is getest met 433 webtests, de websmoke- en assetcontroles, docscontrole,
C++-formatcontrole, gerichte hosttests en configvalidatie voor Q Single en Q Duo. De Q Duo-firmware
compileert en is via OTA geïnstalleerd op de testcontroller. De live build is
`Sep  6 2026 14:25:56 ph-passive-1`; SHA-256 van de OTA-image:
`b562eba597639609de0663e01bb5189f4336812896fe1c01cdf8bfc2fbe539e0`.

Na de herstart waren 139 van de 140 vergeleken waarden gelijk; uitsluitend passieve opt-in stond
zoals bedoeld uit. De verwijderde hydrauliek- en vloeistofentities waren niet meer aanwezig.
De verklaring `No other heat in CM2` bleef behouden, calorimetrie bleef onbevestigd en automatisch
toepassen bleef `false`. De webapp is mobiel met productieassets en live op de controller
gecontroleerd; de gecontroleerde browserconsole bevatte geen fouten of waarschuwingen.
De status gaf 104591 bytes vrije interne heap, 61440 bytes grootste intern blok, 39828 bytes
minimum, 3648 bytes loop-stackmarge en 56112 bytes runtime in PSRAM. Dit is een beperkte
OTA-verificatie; de eerder genoemde volledige geheugen- en winterkwalificatie blijft open.
Daarna is alleen passieve opt-in via de webapp hersteld; de status bevestigt `enabled=true`,
`storage_ready=true`, nul records en `auto_apply_allowed=false`. In CM0 blijven de
meetkwaliteits- en warmtebijdrageblokkades van kracht.

## Automatische controle van ketelbijdrage

CM2 is `Heating - Heat Pump Only`. De regeling trekt ketelopdracht en uitgangen in voordat zij
van ketelbedrijf naar CM2 overgaat. Daarom is de aanvullende learnerselect `Andere warmtebron`
verwijderd uit firmware, webbediening, polling en backupvelden. Oude opgeslagen selectwaarden
worden genegeerd. De reguliere instellingen voor de daadwerkelijk aangesloten ketel blijven leidend.

De leerfunctie vereist CM2 in verwarmingsbedrijf en een actuele, geldige `COMMAND_SOURCE_NONE`
zonder demand, heat request, actieve keteluitgang, R1 of nog toegepaste OT-aanvraag. Ontbrekende/stale opdrachten, pauze,
transportwissel, rearm en verbindingsmismatch blokkeren. `CONTROL_CONTRACT` onderscheidt dit
regelbewijs van een fysieke receipt; het geldt uitsluitend voor het boiler-veld met `NO_HEAT`.
Het CM2-contract is leidend voor zowel R1 als OpenTherm. Bij een geselecteerde OpenTherm-ketel
sluit een verse fysieke Status READ_ACK met CH/flame-activiteit de betreffende meting uit.
Ontbrekende, ongeldige of verouderde OT-telemetrie vormt geen extra voorwaarde voor CM2. `oq_boiler_transport_active` duidt CH-activiteit aan en wordt
daarom niet als beschikbaarheid van een inactieve OpenTherm-link gebruikt.

CM2 bewijst geen afwezigheid van zon, interne warmte of onafhankelijk aangestuurde warmtebronnen
(ook niet achter R2). Die modelonzekerheid en het calorimetriebewijs blijven afzonderlijke gates.
Deze wijziging bedient geen actuatoren en verandert geen verwarmingsregeling.

Het controllercontract bewijst geen fysiek uitgedoofde ketel bij linkverlies vanuit actief OT-bedrijf.
Na lokale intrekking kan de ketel nog op zijn eigen communicatietime-out wachten. Daarom mag dit
contract niet worden uitgelegd als onafhankelijk gemeten nul-ketelvermogen; voor modelkwalificatie
blijven de calorimetrie en ongemodelleerde-warmtevoorwaarden vereist. Automatisch toepassen staat uit.

De gebruiker heeft bevestigd dat de geselecteerde lokale PT1000 in de gezamenlijke aanvoer ná HP2
én ketel zit; de actuele bronselectie is `Local` / `PT1000` en de ketelkoppeling is `R1`.
Daarmee is `T_supply_PT1000 - T_HP2_out` een kandidaat voor detectie van extra warmte tussen de
Duo-uitlaat en de woningaanvoer. Bij Single is HP1 out de referentie. Dit is nog geen nieuwe
leerkwaliteitgate: daadwerkelijke bron/fallback, raw ontvangsttijden, debiet, transportvertraging,
temperatuuroffsets en onzekerheid moeten eerst in die vergelijking worden vastgelegd. Een enkel
positief verschil of een nulverschil bewijst op zichzelf geen ketelactiviteit of afwezigheid daarvan.

Validatie van de vereenvoudiging: 13 learner-hostbinaries, 9 profiel-/receipt-contracttests,
433 webtests, websmoke, C++-formatcontrole, docs- en assetcontrole geslaagd. Q Single-configvalidatie
en volledige Q Duo-compilatie geslaagd. De cold review is apart uitgevoerd; het gevonden venster
met nog toegepaste OT-aanvraag is afgedekt met een verplicht contractveld en negatieve regressietest.
OTA-image `Sep  6 2026 15:26:14 ph-passive-1` is verstuurd met SHA-256
`7d908ce9b7e4c7069a6f633655c4030245fd92d3d60e8260b9030f0cc3b05077`.

Live OTA-verificatie: build-identiteit bevestigd; alleen de verwijderde `externalHeat`-entity
ontbreekt. Van de resterende 139 gecontroleerde waarden bleven 138 gelijk en stond uitsluitend
passieve opt-in na reboot uit. Calorimetrie bleef onbevestigd, records bleven nul en automatisch
toepassen bleef uit. De live webapp toont geen andere-warmtebronveld; de browserconsole is schoon.
Vrij intern geheugen 105067 bytes, grootste intern blok 61440 bytes, minimum 39904 bytes,
loop-stackmarge 3488 bytes en runtime-PSRAM 56112 bytes. Dit vervangt geen volledige HIL-
geheugenkwalificatie. De ΔT-controle is in deze build nog niet geïmplementeerd.

## Vastlegging als draft PR naar dev

De volledige wijziging is voor publicatie opnieuw tegen `dev` bekeken, verdeeld over modelcode,
firmware/opslag en webbediening. Daarna is een afzonderlijke koude review van de complete diff uitgevoerd.
De audit omvat ook bronontvangst, veilige blokkades, gelijktijdige HTTP-aanvragen, herstel na reboot,
schema/buildcompatibiliteit, nieuwe installaties, gegevens wissen en de bestaande handmatige regeling.

De pre-PR-correcties behandelen beschadigde modelwatermarks, de inclusieve 42-dagengrens
(maximaal 43 verschillende UTC-dagen), gecombineerde batch/dynamische blokkeerredenen en het
vermijden van een tijdelijke heapallocatie voor de buildidentiteit. De web-app stopt polling bij
stooklijn en beschermt wissen met bevestiging, afhandeling van late antwoorden en controle op het
werkelijke wisresultaat. Opslagherstel en reset zijn aanvullend met geïnjecteerde lees-, schrijf-, teruglees- en NVS-fouten getest.
De koude review vond bovendien dat de diagnostische ring tijdens pauze bleef verzamelen; ook die
ring is aan opt-in gebonden, met een onderbreking van de dekking bij pauze en hervatten.

De uiteindelijke hostrun slaagt met 81 binaries; de Python-contractsuite met 236 tests.
De volledige Q Duo Wi-Fi-build met ESPHome 2026.8.2 slaagt na de laatste auditfixes:
205679 bytes statisch RAM en 2258747 bytes applicatie-image. Dit compileverslag is geen meting
van de beschikbare interne heap tijdens bedrijf en vervangt hardwarekwalificatie niet.

De config-only projectwrapper stopt op drie stijlmeldingen die al in de ongewijzigde `dev`-bestanden
staan: `configs/hil/input_sources_fast_duo_wifi.yaml` (entity category) en twee lambda-commentchecks
in `openquatt/oq_common.yaml`. De directe ESPHome-configchecks van Q Single/Duo en Waveshare
Single/Duo slagen. De checker is uitsluitend aangevuld voor de nieuwe Q-profielsectie en packagevolgorde.

De eerdere OTA- en browsermetingen hierboven horen bij de genoemde eerdere testbuilds.
De laatste auditcorrecties zijn nog niet opnieuw naar de controller gestuurd. De extra Chrome-
previewcontrole van het resetvenster liep vast in de browserautomatisering; de nieuwe resetpaden
zijn wel onderdeel van de 436 geslaagde webtests. Een volledige browsermatrix en Safari/iOS blijven
onderdeel van vrijgave. Deze draft claimt geen afgeronde hardware- of winterkwalificatie.
