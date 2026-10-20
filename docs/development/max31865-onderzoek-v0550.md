# MAX31865 foutonderzoek en testplan voor v0.55.0

Onderzoek van 20 oktober 2026 op `v0.55.0` (`dbe66c23311e8c6713d8f51fc8f7f45309fdccb8`), vergeleken met `v0.54.1` (`7b1d448b42ca1f36ef99f1c7ea368471add96507`).

De timingfix wordt als testbare wijziging tegen `dev` aangeboden. Het gewone Q-profiel activeert de aangepaste sequentie voor Single en Duo; het diagnoseprofiel kan dezelfde component terugzetten naar de oorspronkelijke meetsequentie voor gecontroleerde vergelijking. De oorzaak blijft een hypothese totdat de hardwaremetingen zijn uitgevoerd.

De sterkste onderzoekslijn is een **bestaande, voor het analoge ingangsfilter onvoldoende gespecificeerde driversequentie**. Het aangeleverde schema maakt deze hypothese concreet. De gebruiker heeft op 20 oktober 2026 bevestigd dat de referentieweerstand van **1500 Ω in firmware klopt**; de eerder genoemde mogelijke Rref-mismatch vervalt voor de onderzochte hardware. Een oorzakelijke regressie in v0.55.0 is nog niet bewezen; voedings-/contactstoring en beschadigde SPI-reads blijven mogelijk. Korte PT1000-uitval was al gemeld op v0.45.2, maar die melding bevat geen bewijs van precies dezelfde MAX31865-foutbits. De nu gemelde toename verdient daarom een gecontroleerde meting, ook als eerdere firmware op dezelfde installatie probleemloos leek.

## Verschillen tussen de releases

| Onderdeel | Gecontroleerde bevinding | Betekenis |
| --- | --- | --- |
| ESPHome | Beide tags pinnen `esphome==2026.9.0` in `.github/requirements-esphome.txt`. | Geen nieuwe upstream MAX31865-driver door deze versiepin. |
| Lokaal sensorblok | Vanaf `select:` is het oude blok byte-identiek aan `profiles/heatpump_controller_q/local_sensors.yaml`, afgezien van de nieuwe bestandskop. | PT1000-configuratie, bereikfilter, pollinterval en logsuppressie zijn gelijk. |
| Netwerk | `connection/wifi_eth.yaml` en `OpenQuattNetworkManager.cpp` hebben in beide tags identieke Git-blobs. De oude `duo_wifi.yaml` includeert al `duo.yaml`, met de expliciete toelichting dat WiFi/Ethernet-assets aliassen zijn. | Gecombineerde initialisatie is **geen nieuwe wijziging tussen deze tags**. Oude ongebruikte WiFi-only/power-downbestanden zijn verwijderd. Controleer bij melders wel de werkelijk geïnstalleerde build/herkomst. |
| SPI | MAX31865: SPI3, MOSI GPIO7, MISO GPIO4, SCLK GPIO6, CS GPIO5; 4 MHz mode 1 volgens de upstream driver. W5500: GPIO10/11/12/13, IRQ GPIO9. | Het gepinde Ethernet-codegen kiest op ESP32-S3 standaard SPI2; de driver initialiseert die host. Geen gedeelde host/pinnen aangetoond. |
| Selector | `oq_sensor_sources.yaml`, `oq_sensor_source_runtime.h` en `oq_supply_hold_logic.h` zijn functioneel ongewijzigd. | Fallback/hold verklaart geen nieuwe analoge foutbits. |
| Logging | `logger` blijft DEBUG gecompileerd, INFO bij start, met runtime-taglevels. De bestaande eerste-fout-/herhaalfoutonderdrukking verhuisde ongewijzigd. De wijziging in log history betreft HTTP-foutresponses. | Geen aangetoonde nieuwe MAX31865-logzichtbaarheid. Logs tellen geen volledige foutfrequentie: herhaalde fouten worden onderdrukt. |
| Scheduling en belasting | Sensoren zijn een apart package geworden. Recorder/configuratiesnapshots en meerdere verwarmings-/actuatorpaden veranderden. | Registratievolgorde, relatieve 5s-fasen, CPU/PSRAM/HTTP-belasting en werkelijk pomp-/compressorgedrag kunnen verschillen. Een daardoor hogere foutkans is nog een hypothese. |
| NVS | De Q-partitie groeit via bootmigratie naar 64 KiB. | Geen direct pad van deze eenmalige migratie naar herhaalde RTD-faults aangetoond. Alle testvarianten blijven op v0.55.0; geen standaarddowngrade. |

