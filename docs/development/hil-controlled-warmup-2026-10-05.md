# HIL geleidelijk opwarmen — 5 oktober 2026

**Regelwerking, instellingopslag en tijdgrenzen: PASS voor de uitgevoerde
proeven.** Ook automatisch stoppen en beide netwerkprofielen zijn doorlopen.
De zware test rapporteert kandidaat-watermarksommen van 1.660/1.300 B;
dit bewijst geen gelijktijdige vrije heap van 1,3 kB. De lichtere kandidaatproef
houdt een watermarksom van 27.704 B. Meetgrenzen en het bestaande NVS-budget
blijven reviewpunten; dit rapport is geen universele geheugenkwalificatie.

Productbron: `715a108cdbf101edba693614738cb85f4829f63d`.
Referentie `dev`: `63f468d193993eaa0878b24f2262a23fd942a756`.
Dit rapport hoort bij [PR #786](https://github.com/OpenQuatt/OpenQuatt/pull/786).

## Opzet en meetgrenzen

Uitsluitend de desktop-testcontroller `openquatt-test` en de gecombineerde
simulator; geen productieapparaat of echte warmte-installatie. Q WiFi Duo,
twee gesimuleerde ODUs. Elke vergelijking gebruikt een verse applicatieboot
na OTA; geen fysieke koude powercycle.

Native HA betekent hier een versleutelde native-API-fixture met echte
controllercommunicatie, geen volledige Home Assistant-installatie.

Identieke private meetoverlay, normale geheugen-/bron-/veiligheidsklokken,
één begrensde SSE-capture, echte native-API-client, lokale CIC/MQTT-fixture,
Modbus/OpenTherm-verkeer en drie gelijktijdige webrequests (`/`, `/0.js`,
`/0.css`). Assetverkeer liep al vóór de handmatige TLS-manifestcheck.
Succes is vastgesteld via de actuele succesvolle manifestcallback, niet via
installatietellers of alleen een knop-ACK. Per ronde één check.

De observatieproducer draait elke seconde. Gepollde waarden kunnen dubbel
zijn. ESP-IDF 5.5.5 telt bij `heap_caps_get_minimum_free_size` de minima per
heapregio op; die kunnen op verschillende momenten zijn bereikt. Deze
watermarksom is geen echte gelijktijdige globale minimumvrije heap. Actuele
heap, largest block en fragmentatie zijn steekproeven; subsecondpieken en
precies dezelfde allocatiefase in beide runs zijn niet gegarandeerd gemeten.
De HIL gebruikt `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`; standaard Debug
Heap Min Free en UsageTelemetry gebruiken `MALLOC_CAP_INTERNAL`. Productie-
waarden met die andere selectie zijn niet rechtstreeks vergelijkbaar. Learningstatus
blijft een aparte 10s-cache. Loop-, CIC- en MQTT-stackmetingen zijn geen meting
van de TLS-workerstack. Geen nieuwe buffers, taken, stackgroottes of globaal
allocatiebeleid toegevoegd aan de productcode.

## Zware gepaarde netwerkproef, normale klokken

| Meting | Referentie | Kandidaat |
| --- | ---: | ---: |
| Statische RAM met dezelfde overlay | 216.207 B | 217.271 B |
| Som van afzonderlijke interne regio-watermarks | 12.820 B | **1.660 B** |
| Laagste bemonsterde actuele interne heap | 44.855 B | 24.379 B |
| Laagste bemonsterde grootste interne block | 27.648 B | 11.264 B |
| Loop-stackwatermark | 4.150 B | 4.198 B |
| Minimum bemonsterde vrije PSRAM | 6.612.636 B | 6.606.196 B |
| Fysieke automatische stop na CH0 | 339,989 s | 339,170 s |
| Logische CM0/outputs0 na CH0 | 349,631 s | 348,826 s |

Beide proeven: succesvolle TLS-manifestcallback tijdens assetverkeer, beide
ODUs daadwerkelijk in heating, vervolgens normale CH-permissie ingetrokken.
Fysiek stopbewijs is simulator working mode 0 én laatst ontvangen Modbus
`3999=0`; logische applied-levels/CM0 zijn afzonderlijk gelezen.
Geen force-stop als automatische PASS geteld.

De ingestelde minimumlooptijd is 300 s. Power House evalueert de request
elke 60 s; deze cadence wordt vóór de detectie van de gemeten heatingedge
toegepast. De circa 340 s fysieke stop past binnen die bestaande cadence.
De precieze interne starttimestamp is niet gemeten. Terugmelding/CM0 volgt
circa tien seconden later. De eerdere afwijking van ruim tien minuten is
in deze actuele gepaarde proeven niet gereproduceerd; de historische oorzaak
is daarmee niet bewezen.

De drie parallelle assetdownloads herhalen gedurende twee minuten: dit is
zwaarder dan één normale paginalading. De proef is een stresstest, geen
claim dat een gebruiker dagelijks dezelfde verkeerscombinatie veroorzaakt.

Het private veld `intent` is de Power House fast-intentcode, niet de volledige
HeatIntentboolean. Request-, dispatch-, logical applied-levels, gewenste
working modes en fysieke Modbus/ODU-status zijn onderscheiden. Interne
gewenste modes zijn diagnostiek, geen zelfstandig fysiek aansturingsbewijs.

## Eenmalige paginalading met HA en TLS

Dezelfde verzegelde binaries, opnieuw via OTA gestart. CIC en MQTT zijn vóór
de boot uitgezet en na de boot teruggelezen. Eén native HA-client, één HTML-
overdracht gevolgd door JS/CSS eenmaal, één TLS-manifestcheck en normaal
OpenTherm/Modbus-verkeer. Geen herhaalde downloads of SSE-stream.
Beide firmwarechecks hadden een succesvolle manifestclosure.

| Lichter netwerkprofiel | Referentie | Kandidaat |
| --- | ---: | ---: |
| Som van afzonderlijke interne regio-watermarks | 34.632 B | 27.704 B |
| Laagste bemonsterde actuele interne heap | 73.303 B | 69.071 B |
| Laagste bemonsterde grootste interne block | 31.744 B | 31.744 B |
| Loop-stackwatermark | 4.358 B | 4.326 B |

Dit past beter bij één paginalading dan de twee minuten herhaalde downloads.
Het is een begrensde vergelijking: browser-JavaScript is hier niet uitgevoerd,
de opwarmfunctie is ingeschakeld maar zonder actieve sessie, en niet alle
mogelijke bron-/netwerkcombinaties zijn belast. De productiecontroller is niet
benaderd. Zijn circa 39 kB via een andere capselectie is geen identieke meting.

## Functionele regeling en opslag

Echte bronroutes API-input, OpenTherm-thermostaat, versleutelde native
Home Assistant, lokale CIC-HTTP en lokale MQTT-QoS0 zijn geselecteerd en
teruggelezen. De regeling is dus niet beperkt tot Tado.

- Start alleen bij een verhoging boven de ingestelde grens; exact de grens
  start niet. Inschakelen alleen start geen sessie.
- Vaste tussendoelen, verhogen/verlagen, uitschakelen en stoppen binnen de
  echte comfortband. CH uit houdt daadwerkelijke ODU/keteloutputs uit.
- Controller uit, parameterwissel, bron A→B→A, uitgestelde/ingetrokken CH,
  externe vermogensvraag, CM100/manual flow, Force CM98 en stooklijn-bypass.
- Ongeldige HA-kamerwaarde en herstel zonder replay. CIC met receipt 19.0005,
  echte producer/cache-overgang 19.025, alleen ontbrekende kamerwaarde bij
  hetzelfde einddoel 21; annuleren en herstel zonder replay.
- MQTT-inputs via de werkelijke topics, vertrek/terugkeer zonder replay.
  Geen MQTT-TLS-, QoS1/2- of cloud-CIC-kwalificatie geclaimd.

Vier afwijkende instellingen zijn opgeslagen: enabled=true, trigger 2 °C,
step 0,2 °C en step time 10 min. Werkelijke reboot aangetoond door
boot_ms 1.072.382→12.611, alle vier waarden behouden; de sessie hervat niet.
Dit is concrete readback na een herstart, geen langdurige NVS-capaciteitstest.

## Tijdgrenzen met uitsluitend de opwarmklok ×96

Een afzonderlijke tijdelijke timerfirmware wijzigt alleen warmup
`Input.now_ms`. Bronactualiteit, minimumlooptijd, veiligheidsregeling,
netwerk en OTA houden de normale klok. Stapduur in deze proef: 60 min,
dus 37,5 s echte tijd; vaste sessielimiet 8 uur, dus 300 s.

Gemeten kamer 19,0 °C, thermostaatdoel 21 °C:
19,1→19,2→19,3→19,4→19,5 °C. De eerste hogere stap is gezien na 52,197 s
vanaf de doel-write en niet vóór de 37,5 s-grens. De vaste maximale
stap 0,5 °C is bereikt en niet overschreden. Eerste inactive na 308,713 s
vanaf de write; coherent einddoel 21 °C na 311,269 s. De extra tijd omvat
bron-/diagnostiekcadence, niet een wijziging van de limiet.

Alle samples: CH=false, CM0, applied 0/0, boiler=false/boilerHeat=false,
simulator working modes 0 en Modbus 3999=0. Dit bewijst deadlinegedrag;
geen echte achtuursduurproef of besparingsmeting.

## Geheugenbeoordeling

De nieuwe controlpaden bevatten kleine vaste structuren en scalars; de
onafhankelijke review vond geen directe nieuwe dynamische allocatie.
De statische delta is 1.064 B. Extra template-entities en callbacks tellen
wel mee voor de runtimefootprint. Dit verklaart de grotere gemeten piek niet.

Kandidaat: de watermarksom van 1.660 B verschijnt tijdens een nog lopende
TLS-check; bij de eerste observatie is de actuele vrije heap 24.379 B en
largest block 11.264 B. Dat bewijst geen globaal vrij geheugen van 1.660 B.
Referentie: de uiteindelijke watermarksom van 12.820 B ontstaat later dan de
TLS-closure. De oorzaak en een echte globale minimumwaarde zijn niet gemeten.

Deze semantiek is in de werkelijk gecompileerde IDF 5.5.5-bron van beide
artifacts gecontroleerd: `components/heap/heap_caps.c:289–298` sommeert
`multi_heap_minimum_free_size`; de waarschuwing staat expliciet in
`include/esp_heap_caps.h:222–229`. Bron-SHA256:
`a1ddc96cda0df2758552c611e409bde6358b8b18145470f928a33cb59628f2a1`.

Een korte extra netwerkvergelijking gebruikt exact dezelfde verzegelde
firmwares en 120 s belasting, zonder opnieuw de vijfminutenstop te doorlopen.
| Herhaling netwerkdeel | Referentie | Kandidaat |
| --- | ---: | ---: |
| Som van afzonderlijke interne regio-watermarks | 6.856 B | **1.300 B** |
| Laagste bemonsterde actuele interne heap | 48.067 B | 43.987 B |
| Laagste bemonsterde grootste interne block | 16.384 B | 21.504 B |
| Loop-stackwatermark | 4.182 B | 4.198 B |

Beide TLS-checks hadden een succesvolle manifestclosure. De referentie
varieert van 12.820 naar 6.856 B; de kandidaat bereikt in beide boots slechts
1.660/1.300 B. De lage watermarksom is opnieuw gezien, maar dat is geen bewezen
bijna-uitputting. Timingvariatie en verschillende allocatie-/meetmomenten
bewijzen geen vaste door deze feature veroorzaakte heapdelta. Geen crashes
gezien is geen voldoende marge. Meer vrije PSRAM vervangt de interne
DRAM die netwerk/TLS/FreeRTOS nodig hebben niet.

## Softwarevalidatie

Hostregressies 106/106, Pythoncontracten 379/379, webtests 765/765,
websmoke, actuele productie-assets, C++format en docschecks geslaagd.
Directe ESPHome 2026.9.0 Q Single/Duo-configvalidatie en
[CI inclusief firmwarecompiles](https://github.com/OpenQuatt/OpenQuatt/actions/runs/37343427875)
geslaagd. Complete diff en onafhankelijke koude review uitgevoerd.

Gebruikersuitleg in Nederlands en Engels bevat getalvoorbeelden:
17→20,5 is groter dan 1,5; 19→20,5 is exact 1,5 en start niet.
Kamer18,0 + stap0,1 geeft tussendoel18,1; bereiken schuift direct door.
Na45min zonder bereiken groeit de stap naar0,2. Inschakelen alleen start geen
sessie. Productie-interface gecontroleerd op desktop/mobiel390 in Chrome,
NLlicht/ENdonker; invoer/cursor blijven behouden bij live statusverversing.
Safari/iOS niet gecontroleerd.

Het bestaande NVS-budget faalt al op dev: 519 entries / 111 vrij; kandidaat
522 / 108 vrij, vereist 126. De feature voegt één blob van 3 entries toe.
Een herstart-readback kan de concrete instellingen controleren, maar
kwalificeert geen langdurige NVS-slijtage/garbage-collection of capaciteitsmarge.
Geen partitionmigratie of ongerelateerde budgetverruiming in deze PR.

De algemene config-onlyvalidatie stopt op bestaande stylebevindingen in
`oq_sensor_sources.yaml` en `profiles/heatpump_controller_q.yaml`, plus een
lokale niet-gecommitteerde HIL-config. Directe ESPHomevalidatie hierboven slaagt.

## Artifactidentiteit

Alle applicatiebinaries gebruikt ESPHome 2026.9.0/IDF 5.5.5; Q WiFi Duo.
Simulator daadwerkelijk gelezen: v0.5.0,
`openquatt-modbus-opentherm-v2`, profielen V1.5/V1.5. Geen simulatorflash;
geen simulator-binary-SHA-readback geclaimd.

| Binary | Config-hash | OTA-bytes |
| --- | --- | ---: |
| Referentie met meetoverlay | `0x1bf8c2fb` | 2.434.320 |
| Kandidaat met meetoverlay | `0xfa74c3c9` | 2.441.472 |
| Timer ×96 | `0x1b7cccc9` | 2.441.072 |
| Normale kandidaat zonder meetoverlay | `0x8dd26314` | 2.439.920 |

OTA-SHA256, in dezelfde volgorde:

- `c158dab10034c1e06f44e98724c36c5ee34752f25bedd80f5bfc8d54229d521f`
- `d86fd4c780541cc4e53c362c9c68d9db51438e2651fc4ea3696318ebf54f40cc`
- `ab65459445c7ba2f61826de1941c66ce3256c336d4bd8d0e2c12726c274aa919`
- `7dd5a2d2a4df7a4a49205ea42e526804b9285ca53a6b8a7969a4fe6fc3c2d4c7`

Configbestand-SHA256, in dezelfde volgorde:

- `cc63586fdb1c65e62a83d915c1909090ee6c1bbd8c16d96b14c8ab8bd233e4a0`
- `492a2b9c6af3666114c65b677dd40bfb8c027fba764cf475163204698e5907bc`
- `67076b11d1612d97bcc822a89fb6893839452ce9f34763ac356fb03a44e4bc77`
- `179abea5d89d74bcb6b7627e01aa470dee1584c05f48a1c7a92c74090de5b847`

Product-YAML vóór én na de tijdelijke compile, byte-exact:
`a2420a9e0f1625e01fb1c6eb5198b79f88ba266eca8b435f09dd3b7a85318c52`.
Tijdelijke clockvariant:
`288ea2cfb0f31400d32812728e825c3b835789d065a28ea1e7896f9edf450fe4`.
Geen testinstrumentatie, fixtures, ruwe logs of binaries in de feature-PR.

## Rapportidentiteit en eindstate

Lokale metadata: `.tmp/hil/pr786-v1/completed-results.json`.
Ronden hieronder liggen in `.tmp/hil/pr786-v1/runs/<ronde>/report.json`.
De mapnaam is `2026-10-05T<tijd>-<scenario>` (zonder spaties).
Alle negen hebben success=true, safeCleanup=true en lockReleased=true.
Dit zijn assertions voor de beschreven scenario's, geen blanketgeheugen-PASS.

| Ronde (UTC) | Report-SHA256 |
| --- | --- |
| 17-31-34-015Z matched-baselineNormal | `de30c718a7085fd24991747e4587d74e3b2d31c534e17c0f00c658393002cf4c` |
| 17-46-39-826Z matched-candidateNormal | `5146b2281f5db4e9dc9e6c34d0c7ff3ee7d13a8ab4efea2a25785ccabc2638e6` |
| 17-57-32-747Z matched-baselineNormal | `fa38829725fdb9c972a497e01c97f71b52419e42b99f1c257e549b67cf0953eb` |
| 18-01-09-057Z matched-candidateNormal | `d2286984eff6174d01552dce9aba4c231523bb3c6baa2676d096356c2b2af507` |
| 18-04-56-844Z functional-candidateNormal | `eaa1aa248ae554546af81843c57ef5e2d1eae5aaf12a91cc8af078c0c530425f` |
| 18-24-03-989Z matched-baselineNormal | `b5cf90c3a60d6b6354d8acd76523db1f002efe7ebeee9d9a10192528b235747f` |
| 18-25-40-703Z matched-candidateNormal | `bc750c3f14dfc418068c7278226fc034a309677ab7d48eaf7ed40e8dd64cc15a` |
| 18-27-24-378Z timer-timer96 | `a72e89ce41ec8928848f9f57d078315855cd812e3b8c3aeb7fb2a7b233b16660` |
| 18-37-24-529Z canonical-canonical | `eb753409102ae51b0813d7416250ed9a86c7c52c9cf5974bb8b09768b408f80a` |

Afgebroken eerdere ronden wegens bootcache/onjuiste TLS-teller/interne
REST-selects tellen niet als vergelijkingsbewijs. Acht offline interruption-/
cleanup-fakes en syntaxcontrole zijn geslaagd; onafhankelijke koude reviews
van productdiff, tooling, meetsemantiek en finale zijn uitgevoerd.

Om 18:38:23 UTC is de normale kandidaat zonder meetoverlay bevestigd:
config 0x8dd26314, geen HIL-profiel, normale klok, geen private native-keyoverride.
Force CM0/CM0, warmup inactive, target 21, applied 0/0, ketel/heat false.
Alle zes services uit; beide ODUs working mode 0/last Modbus 3999=0,
drop/exceptions/defrost 0. CIC/MQTT uit, eigen fixtureprocessen 0 en lablock vrij.
Gewone testinstellingen zijn niet teruggezet; de volgende proef stelt haar
eigen inputs/selections in. Werkboom en product-YAML zijn schoon/hersteld.
