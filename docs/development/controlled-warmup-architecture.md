# Controlled warmup v1

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
| Boot | Opgeslagen instellingen toepassen; sessie en vorige setpoint zijn nooit persistent. |

De heating-supply-source callback is verwijderd: v1 werkt alleen met Power House
en die selector bestuurt uitsluitend de stooklijn. Normale mode-detection stopt
bij CM buiten 0..3, handbediening, koelen of uitgeschakelde OpenQuatt.

V1 ondersteunt uitsluitend Power House. De oorspronkelijke stooklijnintegratie
wijzigde hoofdzakelijk ruimte-stop/herstartgrenzen, terwijl het waterdoel gelijk
kon blijven; dat bewijst geen begrensd opwarmvermogen. Vier instellingen zijn
publiek: enabled, trigger, stap en staptijd. Maximum offset is 0,5 K en duur 8 uur.
Het bestaande zes-float opslagblok blijft compatibel om een NVS-migratie te
vermijden. Validatie van het volledige blok blijft fail-closed; slots 4/5 worden
na geldige restore genormaliseerd naar de vaste limieten.

Power House gebruikt het effectieve target voor room feedback. Heat intent krijgt
het echte target voor gebruikersedges; tussenstappen veroorzaken geen fast-start.
Tijdens en direct na warmup wordt comfort memory gereset. Run extension blijft
op het echte target werken. Permission, dispatch en boiler support blijven downstream.

## Releasekwalificatie

De eerdere matched HIL meldde minimum interne heap 19.044→13.848 B (−5.196 B).
Een kleinere entitieset is geen verklaring of nieuwe meting. Herhaal baseline en
kandidaat met dezelfde build/config, koude boot en overlappende netwerkbelasting;
meet current/minimum internal heap, largest block, fragmentatie en stackwatermarks.

De eerdere permission-withdrawal-proef meldde simulator standby terwijl ODU2
logical/applied level langer actief bleef. Leg gelijktijdig heating intent,
dispatch target, laatste uitgezonden Modbus-commando, applied-level feedback en
werkelijke ODU-working-mode vast, inclusief baseline. Zonder die vergelijking
blijft onduidelijk of reporting lag of behouden aansturing de oorzaak is.

Geen ready-for-review of merge zolang beide bevindingen niet verklaard en
op hardware opnieuw gevalideerd zijn. Host- en harnesstests vervangen dat bewijs niet.
