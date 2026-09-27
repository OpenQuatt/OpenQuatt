# HIL-werkafspraak en archief van PR #761

Besluit van 27 september 2026: behoud de basisharness uit #759. Sluit #761 zonder merge; uitgebreide scenario's blijven herbruikbaar buiten dev. Voer nu geen boiler-OTA uit. Dit archief is geen firmware-release en de archiefbranch is niet bedoeld om te mergen.

## Werkafspraak voor relevante PR's

Bij wijzigingen aan control, communicatie of lifecycle is gericht desktop-HIL-bewijs een reviewvoorwaarde. Kies vooraf de kleinste scenarioverzameling die het gewijzigde gedrag en de relevante failure boundaries dekt. Denk aan guards, verlies/herstel van peers, verloren acknowledgements, stale inputs, onderbreking en veilige eindtoestand. Een compile, host-fake of geslaagde HTTP-write vervangt geen fysiek bewijs. Gebruik de basisharness uit #759 voor identiteit, snapshots, veilige uploads/herstel en logging. OTA/reboot vereist voorafgaande toestemming.

Leg het bewijs bij de betrokken PR vast voordat deze wordt gemerged. Een blokkade of niet uitgevoerde test wordt expliciet beschreven; noem die geen PASS. Deze afspraak is een reviewregel, geen ingevoerde automatische GitHub branch-protection-check. Docs-only wijzigingen hebben geen HIL nodig. Bij embedded geheugenwijzigingen geldt bovendien de bestaande hardwarekwalificatie met interne heap/largest-block/stackmarges onder representatieve belasting.

Gebruik in de PR dit bewijsblok:

```text
HIL-domein en aanleiding:
Controller: git commit SHA, configpad, config hash, firmware-binary SHA256
Simulator: repository/commit SHA, versie, contract, actieve profielen
Labidentiteit en topology: uitsluitend testcontroller, geen productieapparaten
Instellingen: beginsnapshot, scenario-instellingen, productieguards/overrides
Assertions: verwachte transitions, fysieke readbacks, counters, freshness
Uitvoering: UTC-start/einde, PASS/FAIL/BLOCKED per case
Failure boundaries: injecties, retries/ACK-verlies, onderbreking, cleanup
Eindtoestand: firmware, settings, stopconfirmatie, outputs, injecties, lock
Bewijs: duurzaam downloadbare rapporten/logs + checksums
Beperkingen en niet uitgevoerde tests:
```

Verwijder credentials en persoonsgegevens vóór publicatie; geen secrets.yaml of firmware met lokale credentials publiceren. Bewaar originele recoverybestanden lokaal zolang herstel nodig is. Gepubliceerde geredigeerde snapshots zijn bewijs, geen bruikbare recovery-input.

Neem een scenario pas structureel op als het herhaaldelijk bij relevante PR's gebruikt is en een stabiel, onderhouden contract bewaakt. Tijdelijke scenario's kunnen bij de PR als archief worden bewaard. Nieuwe tests horen bij de wijziging die ze rechtvaardigt; oude fysieke resultaten valideren een nieuwe firmware-SHA niet automatisch.

## Bewaard bewijs

| Domein | Cases | Fysiek resultaat | Config hash | Simulator |
|---|---:|---|---|---|
| Duo-start en beide peer-lossrichtingen | 4 | PASS | 0xdc3e0771 | v0.4.1 |
| Mono-communicatieverlies en herstel | 2 | PASS | 0x58303cbb | v0.4.1 |
| Duo-communicatieverlies en herstel | 2 | PASS | 0xdc3e0771 | v0.4.1 |
| Defrostgrenzen, peer-overlap en één completion | 4 | PASS | 0xf78b6c5e | v0.5.0 |
| Ketelassist en permissies | 4 | NIET FYSIEK UITGEVOERD | lokale build 0xe920fdee | alleen smoke op v0.5.0 |

De boiler-smoke PASS bewijst alleen read-only preflight op de oorspronkelijke controller 0x329dfc03. Hosttests: 62/62; HIL Python-contracttests: 13/13; docs, compile en cold review geslaagd vóór archivering. Niet elke eerdere run is PASS: alle beschikbare rapporten, ook mislukte/afgebroken voorbereidingen, zijn behouden en blijven afzonderlijk herkenbaar aan stage/success/failure. De twaalf fysieke PASS-cases zijn uitsluitend de vier runs hieronder.

- evidence/2026-09-26T20-01-14-988Z-duo/
- evidence/2026-09-26T21-17-21-114Z-communications-mono/
- evidence/2026-09-26T21-37-58-320Z-communications/
- evidence/2026-09-27T07-15-52-234Z-defrost/
- evidence/2026-09-27T08-13-11-765Z-boiler/: alleen smoke
- evidence/pr761-lab-baseline/final-restore.json en evidence/pr761-defrost-upload/final-state.json: eindherstel.

Oorspronkelijke controllerfirmware 0x329dfc03 is steeds hersteld. Simulator v0.5.0 blijft geïnstalleerd met V1.5/V1.5; run extension is hersteld op aan. Desktop-HIL bewijst geen echte ketel/hydrauliek, echte ODU-defrostalgoritmen of worst-case geheugenveiligheid.

Historische bron-SHA's zijn niet volledig per oudere run vastgelegd: config hashes zijn geen git-SHA. Reconstrueer die niet op basis van aannames. Definitieve scenario-source: OpenQuatt commit 40dfd3f822c6d825144b1994fb74f9bcff6f2355. Defrostsimulator-source: OpenQuatt-Simulator commit 76653b34611c2fad79eb81250c17789e9cb4c4ff, later gemerged via #7 (f281e9dd3b9df582b4f98e663a7bc00504a0806e).

## Download, controle en hergebruik

Download hil-761-2026-09-27.tar.gz en SHA256SUMS van deze map. Controleer:

```sh
shasum -a 256 -c SHA256SUMS
tar -xzf hil-761-2026-09-27.tar.gz
```

manifest.json bevat per bestand SHA256, omvang en privacybewerking. source/ bevat de HIL-scripts, scenario's, overlays en handleiding op bovengenoemde commit. De volledige repo blijft beschikbaar op die commit en deze archiefbranch; het pakket bevat geen dependencies, secrets of firmwarebinaries. Voor hergebruik: checkout die commit in een afzonderlijke checkout, installeer de repo-dependencies, geef eigen lab-URLs/secrets op en volg docs/development/hil-testing.md. Snapshot-schema's/entiteiten kunnen bij latere firmware veranderen: valideer eerst compatibiliteit en voer de nieuwe PR opnieuw uit. Kopieer historische instellingen niet blind.

Bewaar deze archiefbranch; verwijder haar niet samen met de gesloten PR-branch. Gebruik een commit-pinned downloadlink uit het sluitbericht voor onveranderlijke inhoud.
