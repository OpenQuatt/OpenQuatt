# HIL-regressie-inventarisatie

Deze inventarisatie vertaalt eerdere hardwarebewijzen naar blijvende
systeemcontracten volgens het [HIL-onderhoudsmodel](hil-testing.md#onderhoudsmodel).
Het is een toelatings- en implementatieplan, geen nieuwe HIL-run of vrijgave.
De voorgestelde scenariofiles en case-identiteiten hieronder bestaan nog niet.
Een historische PASS geldt alleen voor de toen geteste firmware en opstelling.

## Bronnen en bestaande dekking

Beoordeeld op de `dev`-basis na merge van
[#759](https://github.com/OpenQuatt/OpenQuatt/pull/759) op 26 september 2026.
PR-beschrijvingen zijn historische bewijsbronnen; hun afvinklijsten kunnen
oudere revisies betreffen. De huidige testbestanden zijn apart gecontroleerd.

| Bron | Hardwarebewijs en beperking | Bestaande lagere testlaag | Besluit |
|---|---|---|---|
| [#708](https://github.com/OpenQuatt/OpenQuatt/pull/708) | Duo: één peer weg houdt CM2; beide weg geeft causale stop-unconfirmed en CM4; herstel via CM1 naar CM2. Single is hiermee niet hardwarematig bewezen. | `oq_hp_incident_engine_test.cpp`, `hp_fallback_logic_test.cpp`, `incident-monitoring.test.mjs` | Twee communications-contracts; UI-groepering en causale randgevallen blijven lager. |
| [#709](https://github.com/OpenQuatt/OpenQuatt/pull/709) en [#713](https://github.com/OpenQuatt/OpenQuatt/issues/713) | Eén peer vooraf offline: cold-start vrij, CM1 → CM2. De oorspronkelijke HP1-dispatchfout is apart hersteld in commit `f50780a9`; #709 bewijst die fix niet. | `hp_supervisory_logic_test.cpp`, `cold_start_probe_test.cpp`, `oq_power_house_dispatch_logic_test.cpp` | Eén geparametriseerd Duo-startcontract voor beide richtingen; vereist nieuwe end-to-end validatie op de geïntegreerde fix. |
| [#742](https://github.com/OpenQuatt/OpenQuatt/pull/742) | Geforceerde cyclus, flow net onder/boven minimum, peer-overlap en linkherstel bewezen op kandidaatfirmware. | Host-/contracttests staan op de PR-branch, nog niet op deze `dev`-basis. | Vier defrost-contracts na merge en hercontrole van #742; geen afhankelijkheid stilzwijgend naar `dev` kopiëren. |
| [#754](https://github.com/OpenQuatt/OpenQuatt/pull/754) | OT-setpointgrenzen, nul-kamertemperatuur, volledige restore en testidentiteit. | `input_source_logic_test.cpp`, `tests/hil/harness.test.mjs` en bestaande stage `setpoint-validity` | Reeds aanwezig; geen duplicaat. |
| [#675](https://github.com/OpenQuatt/OpenQuatt/pull/675) | V2 Power Input en performanceketen met simulatorfixtures. | `energy_logic_test.cpp`, `hp_perf_v2_model_test.cpp` en `v2-performance.mjs` | Bestaand domein behouden; geen nieuwe issuefile of hernoeming van legacy profile. |
| [#758](https://github.com/OpenQuatt/OpenQuatt/pull/758) | R1-targetketen, beide latchrichtingen, bewegend target, safety-inhibit, OT/R1-uitsluiting en OT-vraagverlies. | `boiler_relay_target_logic_test.cpp`, `boiler_transport_logic_test.cpp`, `ot_boiler_safety_interlocks_test.cpp` | Twee boiler-contracts voor fysieke uitgang/protocolketen; exacte grenzen blijven hostmatig. |
| [#726](https://github.com/OpenQuatt/OpenQuatt/pull/726) | Run extension, comfortstop, warme herstart en disable tijdens actieve extensie. | `oq_power_house_run_extension_logic_test.cpp`; input/source-suite al aanwezig | Reserve voor gerichte controlkwalificatie; geen complete state-machine als permanente HW-matrix dupliceren. |
| [#743](https://github.com/OpenQuatt/OpenQuatt/pull/743) | Gemengde V1/V1.5, variantgating, bodemplaatwritevolgorde en readback. Geen EEPROM-commit. | `oq_odu_bottom_plate_settings_test.cpp`, `odu-settings-capabilities.test.mjs` | Reservecontract voor service-write/readback; alleen toevoegen bij werk aan die interface. |
| [#727](https://github.com/OpenQuatt/OpenQuatt/pull/727) | Fieldobservatie: bekende partial-response warning afwezig; geen gecontroleerd A/B-causaliteitsbewijs. | `modbus_recovery_test.cpp`, `test_modbus_reliability_telemetry_contract.py` | Parsermatrix blijft hostmatig; fysieke UART-faulttest alleen met onafhankelijke controllerobservatie en expliciete fault-toestemming. |
| [#748](https://github.com/OpenQuatt/OpenQuatt/pull/748), [#749](https://github.com/OpenQuatt/OpenQuatt/pull/749), [#750](https://github.com/OpenQuatt/OpenQuatt/pull/750) | Flow-/pompdiagnostiek en lage waterdruk met link-loss; geen echte hydraulische pomp- of druksensortest. | `lowflow-diagnosis.test.mjs`, `installation-monitoring.test.mjs` en bestaande flow-/boilerhosttests | Geen aparte HW-case voor UI-copy/rendering. Een waterpomptest is een afzonderlijk actuatorcontract. |
| [#685](https://github.com/OpenQuatt/OpenQuatt/pull/685) | MQTT-batches en beperkte heapmetingen op oudere featurebuilds; definitieve cadans en opt-out-stilte over een grens niet bewezen. | `openquatt_performance_telemetry_policy_test.cpp`, `test_performance_telemetry_contract.py` | Netwerk/persistentie-kwalificatie blijft open; geen hardware-PASS of geheugenbudget afleiden uit die oude meting. |
| [#734](https://github.com/OpenQuatt/OpenQuatt/pull/734), [#735](https://github.com/OpenQuatt/OpenQuatt/pull/735), [#737](https://github.com/OpenQuatt/OpenQuatt/pull/737), [#738](https://github.com/OpenQuatt/OpenQuatt/pull/738) | Recovery/auth en gedeeltelijke HIL; fysieke knop, echte HA-reconnect, provisioning en power-cut hebben afzonderlijke voorwaarden. | `openquatt_recovery_state_test.cpp`, uitvoerende recovery-fixtures onder `scripts/tests/fixtures/` | Apart recovery-domein bij wijzigingen daaraan; niet in de controlkernset vermengen. |

Hostbestanden staan onder `tests/host/`, Python-tests onder `scripts/tests/` en
webtests onder `openquatt/web/tests/`. Vermelding van bestaande dekking betekent
niet dat ieder end-to-end pad daarmee bewezen is. Voeg ontbrekende logica eerst
aan die lagere laag toe; reserveer hardware voor de pakket-, bus- en uitgangsketen.

## Voorgestelde kernset: tien contracts

De case-identiteit bevat geen issue-/PR-nummer. Parameterwaarden en historische
herkomst mogen wel in de testcase of het runrapport staan. Dit is geen
verplichte suite per PR.

| Domein / case | Setup en hardwareassertion | Wat blijft lager / toelatingsvoorwaarde |
|---|---|---|
| `communications` / `all_peers_lost_enters_safe_fallback` | Vanuit stabiele Duo-CM2 beide ODU-responses onderdrukken. Bevestig verse linkstatus, `stop_unconfirmed_due_to_link_loss`, verlopen stopwacht en CM4 met keteluitgang. Stop wordt niet als bevestigd voorgesteld. | Alle fallbackguards en stopfout vóór link-loss blijven in incident-/fallbackhosttests. Geldige flow/supply en expliciete fallback-opt-in zijn precondities. |
| `communications` / `link_recovery_hands_control_back_cleanly` | Vanuit de vorige fallback beide responses herstellen. Observeer recovery, handback via CM1 naar CM2 en het wissen van de causale permissie; geen onbedoelde CM0-flap. | Onderbroken recovery, draaiende-compressortelemetrie en timer-wrap hostmatig. Dit is een eigen assertionfase binnen dezelfde setup, geen tweede volledige outage-run. |
| `duo` / `remaining_peer_keeps_heating` | Eén peer valt tijdens verwarmen weg; de andere houdt geldige communicatie en verwarmt. Geen volledige-uitvalaggregate of CM4/ketelstart. Parametriseer welke peer uitvalt. | Aggregatie en candidatekeuze hostmatig. Observeer werkelijk compressorfeedback, niet alleen CM2 of een HTTP-ACK. |
| `duo` / `available_peer_starts_from_idle` | Eén peer vóór vraag offline, veilige verse waterdata van de andere. Verifieer CM1 → CM2 én compressorstart van uitsluitend de beschikbare peer. Parametriseer HP1/HP2. | 5/12 °C-matrix en re-arm vóór compressorstart hostmatig; dat korte racevenster was niet deterministisch fysiek te stagen. Controleer de geïntegreerde #713-fix, niet alleen cold-startvrijgave. |
| `defrost` / `rejects_below_minimum_flow` | Verwarmende ODU met verse flow aantoonbaar onder de geconfigureerde minimumflow: aanvraag geweigerd en nul nieuwe `3999=4`-writes. | Exacte floatgrens en refusal-prioriteit hostmatig; registercounter moet per ODU de delta tonen. Afhankelijk van #742. |
| `defrost` / `accepts_at_minimum_flow` | Verse flow aantoonbaar op/boven minimum, alle guards vrij: geaccepteerde aanvraag met exact één `3999=4`. | Historische HIL was 249,054/250,290 L/h, dus geen bewijs van exact 250,000 L/h. Alleen een bewezen exacte fixture mag een exact-grens-PASS heten. Afhankelijk van #742. |
| `defrost` / `blocks_when_peer_defrost_active` | HP1-defrost bevestigd actief; HP2-aanvraag geblokkeerd zonder write. Na HP1-completion wordt HP2 weer beschikbaar. | Peer-gate en symmetrie hostmatig; niet beide kanten als losse permanente scripts. Afhankelijk van #742. |
| `defrost` / `forced_defrost_completes_once` | Geaccepteerde aanvraag doorlopen tot RESYNC/COMPLETE, `fdefcnt` +1 en normale actuele regeling hersteld; totaal één `3999=4`. | Verloren ACK, boot-adoptie, stale ownership en CM0-circulatie hostmatig; geen automatische herhaling van de trigger. Combineer met de vorige positieve flowcase. Afhankelijk van #742. |
| `boiler` / `command_target_reaches_selected_transport` | Normale CM3-promotie met geldig target. R1 volgt een lage/hoge supplyfixture; OT ontvangt CH/TSet en keert bij vraagverlies terug naar uit/0. | Hysteresegrenzen, latchgeschiedenis, minimumtijden en moving-targetmatrix hostmatig. R1 en OT zijn transportparameters, geen issuecases. |
| `boiler` / `safety_and_transport_exclusion_override_request` | Bij geldig boilercommand wint een temperatuur-inhibit onmiddellijk; levende OT-peer blokkeert R1. Bevestig fysieke/gerapporteerde uitgang en herstel. | Volledige guardmatrix hostmatig. Temperatuur-inhibit is geen low-flowbewijs; defrost geeft geen directe CM3-bypass. |

## Uitvoering en herstel vóór toelating

Een nieuwe scenario-implementatie wordt pas toegelaten wanneer:

1. baselinefirmware, config-hash, simulatorcontract en benodigde simulatorfeatures
   live zijn vastgesteld; broncode of een eerdere flash is geen live featurebewijs;
2. de setup verse, stabiele relevante inputs en echte controlleruitgangen bewijst;
   langdurige API-fixtures worden gecontroleerd ververst, zonder freshnessfouten
   te maskeren die juist onderwerp van de case zijn;
3. snapshot/recovery alle gewijzigde settings en fault-gates kent vóór de eerste
   write; onderdrukte responses en actieve injecties blijven nooit achter;
4. vroeg afbreken, interrupt en fouten tijdens setup, assertions en restore door
   uitvoerende fake-tests worden gecontroleerd; een mislukte restore houdt de
   run geblokkeerd en bewaart het recoveryartifact;
5. normale firmware en controllerinstellingen aantoonbaar zijn hersteld; iedere
   nieuwe timingoverlay blijft test-only met identiteit `openquatt-test`;
6. de kleinste relevante fysieke run op exact de gepubliceerde testcaseversie
   slaagt. Host-/fake-PASS wordt afzonderlijk gerapporteerd van hardware-PASS.

Gebruik bestaande `scripts/hil/`-locking, request-gating en snapshot/restore.
Nieuwe domeinen mogen de generieke lifecycle niet kopiëren. Breid ontbrekende
snapshotvelden gericht uit en controleer oude recoveryartifacts expliciet;
een schemawijziging mag geen stil gedeeltelijk herstel veroorzaken.

Neem OTA en restore-reboots op in het runplan: de huidige gedeelde runner voert
bij muterende runs een firmware-restore via OTA uit. Test alleen de
testcontroller en simulator, nooit het productiesysteem.

## Selectie en implementatievolgorde

- Eerst `duo` en `communications` op geïntegreerde `dev`: vier contracts, met
  gedeelde setup en parametrisatie in plaats van losse incidenttests.
- Daarna `defrost`: vier contracts na merge van #742 en verificatie van het
  live forced-defrostmodel. Geaccepteerde flow en completion delen één cyclus.
- `boiler`: twee contracts bij ketel-/transportwijzigingen of brede kwalificatie.
- Bestaande `input-sources` en `v2-performance` blijven selectief beschikbaar;
  deze inventarisatie voegt geen verplichte `all`-run toe.
- Bodemplaat/service, run extension, UART, recovery en MQTT/persistentie blijven
  afzonderlijke uitbreidingskandidaten. Voeg ze alleen toe bij een aantoonbare
  lagere-testlaagkloof, met eigen hardwareprecondities en runselectie.

Openstaande Single-validatie, echte sensor-/hydrauliekbewijzen, flash-power-cut
en worst-case geheugen-/stackmarge blijven zichtbaar als aparte kwalificatie.
Ze worden niet afgevinkt door een Duo-simulatorrun of compilemeting.
