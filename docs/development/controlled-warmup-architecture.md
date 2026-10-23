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
De pure `control_target`-getter houdt bij een nieuwe verhoging met voldoende verschil tot de gemeten kamer dan
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
publiek: enabled, trigger, stap en staptijd. Maximum boost (0,5 K) en duur (8 uur)
zijn compile-time constanten. De runtime kent geen strategiecodes; de adapter en
mode-callback bewaken uitsluitend automatische Power House-regeling. Vier publieke
meetwaarden tonen actieve sessie, vast tussendoel, boost en effectief doel. Sessiestatus en verstreken
duur zijn daarnaast beschikbaar via de service-API (zie sessiediagnostiek).

Bij de standaardinstellingen start een nieuwe thermostaatverhoging naar 20,5 °C
bij 18,0 °C gemeten kamer (2,5 °C verschil); bij 19,0 °C is het verschil precies
1,5 °C en start de functie niet. De verhoging zelf hoeft niet meer dan 1,5 °C te
zijn. Een eerste waarde na boot, enable, reconnect of invalidation legt alleen
een baseline vast; een nieuwe positieve setpoint-edge blijft nodig. Dit voorkomt
onmiddellijk herstarten na timeout of onderbreking bij een blijvend koud huis.

Het eerste vaste tussendoel is kamer + ingestelde stap, begrensd op het echte
doel. Boost (`State::offset_c`) begint op nul. Bij timeout blijft het tussendoel
staan, boost groeit met één stap tot 0,5 °C en de staptimer start opnieuw. Het
effectieve doel is `min(thermostaatdoel, tussendoel + boost)`. Bij bereiken of
overschrijden van het vaste tussendoel wordt boost nul en volgt direct een nieuw
tussendoel vanaf de gemeten kamer. Een late tick haalt geen gemiste boosts in.
De sessie eindigt bij bereiken van het volledige thermostaatdoel of na acht uur;
de comfortband is geen sessie-eindgrens meer. Verlagen annuleert direct.

Een boost kan binnen de Power House-comfortband nog nul room feedback geven.
Daarbuiten wordt de extra ruimtecorrectie boven op de onboosted vraag inclusief
het minimumvermogen gevraagd: `max(basisvraag, Pmin) + extra ruimtecorrectie`.
De basisvraag gebruikt het vaste tussendoel; de extra correctie is het verschil
met de feedback op het geclampte effectieve doel. Boost wordt zo niet opnieuw
opgeslokt door de Pmin-vloer en wordt niet dubbel opgeteld. De samenstelling
gebeurt vóór de bestaande enveloppe, slew en waterlimiet. Een hogere vraag
betekent door compressorstanden en optimalisatietoleranties niet bij ieder
klein booststapje een hogere frequentie.

Deze extra Pmin-compositie geldt alleen tijdens een actieve sessie met geldige
warmte-intent/recovery, modeldata en niet-begrenzende waterfactor (>= 0,999).
Bij een gedeeltelijke waterbegrenzing geldt de normale vraagberekening; er
wordt geen extra Pmin-lift aangebracht. Warmtetoestemming, versheid,
minimum-off, elektrische limieten en frequentie-/actuatorgrenzen blijven leidend.

Eén extra runtime-float (`warmup_request_w_`, 4 B veldopslag, niet persistent)
bewaart alleen de HP-slew. De oorspronkelijke vraaghistorie en `fast_floor_w_`
blijven vrij van de nieuwe lift, zodat deze ook via volgende ticks niet als
extra huistekort/ketelvraag terugkomt. De bestaande huisvraag en recoveryvloer
blijven behouden. De nieuwe scalar wordt gewist bij cancel/reset, wegvallen van
intent/modeldata of waterbegrenzing; herstart/OTA herstelt hem niet. Er komen
geen buffers, allocaties, taken of NVS-velden bij. De totale object-/firmware-RAM
en fysieke marge moeten nog met de nieuwe firmware worden gekwalificeerd.

Bij bereiken van een stap vervalt de extra boost direct voor de consumer,
ook als deze vóór de 1s-owner draait. De HP-vraag bouwt met de bestaande
fall-slew af naar de normale recoveryvraag; het runrecht blijft behouden.
Bestaande run-extension- en caplogica volgen daarna ongewijzigd.

`tests/host/warmup_boost_dispatch_test.cpp` rekent met de echte warmup-runtime,
heat intent, vraagberekening, waterzijdige V1-vermogenskaart en dispatcher.
De fixture verbindt deze functies zoals de runtime en test Single en Duo met
één beschikbare HP, plus een geblokkeerde tweede HP. Voorbeeld: kamer 19,10 °C,
tussendoel 19,18 °C, thermostaatdoel 20 °C, reactie 3.000 W/K, comfortband onder
0,1 °C, huisvermogen 5.000 W bij −10 °C, nulvermogen bij 18 °C,
waterinlaat 30 °C en flow 800 L/h. Bij 12 °C buiten is Pmin circa 2.528 W:

| Boost | Extra ruimtecorrectie | HP-vraag na instellen | Gekozen frequentie |
|---|---:|---:|---:|
| 0,0 °C | 0 W | 2.528 W | 30 Hz |
| 0,1 °C | 240 W | 2.768 W | 30 Hz |
| 0,2 °C | 540 W | 3.068 W | 39 Hz |
| 0,3 °C | 840 W | 3.368 W | 39 Hz |
| 0,5 °C | 1.440 W | 3.968 W | 49 Hz |

