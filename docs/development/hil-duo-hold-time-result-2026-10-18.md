# Duo-holdtimers: gedeeltelijk HIL-resultaat, 2026-10-18

**SCOPED PASS voor de urgente en normale kandidaat-holds en de afzonderlijk gemeten Auto-herintrede.** De baselinefout is fysiek bevestigd; volledige profiel- en geheugenacceptatie is nog niet afgerond.

## Bron en bewijs

- Baselinebron: `dbe66c23311e8c6713d8f51fc8f7f45309fdccb8`; image ESPHome `2026.9.0`, config `0xa245efd3`.
- Kandidaatbron: `d571884ddd3e3e77f26db3f440bd08c9638d0efe`; gemeten observatieimage ESPHome `2026.9.0`, config `0xb16c23a0`.
- Baselinecase: `baseline-timer-holds-hp2-e68cff1d-c38e-499a-ac43-32387c3e2162`.
- Kandidaatcase: `timer-v2-urgent-holds-hp2-d8a519a7-8b88-48fa-9bd9-b46eeed43864`.
- Beide cases staan in campagne `2026-10-17-quality-three-prs`; originele resultaten blijven ongewijzigd.

| Case/bestand | SHA256 |
| --- | --- |
| Baseline `result.json` | `7ad1b641cbea62981baec99814a190bfd01b2cd6165f2f7c7540110f56c27903` |
| Baseline `observations.jsonl` | `b1fbb9544e703cda45d97ad992443e633479a70080bf4f455f9f91220fb21294` |
| Kandidaat urgent `result.json` | `13630bfab953a3ec1c950e1620290ca7a1c7028c9d8859732a73a9cdd8daacc8` |
| Kandidaat urgent `observations.jsonl` | `fae70e3af1abb3cfc79922b976c760cc2f9712040982a1efb41f3638df498b2a` |
| Kandidaat normaal `result.json` | `e885a92e371403576b4ad4df82146b1271b13077bfb8bd682f66d1c2da023bf2` |
| Kandidaat normaal `observations.jsonl` | `edf44bc2b78fe7e520822c81dd940dbfb107e43c4ea366e082b52a0002d5b895` |
| Normale case `capture-audit.json` | `900e9ff574d0f3bc47626bdb833b1c1039ed63cd41334745748dcf976e51ae6f` |
| Geheugenvenster `ledger.json` | `8b3f38be00a83122fce533c853ecbde334b44065fd449fcf6cf696500fc499c6` |
| Geheugenvenster `samples.jsonl` | `673f7a743f2c1f20c2d13d8c20c2f0dce71b3d0d862d67ba022e9e64b52ee8b4` |

## Urgente baseline-meting

CM2/Auto, alleen HP2 actief, HP1 gezond/start-ready idle, RECOVERY en discrete/effectieve vraag 3/20.
Flow circa 796 L/h, geselecteerde supply 32,48°C en effectief target 40°C bleven in de dispatchsnapshots gelijk.
Enable/emergency/latch bleven nul; de disablecounter liep op terwijl Duo uit stond.

| Observatie | Voor | Na | Verschil |
| --- | ---: | ---: | ---: |
| Dispatchklok | 571223 ms | 586229 ms | **15006 ms** |
| Disable elapsed | 540 s | 570 s | **30 s** |

De oude implementatie gaf 14,994 seconden te veel tegoed bij één beschikbaarheidsgedreven urgente dispatch.
Normale `ODU 1 responses enabled` ging kort OFF; SUSPECT/available=false werd circa 8,877s na het OFF-verzoek gezien.
Direct ON werd bevestigd om 23:13:04.798 UTC; geen UART-injectie of waargenomen LOST.
Drie fysieke snapshots toonden alleen HP2; maximaal fysiek samplegat 39,227s, geen continue uitgangsregistratie.

De urgent-subuitkomst heet PASS, maar totaal bleef `NOT_EVALUATED`/`INCOMPLETE`: de oude helper eiste na herstel
binnen 90s ten onrechte opnieuw start-ready. Peer was healthy/must_stop=false/stopped/stop_confirmed met normale min-off.
Die herstelvoorwaarde is later gecorrigeerd; de oude case wordt niet opgewaardeerd. Reset/Auto zijn niet uitgevoerd.
Finally-cleanup bevestigde responses ON, CM0, alle zes fysieke requested/accepted/Hz nul, controller requested/applied nul
plus drie counters/latch nul. Normale 60s-holds zijn niet uitgevoerd.

