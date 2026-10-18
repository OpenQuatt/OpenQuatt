# HIL-stooklijnbootadoptie — 18 oktober 2026

Status: baseline en twee kandidaatbootcases zijn gemeten. De eerste kandidaatcase blijft verstoord; de herhaalmeting bemonstert de nominale 180s-hold en daarna zonder waargenomen peerstart/ownerstop. Matched load heeft nog GEEN GESLAAGD GEKWALIFICEERD COHORT.
De ruwe collectors behouden `NOT_EVALUATED`; de afgebakende beoordeling hieronder volgt uit primaire en onafhankelijke controle van de frozen data.
Alle tijden hieronder zijn UTC; baseline 17 oktober, kandidaat 18 oktober 2026.

## Herkomst en vaste opstelling

- Controller: `openquatt-test.local` / `192.168.2.86`; simulator: `192.168.2.63`.
- Doel: Q-edition, WiFi, Duo; ESPHome `2026.9.0`, ESP-IDF `5.5.5`.
- Baselinebron: `dbe66c23311e8c6713d8f51fc8f7f45309fdccb8`; config `0x675eeb85`.
- Genormaliseerde observation-v2-overlay-SHA256: `148b7f2cd34054fc2f053d6f4c09e53eadeb901c4d4b639c80f1c5927f5448fd`.
- OTA: 2497888 bytes; SHA256 `9658ca8c4c6cd04af6074ea693433e57d5cd5e63ded4278993b91c5ac310a475`.
- Case: `baseline-v2-fixed-pwm-hp2-owner-ota-f58d8e55-cd34-40d7-8fe2-af4e8cff7a87`.
- Raw casebestanden staan in `quality-integration-check/.tmp/quality-validation/hil/2026-10-17-quality-three-prs/<case>/`.
- Frozen `result.json`-SHA256: `8654070291750cf7d82a2428ea92e429f17f8baebcdc985335353eb9c88ad9d0`.
- Frozen helper-SHA256: boot `c0c17dad05065f0a9d0947ee1b0b8a927a378bcafbf14afbc801097a4d8fd019`; observer `8f8fc350091c63a81c53290a876824a12aa1bb401d58a5fca06bc0b19c252aee`.
- Bronnen: `boot-config.json`, `boot-adoption-source.mjs`, `observe-source.mjs`, `result.json`, `reboot-window.jsonl`, observatie-JSONL en controllerlog; bijbehorend baseline-v2-firmwaremanifest.

## Bootmeting

| Onderdeel | Baseline | Kandidaat, eerste case 21ed7b72 |
| --- | --- | --- |
| Firmware en bron | Bovenstaande manifestidentiteit | `7ab5e250cfde5057b884669223eb101f3ec49ac7`, config `0x82232815` |
| Beginconditie | HP2 actief, HP1 idle én runtime lead; normale ODU-responses, geen manual/frozen/faulttelemetrie | Zelfde preconditie, beide healthy/startready; raw `boot-preconditions` bevestigd |
| Bootbewijs | Zelfde-image-OTA; uptime 0.351669 → 0.017633 h, nieuwe CSRF-identiteit, waargenomen na 83.163 s | Zelfde-image-OTA; uptime 0.168283→0.001791 h, nieuwe CSRF, 82.665 s |
| Extra peerstart | HP1 requested/accepted/Hz 0/0/0 → 1/1/20 | Niet waargenomen tot 01:14:06.125; daarna HP1-start na HP2-stop/SUSPECT, vóór nominale holdexpiry |
| Gelijktijdige capaciteit | Beide accepted>0 én Hz>0 in 93 samples; beide Hz>0 in 94 | 0 samples met beide accepted>0 of beide Hz>0; één sample beide fysiek 0 |
| Observer | 110 samples, 360.455 s, 0 leesfouten | 110 samples / 360.678 s / 0 leesfouten; 9 supply-NA- en 10 curve-NA-samples |
| Eindtoestand | HP1 blijft1/1/20 Hz; HP2 wordt0/0/0 Hz | HP1=1/1/20 Hz, HP2=0/0/0 Hz; HP2 recovering/must_stop in laatste incidentsample |

