# Tijdelijke HIL-broncode bij #786 en #787

Besluit 4 oktober 2026: behoud de bestaande basisharness uit #759. De nieuwe
warmup- en HA/CIC/MQTT-scenario's, fixtures en overlays worden niet naar `dev`
gemerged. PR #787 wordt gesloten; #786 bevat de productieaanpassing met het
generieke source-contract en compacte host-/web-/contracttests.

Dit volgt de [werkafspraak bij #761](https://github.com/OpenQuatt/OpenQuatt/blob/9d39292e9fba7c316df0187169bd321402c0e30d/hil-archive-761/README.md):
gericht desktop-HIL-bewijs bij relevante control-, communicatie- en lifecycle-PR's;
scenario's pas structureel opnemen na herhaald gebruik voor een stabiel contract.

Deze archiefbranch is bedoeld voor hergebruik buiten `dev`, niet om te mergen.
Behoud deze branch ook na het sluiten van #787. De volledige source vóór de
archivering staat op commit `7a6610d6f386583b5708e9d15c04d639444caa9c`.
De SHA256SUMS in deze map identificeren de bewaarde HIL-bestanden op deze branch.
Een lokaal netwerkadres in het documentatievoorbeeld is vervangen door een
documentatieadres. Er zijn geen nieuwe firmwarebinaries of hardwarelogs toegevoegd.

## Bewaard

- `scripts/hil/local-input-fixture.{mjs,py}`: lokale CIC/MQTT-producers.
- `scripts/hil/native-ha-fixture.{mjs,py}`: native-HA-fixture.
- `scripts/hil/run-source-transports.mjs` en het transportscenario/overlay.
- `scripts/hil/run-controlled-warmup.mjs` en het warmupscenario/overlay.
- De bijbehorende harnesstests, runner-hooks en HIL-gebruiksdocumentatie.

Deze checkout bevat de toenmalige featurecode om de scenario's te kunnen
hergebruiken. Dat is geen release of bewijs van hardwarekwalificatie.

## Validatiegrens

Op de bewaarde source waren alle 106 hosttests geslaagd. De HIL-harnesstests
hadden 47 PASS en één Windows-platformskip. Dat zijn uitvoerende fake-/harnesstests,
geen fysieke HIL-runs. De source-overlay is met ESPHome 2026.9.0 geconfigureerd.
Nieuwe hardware-HIL, de echte 8 uur-soak, een matched heapvergelijking en de
ODU2-permission-withdrawal-vergelijking zijn niet uitgevoerd voor deze source.

Historische bevindingen bij #786 blijven releaseblokkades: minimum interne heap
19.044 naar 13.848 B (-5.196 B), en ODU2 logical/applied level dat na permission
withdrawal langer actief bleef terwijl de simulator standby meldde. De oorzaak
van beide is onbewezen. Zie de PR voor de historische claims en beperkingen.

## Hergebruik en bewijs

Checkout deze archiefcommit afzonderlijk en controleer `SHA256SUMS`, bijvoorbeeld
met `sha256sum -c hil-archive-786-787/SHA256SUMS`. Installeer de repositorydependencies
en gebruik eigen lab-URLs/secrets volgens `docs/development/hil-testing.md`.
Gebruik uitsluitend de testcontroller en simulator; OTA/reboot vereist voorafgaande
toestemming. Controleer vóór mutatie compatibiliteit, live identiteit, veilige
uitgangen en volledige snapshot/recovery. Kopieer oude instellingen niet blind.

Kies de kleinste relevante scenarioverzameling voor de actuele firmware. Bewaar
controller-SHA/config-hash/binary-SHA256, simulatorversie/SHA/profielen,
instellingen, assertions, failure boundaries, eindtoestand en geredigeerde
rapporten/logs met checksums bij de betrokken PR. Vermeld niet uitgevoerde en
geblokkeerde tests expliciet; historische PASS geldt niet voor een nieuwe SHA.
Alleen bewezen herhaald gebruik rechtvaardigt latere opname op `dev`.