## Urgente kandidaat: SCOPED PASS

De relevante raw reads behielden CM2/Auto/RECOVERY, vraag 3/20, supply 32,48°C, target 40°C, flow 795,9839 L/h,
HP2 requested/applied F1 en HP1 nul; enable/emergency/latch nul. De holdklokken zijn niet versneld.

| Observatie | Voor | Na | Verschil |
| --- | ---: | ---: | ---: |
| Dispatchklok | 807028 ms | 817028 ms | **10000 ms** |
| Disable elapsed | 475,016 s | 485,016 s | **10,000 s** |

Afwijking tellertegoed tegenover dispatchdelta: **0,000s**, binnen de 0,5s-tolerantie.
Voor/na zijn telkens gelijke timerbundels uit twee reads circa 1,285s uit elkaar. Een eerder wisselend paar is overgeslagen.
De voorafgaande gewone dispatch gaf 30001ms/30,001s. Dit ondersteunt de begrensde urgente elapsed-assertie.
Alle 109 controllerrows en 11 fysieke rows hadden de vereiste numerieke velden; geen NA/missing in die gecontroleerde velden.
Tien actieve fysieke snapshots toonden sole HP2 F1/20Hz en HP1 requested/accepted/Hz nul; de elfde is finale stilstand.
Maximaal actief samplegat 28,400s; rond de pulse 8,585s. Korte onzichtbare fysieke veranderingen zijn niet uitgesloten.

SUSPECT/available=false/must_stop=false werd 3,733s vanaf het OFF-begin waargenomen; ON-readback volgde 1,580s later.
Geen LOST is waargenomen. Herstel werd healthy/stopped/stop_confirmed met available=false; min-off bleef gerespecteerd.
De onafhankelijke 22s-restorechild werd na directe ON geannuleerd; zijn outputbestand is leeg.
Het fallbackdeadlinepad en de child-join zijn daardoor **niet afzonderlijk bewezen**.

## Kandidaat-reset, Auto en cleanup

Reset is waargenomen in CM1/Force CM0 op dispatchklok 847029ms: alle drie counters plus latch nul.
HP2 mocht daarbij nog normaal coasten; resetbewijs is geen fysieke stopclaim.
De oude Auto-herintredecase blijft **INCONCLUSIVE**: alle 66 a/b-reads tussen 01:38:07 en 01:39:07 UTC bleven CM1.
Er is geen eerste kwalificerende CM2/3-holdtick bewezen; alleen een nulwaarde zou daarvoor onvoldoende zijn.

Eindread 01:39:11 UTC: CM0/Force CM0, controller requested/applied en counters/latch nul.
01:39:13 UTC: beide simulator requested/accepted/Hz nul; peerresponses ON bevestigd; cleanup zonder gerapporteerde fouten.
Aanvullende operatorcontrole om 01:40:39 UTC las exact b16, Enabled=false met bevestigde ACK, CM0 en monitor OFF.
Operatorcontrole vond circa 01:40:15 UTC geen eigen logchildren meer. De oude SSE-finale receipt is echter 0B:
geen claim van graceful sessionClosed, sluitende child-joinregistratie of volledige logdekking.
Topniveau blijft `NOT_EVALUATED`; completion `OBSERVED` is **geen complete HIL-PASS**.

## Auto-herintrede, afzonderlijke herhaalmeting: afgebakend bewijs

Case `timer-v2-auto-reentry-cab1a1ba-11cd-434c-9818-8b9e83d82f17`, dezelfde bron `d571884d`, config `0xb16c23a0` en OTA-SHA256 `dc0a3e6c882cf198fd7530b1fa15bfead6c7ed73b2815be85d92332f1d26eede`.
De frozen helper heeft SHA256 `41d71f4402613005b3b390a09e82e3ca7ca3bbd4d2d39c7511704404288137f4`; geen klok-, firmware- of safety-aanpassing tijdens deze proef.
Eerdere case `timer-v2-auto-reentry-d653e6a9-90ee-44a3-b98f-94920aad89ed` stopte vóór mutaties: external system pump flow stond uit, nul samples en geen SSE-child. `INCOMPLETE_CLEANUP` betekende daar dat cleanupautorisatie niet was bereikt; geen achtergebleven mutatie is daarmee aangetoond. Na normale switch-ACK bleef de helper ongewijzigd.
Het afzonderlijke `timer-auto-external-preflight.json` bevestigt OT-heating-enable geldig/geselecteerd, beide HP's healthy/stopped/stop-confirmed/start-ready, Enabled=false en fysieke nulstanden. Dit zijn operatorreadbacks met deels benaderde receipttijden, geen atomaire snapshot.