De laatste fysieke HP1-nulreadback is 23:44:25.619; de eerste1/1/20 Hz-readback is 23:44:29.450.
HP2 blijft daarnaast accepted 1 / 20 Hz tot de sample van 23:49:30.132; dit overlappende samplevenster beslaat 300.682 s.
Om 23:49:33.976 is HP2 requested 1 / accepted 0 / 20 Hz; om23:49:36.926 zijn alle drie 0.
HP1 requested/accepted/Hz is positief in 104/110 samples en blijft positief tot de laatste sample 23:50:06.400. De 93 overlappen met beide accepted>0 én Hz>0 verschillen van 94 rijen met beide Hz>0: om 23:49:33.976 is HP2 accepted=0 terwijl zijn Hz-readback nog 20 is.
Dit is een waargenomen extra fysieke simulator-peerstart, geen uitsluitend softwarematige aanvraag.

Alle 110 samples tonen CM2, RECOVERY, runtime lead HP1, curvevraag 3–4 en beide pomprelais aan.
Flow blijft 795.9839 bij Manual PWM 154; lowflow blijft false. Dit bewijst geen automatische pompregeling.
Supply loopt 30.04–32.48°C met bron `hp2 water out (fallback)`; dit is geen Local-sensorbewijs.
OT-requests 1536→1890 en responses 1483→1837; timeout 0, invalid 6 onveranderd, maximale antwoordleeftijd 1020 ms.

## Tijddekking en grenzen

- Rebootwindow: 25 fysieke simulator-snapshots, 0 simulatorleesfouten; 17 controllerpollfouten tijdens OTA/boot.
- Maximale afstand tussen succesvolle rebootwindow-snapshots: 12.050 s; observerrijen maximaal 3.854 s.
- Observerrijen lopen 23:44:09.813–23:50:06.400, samen 356.587 s tussen eerste en laatste rij; looptijdbudget 360.455 s.
- Alle relevante numerieke controller- en simulatorwaarden zijn eindig; geen `NA`/null naar 0 omgezet; 0 directe fallbacklezingen.
- Log meldt HP2-adoptie 23:44:20 en HP1 measured compressor start 23:44:22; laatste observatie 346.4 s na de adoptielog.
- Publicatie-, log- en GET-tijden zijn verschillend; fysieke velden worden afzonderlijk gelezen. Geen atomaire snapshot of exacte starttijdclaim.
- Oude 180s-logcapture: 4 verbindingen, 5 fouten, 186.16 s looptijd; reconnectgaten, geen continue logdekking.
- Normale simulatorflags zijn vóór de stimulus gecontroleerd; er is geen continue hercontrole van alle flags.
- Beide bootcases betreffen HP2-owner/HP1-idle-lead. De omgekeerde richting is met deze resultaten niet fysiek onderbouwd.

## Kandidaat, eerste case: eerste acquisitie behouden, volledige 180s-invariant niet bewezen

Case `curve-v2-fixed-pwm-hp2-owner-ota-21ed7b72-8da0-4a70-a76c-1b08a67fe13f`; OTA 2498400 bytes, SHA256 `065b5f6ef7a50a3a5c105c54ca78bdebe82ae30f95be370ad816514bc81c9ac9`.
Frozen result-SHA256 `0197937c812125b846a1b66c621310f47575c8b685ba0cfe4c538f12dc9bf40c`; helpers en genormaliseerde overlay zijn byte-/hashgelijk aan baseline.
Vóór OTA: HP2=1/1/20 Hz, HP1=0/0/0 Hz/idlelead/startready, CM2, curve 3, flow 795.9839; normale simulatorflags. Manual PWM 154 blijft in alle 110 observaties gelijk.
Rebootwindow 25 samples / 0 simulatorleesfouten / 17 controllerpollfouten / maxgap 10.806 s. Observer 356.668 s tussen eerste/laatste rij, maxgap 3.957 s; fysieke waarden alle eindig, 0 fallbacklezingen.
Adoptlog 01:11:33; HP2 blijft naast idle HP1 in 51 samples behouden tot 01:14:06.125. Laatste healthy incidentsample 01:14:00.167; eerste SUSPECT/stopping 01:14:09.983.
Dan HP2 requested 1 / accepted 0 / Hz 0; om 01:14:12.957 beide geheel 0; HP1 wordt 1/1/20 Hz om 01:14:15.951 en is positief in 57 samples. Geen fysieke overlap waargenomen.
Nominale 180s-hold zou op basis van de adoptielog tot 01:14:33 lopen; ownerstop is circa 23 s eerder. Dit is geen bewezen reguliere holdexpiry.
HP2-diag laatste healthy→eerste SUSPECT: req 383→389, read 357→361, write 26→28, drop 13→13; age 6195→3557 ms; stopwrites W1999=0/w3999=0 zichtbaar. Dit zijn geen per-veld-freshnesstijden.
Bron: incidentengine:445 maakt SUSPECT bij incomplete linkround; :658/:660 laat must_stop=false/startavailable=false toe. SUSPECT alleen bewijst de stopoorzaak niet: candidate-logica behoudt bestaande capaciteit.
Actuator:500–507 laat STOPPING of live boot-safetyverlies boven de hold prevaleren; safetycapture vereist 15s-verse kritieke telemetrie. Welke gate verloren ging is niet vastgelegd: geen causale PR-schuld- of safetyfailoverclaim.
Later HP2 SUSPECT 01:16:27.547, LOST/must_stop 01:16:57.079; supply NA in 9 samples 01:16:30.495–01:16:57.079 en curve NA in 10 samples tot 01:16:59.952. 0 leesfouten betekent hier geen complete geldige context.
CM1/CM2=12/98 samples; flow/pompen blijven gelijk, geen lowflow. OT req 916→1271 / rsp 862→1217, maxage 1045 ms, timeout 0 / invalid 6 gelijk; OT-voortgang bewijst geen gezonde ODU-link.
Laatste observatie 346.603 s na adoptie; oude SSE 6 connecties / 6 fouten / 182.06 s is aanvullend en mist de overdracht. Beoordeling: beperkte eerste-acquisitiewaarneming, volledige 180s-/na-expiry-invariant NIET BEWEZEN.

