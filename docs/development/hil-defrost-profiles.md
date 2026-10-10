# HIL-plan: persistente defrostprofielen

Dit plan is voorbereiding, geen uitgevoerde HIL-run. Het bewaakt P276-opslag per
HP in controller-NVS en veilig opnieuw toepassen na een echte restart. Eerdere
manual-defrost-, flow- en CM1-stopresultaten bewijzen dit contract niet.

## Voorwaarden en eigenaarschap

- Wacht tot de huidige eigenaar van de desktopbench klaar is. Gebruik daarna de
  globale `hcq-desktop-lab`-lock uit `scripts/hil/session.mjs`, ook voor handmatige
  stappen; geen parallelle smoke-, SSE- of REST-clients vanuit andere chats.
- Leg kandidaatcommit, firmware/config-hash, simulatorversie, actieve én pending
  ODU-profielen/adressen, controllerinstellingen en beide defroststatussen vast.
  Verifieer testcontroller `openquatt-test`; productie blijft buiten scope.
- Gebruik `duo_hil.yaml` voor de labidentiteit. De normale Q Duo-config voor
  WiFi en Ethernet is `configs/heatpump_controller_q/duo.yaml`; flash die niet met de
  productie-hostname op deze bench. OTA, controller-/simulatorreboot en een
  eventuele NVS-reset vereisen toestemming voor de concrete run.
- Controleer op de draaiende simulator exact
  `openquatt-modbus-opentherm-v2`. Manual-defrostdekking vereist bovendien
  `manual-defrost-v1`. Een bronmarker bewijst geen geïnstalleerde firmware.
- Lees vooraf `hil-testing.md` en `tests/hil/scenarios/README.md`. Gebruik één
  `RequestGate`, standaard 1.500 ms tussen writes, begrensde waits en één
  begrensde controllerlogstream. Geen UART-faultinjectie of tweede Modbus-master.

De huidige snapshot schema 3 bewaart deze nieuwe defrost-NVS-profielen niet.
De API heeft geen delete-profielactie. Een bestaande runner herstelt dus niet
automatisch `profile_available=false`. Voor deze run worden gewone
testcontrollerinstellingen niet hersteld: elke volgende test begint met eigen
firmware en expliciet ingestelde inputs. Stel daarom de benodigde profielkeuze
en auto-uit-baseline via save plus readback vast en registreer de eindstate.
Dat is geen bewijs van een lege NVS. Alleen de fresh-installcase heeft een
expliciet afgesproken, uitsluitend labgerichte reset/provisioningstap nodig;
een gewone OTA bewijst dat niet. Ontbrekende delete/recovery-API blokkeert de
overige cases of deze voorbereiding niet.

## Geverifieerde controllerinterface

De actuele component en webclient gebruiken:

- GET `/openquatt/odu-defrost/hp1/status` en `hp2/status`;
- POST `/openquatt/odu-defrost/hp1/load` en `hp2/load`;
- POST `/openquatt/odu-defrost/hp1/save` en `hp2/save`.

POST gebruikt `Content-Type: application/x-www-form-urlencoded` en een
`URLSearchParams`-body met de actuele `csrf_token` uit status. `save` voegt
`mode`, `expected_mode` en `auto_reapply` (`true`/`false`) toe. Gebruik dezelfde
controllerorigin/Host en bestaande authenticatie. Haal na iedere reboot een
nieuw token op; tokens/credentials horen niet in rapporten. Het opgeslagen
`expected_mode` komt uit een voltooide verse `load`, nooit uit een oude UI-cache.

HTTP 200 kan alleen `QUEUED` betekenen. Wacht op `busy=false` en de eindstatus,
lees vervolgens opnieuw. Bewaar per HP `online`, `fresh`, `identity_ready`,
`variant`, `loaded`, `defrost_mode`, `operation_mode`, `compressor_hz`, `state`,
`guard`, `profile_available`, `desired_mode`, `auto_reapply`, `profile_state`.
`defrost_mode=-1` vóór `load` bewijst geen registerwaarde. Ook `guard` is de
manual-triggerguard; een rustende HP hoeft daar niet `READY` te tonen voor save.

Modes 0, 1 en 3 zijn ondersteund op V1; mode 4 alleen op de daarvoor
ondersteunde varianten. Kies voor de onafhankelijkheidstest bijvoorbeeld HP1=1
en HP2=3 nadat beide actuele identities en modes zijn vastgesteld.

## Minimale cases

