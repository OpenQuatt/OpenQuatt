# Recorderpersistentie en backendtrend: HIL-resultaat, 18 oktober 2026

**Normale bevestigde persistentie en clean retries fysiek bevestigd.** Dit is begrensd bewijs; geen overall PASS of bewezen geheugenmarge.

## Geteste firmware en scope

- Functionele baseline: ESPHome 2026.9.0, config `0xa245efd3` (eerste observatiebuild).
- Backendbaseline: observatie-v2, config `0x675eeb85`, bron `dbe66c23311e8c6713d8f51fc8f7f45309fdccb8`.
- Fysiek geteste recorderkandidaat: observatie-v2, config `0xa049107a`, bron `6e46f722531d5dd4d25f933853c2dd906c7b7b5d`.
- Geteste kandidaat-OTA: 2497872 bytes; SHA256 `d3ac6cf51f097f86f3241fc3af2904067a3b177f7528d73027f9e388a08c8dc7`.

De v2-overlay gebruikt zes geheugen-/stackmetingen iedere `1s`, `force_update: true` en gate `ALWAYS_OFF`.
De functionele baseline en kandidaat hebben verschillende observatiebuilds; dit is geen matched netwerkcohort.
De latere UI-revisie `52d3c3e2` valt buiten deze fysiek geteste kandidaatbuild; dezelfde config hash onderscheidt beide revisies niet.
Alle geheugen-/stackwaarden hieronder zijn bytes; meettijden zijn verstreken tijden.

## Latere UI-revisie: assetidentiteit en warme HTTP-observatie