## Kandidaat, herhaalmeting: geen peerstart/ownerstop in bemonsterd hold- en vervolgvenster

Case `curve-v2-fixed-pwm-owner2-ota-log-v2-5441343f-991b-4620-b7f1-f4f5c940bf50`; dezelfde bron `7ab5e250cfde5057b884669223eb101f3ec49ac7`, config `0x82232815` en OTA-SHA256 `065b5f6ef7a50a3a5c105c54ca78bdebe82ae30f95be370ad816514bc81c9ac9` / 2498400 bytes.
Frozen result-SHA256 `c7971ecfd9232aca0bf705251175d8647b686f3e31e9c280f6a2f61ddc97713a`; bootcollector-v2 `8df4b068602346092d73d40515dca15e450fe80614b2ba24b4943f6e2255b03f`; observer onveranderd `8f8fc350091c63a81c53290a876824a12aa1bb401d58a5fca06bc0b19c252aee`.
Preconditie 02:40:10.573: HP2=1/1/20 Hz, HP1=0/0/0 Hz/idlelead/startready, beide healthy, CM2, curvevraag 3, OT-room/setpoint 20/20.5, flow 795.9839; normale simulatorflags, geen manual/frozen/faulttelemetrie.
OTA-ACK plus onafhankelijk rebootbewijs: uptime 0.167637→0.001210 h, nieuwe CSRF, exacte config terug na 84.399 s. Dit is geen bootclaim op uitsluitend uploadsucces.
Alle 26 reboot-snapshots en 110 postbootrijen tonen HP1 requested/accepted/Hz=0/0/0 en HP2=1/1/20; geen peerstart, ownerstop of overlap waargenomen. Reboot 0 simulatorleesfouten / 17 controllerpollfouten / maxgap 10.471 s; aansluiting op eerste observerrij 3.845 s.
Postboot: 110 samples tijdens de 361.089s-observatiefase (rawspan 357.182 s; rijen 02:41:42.027–02:47:39.209), maximale rijafstand 3.885 s, 0 leesfouten/fallbacklezingen. Alle verplichte controller-/simulatorvelden zijn eindig zonder null/NA; incidents en ODU-diagnostiek zijn bewust slechts in 37 rijen gelezen.
Curve-aanvraag blijft HP1/HP2=0/1; gepubliceerde HP1 requested/applied blijft 0/0. HP2 requested/applied is de eerste 5 rijen 0/0 en vanaf 02:41:57.821 1/1, terwijl de onafhankelijke fysieke readbacks steeds 1/1/20 blijven; afzonderlijke publicatie-/GET-tijden, geen atomaire snapshot.
Adoptlog 02:41:52; nominale 180s-grens 02:44:52. Conservatieve holdbrackets 02:41:48.003–02:44:54.894, maxgap 3.880 s; er zijn 4 rijen vóór adoptie, 55 binnen het nominale holdvenster en 51 erna; laatste rij 347.209 s na adoptie en 167.209 s na die grens. Dit bemonstert de volledige tijdsperiode; transiënten tussen GETs zijn niet uitgesloten.
Alle 110 rijen: CM2, RECOVERY, vraag 3, supply 32.48°C met `hp2 water out (fallback)`, beide pomprelais aan, Manual PWM 154, flow 795.9839 en lowflow=false. Geen Local-sensor- of automatische-pompregelingsbewijs.
HP2 is in de eerste incidentsample recovering en vanaf 02:41:51.859 healthy/running; HP1 vanaf 02:42:01.671 healthy/stopped/startavailable. Laatste incidentsample 02:47:36.271: beide healthy, must_stop=false. Geen SUSPECT/LOST waargenomen in deze 37 incidentsamples.
OT req 748→1104 / rsp 695→1051, monotone counters, maxage 1014 ms; timeout 0 / invalid 6 gelijk. ODU-diagnostiek: HP1 drop 28 en HP2 drop 0 onveranderd; maximale any-request-age 6219/7063 ms. Dit is geen continue bus- of per-veld-freshnessclaim.
SSE-v2: 2 verbindingen / 1 reconnect in het OTA-venster, ConnectionError 1 / ConnectTimeout 2, 460.511 s; eigen child gesloten/gejoined, geen forced kill, uncertain=false. Capture bevat 9 component-long-operation-WARNs van 61–92 ms en geen E-regels. Hij blijft aanvullend, geen continue logclaim; 540s/20s-timeout verschilt van de oude baselinecapture van 180s/15s-timeout.
Begrenzing: alleen HP2-owner/HP1-idle-lead op simulator, onder bovenstaande vaste fixture. Eerste kandidaatcase 21ed7b72 met SUSPECT/stop/NA blijft afzonderlijk geldig bewijs met beperkingen; geen vervanging of onvoorwaardelijk globaal PASS. Geen gematchte memory-/loadvergelijking.

