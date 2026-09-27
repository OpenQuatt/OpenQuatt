# HIL-resultaat: persistente defrostprofielen, 27 september 2026

Gerichte run volgens [het profieltestplan](hil-defrost-profiles.md), op de
afzonderlijke `openquatt-test`-controller met de gecombineerde desktopsimulator.
Alle tijden hieronder zijn UTC. Gewone testinstellingen worden niet hersteld;
de volgende run stelt zijn eigen baseline expliciet vast.

## Geteste firmware

- Kandidaat: `88cfc0ad7df825f5605fad2e4a7d7ef24c3edccd`.
- Config: `configs/heatpump_controller_q/duo_hil.yaml`.
- ESPHome: `2026.9.0`; buildtijd `2026-09-27 11:47:49 +0200`.
- Config-hash: `0x380c9c05`; na OTA door de controller teruggelezen.
- Geüploade OTA: 2.338.288 bytes, SHA256
  `7e8aec7fbfe2e12b5458801281f3cfec5484365ec4574b498193477aa6effb02`.
- Live simulator: `v0.5.0`, `openquatt-modbus-opentherm-v2`,
  `manual-defrost-v1`; aanvankelijk beide ODU's `V1.5`.

De globale `hcq-desktop-lab`-lock, gedeelde `RequestGate`, snapshots en actuele
form-urlencoded/CSRF-route zijn gebruikt. Geen productiecontroller, NVS-reset,
UART-faultinjectie of tweede Modbus-master gebruikt. De eerste compilepoging
met een oude lokale ESPHome 2026.7.3 stopte vóór OTA; daarna is de bestaande
2026.9.0-installatie geverifieerd en gebruikt. Eén afgewezen poging om de
controllerhost door een vast IP te vervangen is niet uitgevoerd; de run is
vervolgd op de oorspronkelijke geautoriseerde hostname.

## Gedrag en readbacks

| Contract | Bewijs | Resultaat |
|---|---|---|
| Zonder geldig profiel automatisch uit | Eerste kandidaatboot 09:49:36: beide `profile_available=false`, `auto_reapply=false`, `desired_mode=-1`, `profile_state=NONE`; verse loads P276=0. Geen NVS gewist. | PASS voor runtime-default; geen volledige provisioning/resettest |
| Expliciete modeopslag per HP | 09:50:28 HP1 P276=1/desired=1/auto=false/IN_SYNC; 09:50:44 HP2 P276=3/desired=3/auto=false/IN_SYNC. HP1 bleef 1 bij HP2-save. | PASS |
| NVS na echte controllerrestart | Restart 09:50:45; 09:51:18 uptime=2,741 s, beide profielrecords aanwezig met afzonderlijke keuzes 1 en 3, auto=false. Verse loads bevestigden beide P276-waarden. | PASS |
| Auto aan via ongewijzigde mode | 09:52:09 HP1 save dezelfde P276=1 met auto=true: SAVED/IN_SYNC; HP2 bleef desired=3/auto=false. Restart 09:52:40: profiel en auto=true teruggeladen; automatische load geobserveerd. | PASS |
| Reconcile na normale simulatorreset | Reset 09:54:53 gaf geobserveerde OFFLINE/ONLINE, identity ongeldig tijdens uitval. HP1 auto=true schreef eenmaal mode1 en las die terug; HP2 auto=false bleef fysieke P276=0 tegenover desired=3. | PASS |
| Identity mismatch na herdetectie | Controllerrestart 09:57:10 detecteerde HP1 V1 tegenover opgeslagen V1.5. Normale V1-simulatorreset; verse P276=0 om 09:58:28, nog steeds 0 om 10:00:55, desired=1/auto=true/IDENTITY_MISMATCH gedurende >147 s. | PASS |
| Auto uitschakelen zonder modewijziging | HP1 weer V1.5 voor deze assertion; verse P276=1, save dezelfde1 met auto=false om 10:02:12: SAVED/IN_SYNC. Restart 10:02:14; 10:02:37 uptime=4,621 s, desired=1/auto=false aanwezig. | PASS |
| Auto uit laat mismatch staan | Simulatorreset 10:02:47; loads HP1 P276=0/desired=1 om 10:03:04 en HP2 P276=0/desired=3 om 10:03:07, beide auto=false. Verse loads 10:05:27/10:05:31 bleven beide 0, na >142/144 s. | PASS |

Een HTTP 200 is alleen als queue-ack gebruikt; assertions berusten op
`busy=false`, eindstatus en verse registerloads. De SSE-capture rond de eerste
simulatorreset bevat om 09:55:23 `HP1 defrost mode write sent once (1)` en
`HP1 defrost mode saved and verified (1)`. Dit was een daadwerkelijke P276-write,
geen `NO_CHANGE` of UI-cache. Er is geen manual `3999=4` gestart in deze run.