CM0/Force CM0-reset is coherent gezien op klok 451896 ms, met counters/latch en fysieke outputs nul. Auto-ACK volgde om 06:11:01.765 UTC.
De eerste CM2-readback om 06:11:49.575 UTC bevatte nog klok 481899 ms uit CM1: die cached nul telt niet als kwalificerend nulanker.
De latere nul op 511938 ms wordt achteraf bevestigd door twee verschillende positieve dispatches:

| Dispatchklok (ms) | Disable elapsed (s) | Delta vanaf nulanker | Enable / emergency / latch |
| ---: | ---: | ---: | --- |
| 511938 | 0 | 0 ms | 0 / 0 / 0 |
| 541939 | 30,001 | 30001 ms | 0 / 0 / 0 |
| 571951 | 60,013 | 60013 ms | 0 / 0 / 0 |

De helper koos de laatste herhaalde coherente nulreadback om 06:12:24.296 UTC; dezelfde nulklok was al coherent zichtbaar om 06:12:01.145. Positieve readbacks volgden om 06:12:30.074 en 06:13:05.049 UTC. Dispatchklokken en gepubliceerde receipttijden zijn verschillende grootheden; dit is geen onafhankelijke stopwatchmeting op basis van die drie GET-tijden.
Het geselecteerde venster bevat acht samples, zeven coherent. Alle acht behouden CM2/Auto/Enabled, RECOVERY, discrete/effectieve vraag 4, enable/emergency/latch nul en fysieke HP1=1/1/20 Hz, HP2=0/0/0. De counterdelta klopt tot 0,000 s met de dispatchdelta, binnen de 0,5s-tolerantie.
De volledige run heeft 23 samples, 19 coherent, maximaal 8,313 s tussen samples. OT req 504→634/rsp 475→605, ieder volgend samplepaar vooruitgang, maxage 998 ms; timeout 0 en invalid 3 blijven gelijk. MaxWater 40°C, API-target 40°C, PID 0,02/0/0, Manual PWM 154 en flow circa 796 L/h blijven gewone instellingen; minimumruntime 300 s en minimum-off zijn niet omzeild.

**SCOPED PASS voor dit gepubliceerde CM2-nulanker met twee positieve elapsed-dispatches.** De oude 66-CM1-case blijft INCONCLUSIVE. Het nieuwe bewijs is bemonsterd, niet atomair en geen continue bus-/uitgangsregistratie; het bewijst geen Duo-latchomslag, profiel-emergency of fysieke defrost.
Force CM0-ACK om 06:13:11.091 UTC liet de gewone minimumruntimecoast gelden. Finale readback 06:17:47.450 UTC: CM0/Force CM0, Disabled, monitor OFF, vier controlleroutputs, drie counters/latch en zes fysieke outputs nul. Cleanup confirmed/physicalZeroConfirmed=true, errors leeg; geen pending transportonzekerheid. De run duurde 469,209 s; de collector blijft `NOT_EVALUATED` met `OBSERVED_REQUIRES_ROOT_AUDIT`.
SSE `59d4733b-3518-4cb8-b3fb-9f55cdae388e`: 457,044 s / 6678 B, één verbinding, nul reconnects/capturefouten, eigen child met SIGTERM/exit0 gejoined zonder SIGKILL; sessionClosed=true, activeStreams=0/max=1. De nul capturefouten betreffen het SSE-transport: de controllerlog bevat long-operation-WARNs van 60–84 ms en één Modbus-WARN tijdens coast omstreeks 06:16:31 UTC (seq69: `Stop waiting for response from 2 109959us after last send`). Hieruit volgt geen incident- of DUT-oorzaak. Geen geheugenmonitor of gematchte loadmeting in deze case.

| Bestand | SHA256 |
| --- | --- |
| Auto `result.json` | `cf45770e196c0c9c5ab4d334087f3e7023b936f48d6282660fd751d1b535c366` |
| Auto `events.jsonl` | `3e2f2bee1c2befe0971a614c35432903ca1d57c9ce82707ffc03c03353ff6fca` |
| Auto SSE-metadata | `619dbc7700cd28523573907a89acab98dc2256254c42142919eca38478462067` |
| Externe preflight | `ca0c1678b26607b8c1bb8a8e25a1b2848a5d3fa223b85f112392233c0c8541f5` |