## Host- en brondekking

`tests/python/test_heating_curve_boot_resume_runtime.py` in kandidaatbron `7ab5e250cfde5057b884669223eb101f3ec49ac7` bouwt dezelfde harness voor Duo en Single.
Hij gebruikt echte productie-methoden `dispatch_tick`, `reset_request_`, boot-update, `resume_boot_cycle_` en `apply_level_`.
Peripheral entities, voorspellingen en hardwarewrites zijn stubs; dit vervangt geen hardware- of volledig ESPHome-integratietest.
Bronmatig aanwezige scenario's: beide Duo-richtingen; pending/adoptie tussen dispatch en tick; urgente recadence; safetyabort/expiry;
ontbrekend F1-model; verboden frequentie; legitieme extra capaciteit; twee beschermde bootcycli met gespreide expiry;
gekoppeld pair-model versus ontbrekend Single-model; Single zonder peerstart.
Aanvullend staan candidate-captureasserties in `tests/host/hp_candidate_logic_test.cpp`.
Validatie op de productiebron: `python3 scripts/run_tests.py host python`, 127 hostprogramma’s en 475 Python-tests groen. [Linux-CI 39061558781](https://github.com/OpenQuatt/OpenQuatt/actions/runs/39061558781) is groen, inclusief ASan/UBSan/LSan en Single-/Duo-firmwareprofielen. Dit blijft aanvullend host-/buildbewijs, geen vervanging van de fysieke meetgrenzen.

## Bestaande HTTPD-grens, afzonderlijk van de bootcase

De HTTPD-task heeft 4352 B interne DRAM-stack: ESP-IDF 4096 B plus ESPHome 256 B; geen PSRAM-stack.
Afzonderlijke baseline-v2-waarnemingen: idle 90 samples met 524 B; oude recorder-backend-plus-SSE-run 210 samples met 396 B. Een latere afgebroken voorbereiding rapporteerde 316 B; zie de afzonderlijke grenzen hieronder.
Raw bronnen: `memory-window-8f2e8d73-9a59-4d36-ba8d-40fa6e7af711` en `recorder-backend-81379192-a97d-4349-a011-123a2ae13b79` onder dezelfde campaign.
Dit is een cumulatief minimum sinds taskstart, geen actuele vrije stack of bewezen gezonde worst-case-marge.
De bootcase had de memorymonitor uit; zijn rijen bevatten geen zelfstandige HTTPD-meting.

## Kandidaat: afzonderlijk 60s-idlevenster na normale stop

Raw `memory-window-9e1a3520-30ae-411c-b10f-462a07ec90bd`; ledger-SHA256 `3318b0421a535d400c20c29b2026ea9ed99135aded50ccdb1efcb8736b2be607`.
Zelfde config `0x82232815`, alle 60 rijen CM0; uptime niet dalend en de helpergate voor dezelfde CSRF-identiteit bleef geldig. Geen restart onderdeel van deze meting.
60/60 eindige meetrijen in 60.028 s; 0 lees-/cadencefouten of gemiste slots; maximale startafstand 1003.913 ms, leestijd 252.516 ms en startvertraging 6.059 ms.
Internal current free 87999–88023 B (mediaan/start/eind 88023 B); cumulatief minimum 39828 B; largest internal block 32768 B; PSRAM free 5341976 B. Loop-HWM 4022 B en HTTPD-HWM 520 B, alle vijf laatste waarden constant; bytes zonder factor 4.
Monitor vóór uit, tijdens alle meetrijen aan, daarna uit bevestigd met 1500ms-barrière. Normale simulatorflags en beide requested/accepted/Hz exact 0 vóór en na; elk grenssnapshot omvat 21 afzonderlijke GETs en duurde circa 5.3 s.
OT req 1479→1551 / rsp 1425→1497; timeout 0 / invalid 6 gelijk, grensleeftijden 440/796 ms. Fysieke nulstanden zijn geen continue 1Hz-waarneming.
Dit is een afzonderlijke idlemomentopname na de bootcase met SUSPECT/stop- en NA-context, geen gematchte actieve baselinevergelijking. Cumulatieve minima bevatten eerdere belasting; geen worst-case-, allocatiemarge- of gezonde-stackclaim.

## Kandidaat: afzonderlijk 10s-idlevenster na de volledige herhaalmeting

Raw `memory-window-95d01802-306a-4aee-bb06-bdb74cabfd56`; ledger-SHA256 `5453bca4faa4d079a4736d339b83fc8bd264e40821524d2f65f964bc491dfac9`.
Na normale stop: config `0x82232815`, alle 10 rijen CM0, dezelfde-CSRF-helpergate geldig; de gecachte uptime blijft 0.117877 h en bewijst zelfstandig geen tijdsvoortgang.
10/10 eindige meetrijen in 10.026 s; 0 lees-/cadencefouten of gemiste slots; maxgap 1001.437 ms, leestijd 251.401 ms, startvertraging 4.220 ms. Rijen 02:49:18.829–02:49:27.602.
Internal current free 88691 B, cumulatief minimum 39828 B, largest internal block 32768 B en PSRAM free 5341976 B zijn constant. Loop-HWM 4022→3990 B (range 3990–4022 B); HTTPD-HWM constant 520 B. Alle waarden zijn bytes, geen factor 4.
Monitor vóór/na uit, tijdens alle rijen aan; OFF-readback na 1500ms-barrière bevestigd. Beide fysieke requested/accepted/Hz-drietallen vóór/na exact 0, normale simulatorflags; iedere fysieke grenslezing is 21 losse GETs over circa 5.3 s.
OT req 1199→1222 / rsp 1146→1169, timeout 0 / invalid 6 gelijk; grensleeftijden 518/376 ms. Cumulatieve minima omvatten de voorafgaande boot-/SSE-belasting; deze korte idlemeting is geen gematchte actieve baseline, continu fysiek bewijs of worst-case-/allocatiemargebewijs. Het eerdere 60s-venster bij de verstoorde eerste case blijft afzonderlijk staan.

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

## Dekking en openstaande kwalificatie

| Onderdeel | Resultaat / verwijzing |
| --- | --- |
| Kandidaat: bron/config/OTA-SHA, beginfixture en richting | Beide HP2-ownercases vastgelegd; herhaalmeting gebruikt afwijkende SSE-v2-load |
| Kandidaat: fysieke peerstart, ownercontinuïteit en postadoptieduur | Eerste case verstoord; herhaalmeting zonder waargenomen peerstart/ownerstop tot 347.209 s na adoptie, met expliciete meetgaten |
| Gematchte baseline-/kandidaatload: twee web/exportslots + één SSE, bestaande HA en private MQTT | GEEN GEKWALIFICEERD COHORT; afgebroken voorbereidingen afzonderlijk bewaard |
| Gematchte internal free/min/largest, PSRAM en loop-/HTTPD-HWM met allocatiemarge | ONBEWEZEN; geheugen-/releaseacceptatie geblokkeerd |
| Afgebakende primaire en onafhankelijke beoordeling | Baselineregressie bevestigd; herhaalde sole-HP2-bootadoptie en volledige bemonsterde hold SCOPED PASS |