De zelfstandige automatische MAX31865-zelftest gebruikt interne wachttijden. CPU-scheduling rekt die interne 100 µs niet op. Scheduling beïnvloedt wel de tijd tussen hostcommando's, netwerklast/voedingsruis en wanneer de selector een mislukte sample ziet. Een softwarewijziging in de regeling kan ook de werktemperatuur of elektrische schakelmomenten veranderen.

Code: [oude WiFi-alias](https://github.com/OpenQuatt/OpenQuatt/blob/v0.54.1/configs/heatpump_controller_q/duo_wifi.yaml), [netwerkprofiel](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/connection/wifi_eth.yaml), [Q-profiel](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/profiles/heatpump_controller_q.yaml), [lokale sensoren](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/profiles/heatpump_controller_q/local_sensors.yaml#L94-L193), [recorder](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp#L944-L994), [releasevergelijking](https://github.com/OpenQuatt/OpenQuatt/compare/v0.54.1...v0.55.0).

## Wat de driver en foutmeldingen aantonen

De [ESPHome-driver](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/max31865/max31865.cpp) schrijft bij 50 Hz/tweedraadsconfiguratie achtereenvolgens:

1. `0x87`: BIAS aan, fault-clear en automatische fault-detect in dezelfde write.
2. Poll configuratieregister tot D3:D2 nul is; afbreken na 6000 µs als die bits nog staan.
3. Direct `0xA1`: BIAS aan en one-shot conversie.
4. Met een scheduler-timeout van 66 ms: RTD-register lezen, BIAS uit (`0x01`), fault-status lezen.
5. Fault-status D2–D5: `NaN` publiceren en component Error zetten. Bij de eerstvolgende geslaagde cyclus wordt Error gewist.

Een gewiste Error na vijf seconden betekent een volgende cyclus zonder deze analoge Error of volledig 0/1-register. Het YAML-bereikfilter kan de berekende temperatuur daarna nog ongeldig maken. De fout wordt niet vijf seconden continu gemeten. D3 (`0x08`) en D4 (`0x10`) zijn comparatorresultaten met de interne FORCE−-schakelaar geopend tijdens de zelftest. Ze bewijzen geen losgeraakte sensorkabel. Ook een aannemelijke ADC-code kan samengaan met deze zelftestbits.

De ESPHome-scheduler plant op gehele milliseconden. Een 66ms-timeout kan daardoor vanaf de conversiewrite circa 65–66 ms plus loopvertraging betekenen; het is geen gegarandeerde ondergrens van 66 ms. Meet `conv_us` en zo mogelijk DRDY op de chip. Een afzonderlijke proef met een paar milliseconden extra vóór het lezen kan te vroege/oude ADC-data onderscheiden. Dit is eveneens bestaand upstreamgedrag en verklaart op zichzelf geen D3/D4-comparatorbits.

De driver handelt de bits af die hij uit register `0x07` leest. De screenshots bewijzen niet dat die bytes correct over SPI zijn ontvangen. Dubbele reads en een logic-analyzeropname moeten dat onderscheiden. Een reproduceerbare `0x08`/`0x18` met bijbehorende RTD-faultbit en correcte configuratie ondersteunt echt gelatchte chipstatus; wisselende bytes zonder nieuwe conversie, onmogelijke configuratie of gereserveerde bits ondersteunen een digitaal leesprobleem. Over-/onderspanning kan een fault tussentijds werkelijk laten veranderen; een verschil tussen reads is daarom op zichzelf geen sluitend bewijs van SPI-corruptie.

## Schema en timing

Het aangeleverde schemafragment toont C6 = **100 nF tussen RTDIN+ en RTDIN−**, FORCE+/RTDIN+ gekoppeld, FORCE−/RTDIN− gekoppeld, FORCE2 aan GND, en twee voedingsontkoppelingen van 100 nF. Dat past bij tweedraadsmeting. De netnaam `SPI2_*` in het schema is geen bewijs van de ESP32-hostkeuze.

Voor de onderzochte hardware is **Rref = 1500 Ω bevestigd** en komt die overeen met de firmware. De labels R12 = 1 kΩ en R13 = 3 kΩ in het aangeleverde schemafragment zijn daarmee geen basis om van een werkelijk bestukte 4 kΩ uit te gaan. De Rref-mismatch is geen open onderzoekslijn meer; er is geen reden om de firmwarewaarde of kalibratie hiervoor te wijzigen. Met een bekende 1000 Ω testweerstand verwacht je idealiter RTD-data rond `0xAAAA`, afgezien van de faultbit. Printrevisie en C6-bestukking/-tolerantie blijven relevant voor de timingvergelijking.

De datasheet stelt verschillende eisen: vóór one-shot na BIAS-aan `10,5τ + 1 ms`; na zelftest minimaal `5τ + 1 ms`; bij een filtertijdconstante boven 100 µs handmatige foutdetectie, met minimaal `5τ` vóór en tussen de handmatige stappen. Fault-clear schrijft D1 terwijl D5/D3/D2 nul zijn. De huidige driver borgt deze scheiding en wachttijden niet. De wachttijd van 66 ms **na** conversiestart vervangt geen settling **vóór** het bemonsteren. Bron: [datasheet, p. 13–14 en 19](https://www.analog.com/media/en/technical-documentation/data-sheets/max31865.pdf).

Een eenvoudige circuitbenadering bij de bevestigde Rref = 1500 Ω en C6 = 100 nF geeft tijdens normale bias ongeveer `(Rref || R_PT1000) × C6`: circa 60 µs bij 0 °C en 66,5 µs bij 50 °C. Deze normale tijdconstante ligt dus onder 100 µs. Bij het openen van FORCE− verandert het circuit: de differentiële condensatorspanning kan via de PT1000 afbouwen, met ongeveer `R_PT1000 × C6`. Dat is rond 100 µs bij 0 °C en 119 µs bij 50 °C; bedrading, toleranties en interne schakelaars zijn hierin niet verwerkt. De bevestigde referentieweerstand neemt de timinghypothese daarom niet weg: de relevante stroomweg tijdens de zelftest verschilt van die tijdens normale bias. Deze berekening ondersteunt een marginale automatische test, vooral bij warm water; het is geen gemeten tijdconstante van elke printrevisie. D4 kan bovendien op gemeenschappelijke settling of storing wijzen, die dit eenvoudige differentiële model niet volledig beschrijft.

## Gevolgen voor regeling

| Signaal/pad | Gedrag bij een mislukte PT1000-cyclus |
| --- | --- |
| RTD-bytes | Kunnen een aannemelijke weerstand plus faultbit bevatten; de diagnosepatch bewaart beide reads. |
| `water_supply_temp_pt1000` | Driver publiceert `NaN` bij D2–D5 of een volledig 0/1-register. Het YAML-filter maakt ook temperaturen buiten −10…100 °C ongeldig. D6/D7 zijn upstream waarschuwingen; die semantiek blijft behouden. |
| `water_supply_temp_esp` | Neemt bij selectie PT1000 de waarde over, inclusief `NaN`. Geen automatische omschakeling naar DS18B20. |
| `water_supply_temp_selected` | Bij ongeldige gekozen bron eerst verse ODU-fallback: Duo uitsluitend HP2, Single HP1; online, eindige waarde en maximaal 60 s oud. Daarna pas dezelfde-bron-hold, standaard 15 s voor Local/CIC en 300 s voor HA. |
| Hold | Bevat de gekalibreerde laatst geldige selectorwaarde. Bron/fingerprintwissel of verlopen hold wist de cache. Fallback vernieuwt de hold niet. Een geldige ODU-fallback kan wel langer dan 15 s worden gebruikt. |
| `oq_system_supply_temp` | Neemt de geselecteerde waarde over. Ruwe sensor-Error betekent dus niet automatisch ongeldige regelaanvoer of compressorstop. |
| Stooklijn | Eindige fallback/hold voorkomt een reset uitsluitend door ontbrekende aanvoer, maar een temperatuursprong bij fallback kan de PID-uitgang beïnvloeden. Zonder geldige aanvoer reset de strategie controlstate en wordt strategie-output ongeldig. |
| Power House | Gebruikt de systeemaanvoer voor het prestatiemodel en heeft daarnaast een eigen geldige ODU-outlet-route. Het gedrag is niet identiek aan de PID-reset van de stooklijn; log dispatchreden, geldigheid en werkelijk toegepaste niveaus. |
| Beveiliging/overige consumenten | CM4-ketelfallback vereist geldige geselecteerde aanvoer; koelen heeft een sensor-missing-pad. Thermal limits bewaart bij `NaN` bestaande trips maar zet zonder trip de limit factor op 1. Alleen dit component is dus geen algemene sensoruitvalstop. Test de volledige actuator- en strategie-uitkomst. |

Alle tussenliggende temperatuurtemplates lopen elke 5 s. De holdtermijn telt vanaf de laatste geldige **selector-evaluatie**, niet vanaf het laatste fysiek ontvangen ADC-resultaat. Een oude maar nog eindige proxy kan die timestamp nog verversen. De keten en strategie-ticks voegen fasevertraging toe; een korte ruwe fout kan zelfs geheel tussen twee proxypolls vallen. Beoordeel daarom afzonderlijk ruwe fouten en controllergedrag, met monotone timestamps. Een lagere hoeveelheid selectoruitval na een timingpatch bewijst geen lagere hoeveelheid chipfaults.

Code: [selector en fallbackprioriteit](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_sensor_source_runtime.h#L146-L217), [ODU-freshness](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_sensor_source_runtime.h#L758-L770), [lokale proxy](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/oq_local_sensors.yaml#L32-L58), [hold](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_supply_hold_logic.h), [stooklijn](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_heating_curve_runtime.h#L197-L233), [Power House](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_power_house_runtime.h#L185-L209), [thermal limits](https://github.com/OpenQuatt/OpenQuatt/blob/v0.55.0/openquatt/includes/control/oq_thermal_limits_logic.h#L48-L59), [eerdere uitval en acceptatietests](https://github.com/OpenQuatt/OpenQuatt/issues/418).

## Timingfix en afzonderlijke diagnosevarianten

`components/max31865` is een lokale override van ESPHome 2026.9.0. Het gewone Q-profiel activeert afzonderlijke fault-clear, circa 10 ms BIAS-settling, twee handmatige fault-detectstappen met circa 10 ms ertussen en circa 10 ms settling vóór conversie. De uitleestimeout is 67 ms bij 50 Hz, met 1 ms marge tegen schedulerkwantisatie. Diagnose staat standaard uit en voegt dan geen SPI-reads of cyclische diagnoseberichten toe.

`scripts/diagnostics/max31865_duo.yaml` gebruikt dezelfde Q Duo-configuratie en overschrijft de timing expliciet voor vergelijking met de oorspronkelijke v0.55.0-sequentie. `duo_wifi.yaml` bestaat op deze tag niet meer; de canonieke `duo.yaml` bevat beide netwerken. De bestaande netwerkvoorkeur blijft van kracht. Gebruik dezelfde commit voor alle varianten, zodat veranderingen op `dev` geen extra factor in de vergelijking worden.

Met `diagnostics: true` komen één diagnosebericht per meetcyclus, twee RTD-/fault-reads, configuratie-readback na BIAS-uit, meettijden en tellers beschikbaar. Tag `oq.max31865_diag` blijft zichtbaar ondanks de bestaande onderdrukking van tag `max31865`. Alle extra diagnostische reads/logs gebeuren na de oorspronkelijke conversie/read/power-off-sequentie; logging beïnvloedt desondanks belasting. Vergelijk daarom ook met uitgeschakelde diagnose.

| Variant | `separate_clear` | `bias_settle` | `manual_settle` | `conversion_settle` | `read_margin` | Vergelijking |
| --- | --- | --- | --- | --- | --- | --- |
| D0 | false | 0ms | 0ms | 0ms | 0ms | Alleen observatie bij de upstream meetsequentie. |
| Dclear | true | 0ms | 0ms | 0ms | 0ms | Versus D0: uitsluitend fault-clear scheiden; een extra SPI-write geeft onvermijdelijk enige extra tijd. |
| Dpre | true | 10ms | 0ms | 0ms | 0ms | Versus Dclear: uitsluitend BIAS laten stabiliseren. |
| Dmanual | true | 10ms | 10ms | 0ms | 0ms | Versus Dpre: automatische zelftest vervangen door twee handmatige stappen met wachttijd. |
| Dfull | true | 10ms | 10ms | 10ms | 0ms | Versus Dmanual: uitsluitend settling na zelftest toevoegen. |
| Dfixed | true | 10ms | 10ms | 10ms | 1ms | Versus Dfull: uitsluitend marge vóór uitlezen toevoegen; gelijk aan de Q-profieltiming. |

De 10 ms zijn conservatieve **testwaarden**, geen vastgestelde printtijdconstanten. De standaard foutbeoordeling, bereikfilter, 5s-polling, fallback, hold en regeling blijven gelijk. Het testcomponent voorkomt overlappende reads tijdens een wachttijd en probeert bij detector-timeout BIAS/testbits uit te zetten, publiceert `NaN` en laat een volgende cyclus opnieuw proberen. De oorspronkelijke begrensde pollinglus van maximaal circa 6 ms blijft; de toegevoegde millisecondenwachttijden gebruiken scheduler-callbacks. Er worden geen extra taken of grote buffers geïntroduceerd.

Gewone Q-firmware en optionele diagnoseconfiguratie valideren:

```sh
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo.yaml --jobs 1
python3 scripts/dev.py validate --config-only --config scripts/diagnostics/max31865_duo.yaml --jobs 1
```

Bijvoorbeeld Dmanual valideren of bouwen door de vier substitutions expliciet mee te geven; voor Dfull verandert alleen `pt1000_conversion_settle`:

```sh
esphome -s pt1000_separate_clear true -s pt1000_bias_settle 10ms -s pt1000_manual_settle 10ms -s pt1000_conversion_settle 0ms config scripts/diagnostics/max31865_duo.yaml
```

Voor Dfixed geef je `pt1000_separate_clear=true`, alle drie settling-substitutions op `10ms` en `pt1000_read_margin=1ms` mee. De gewone Single-/Duo-builds gebruiken deze timing al zonder diagnose.

Voor een echte build vervang je `config` door `compile`, met maximaal twee compilerjobs en sequentieel gebouwde varianten. Bewaar firmwarehash, substitutions en een bijpassende bronkopie per variant. Deze wijziging past de NVS-indeling niet aan; vermijd een downgrade naar firmware vóór de bestaande v0.55.0-migratie.

## Hardwaretestplan en besliscriteria

1. **Identificeer de installatie.** Leg firmwarehash/herkomst, uptime, printrevisie/batch, voedingsadapter, PT1000-/flowmeterbekabeling, netwerkmode en water-/omgevingstemperatuur vast. Gebruik de bevestigde Rref van 1500 Ω als uitgangspunt. Controleer C6-bestukking/-tolerantie op getroffen en gezonde printen; de referentieweerstand hoeft voor de onderzochte hardware niet opnieuw als vermoedelijke afwijking te worden bevestigd.
2. **Reproduceer vóór de fix.** Meet ongeïnstrumenteerde v0.55.0 en D0 op dezelfde getroffen print. Herhaal op een gezonde print van dezelfde en zo mogelijk andere revisie. Gebruik dezelfde temperatuur, voeding, bedrading, netwerklast en instellingen. Houd per variant minimaal 24 uur aan bij zeldzame fouten, en langer als D0 in die tijd nauwelijks fouten geeft. Bij 5 s zijn dat 17.280 cycli/dag; tel fouten per uitgevoerde cyclus en per bit, niet alleen logregels. Nul fouten is geen bewijs van nul risico; bij onafhankelijke cycli is de ruwe 95%-bovengrens ongeveer `3/N`.
3. **Onderscheid sensor van elektronica.** Test eerst een korte, bekende precisieweerstand van 1000 Ω en circa 1200 Ω direct aan de ingang. Herhaal met de echte sensor/kabel en reproduceerbare warme/koude omstandigheden. Wissel daarna afzonderlijk print, sensor/kabel en voeding. Fouten met vaste weerstand wijzen naar print/voeding/driver/SPI; verdwijnen na kabelwissel ondersteunt een extern elektrisch probleem.
4. **Meet de sequentie.** Logic analyzer op MAX31865 CS/SCLK/MOSI/MISO, met voldoende samplefrequentie voor 4 MHz; optioneel W5500 CS/SCLK voor correlatie. Trigger op de BIAS-/testwrites en decodeer register `0x00`, `0x01/02`, `0x07`. Vergelijk MISO met beide firmware-reads. Scope tegelijkertijd BIAS, REFIN−, RTDIN− en 3,3 V bij de chip tegen lokale GND. Gebruik een geschikte hoogohmige/differentiële probe: de probe mag de marginale RC niet zelf veranderen. Controleer tijdens FORCE− open of beide ingangen tijdig boven `0,85 × BIAS` komen.
5. **Loop Dclear → Dpre → Dmanual → Dfull.** Verander telkens één factor en herhaal daarna D0 op dezelfde warme toestand. Dpre die helpt ondersteunt onvoldoende opstartsettling; Dmanual die aanvullend helpt ondersteunt de interne 100µs-test als oorzaak; Dfull die alleen de temperatuurkwaliteit verbetert past bij settling na de zelftest. Blijven D3/D4 bestaan met ruime gemeten settling, onderzoek contact, lek naar GND, voedingsdip, koppeling en bestukking verder. Een positieve A/B-uitkomst moet met analoge/digitale meetsporen overeenkomen.
6. **Onderzoek digitale en belastingseffecten afzonderlijk.** Houd eerst alle timingwaarden vast en wijzig alleen SPI van 4 MHz naar 1 MHz via `data_rate` op de extended sensor. Test daarna afzonderlijk netwerk rust/belasting, web/recorder-export, HA/API/MQTT, WiFi reconnect en Ethernetovergangen, en correlateer pomp-/relais-/compressortransities. WiFi-voorkeur alleen verwijdert de reeds geïnitialiseerde W5500-driver niet. Bewaak huidige én minimale interne heap, grootste interne blok en relevante stack-watermarks bij gelijke belasting; totaal vrij PSRAM is onvoldoende bewijs.
7. **Injecteer echte fouten en controleer de regelketen.** Open sensor, kortsluiting en hoogohmig/intermitterend contact op een veilige labopstelling; laat echte fouten langer dan de hold duren. Meet elke ruwe publicatie plus proxy, geselecteerde aanvoer, effectieve bron, hold/fallback, ODU-freshness, systeemaanvoer, PID-reset, strategie-/dispatchgeldigheid, vraag en daadwerkelijk toegepaste compressorniveaus. Test met geldige ODU-fallback én zonder fallback; bij Duo ook HP1 geldig maar HP2 ongeldig. Test herstel rond de holdgrens, bronwissel PT1000/DS18B20/CIC, beide verwarmingsstrategieën, koelen en herstart tijdens een wachtfase. Gebruik gebeurtenistimestamps of passieve meetregistratie: de standaard recorder met 10s-samples kan een 5s-uitval missen. Voeg geen regelupdates toe vanuit de diagnose om de fasen kunstmatig te synchroniseren.

Acceptatie: reproduceerbare foutreductie tegenover D0 bij gelijke omstandigheden, juiste temperaturen met bekende weerstanden en echte PT1000, overeenstemming tussen chipstatus en SPI-trace, blijvende detectie van echte fouten, terugkeer naar `NaN` als fallback ontbreekt en hold verloopt, en ongewijzigde strategie-/actuatorveiligheid bij langdurige uitval. Een geldige continue ODU-fallback mag beschikbaar blijven. Een opvallende verslechtering van interne geheugenmarge of herstelgedrag blokkeert promotie. Promoveer pas naar een stabiele release na onafhankelijke review en deze fysieke acceptatie; de PR en testfirmware dienen juist voor deze validatie. Logonderdrukking of een langere hold is geen oorzaaksfix.

## Minimale voorgestelde fix

Bij bevestiging van de timingoorzaak: een lokale, doelgerichte MAX31865-drivercorrectie met afzonderlijke fault-clear, expliciete BIAS-settling, handmatige fault-detect voor dit ingangsfilter en settling vóór one-shot. Behoud faultmaskers/ongeldige publicatie, begrens de detectorwachttijd, serialiseer meetcycli en schakel BIAS/testmodus bij foutafhandeling uit. Kies de uiteindelijke wachttijden uit de slechtste gemeten/onderbouwde RC per ondersteunde printrevisie, inclusief tolerantie; de testpatch maakt die stappen afzonderlijk toetsbaar. Een uitsluitend langere wacht vóór de automatische test corrigeert de vaste interne open-switch-wachttijd niet.

Als SPI-frames aantoonbaar corrupt zijn, ligt de fix bij signaalintegriteit, hostconfiguratie of overdracht, ondersteund door een afzonderlijke clocksnelheidstest. Als de spanning tijdens ruime settling werkelijk fout blijft, herstel de elektrische oorzaak. De bevestigde referentieweerstand van 1500 Ω blijft ongewijzigd.

## Uitgevoerde validatie en grenzen

- `validate --config-only` voor Q Duo, Q Single en het diagnoseprofiel: geslaagd, inclusief stijl, docs, web/docs-contract, webbundel en NVS-budget.
- ESPHome-configuratie voor D0, Dclear, Dpre, Dmanual, Dfull en Dfixed: geslaagd. Onvolledige handmatige configuratie en wachttijd boven 100 ms worden afgewezen.
- Volledige ESPHome Q Duo-firmwarecompile: geslaagd, sequentieel met maximaal twee compilerjobs. Single is configuratiegevalideerd; de PR-build controleert beide topologieën.
- `npm run check:cpp-format`: geslaagd; de functionele profielwijziging blijft beperkt tot de lokale Q-sensorconfiguratie.
- Hostharness compileert en voert de echte driver uit: standaard registervolgorde en ingestelde 66ms-timeout, Q-uitleesmarge van 1 ms, overlap in alle wachtfasen, analoge foutbits, waarschuwingen, aanhoudende fouten/herstel, strikte aparte clear, handmatige fasen, tegenstrijdige dubbele reads, diagnose aan/uit, onbruikbare RTD-read, detector-timeout/hernieuwde poging en herstart vanuit een achtergelaten manual-teststatus slagen.
- Dertien unittestcases voor driver, input-sourcecontracten en Q-configuratiecompositie: geslaagd. Bestaande hold-hosttest: geslaagd.
- Fysieke RC-/SPI-/voedingsmetingen, runtime-geheugenmarges, flash en HIL zijn niet uitgevoerd. De fake SPI simuleert registergedrag; hij bewijst geen siliciumgedrag of elektrische veiligheid en kan de oorspronkelijke gecombineerde clear-write niet valideren.
- Nieuwe onafhankelijke koude review van de volledige timingfix tegen `dev`: geen blokkerende bevindingen. De gevonden extra diagnose-read op de 0/1-fouttak is verholpen, met extra assertions en statische hercontrole.