## Normale 60s-holds: SCOPED PASS

Case `timer-v2-normal-holds-hp2-a7ed2d38-700f-41eb-98fb-9e2bba0d036a`, config `0xb16c23a0`.
De eerdere poging `c1c76ff5-8c96-41af-b9d8-3408c1c208c6` stopte vóór mutaties op CH=false:
preflight-INCONCLUSIVE, nul samples, geen timerbewijs. Na gewone CH=true-ACK bleef de helper ongewijzigd.
De normale run gebruikte Manual PWM 154, flow circa 796 L/h, verse OpenTherm-data, Factory-profielen en bestaande MaxWater 40°C.
De API-target bleef binnen die cap; MaxWater, minimumruntime en minimum-off zijn niet verlaagd of omzeild.
HP2 bereikte fysiek F9/55 Hz binnen 456,478s van de 480s-rampgrens, bij supply 35,77°C en target 40°C.

| Hold | Dispatchklokken (ms) | Elapsed (s) | Latch | Werkelijk tot omslag |
| --- | --- | --- | --- | ---: |
| Enable | 1252495 → 1282658 → 1317658 | 0 → 30,163 → 65,163 | 0 → 0 → 1 | 65,163 s |
| Disable | 1412663 → 1447663 → 1477663 | 0 → 35 → 65 | 1 → 1 → 0 | 65,000 s |

Beide nulticks zijn achteraf gekoppeld aan positieve voortgang en dezelfde dispatchdelta; afwijking 0,000s binnen 0,5s.
MAINTAIN en de kwalificerende vraag/error bleven vanaf de bewezen klok geldig. Per hold zijn alle 13 coherente
pre-ready-samples over vier klokken gecontroleerd, inclusief eerdere entryrows; ook alle 17/15 ruwe rows vóór 59,5s.
Enable behield uitsluitend HP2 en alle peeroutputs nul; disable behield beide curve/control/fysieke outputs positief.
Geen waargenomen vroege aanvraag of verborgen correctie binnen dezelfde gepubliceerde klok.
HP1-controllerrequest verschijnt pas bij enable-ready; fysiek HP1 F1/20 Hz volgt om 02:04:31 UTC.
Na disable-ready koos de curve HP1 single; capacitytekst volgde om 02:07:29 UTC. HP2 liep op de omslag nog F3/30 Hz:
actuatorafbouw en fysieke stop volgen afzonderlijke normale gates en zijn geen vroegere of latere holdomslag.

123 samples, waarvan 100 coherent; geen ontbrekende vereiste numerieke velden. Grootste ruwe gap binnen de
holdintervallen 6,381/8,357s; globaal 33,383s buiten die intervallen. Dit blijft bemonsterd bewijs met 5s/30s UI-publicatielag.
OpenTherm req 755→1579/rsp 742→1566, ieder opvolgend paar voortgang, maximale age 1009ms; timeout 0→0/invalid 3→3.
Vier flagreadbacks: CH/responses ON, Fast/UART/manual/no-flow/defrost/freeze/extrapolation OFF, beide Factory.
Alle samples: MaxWater 40°C, monitor OFF, emergency nul, flow ≥400 L/h en gezonde/start-ready incidentstatus.

Force CM0-ACK om 02:07:31 UTC; HP2 fysiek nul om 02:07:44 UTC. HP1 bleef gedurende de normale 300s-runtimecoast
op F1; controller nul om 02:09:50, fysiek nul om 02:09:55, finale CM0/alle outputs/counters/latch nul om 02:10:05 UTC.
Cleanup bevestigd zonder errors. Responses zijn niet gemuteerd; de latere geheugencontrole las beide opnieuw ON.
De run duurde 998,035s. Wrapper `timer-normal-log-3807b468-35b0-4134-a847-55ce10a6b7a3` bevestigt eigen child exit0/join,
sessionClosed, activeStreams=0/max=1, 1010,408s/14740B, één verbinding zonder reconnect/error, alleen SIGTERM, geen kill.
Dit is begrensde logdekking en timer/requestbewijs; geen algemene product-PASS of matched geheugenproef.

## Afzonderlijk geheugenvenster na normale control

Venster `memory-window-137f56c7-61eb-4d41-8b1f-34acf744b80c`: tien geldige 1s-samples in CM0,
02:11:11–02:11:20 UTC, maximale startgap 1001,991ms, nul gemiste slots; alle onderstaande waarden bleven gelijk.