| Case | Stimulus | Vereist bewijs |
|---|---|---|
| Lege NVS | Boot met aantoonbaar geen profielrecords; geen save | Beide HPs `profile_available=false`, `auto_reapply=false`, `desired_mode=-1`; geen profielwrite/reconcile. Leg P276 via `load` vast. |
| Expliciete save, auto uit | Idle HP1: verse load, save een andere ondersteunde mode met `auto_reapply=false` | `SAVED`, fysieke P276-readback, `desired_mode` gelijk en `IN_SYNC`; HP2 profiel en P276 onveranderd. |
| NVS na restart, auto uit | Echte controllerrestart na voltooide save | Uptime reset én firmwareidentiteit vastgelegd; HP1 profiel/modekeuze/auto uit overleeft. Geen automatische P276-write. |
| Auto aan en onafhankelijkheid | Save HP1 met auto aan; save HP2 met een andere mode en auto uit | Beide afzonderlijke NVS-keuzes en readbacks; wijziging HP2 verandert HP1 niet. Herhaal echte controllerrestart. |
| Reconcile zichtbaar | Maak P276 van HP1 aantoonbaar anders dan zijn opgeslagen keuze via gedocumenteerde normale simulatorreset/profielconfiguratie; behoud controller-NVS; restart controller | Eerst mismatch vastleggen, daarna automatische load, P276-write en readback naar `desired_mode`, uiteindelijk `IN_SYNC`; HP2 blijft anders zolang auto uit staat. |
| Auto uitschakelen | Idle HP1: save dezelfde actuele mode met auto uit; restart controller en creëer opnieuw gecontroleerde P276-mismatch | Consentwijziging overleeft NVS/restart; mismatch blijft bestaan gedurende minstens twee reconcile-intervallen; geen automatische write. |
| Identity mismatch | Met auto aan: verander uitsluitend via bekende pending/active simulatorprofielroute naar een andere ondersteunde variant; geautoriseerde simulatorrestart | Nieuwe identity bevestigd; `IDENTITY_MISMATCH`, geen toepassen oud profiel, P276 blijft nieuwe fixturewaarde. HP2 blijft onafhankelijk. |

Voor de negatieve auto-uit-case moet een P276-mismatch daadwerkelijk zichtbaar
zijn. Als de normale simulatorreset dezelfde mode teruggeeft, kies vooraf een
andere ondersteunde opgeslagen mode. Geen niet-bestaande register-write-URL
bedenken. Als een geïnstalleerde simulator geen geschikte normale reset/
profielroute heeft, is reconcile-dekking BLOCKED, geen PASS.

Reconcile wordt na identitywijziging na 5 s geprobeerd en daarna met een
60 s-interval. Een operatie heeft een 30 s-timeout. Gebruik begrensde waits met
ruimte voor polling en minstens twee intervallen voor negatieve assertions.
Verifieer de writeguard afzonderlijk bij lopende compressor en bij actieve
peer-defrost met normale simulatie: NVS-doel mag niet voortijdig op de ODU worden
toegepast. De succesvolle retry volgt pas wanneer beide HPs weer idle zijn,
telemetrie vers is, identity geldig is en service-/incidentgates vrij zijn.
Leg uitstel, ongewijzigde P276 en latere readback vast; logregels alleen zijn
onvoldoende. De profileroute schrijft P276, geen manual `3999=4`.

## Geheugen en bewijs

Vergelijk baseline en kandidaat met dezelfde Q Duo labconfig, simulatorprofielen,
inputs en belasting vanaf cold boot. Meet vóór load, na saves, na restart/reconcile
en tijdens gelijktijdige normale HA/web/API/MQTT/Modbus/OpenTherm-belasting.
Neem een geautoriseerde OTA-belasting mee waar nodig voor het worst-casebudget.
De bestaande smoke levert `Heap Free`, `Heap Min Free`, `Heap Max Block`,
`Heap Fragmentation` en `PSRAM Free`. Verifieer welke daarvan daadwerkelijk
interne DRAM meten; vrije PSRAM/compile-RAM vervangt dat bewijs niet. Registreer
relevante task-stack-high-watermarks via bestaande beschikbare diagnostiek;
ontbreekt die, meld stackdekking expliciet als ontbrekend. Geen endpoint verzinnen.

Leg voor iedere boot de actuele vrije interne heap, de minimum-since-boot
watermark, grootste interne block, fragmentatie en stackmarges vast. Vergelijk
watermarks uitsluitend binnen dezelfde bootfase. Zonder afgesproken marge voor
de grootste gelijktijdige allocatie en verklaarde baseline/kandidaatverschillen
geen releaseveilig geheugenoordeel. Nieuwe profielstate blijft langlevend;
ook de twee handlers en eventuele onderliggende NVS-allocaties tellen mee.

Bewaar in het runrapport tijdlijn, exacte commits/config-hashes, rebootbewijs,
geredigeerde statusreadbacks van beide HPs, P276 voor/na, memory/stackmetingen,
finale controller- en simulatorinstellingen en PASS/FAIL/BLOCKED per case.
`PERSIST_FAILED` en flash-syncfouten vereisen aparte host/fake-dekking; dit plan
claimt geen fysieke NVS-foutinjectie. Bij runfout forceer normaal CM0 en stop
actieve faultinjectie indien aanwezig, onder dezelfde lock. Laat gewone
controller-/simulatorinstellingen staan volgens de afspraak voor deze bench;
registreer de eindstate voor de volgende eigenaar.

## Voorbereidingsstatus

Alleen bron- en harnessinspectie; geen devicequery, smoke, OTA, reboot,
NVS-reset of muterende test uitgevoerd. Er is nog geen uitvoerbare
`defrost`-scenariorunner; bovenstaande stages zijn een testplan, geen bestaande
CLI-opties.