## Fixturegrens bij variantwijziging

De eerste live wissel V1.5 → V1 om 09:55:57 rebootte zo kort dat de controller
geen Modbus-OFFLINE-edge observeerde. Zijn gevalideerde variant bleef daardoor
V1.5; om 09:56:28 paste hij de opgeslagen mode1 toe op de nieuwe simulatorfixture.
Dit is geen PASS voor identity-mismatch. Mode1 is ondersteund op beide varianten.

`oq_HP_io.yaml` zet bij een geobserveerde offline-edge
`odu_generation_revalidation_required=true`; online start generation detection,
en de defrost-writeguard vereist complete identity zonder revalidation.
De expliciete controllerrestart detecteerde V1 en maakte de hierboven geteste
`IDENTITY_MISMATCH`-assertion mogelijk. Een hot variantwissel die transportmatig
onopgemerkt blijft valt buiten deze fixtureassertion. Verander simulatorprofielen
daarom uitsluitend met expliciete generation-detectie of controllerrestart vóór
een identitytest. Deze run bewijst geen detectie van elke willekeurige korte
devicewissel zonder transportevent.

## Geheugen en resterende dekking

De actuele ESPHome `debug_esp32.cpp` gebruikt `MALLOC_CAP_INTERNAL` voor
`Heap Free`, `Heap Min Free`, `Heap Max Block` en fragmentatie. De gerapporteerde
watermark is minimum-since-boot; PSRAM is afzonderlijk.

De oude firmware vóór OTA had in een bestaande boot 95.811 bytes vrije interne
heap, minimum 39.828 en grootste block 51.200. Kandidaatmetingen na saves en
restarts lagen rond 96–104 kB vrij, minimum 39.828 en grootste block 51–63 kB;
PSRAM rond 6,79–6,81 MB. Er is daarmee geen aangetoonde regressie in deze
gerichte rustige run, maar dit is geen releaseveilig geheugenoordeel.

Niet uitgevoerd: identieke baseline/kandidaat cold power-on vergelijking met
simultane HA/web/API/MQTT/Modbus/OpenTherm/OTA-belasting, task-stack-high-watermarks
en een afgesproken worst-caseallocatiebudget. Ook fysieke NVS-syncfoutinjectie,
compressor-running/peer-defrost-interleavings en volledige fresh-install/reset
vallen buiten deze run. Hosttests en oudere manual-defrost-HIL vervangen deze
ontbrekende runtimebewijzen niet.

## Lokale ruwe evidence

De snapshots, geredigeerde status-events, artifactidentiteit en begrensde SSE
staan in de volgende genegeerde runmappen:

- `.tmp/hil/defrost-profiles-2026-09-27T09-44-48-322Z/` (oude compilepoging);
- `.tmp/hil/defrost-profiles-2026-09-27T09-46-55-796Z/` (compile/OTA/NVS);
- `.tmp/hil/defrost-profiles-2026-09-27T09-53-58-101Z/` (reconcile/identity/disable).

Dit document bewaart de relevante concrete readbacks; de lokale ruwe logs en
firmwarebinary worden niet aan Git toegevoegd.

## Eindstate en oordeel

Om 10:05:36: kandidaat `0x380c9c05` actief; `CM Override=Force CM0`; beide
HP's online/fresh/identity_ready, variant V1.5, compressor 0 Hz, operation mode0,
geen actieve/manual defrost. Beide fysieke P276=0, opgeslagen HP1desired=1 en
HP2desired=3, beide `auto_reapply=false`/`profile_state=PENDING`.
Simulator pending én actieve profielen zijn beide V1.5 op adressen 1 en 2;
defrosttellers sinds de laatste simulatorreset alle 0. De finale geverifieerde
UART-fault-injection-switch geeft `value=false`/`state=OFF`.

De sessie eindigde exit0, beide begrensde SSE-streams zijn gestopt en de globale
benchlock is vrij. Geen oude firmware of gewone testinstellingen teruggezet.
`git diff --check` slaagt; er zijn alleen documentwijzigingen en genegeerde
lokale HIL-artefacten, geen firmware-implementatiewijzigingen uit deze run.

Gerichte profielpersistentie/restart/reconcile: **PASS** binnen bovenstaande
identityfixturegrens. Geheugen-/stackreleasekwalificatie: **PARTIAL**, om de
expliciet ontbrekende belasting-, baseline- en margegegevens.