| Metriek | Waarde (bytes) |
| --- | ---: |
| Internal free | 88807 |
| Internal minimum sinds boot | 39828 |
| Largest internal block | 47104 |
| PSRAM free | 5341976 |
| Loop-stack HWM | 3990 |
| HTTP-stack HWM | 412 |

Exact b16, geen reboot tussen boundaries, beide fysieke outputs nul/responses ON; monitor OFF plus 1500ms-barrière
bevestigd om 02:11:25 UTC. De sensoren zijn gecachte 1s-metingen; minimum en stack-HWM zijn cumulatief.
Dit venster meet de toestand ná control, niet de actieve piekbelasting, en is geen baselinevergelijking of veiligheidsmarge.

## Dekking en openstaande metingen

| Onderdeel | Baseline | Kandidaat |
| --- | --- | --- |
| Urgente disablecounter bij Duo uit | Eén fout: 15,006s werkelijk/30s tegoed | SCOPED PASS: 10,000s/10,000s |
| Afzonderlijke non-heating mode-reset | NIET UITGEVOERD | CM1/counters nul waargenomen |
| Auto-herintrede eerste kwalificerende tick | NIET UITGEVOERD | Oude case INCONCLUSIVE; herhaalmeting: nul → 30,001 → 60,013 s, SCOPED PASS |
| Normale 60s enable- en disable-hold | NIET UITGEVOERD | SCOPED PASS: 65,163s / 65,000s |
| Fysieke emergency-/defrostgates | NIET UITGEVOERD | **NOG NIET GEMETEN** |
| Matched-load HA/MQTT/SSE/web/API en geheugen | **GEEN GEKWALIFICEERD COHORT** | **GEEN GEKWALIFICEERD COHORT** |

Hosttestbron `tests/python/test_heating_curve_duo_hold_runtime.py` bevat eerste/urgente ticks, 59999/60000ms,
vertraagde scheduler, conditieverlies/herintrede, uint32-wrap, saturatie, mode-reset, emergency en defrost.
Dit zijn hostscenario's; fysieke dekking blijft beperkt tot de hierboven gemeten cases. De gecombineerde testrun hieronder valideert de gezamenlijke bron.

## Geheugencontext en grenzen

Bron-/ELFaudit: runtime-object 68→104 bytes (**+36 B**), gelinkte statische RAM **+48 B**.
Productiemanifest bij d571884d: 216715 bytes statische RAM, uitsluitend buildmetadata; overlays hebben eigen footprint.
De HTTPD-stack is 4352 interne bytes. Verschillende baselinecontexten rapporteerden 524 B (idle), 396 B (oude backendrun) en later 316 B (afgebroken voorbereiding); dit zijn cumulatieve HWM-waarnemingen, geen matched kandidaatmeting of margeclaim.
Vervolg: overige Auto-/profielvarianten en ontbrekende emergency-/defrostdekking afzonderlijk beoordelen; matched-load uitsluitend met gelijke koude start en recorderhistorie.

## Gecombineerde software-integratie

Integratiebron `b5259dc7b52f09853a470913187f65df00bb7ad7` bevat de oorspronkelijke drie PR's op `c2bb4c361b8430a6f9d4203f36f746264ecd5fa2`, plus uitsluitend de zes UI-bestanden uit `52d3c3e2f70f889a8b8714e633cc27a5573b136b`: `state-slices.js`, `debug-recording.js`, `i18n/en.js`, `i18n/nl.js`, `systeemrecorder.test.mjs` en `web-budgets.mjs`. De overgenomen patch is byte-identiek aan die UI-commit; patch-SHA256 `2d78da27c096404b39fcc1bc64a26b9e080c9396bdc65865bc36e71d875a96d9`.

Op deze gezamenlijke bron slaagt `python3 scripts/run_tests.py all`: **127 hosttests, 479 Python-tests, 27 HIL-harnesstests (offline) en 903 webtests**, inclusief webbuild en webkwaliteitschecks. Testlog: `quality-integration-check/.tmp/quality-validation/tests-all-recorder-ui-52d3.log`; SHA256 `484707e0bef3304117fedfdd901a67ce5f7e342a87faf53e9e8bc634d1061db8`.

Dit is software-integratiebewijs. De bestaande integratie-firmware blijft gebouwd uit `c2bb4c361b8430a6f9d4203f36f746264ecd5fa2` en bevat deze UI-delta niet. De testrun bouwde of flashte geen firmware; `b5259dc7` is niet fysiek getest. De afzonderlijke HIL-metingen behouden hun hierboven vermelde bron-, config- en OTA-identiteit.