De simulatie controleert ook 8 °C buiten, herhaalde ticks zonder opstapeling,
beide owner/consumer-volgordes, geleidelijke afbouw na een stap, identieke
huisvraag/keteltekort ten opzichte van het oude pad, annulering en nieuwe sessie,
water-/vermogens-/frequentiegrenzen, veiligheidsstop, minimum-off en ontbrekend
model. Het is geen replay van de testerinstallatie, volledige ESPHome-runtime,
gekoppeld Duo-watermodel of fysieke HIL; die kwalificatie volgt afzonderlijk.

Het bestaande zes-float opslagblok blijft compatibel om een NVS-migratie te
vermijden. Validatie van het volledige blok blijft fail-closed; slots 4/5 worden
na geldige restore genormaliseerd naar de vaste limieten.

Power House gebruikt het effectieve target voor room feedback. Heat intent krijgt
het echte target voor gebruikersedges; tussenstappen zijn geen thermostaatverhoging.
Tijdens warmup bevestigt een gestopte HP de warmtevraag onder het effectieve regeldoel via
de normale 10s-confirmatie. De comfortband wordt daarvoor niet nogmaals van het
effectieve regeldoel afgetrokken. De minimum-power recovery blijft vervolgens actief tot
`requested_setpoint_c`, ook als Power House het bereiken van
een tussendoel vóór de volgende 1s-warmuptick ziet. Een al draaiende HP mag deze
recovery direct gebruiken met geldige bronnen en warmtetoestemming; dit is geen
nieuwe start en omzeilt de downstream guards niet. Bij verlaten van de limiter
worden recovery en startbevestiging gewist, zodat normaal regelen geen oude
warmup-floor erft. Een onafhankelijk gestopte HP verliest het warmup-runrecht;
een herstart moet de warmtevraag opnieuw bevestigen. Buiten warmup blijft
kamerherstel na een bevestigde start actief tot het volledige setpoint is bereikt,
zoals in de normale regeling op `dev` (#814).
`control_target` verbergt de limiter ook direct bij een lager actueel setpoint,
zonder de sessie in een getter te muteren. Alleen `update` legt die overgang vast.
De extra session-scope bool past in de bestaande 20 B heat-intent state; er komen
geen controlbuffers, taken of opslagvelden bij.
Tijdens en direct na warmup wordt comfort memory gereset. Run extension blijft
op het echte target werken. Permission, dispatch en boiler support blijven downstream.

De regressiesimulatie `tests/host/warmup_heat_intent_test.cpp` gebruikt de echte
warmup-runtime, inputadapter, heat intent, Power House demand en low-load helpers.
De bestaande floor-glue wordt in de fixture nagebootst; fysieke terugmelding,
dispatch en minimum on/off timing blijven hardwaretestscope.

## Sessiediagnostiek in de web-app

De loop bewaart alleen de laatste eindstatus en vaste sessieduur (acht bytes in
de runtime). De bestaande service-statuscomponent ontvangt elke owner-tick één
atomair 32-bit woord: statuscode in de bovenste byte en verstreken seconden in
de onderste 24 bits. HTTP leest dat woord eenmaal en streamt `warmupStatus` en
`warmupElapsed` via `/openquatt/service/status`. Het leest geen mutable controlstate.
De codes 0–9 uit `oq_warmup::Status` zijn een vast wirecontract. Code 0 betekent
geen sessie sinds boot; 1 is een door uitschakelen afgebroken sessie, 2 actief,
3 comfort bereikt, 5 tijdslimiet en de overige codes annulering.

Dit voegt geen ESPHome-entities, buffers, taken of NVS-records toe; de snapshot
gebruikt vier bytes. De Xtensa-compiler voor ESP32 en ESP32-S3 vertaalt de
gebruikte relaxed 32-bit load/store naar uitgelijnde `l32i`/`s32i`-instructies
met `memw`, zonder helpercalls of allocaties (GCC 14.2.0, `-O2` en `-Os`).
De bredere `is_always_lock_free`-trait wordt niet vereist; grootte en uitlijning
blijven met compileasserts bewaakt. Het verandert de regeling niet. UI-sleutels zijn uitsluitend
virtuele statusvelden, opgehaald met de bestaande entity-pollcadence. Volledige
oude/ontbrekende/falende statusantwoorden verwijderen eerdere timerwaarden. Het
laatste resultaat blijft vluchtig tot de volgende sessie of boot. Hosttests
controleren rollover, alle eindroutes, herstart en coherente HTTP-snapshots.

## Releasekwalificatie

Hosttests vervangen geen hardwarebewijs voor terugmelding, dispatch en minimum
on/off timing. Kies voor de actuele firmware de kleinste relevante HIL-proef.
Bewaar HIL-verslagen, ruwe meetdata, logs en testfirmware buiten de repository;
verwijs vanuit de PR naar het bijbehorende bewijs. Leg daarin bron-SHA,
binary-SHA256, controller- en simulatoridentiteit, instellingen, assertions,
meetgrenzen en eindtoestand vast.

De generieke HIL-harness en blijvende host-/web-/contracttests horen wel bij de
code. Geheugenkwalificatie vereist representatieve runtimebelasting met actuele
en minimale interne heap, grootste vrije block en relevante stack-watermarks.

## Testerfeedback 14 oktober 2026

Vast tussendoel en boost zijn gescheiden, inclusief afzonderlijke native
Home Assistant-sensoren en webweergave. De boost gebruikt het bestaande
runtime-offsetveld; de grootte van de runtime verandert niet en de NVS-layout
blijft behouden. Twee extra ESPHome-sensorobjecten en hun API-/webregistratie
hebben wel geheugenimpact; die is nog niet op representatieve hardware gemeten.
De bestaande heap/P1- en NVS-kwalificatiepunten blijven open.
