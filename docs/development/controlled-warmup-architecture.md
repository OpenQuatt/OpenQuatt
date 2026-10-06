# Geleidelijk opwarmen v1

De source resolver levert een coherent `RoomControlSnapshot`: geselecteerde
waarde, validiteit, actuele producer/link, held-status en configuration generation.
CIC-receipts en de 0,001 °C-publicatietolerantie blijven in de sourcelaag.
Een geldige producer mag één sensorupdate vóór de selected cache lopen; een
ontbrekend veld maakt currentness direct onwaar. HA-kamertemperatuur gebruikt
de live ingress-heartbeat; het setpoint blijft stateful.

`Runtime::update(Input, Settings)` is het enige pad dat de toestandmachine
evalueert. De 1s interval in `oq_warmup.yaml` is de eigenaar, ook in standby,
koelen en service. Strategieën en diagnostiek lezen pure getters. De YAML-tick
maakt alleen de inputadapter; stappen, edges en timers blijven pure C++.

Power House kan de nieuwe thermostaatwaarde vóór de volgende 1s-update lezen.
De pure `control_target`-getter houdt bij een voldoende grote verhoging dan
tijdelijk het vorige doel aan en onderdrukt fast-start. Bijvoorbeeld: 17→20 °C
blijft vóór de update op 17 °C begrensd; de update start vervolgens bij een
kamertemperatuur van 17 °C met een tussendoel van 17,1 °C. Getters starten geen
sessie en veranderen geen timers. Uitschakelen of invalidation heft deze
voorlopige begrenzing meteen op; de volgende update legt een nieuwe baseline vast.

Callbacks vragen invalidation aan: het oude resultaat is onmiddellijk verborgen,
maar alleen de volgende update reset de state en legt een nieuwe baseline vast.
Dit bewaart bron-/moduswisselingen die tussen twee ticks plaatsvinden.

| Callback | Reden |
|---|---|
| Room/setpoint source | Generations detecteren ook A→B→A; invalidation verbergt daarnaast het oude target vóór de volgende tick. |
| Heating enable source | Permission-source identity zit niet in room/setpoint generations; wisselen mag geen bestaande sessie behouden. |
| External heat demand source | Externe demand bypass; ook Disabled→extern→Disabled tussen ticks reset de baseline. |
| Heating mode switch | Power House→stooklijn→Power House tussen ticks mag geen sessie hervatten. |
| Enabled en drie nummerinstellingen | Elke expliciete wijziging reset baseline; aanzetten speelt geen oude verhoging af. |
| OpenQuatt Enabled en CM override | Ook uit→aan en Auto→Force CM0→Auto tussen ticks annuleren de sessie. |
| CM100, manual HP, manual flow en quick flow | Elke expliciete start/stop annuleert de sessie, ook wanneer de serviceaanvraag wordt geweigerd of vóór de volgende tick alweer stopt. |
| Boot | Opgeslagen instellingen toepassen; sessie en vorige setpoint zijn nooit persistent. |

De heating-supply-source callback is verwijderd: v1 werkt alleen met Power House
en die selector bestuurt uitsluitend de stooklijn. Normale mode-detection stopt
bij CM buiten 0..3, handbediening, koelen of uitgeschakelde OpenQuatt.

V1 ondersteunt uitsluitend Power House. De oorspronkelijke stooklijnintegratie
wijzigde hoofdzakelijk ruimte-stop/herstartgrenzen, terwijl het waterdoel gelijk
kon blijven; dat bewijst geen begrensd opwarmvermogen. Vier instellingen zijn
publiek: enabled, trigger, stap en staptijd. Maximum offset (0,5 K) en duur (8 uur)
zijn compile-time constanten. De runtime kent geen strategiecodes; de adapter en
mode-callback bewaken uitsluitend automatische Power House-regeling. Twee publieke
meetwaarden tonen actieve sessie en effectief doel. Stopredenen blijven intern.

Bij de standaardinstellingen geeft 17→20,5 °C een verhoging van 3,5 °C en start
opwarming; 19→20,5 °C is precies 1,5 °C en start niet. Bij 18,0 °C gemeten wordt
het eerste tussendoel 18,1 °C. Zodra dit is bereikt, volgt direct de volgende
stap. De 45 minuten zijn een termijn om het tussendoel te halen, geen vaste
wachttijd: is de kamer na 45 minuten nog 18,0 °C, dan groeit de stap van 0,1 naar
0,2 °C en wordt het doel 18,2 °C. Alleen inschakelen terwijl de thermostaat al op
20,5 °C staat start niets; een nieuwe voldoende grote verhoging is nodig.