## Open matched-load- en geheugenacceptatie

Er is **geen gekwalificeerde matched vergelijking van de vier cohorten**. **Geheugen-/releaseacceptatie blijft geblokkeerd**; de afzonderlijke functionele resultaten hierboven veranderen dat niet.

Niet-gekwalificeerde voorbereidingen; geen van deze uitkomsten toont een DUT-fout aan:

- `matched-load-702af47c-9791-4227-963a-559e0f74dcb3`: onvolledige voorbereiding, cumulatieve HTTPD-HWM 316 B.
- `matched-load-8e837175-974d-4f29-ba28-011ba3d69a1e`: HA-aanvoer overschreed na één ACK de 10s-grens. HA-writer invalideerde 03:17:29.852 UTC; relay sloot 03:17:30.040 UTC; de later gestarte webworker eindigde 03:18:21.288 UTC met exit1 en nul cycles/exports. Dit zijn verschillende actortijden.
- `matched-load-b44d1f6b-9086-425b-89a0-4a1f997af529`: ontbrekende vervolgaanvoer, transportoorzaak onbekend; nul exports/geen herstelvenster. Bron-normalisatie en bevestigde cleanup herstelden CM0 na CM98 bij ingress-cleanup. Audit-SHA256 `9a60296d5ac86add8e28dbda1756e34ddf33cf917abc7cb759e88bd80d3ed785`.
- `matched-load-1e9c3b80-05e4-4530-bbd2-0d7f430b6d2d`: krappe planning/onterechte coolingprobe; preflight 04:36:42 UTC overschreed de vaste start 04:36:12 UTC +10s. Correct afgebroken na twee HTTP-responses, nul cycles/exports/meetrijen; twee sessies gesloten en workerthreads gejoined.

SSE `7ab8c700-818a-4d6d-9448-c45bbf3b22b0`: 105,145 s, één verbinding, nul fouten, gesloten/gejoined, exit0/SIGTERM, geen SIGKILL.
Cleanup `matched-mqtt-setup-8fb69256-098f-469c-b149-7fa91d1e10a9`: twaalf ACKs, geen failures/onbevestigde mutaties, monitor OFF +1500ms; bron-normalisatie vóór disable. Eigen publisher en broker gesloten/gejoined met SIGTERM/exit0.

Een losse gecachte baselinelezing rond 04:38:19 UTC gaf HTTPD-HWM **236 B**, lager dan de eerdere 316–524 B. Dit is een cumulatieve watermark, geen stresspiek, bewezen marge of PR-causaliteit. Intern current free/minimum/largest block, PSRAM en stack-HWM blijven afzonderlijke grootheden.

`exploratory-http-sampled-memory` is **warm en ongematcht**. Baseline `425643c2` is afgerond: 900,232 s belasting, 120 s observerherstel, 524 geheugenmetingen, 2301 HTTP-responses en drie exports. Bij één export overlapte werkelijk web-I/O. Geen meetmomenten gemist; twee HTTP-sessies en de ene foutloze SSE-client zijn gesloten en gejoined. Primaire en onafhankelijke controle vinden consistente artifacts; dit is geen geheugen- of releaseacceptatie.
Bron-SHA256 `79b6a375c9b3657ff2a02ec34bc1d04a82fa28dc620ec713f09fb4f37915a930`.
Workload-SHA256 `f9e5995f2b7f061bb835859f112030cc9e57b576aae31e0428501f2c8e8a8d53`; zestien offline tests en primaire/onafhankelijke koude bronreview groen. Dezelfde controllerboot/config `0x675eeb85` en recorderhistorie blijven warme voorgeschiedenis; een verse simulator maakt de controller niet koud.

Dit kwalificeert geen volledige TLS-belasting, echte browserbediening, continue HA/native-/busdekking, worst-case allocatie-/stackmarge of overall acceptatie. Gecachte HTTP-metingen hebben onbekende bronleeftijd en kunnen pieken tussen reads missen.
Normale reboot/persistentie en hostfoutinjecties bewijzen geen fysieke NVS-foutinjectie of stroomverlies-/flashrollbackkwalificatie.
Uitgebreide campagnehistorie en bewijsgrenzen staan in het recorderreport bij [PR #863](https://github.com/OpenQuatt/OpenQuatt/pull/863) (`hil-recorder-persistence-result-2026-10-18.md`).