Actuele recorderhead `52d3c3e2f70f889a8b8714e633cc27a5573b136b` bewaart de gewenste recorderstand bij verloren ACK en biedt expliciete idempotente retry. De websuite heeft 903 groene tests; [CI 39090309010](https://github.com/OpenQuatt/OpenQuatt/actions/runs/39090309010) heeft alle tien jobs groen.

De observatie-OTA heeft 2498096 bytes, SHA256 `36088f7ab4dce80c4c1c0022a3c9fa46b7a6c535dc5a1cb2e608cdb52cd80cda`, config `0xa049107a`. Upload werd bevestigd om 05:17:28 UTC; readback om 05:17:58 UTC toonde CM0/Disabled/monitorOFF. De normale persistentie-/backendcases hieronder blijven uitsluitend bij `6e46f722` horen; de nieuwe warme HTTP-case staat afzonderlijk beschreven.

`served-ui-proof-c377c9e3-6015-47fe-9eaa-5094ddaeabf8` bevestigde één GET `/0.js` vóór de load: 1334944 decoded bytes, SHA256 `e3a0c4960ee0b6897c989e038cbd66d1496484b390778e164f66ca976f583b97`, gzip/UTF-8-reconstructie, HTTP200. Resultaat-SHA256 `83342feb49ee897f742bfd244d78f7cb69220983b67f1ec286b95274d03d1e35`. Dit onderscheidt de geserveerde UI ondanks de gedeelde config hash; het is geen volledige runtime-binaryattestatie of waargenomen wire-SHA (`firmwareBinaryAttested=false`). Baselineproef `served-ui-proof-3c21c23d-79d7-4f5b-a56e-13cac36e80c1` had decoded SHA256 `4cb79255cde3f1228184030a41b2a85d11a5dd0cc512c47f9245674bbeae6fc9`.

De verloren-ACK/retry-actieroute is niet fysiek via browserklikken getest; haar regressiedekking blijft host/webbewijs. Geen coldmatched- of geheugenmargekwalificatie.

## Normale persistentie en clean retries

De afzonderlijke baselineaudit vermeldt PASS binnen de functionele scope.
Onafhankelijke controle van de frozen kandidaatledger/events vond binnen dezelfde scope geen afwijkingen:
beide cases bevatten 17 checks, 20 recorder-mutatie-ACKs en twee bevestigde gewone `Restart`-acties.

- Een echte PSRAM-opname verzamelde drie samples; stoppen bevroor de historie.
- Twee clean `enabled=1`-retries en twaalf gewone gelijkblijvende verzoeken behielden
  `recording_id`, drie rijen, historie-digest, eerste-sample-uptime en `active=false`.
- Uitschakelen en herhaald uitschakelen behielden eveneens de gestopte historie.
- Gewone reboot na bevestigde opt-out gaf `enabled=false`, `active=false` en lege PSRAM-opstarthistorie.
- Gewone reboot na enable gaf `enabled=true`, actieve opname en aantoonbaar nieuwe PSRAM-historie.

| Rebootbewijs | Baseline | Kandidaat |
| --- | --- | --- |
| Opt-out: raw uptime voor → na | 186,618 → 3,458 s | 186,005 → 3,479 s |
| Enable: raw uptime voor → na | 123,454 → 3,503 s | 123,488 → 6,742 s |
| Eerste sample voor → na enabled reboot | 68783 → 3491 ms | 68524 → 3493 ms |
| Fysieke idle-samples / maximaal gat | 57 / 12,827 s | 57 / 10,683 s |

Beide reboots hadden een nieuw boot-CSRF en dezelfde verwachte firmware; tokens zijn niet opgeslagen.
`recording_id` wordt niet als uniek over reboots aangenomen. Alle vastgelegde requested/accepted/Hz-waarden
van beide compressoren waren numeriek nul, zonder NA; dit is bemonsterd bewijs, geen continue bustrace.
`CM Override` start volgens ontwerp op `Auto`; tijdelijke `Force CM0` werd pas na rebootbewijs hersteld.
`OpenQuatt Enabled=false` borgde de idle-opstart. Vóór elke reboot: gateOFF-readback plus 1500 ms barrière.
Beide functionele cases eindigden met bevestigde gateOFF; de kandidaatcase duurde 284,740 s.

## Twaalf echte disable/enable-paren en herstel

Beide backendcases: twaalf paren, 24 bevestigde echte transities en 210 verzamelde `1s`-samples.
De vaste vensters zijn 30 s schema-opwarming, twaalfmaal 10 s rust en 60 s herstel.
Monitorvenster baseline 336,718 s; kandidaat 337,137 s. Kandidaat: 39 fysieke idle-boundaries, zonder NA.
Warmed/final kandidaatopslag: PSRAM, capaciteit 2637, 258 velden, `configuration_pending=false`.
Bootstabiliteit is door de runner bij elke statusread via hetzelfde in-memory boot-CSRF bewaakt;
firmware/ID/readbacks ondersteunen dit. Iedere echte enable start nieuwe historie; hier geldt geen behoudclaim.

| Waarde, na opwarming → na herstel | Baseline | Kandidaat |
| --- | ---: | ---: |
| Intern vrij | 87911 → 87451 (-460) | 89239 → 88787 (-452) |
| Grootste intern vrij blok | 31744 → 31744 | 32768 → 32768 |
| PSRAM vrij | 5341976 → 5341976 | 5341976 → 5341976 |
| Intern minimum sinds boot | 39828 → 39828 | 39828 → 39828 |
| Loop-stack-HWM | 3958 → 3958 | 4022 → 4022 |
| HTTPD-stack-HWM | 520 → 472 | 396 → 396 |

De backendstartcontext was ongelijk: baseline `initialRecorder.sample_count=47`, kandidaat `7`;
geen gematchte koude start. Het laagste bemonsterde actuele interne vrije geheugen was 73075 B bij de kandidaat in `schema-warmup`,
tick 24, vóór de eerste transitie; baseline 84127 B lag in `recovery-settle`, tick 42. Deze minima over current free zijn niet het cumulatieve bootminimum van 39828 B.
PSRAM varieerde in de baseline niet; in de kandidaat 5338036..5358436 B, ondanks gelijke eindpunten.
Het interne bootminimum en beide stack-HWM zijn cumulatief en bleven correct niet-stijgend.
De zes onafhankelijke waarden zijn geen atomair snapshot. HTTPD-HWM 396 B was al vóór de twaalf paren
bereikt en is lager dan de baseline-eindwaarde 472 B: oorzaak en voldoende marge blijven open.
Deze korte runs bewijzen geen lekvrijheid, allocatieaantallen, causaliteit of worst-case geheugenmarge.
Er zijn in deze backendvensters geen exports/historie-digests gemeten; die horen bij de functionele cases.

De eigen aanvullende SSE-captures hadden elk één verbinding, nul reconnects en nul gerapporteerde capturefouten.
Baseline: 345,277 s / 547 bytes; kandidaat: 347,029 s / 3545 bytes, eigen capture exit0/gejoined.
Beide sessions gesloten en nul actieve streams bij afsluiting; kandidaatcleanup 4,432 s, zonder SIGKILL.
Readiness lag vóór het scenario; de capture eindigde erna. De ruwe backend-/captureverdicts blijven
`NOT_EVALUATED`. Een begrensde logscan vond geen allocatiefaal/crash/watchdog/errorlevel-patroonhits;
ook dat bewijst geen volledige foutloosheid of continue Native-/busdekking. Beide eindgates zijn OFF met barrière.

## Aanvullende offline allocatie- en stackaudit

Een afzonderlijke audit van de gepinde baseline- en recorder52-ELF's reproduceert 19 individuele entryframes per image. De recorderframes zijn gelijk: `canHandle` 560 B, `handleRequest` 640 B, `write_status` 144 B, export 224 B en snapshotcapture 80 B. Dit is geen complete callchain of veiligheidsmarge. De URLbuffer van 513 B zit al in het 640B-handlerframe; `canHandle` en `handleRequest` lopen na elkaar. De gewone sensor-REST/JSON-route heeft andere frames. Twee HTTP-clients betekenen geen twee gelijktijdige HTTPD-handlerstacks.

Bronconstanten en DWARF geven 2505536 B vaste recorderpayload in strikt PSRAM, exclusief allocatoroverhead. Eén maximaal gevulde exportsnapshot plus writer vraagt conservatief maximaal 2459072 B extra PSRAM; de samples kunnen één aaneengesloten blok tot 2097152 B vereisen. Het exportslot voorkomt meerdere gelijktijdige recorderexports; ontbrekende externe opslag geeft een fout, geen interne fallback.

Voor het eerder fysiek gemeten profiel met 258 velden en capaciteit 2637 volgt een row-stride van 795 B. Bij de gewone minimale 10s-capturecadans en inclusieve 15min-selectie zijn hoogstens 91 rows nodig: snapshot plus writer maximaal 432529 B. De volledige gebruikte stringdictionary wordt ook bij een korte range gekopieerd. Volle historie bij hetzelfde profiel vraagt maximaal 2456599 B extra. Dit zijn conditionele payloadgrenzen, geen nieuwe recorder52-runtime- of piekmeting.

Allocatoroverhead, andere componenten, SDK/netwerk-/cryptoallocaties en de diepste indirecte send/error/log-callchains vallen buiten die bedragen. Kleine SDK-/JSON-allocaties kunnen interne heap gebruiken; totale vrije PSRAM bewijst geen geschikt grootste aaneengesloten blok. De cumulatieve 316–524B-HTTPD-watermerken leveren daarom nog geen veilige extra stack- of gelijktijdige allocatiemarge op. Receipt-SHA256: `8a7a706c8875b7022e3a0be79ca44b101c7eed9a46df52788e34607447119c09`; beide ELF-/bronhashes, 38 gereproduceerde framerecords en formules zijn daarin vastgelegd.

## Herleidbaarheid en meetgrenzen

Frozen campagne-artifacts onder `quality-integration-check/.tmp/quality-validation/hil/2026-10-17-quality-three-prs/`:

- Functioneel: `recorder-d75af1f9-58aa-4f13-97a2-c2819945631c` en `recorder-09a1a1d9-c1ab-4289-bdcf-5a37734aeba9`.
- Backend: `recorder-backend-53bf4729-88f7-4ae7-8217-342e50041dc2` en `recorder-backend-866e4f90-ffbc-4d6d-a59b-fa96ce9594e8`.
- SSE: `recorder-backend-log-243e16e4-8fee-4e49-80ea-db0c67131113` en `recorder-backend-log-a66355dc-33c3-4984-ac0a-9d34725e762a`.
- Ledgers/events, capture-audits/metadata en `firmware-cohorts-v2.json` zijn de onderliggende bronnen.

Geen fysieke NVS-foutinjectie, stroomverlieskwalificatie, transactionele flashrollback of directe backendtelling.
Partiële writes, dirty retries en make-count-hergebruik steunen op echte component-hosttests.
[CI 39063817603](https://github.com/OpenQuatt/OpenQuatt/actions/runs/39063817603) is groen op `6e46f722`.
De actuele gezamenlijke bron is afzonderlijk gevalideerd zoals hieronder vastgelegd.
`52d3c3e2` heeft afzonderlijk asset-/warm-HTTP-bewijs; geen fysieke browserkwalificatie van de lost-ACK-actieroute. Integratie en volledige matched netwerk-/exportcohorten blijven **NIET GEKWALIFICEERD**.
Verklaring van transienten, HTTPD-stackmarge en het uiteindelijke geheugenmarge-oordeel staan open.

## Gecombineerde software-integratie

Integratiebron `b5259dc7b52f09853a470913187f65df00bb7ad7` bevat de oorspronkelijke drie PR's op `c2bb4c361b8430a6f9d4203f36f746264ecd5fa2`, plus uitsluitend de zes UI-bestanden uit `52d3c3e2f70f889a8b8714e633cc27a5573b136b`: `state-slices.js`, `debug-recording.js`, `i18n/en.js`, `i18n/nl.js`, `systeemrecorder.test.mjs` en `web-budgets.mjs`. De overgenomen patch is byte-identiek aan die UI-commit; patch-SHA256 `2d78da27c096404b39fcc1bc64a26b9e080c9396bdc65865bc36e71d875a96d9`.

Op deze gezamenlijke bron slaagt `python3 scripts/run_tests.py all`: **127 hosttests, 479 Python-tests, 27 HIL-harnesstests (offline) en 903 webtests**, inclusief webbuild en webkwaliteitschecks. Testlog: `quality-integration-check/.tmp/quality-validation/tests-all-recorder-ui-52d3.log`; SHA256 `484707e0bef3304117fedfdd901a67ce5f7e342a87faf53e9e8bc634d1061db8`.

Dit is software-integratiebewijs. De bestaande integratie-firmware blijft gebouwd uit `c2bb4c361b8430a6f9d4203f36f746264ecd5fa2` en bevat deze UI-delta niet. De testrun bouwde of flashte geen firmware; `b5259dc7` is niet fysiek getest. De afzonderlijke HIL-metingen behouden hun hierboven vermelde bron-, config- en OTA-identiteit.

## Open matched-load- en geheugenacceptatie

Er is nog **geen geslaagd, gekwalificeerd matched cohort** en dus geen geldige matched geheugen-/loadvergelijking tussen baseline, curve-, timer- en actuele recorderfirmware.
Afgebroken baselinevoorbereiding `matched-load-702af47c-9791-4227-963a-559e0f74dcb3` bevat een HA-snapshot met HTTPD-HWM **316 B**, lager dan de eerder vermelde 396–524 B; dit is een cumulatieve watermark uit een andere, onvolledige context, geen gematchte stressmeting.
Ook `matched-load-8e837175-974d-4f29-ba28-011ba3d69a1e` kwalificeert niet: de HA-aanvoer/orchestratie overschreed na één echte snapshot-ACK de 10s-grens; de HA-writer invalideerde zijn dekking om 03:17:29.852 UTC en de relay sloot om 03:17:30.040 UTC. De later gestarte web-loadworker eindigde om 03:18:21.288 UTC met exitcode 1, nul cycles/exports en `executionComplete=false`. Dit is mislukt meetbewijs; er is daarmee geen productfout aangetoond.

Afzonderlijke poging `matched-load-b44d1f6b-9086-425b-89a0-4a1f997af529` leverde 44 echte HA-snapshots/ACKs, 35 webcycli en 409 beantwoorde HTTP-requests, maar nul exports en geen herstelvenster. De sampler stopte na uitblijvende volgende geldige input; de precieze transportoorzaak is niet vastgelegd. De laatste geldige HA-bronwaarden waren circa 3,63 s oud; een DUT- of stale-HA-fout is niet aangetoond. Worker en eigen clients zijn gesloten/gejoined zonder forced kill. De vijf nog op MQTT geselecteerde bronnen veroorzaakten tijdens ingress-cleanup CM98; bron-normalisatie en een tweede volledig bevestigde cleanup herstelden CM0. Enabled bleef false en de uptime bleef doorlopen. De frozen audit heeft SHA256 `9a60296d5ac86add8e28dbda1756e34ddf33cf917abc7cb759e88bd80d3ed785`. Ook deze poging is ongekwalificeerd en blijft afzonderlijk van het nieuwe HTTP-observatieprotocol.

De vaste-startpoging `matched-load-1e9c3b80-05e4-4530-bbd2-0d7f430b6d2d` kwalificeert evenmin. De preflight bereikte 04:36:42 UTC, later dan de vooraf vastgelegde start 04:36:12 UTC plus de toegestane 10 s. De opstartplanning bood onvoldoende marge: een onterechte operatorcontrole stopte eerder op een ontbrekende effectieve coolingwaarde, terwijl de raw-/valid-/selected-readbacks geldig waren. De baseline-UI-fingerprintcontrole werd daarna wel voltooid; de deadlinecontrole brak de meting terecht af. Er waren twee HTTP-responses, nul webcycli, nul exports en nul geheugenmeetrijen. Beide aangemaakte HTTP-sessies zijn gesloten, de workerthreads gejoined en maximaal één request was gelijktijdig actief. Dit toont geen DUT-fout aan.

Aanvullende SSE-capture `7ab8c700-818a-4d6d-9448-c45bbf3b22b0`: 105,145 s, één verbinding, nul capturefouten, gesloten/gejoined met exitcode 0 na SIGTERM en zonder SIGKILL. Cleanup `matched-mqtt-setup-8fb69256-098f-469c-b149-7fa91d1e10a9` bevestigde alle twaalf mutatie-ACKs, geen failures en `unconfirmedMutation=false`; monitor OFF met 1500ms-barrière. Bron-normalisatie ging deze keer aan MQTT-disable vooraf; tijdens deze cleanup werd geen CM98 gemeld. Eigen publisher en broker zijn met SIGTERM gesloten/gejoined, beide exitcode 0. Dit is bevestigde runtime-cleanup, geen NVS-duurzaamheidsbewijs.

De latere `Auto`-readback rond 04:45 UTC met voortgaande uptime past bij normale override-expiry: baselinebron `openquatt/oq_substitutions_common.yaml:159` stelt 1800 s in; `openquatt/includes/control/oq_supervisory_state_runtime.h:522–530` schakelt daarna naar `Auto`. Een directe expirylog is niet vastgelegd; dit is geen harde oorzaakvaststelling of DUT-regressiebewijs.

Een losse gecachte zesmetriekenlezing rond 04:38:19 UTC na de mislukte voorbereiding gaf: intern vrij 72403 B, cumulatief intern minimum 39828 B, grootste intern vrij blok 31744 B, PSRAM vrij 5317144 B, loop-stack-HWM 4006 B en HTTPD-stack-HWM **236 B**. Die cumulatieve HTTPD-watermark is lager dan de eerdere 316–524 B. Dit is geen stress-/piekmeting, gematchte vergelijking of bewijs van voldoende marge; een oorzaak of PR-regressie is hiermee niet vastgesteld.

Het nieuwe protocol `exploratory-http-sampled-memory` is expliciet **warm en ongematcht**. Bron-SHA256 `79b6a375c9b3657ff2a02ec34bc1d04a82fa28dc620ec713f09fb4f37915a930`, workload-SHA256 `f9e5995f2b7f061bb835859f112030cc9e57b576aae31e0428501f2c8e8a8d53`; zestien offline tests en de primaire en onafhankelijke koude bronreview zijn groen. De baseline-run `425643c2` is op 05:12:38.736845 UTC afgerond; de begrensde uitkomst staat hieronder. De baselinecontext behoudt dezelfde controllerboot, config `0x675eeb85` en recorderhistorie na de afgebroken poging; een verse simulatorstart maakt die controllercontext niet koud. De protocolgrenzen sluiten HA/native-dekkingskwalificatie, matched-cold-acceptatie, worst-case-geheugenmarge en overall/releaseacceptatie expliciet uit. HTTP-geheugenlezingen blijven gecachte observaties met onbekende bronleeftijd; pieken tussen reads kunnen ontbreken.

### Afgeronde warme baseline: consistent exploratory bewijs, geen kwalificatie

Directory `matched-load-425643c2-e663-4a52-a9a9-2a5e662ead05`, baselineconfig `0x675eeb85`; dezelfde warme controllerboot en recorderhistorie na `1e9c3b80`. De opname begon deze collector met 231 samples. Werkelijke stressstart 04:55:38.411865 UTC; overgang naar observer-only herstel 05:10:38.640774 UTC; herstel eindigde 05:12:38.656641 UTC en workerfinal volgde 05:12:38.736845 UTC. De collector rapporteert 900,232 s webbelasting; de herstel-UTC-stempels omvatten 120,016 s. Dit is geen koude baseline of gematcht cohort.

Er zijn 152 webcycli, 2301 HTTP-responses en 524 geheugenmeetrijen: 14 warmup, 450 stress, 60 herstel. Geen gemiste observerslots; maximale afstand tussen observerresponses 2,203751 s bij nominale 2s-cadans. Alle 524 meetreceipts bevatten CM0, Force CM0, Enabled=false, monitor ON en de verwachte baselineconfig. Gecachte uptime is niet dalend, 0,668023→0,951358 h; dit is geen zelfstandige stopwatch of continue fysieke registratie.

Min/max in onderstaande tabel zijn waargenomen gecachte waarden, geen bewezen gelijktijdige pieken. Alle waarden zijn bytes.

| Metriek (bytes) | Warmup (14) | Stress (450) | Observer-only herstel (60) |
| --- | ---: | ---: | ---: |
| Intern vrij: min–max | 69959–72231 | 62315–71519 | 62763–71159 |
| Intern vrij: mediaan | 71519 | 71159 | 71159 |
| Intern vrij: eerste → laatste | 72231 → 71387 | 71387 → 71151 | 62763 → 71159 |
| Intern minimum sinds boot | 39828 | 39828 | 39828 |
| Grootste intern vrij blok | 31744 | 31744 | 31744 |
| PSRAM vrij: min–max | 5317144–5317144 | 5317144–5317144 | 5141636–5317144 |
| PSRAM vrij: eerste → laatste | 5317144 → 5317144 | 5317144 → 5317144 | 5141636 → 5317144 |
| Loop-stack-HWM | 3990 | 3990 | 3990 |
| HTTPD-stack-HWM | 236 | 236 | 236 |

HTTPD-HWM 236 B bestond al vóór deze stressfase en bleef in alle samples gelijk. De laagste gecachte PSRAM-waarde staat in de eerste herstelrij; onbekende bronleeftijd verhindert een precieze toewijzing aan een allocatie of fasepiek. De berekende verhouding `1 − largest/internalFree` varieert 0,4906–0,5605; dit is geen meting van de allocatorlayout. Herstel houdt beide HTTP-sessies open voor observerreads en is dus niet HTTP-/socketvrij of idle. Sampler en SSE voegen zelf belasting toe.

De drie 15min-exportreceipts behouden opname-ID `1792297032000`, schema 1, 258 velden en dezelfde schema-SHA256 `eca9523fde88de0b129c9855068f89bce5ddb2f50125e48b0d5f3858b545e137`.

| Gepland na start | Rijen / behouden duur | Bytes | Werkelijke web-I/O-overlap | Export-SHA256 |
| --- | --- | ---: | --- | --- |
| 300 s | 90 / 891 s | 14444 | Nee | `616ad4bd7cbad1cb4df32a7487bf0f5d2e70c99582b821f8dffc3f4350478c04` |
| 600 s | 90 / 891 s | 14445 | Nee | `e5419d8ecf4c4f9f9b88b893a46bde306a9fa116fff41f89167b0ac8c276a276` |
| 900 s | 91 / 900 s | 14403 | Ja, één overlappend webrequestinterval | `212b9a5e1690fa3deb075cdc27a1ede1ff7c749de436c07bbf720df77e5e5e60` |

Er is dus bij **één van drie** exports daadwerkelijke web-I/O-overlap vastgesteld; in geen van de drie exportintervallen voltooide een webrequest. Receipt- en HTTP-downloadhashes/tijden zijn onderling consistent. De ruwe exportpayloads zijn bewust niet bewaard; deze offline audit valideert receiptconsistentie, geen nieuwe onafhankelijke payloadreconstructie of zwaarste gelijktijdige allocatie.

De gepinde auditor-SHA256 `7deefc4d21ede7d80f5b4d69e9de72579e2e2e30b689a84b202eb837aa9f045f` levert 9056 controles zonder findings en `CONSISTENT_EXPLORATORY_ARTIFACTS`, met verdict `NOT_EVALUATED`. MQTT-brokerhistorie is aanvullend nominaal 5s-bewijs: maximale waargenomen afstand 5,003 s, toegestaan maximaal 6 s met 1 s auditmarge. Succesvolle lokale guardreceipts liggen maximaal 1,063600 s uiteen en behouden vier generaties en `subscriptionLossCount=0`. De runtime-guard eist current-age ≤3 s; die freshness is **niet retrospectief onafhankelijk gereconstrueerd** uit de opgeslagen laatste currentbestanden. PUBACK bewijst brokeracceptatie, geen continue controllerconsumptie. HA-cacheboekenden bevatten vóór en na negen states, geen continue HA/native-dekkingskwalificatie.

Beide eigen HTTP-sessies zijn gesloten, workerthreads gejoined, maximaal twee workerrequests tegelijk. Daarnaast liep één afzonderlijke SSE-verbinding: capture `21451472-9b33-4912-8841-bafdf94b26b2`, 1197,28 s / 13945 B, nul capturefouten/reconnects, gesloten/gejoined met SIGTERM/exit0 en zonder SIGKILL. Dit is maximaal twee worker-HTTP-requests plus één SSE, geen claim van twee totale verbindingen; logmetadata blijft aanvullend bewijs.

MQTT-cleanup `matched-mqtt-setup-ea67510a-b7e3-498b-95e9-92b9b4a3798d` eindigde met `executionComplete=true`, `verifiedRuntimeConfiguration=true`, `unconfirmedMutation=false`, nul failures en monitor OFF. Bron-normalisatie ging disable vooraf; publisher/broker hebben finale records en zijn volgens de operator met SIGTERM/exit0 gejoined. De afsluitende controller-/simulatorreadback rond 05:15 UTC bevestigde CM0, Enabled=false, monitor OFF en beide requested/accepted/Hz-drietallen nul. Dit is een bemonsterde afsluitgrens, geen continue fysieke dekking of NVS-duurzaamheidsbewijs.

Eigen geschoond auditreceipt: `curve-boot-adoption/.tmp/quality-validation/baseline-exploratory-audit.json`, SHA256 `fedae7fdbecdeffec9c2ce8e18c17f94b4f9c03e8084b4ba0126372798bc39d1`. De definitieve artifacthashes zijn na gemelde writerafsluiting opnieuw vastgelegd. `matchedColdQualification`, `haQualified`, `nativeQualified`, `worstCaseMemoryMarginQualified` en `overallPass` blijven false. Deze baseline-afronding geeft geen resultaat voor de latere recorder-UI-kandidaat of voor een viercohortvergelijking.

### Afgeronde warme recorderkandidaat: bron52d3c3e2

Directory `matched-load-22758548-11d8-4895-9a2d-5d0a8665c9de`, config `0xa049107a`, met bovenstaande geserveerde UI-identiteit. Hetzelfde exploratory protocol/brongebruik79b6/workloadf9, maar een afzonderlijke warme en ongematchte context met 136 initiële samples. Geen eerdere protocolrun/responses gedeclareerd; dat attesteert geen koude boot of afwezigheid van voorbereidingsbelasting.

Werkelijke stressstart 2026-10-18T05:43:17.672258+00:00; herstelstart 2026-10-18T05:58:17.848535+00:00; hersteleinde 2026-10-18T06:00:17.860806+00:00; workerfinal 2026-10-18T06:00:17.945744+00:00. Stressduur 900.195460 s, herstel circa120,012 s. Er zijn 154 webcycli, 2322 HTTP-responses en 524 meetrijen (14/450/60), nul gemiste slots; maximaal 2.099748 s tussen de meetresponseontvangsten. Alle meetrijen tonen CM0/ForceCM0/Disabled/monitorON; gecachte uptime en cumulatieve minima verlopen overeenkomstig hun contract. Geen continue fysieke registratie.

| Metriek (bytes) | Warmup (14) | Stress (450) | Observer-only herstel (60) |
| --- | ---: | ---: | ---: |
| Intern vrij: min–max | 71299–72143 | 63275–71583 | 71211–71343 |
| Intern vrij: mediaan | 71431 | 71211 | 71211 |
| Intern vrij: eerste → laatste | 72143 → 71299 | 71299 → 71211 | 71211 → 71343 |
| Intern minimum sinds boot | 39828 | 39828 | 39828 |
| Grootste intern vrij blok | 31744 | 31744 | 31744 |
| PSRAM vrij | 5317144 | 5317144 | 5317144 |
| Loop-stack-HWM | 3942 | 3910–3942 | 3910 |
| HTTPD-stack-HWM | 316 | 316 | 316 |

Min/max zijn gecachte observaties met onbekende bronleeftijd: korte pieken kunnen ontbreken, fasegrenzen geven geen harde allocatieoorzaak. Cumulatieve bootminima/HWM zijn geen actuele vrije marge. Baseline en kandidaat hebben verschillende warme voorgeschiedenis; hun verschillen bewijzen geen PR-oorzaak, lekvrijheid of verbetering. Observer-only herstel houdt beide sessies open en is niet HTTP-/socketvrij.

| Gepland | Rijen / behouden duur | Bytes | Werkelijke web-I/O-overlap | Export-SHA256 |
| --- | --- | ---: | --- | --- |
| 300 s | 90 / 891 s | 15787 | Nee | `2f90709ad81f484a82e7d61fa89adb9b2f976415322ac12d3fea6cca3f2b798b` |
| 600 s | 90 / 891 s | 15805 | Nee | `841c0b4b01e74ceb72888ad7cf2f5d020c92b234100d783e564878eb4371c8f3` |
| 900 s | 91 / 900 s | 15242 | Nee | `10f58460e97f773006140ace47436e1b7de146c778ba8ad55dd1d1625ff3a53e` |

Alle exports hebben schema1/258velden en opname-ID `1792300837000`; receipt-/downloadhashes en tijden zijn consistent. Bij **0/3 exports** is echte web-I/O-overlap waargenomen; ook nul webcompletions binnen de exportintervallen. Dit levert geen zwaarste gelijktijdige web/exportmeting op. Ruwe exportpayloads zijn niet bewaard; de offline audit bevestigt receiptconsistentie, geen nieuwe onafhankelijke payloadreconstructie.

Na alle joins levert de gepinde auditor7deefc4d 9098 controles zonder findings, `CONSISTENT_EXPLORATORY_ARTIFACTS`/`NOT_EVALUATED`. Nominale5s-brokerhistorie heeft maximaal 5.008000 s afstand (6s toegestaan met1s auditmarge); lokale guardreceipts liggen maximaal 1.037126 s uiteen, met vier gepinde generaties/subscriptionLossCount0. Current-age≤3s is runtimeguardbewijs, **niet onafhankelijk retrospectief gereconstrueerde freshness**. PUBACK bewijst brokeracceptatie, geen continue consumptie.

De HA-postflight is ná monitorOFF: zes memory-states `unknown`, alleen uptime/firmware/status beschikbaar. Dit zijn geen extra geheugenmetingen of continue HA/native-dekking. Beide worker-HTTP-sessies zijn gesloten/gejoined, maximaal twee requests tegelijk; daarnaast SSE `811d4f6e-665c-4e12-8155-157490d11cb0`: 1100.832 s/35524 B, één verbinding, nul reconnects/capturefouten, SIGTERM/exit0/gejoined zonder SIGKILL. Twee workerverbindingen plus één SSE, geen twee-totaalclaim.

MQTT-disable `matched-mqtt-setup-af28e757-dd60-49d4-a7d5-4e4d06d6613f`: twaalf ACKs, verifiedRuntimeConfiguration/executionComplete=true, unconfirmedMutation=false, nul failures; inputs/retained/auth uit/gewist, disconnected, monitorOFF-barrière. Publisher/broker volgens operator TERM/gejoined/exit0; readbacks06:01:58–06:02:02 UTC bevestigen CM0/Disabled/monitorOFF en alle zes simulatorwaarden nul. Bemonsterde afsluitgrens, geen continue fysieke dekking of NVS-duurzaamheidsbewijs.

Eigen auditreceipt `curve-boot-adoption/.tmp/quality-validation/recorder-exploratory-audit.json`, SHA256 `5f8cfa833f5589a806dd29d28dacd9796dcd4d41424004eef73b32853568de4b`, geschoond/mode0600 met definitieve artifacthashes na writerjoins. Coldmatched-, HA/native-, worst-case-marge- en overallPASS-vlaggen blijven false; geen fysieke lost-ACK-/browserretry- of NVS-foutinjectieclaim.

Current internal free, het cumulatieve internal minimum, largest internal block, PSRAM en beide stack-HWM moeten onder dezelfde koude start, recorderhistorie en gelijktijdige belasting afzonderlijk worden beoordeeld. De bestaande momentopnamen bewijzen geen ruimte voor de zwaarste gelijktijdige allocaties of HTTPD-stackpiek.
**Geheugen-/releaseacceptatie blijft geblokkeerd** totdat een geldige matched vergelijking en onderbouwde allocatie-/stackmarge beschikbaar zijn. De lage baselinewatermark bewijst op zichzelf geen regressie door deze PR.
De campagne kwalificeert geen volledige TLS-belasting, echte browserbediening of continue HA/native-/busdekking. Fysieke NVS-foutinjectie en stroomverlieskwalificatie zijn niet uitgevoerd; normale reboot/persistentie en hostfoutinjecties blijven daarvan onderscheiden.