Het bestaande zes-float opslagblok blijft compatibel om een NVS-migratie te
vermijden. Validatie van het volledige blok blijft fail-closed; slots 4/5 worden
na geldige restore genormaliseerd naar de vaste limieten.

Power House gebruikt het effectieve target voor room feedback. Heat intent krijgt
het echte target voor gebruikersedges; tussenstappen veroorzaken geen fast-start.
Tijdens en direct na warmup wordt comfort memory gereset. Run extension blijft
op het echte target werken. Permission, dispatch en boiler support blijven downstream.

## Releasekwalificatie

Volgens de [werkafspraak bij #761](https://github.com/OpenQuatt/OpenQuatt/blob/9d39292e9fba7c316df0187169bd321402c0e30d/hil-archive-761/README.md)
is gericht desktop-HIL-bewijs een reviewvoorwaarde voor deze controlwijziging.
De bestaande basisharness blijft behouden. Nieuwe scenario's worden pas permanent
opgenomen na herhaald gebruik voor een stabiel contract. De tijdelijke warmup-
en HA/CIC/MQTT-scenario's, fixtures en overlays zijn daarom buiten `dev`
[gearchiveerd](https://github.com/OpenQuatt/OpenQuatt/blob/99b30ff75336384b39872956dc7fadbc1b7c9baa/hil-archive-786-787/README.md).
Het generieke source-contract en de compacte host-/web-/contracttests blijven
bij de feature. Dit archief is geen nieuwe hardware-PASS.

Kies voor de actuele firmware de kleinste relevante HIL-proef en leg
controller-SHA/config-hash/binary-SHA256, simulatorversie/SHA/profielen,
instellingen, assertions, failure boundaries, eindtoestand en duurzame
geredigeerde rapporten/logs met checksums bij de PR vast.

De [actuele HIL-resultaten van 5 oktober](hil-controlled-warmup-2026-10-05.md)
vergelijken dev `63f468d1` met productbron `715a108c`: vijf echte inputroutes,
instellingopslag na reboot, automatische stop en een afzonderlijke timerproef.
De normale firmware is na afloop teruggezet; testinstrumentatie blijft buiten Git.

De eerdere heapcijfers en de nieuwe stresswaarden zijn sommen van afzonderlijke
regio-watermarks. IDF kan die minima op verschillende momenten hebben gemeten;
1.300 B bewijst daarom geen gelijktijdige globale vrije heap van 1.300 B.
De HIL gebruikt INTERNAL|8BIT; standaard Debug Heap Min Free gebruikt INTERNAL.
Vergelijk die meetwaarden niet rechtstreeks. Actuele heap en largest block zijn
steekproeven; subsecondpieken blijven een meetgrens.

Een lichte proef met één paginaoverdracht, native HA en TLS geeft watermarksommen
34.632→27.704 B, bemonsterde actuele heap 73.303→69.071 B en grootste interne
block 31.744 B bij beide. De zwaardere herhaalde downloads zijn een stresstest,
geen dagelijks-gebruikprofiel. Geen nieuwe controlallocaties of crashes gevonden;
de precieze allocatieverdeling en alle mogelijke netwerkcombinaties zijn niet
gekwalificeerd. De beperkte normale proef bevat geen actieve opwarmsessie.

Beide actuele permission-withdrawal-proeven stoppen fysiek na circa 340 s en
logisch na circa 350 s. Dit past bij 300 s minimumlooptijd plus de bestaande
60s-Power-House-cadence en terugmelding. De eerdere tienminutenafwijking is niet
gereproduceerd; de historische oorzaak is niet bewezen.

De PR blijft draft voor beoordeling van deze meetgrenzen. De HIL-reeks gebruikte
de oudere dev-basis met een NVS-eis van 126 entries: dev had 111 entries vrij,
de kandidaat 108. Na de rebase op dev `8483e0c3`, inclusief de documentatieopbouw
van #793, slaagt de configuratie-/NVS-controle voor Q WiFi Duo: 108 entries vrij
bij de inmiddels door dev verlaagde eis van 100. Dit is geen nieuwe hardwareproef
of bewijs dat de heapbevinding is opgelost. Geen fysieke koude powercycle of
echte achtuursduurproef uitgevoerd; alleen de opwarmklok is in een private build
×96 versneld. Host- en harnesstests vervangen hardwarebewijs niet.
