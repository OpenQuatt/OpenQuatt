# HIL-resultaat: duurzaam herstartconsent, 27 september 2026

Gerichte vervolgtest op de aparte `openquatt-test`-controller en desktopsimulator.
Het eerdere [profielrapport](hil-defrost-profiles-result-2026-09-27.md) blijft
bewijs voor zijn eigen kandidaat; deze run test het nieuwe consentjournal.
Tijden zijn UTC. Geen productiecontroller, NVS-reset of fysieke flashfoutinjectie.

## Kandidaat

- Commit: `863ec8185bcad04df934c19718a63d440466fd0c`.
- Config: `configs/heatpump_controller_q/duo_hil.yaml`; ESPHome `2026.9.0`.
- Config-hash: `0xcb8f8bdb`, na OTA daadwerkelijk teruggelezen.
- OTA: 2.339.648 bytes; SHA256
  `cb3058b12408ff224f1f8614c8e9d5d42d5b0189f63909492505e8616634987a`.
- Simulator `v0.5.0`, contract `openquatt-modbus-opentherm-v2` en
  `manual-defrost-v1`; beide ODU's V1.5, adressen 1 en 2.
- Globale `hcq-desktop-lab`-lock, voorafsnapshot, geserialiseerde RequestGate
  en bestaande CSRF/Origin-save-route gebruikt.

## Readbacks

| Assertion | Concrete waarneming | Resultaat |
|---|---|---|
| Upgrade behoudt uitgeschakelde keuzes | 10:37:52 nieuwe hash, uptime 1,570 s; HP1 desired1/auto=false, HP2 desired3/auto=false. | PASS voor bestaande uitgeschakelde profielen |
| Onafhankelijke opslag | 10:38:24 HP1 mode1/auto=true/SAVED/IN_SYNC; 10:38:45 HP2 mode3/auto=false/SAVED/IN_SYNC, HP1 ongewijzigd. | PASS |
| Echt rebootherstel | Controllerrestart 10:39:14; 10:39:43 uptime 2,698 s, HP1 auto=true/desired1 en HP2 auto=false/desired3 uit NVS. | PASS |
| Opt-in reconcile na simulatorreset | Simulatorrestart 10:39:23; verse loads 10:40:16 HP1 fysieke mode1/IN_SYNC, 10:40:21 HP2 fysieke mode0 tegenover desired3/auto=false. | PASS |
| Intrekking zonder methodewijziging | 10:40:35 HP1 dezelfde mode1, auto=false/SAVED/IN_SYNC. | PASS |
| Intrekking na echte reboot | Controllerrestart 10:40:37; 10:41:24 uptime 1,802 s, beide keuzes aanwezig met auto=false. | PASS |
| Uitgeschakelde toestemming schrijft niet | Simulatorreset 10:40:52; verse loads 10:41:49/10:41:53 en eindreadbacks 10:41:55: beide fysieke mode0 tegenover desired1/3, auto=false, na ruim 63 s. | PASS |

Een onmiddellijk statusverzoek tijdens de eerste controllerrestart werd
afgebroken (`terminated`); de latere uptime-reset en verse readbacks zijn het
bewijs, niet de HTTP-queueack. Beide compressors bleven 0 Hz, operation mode0.

## Grenzen

De hostregressies testen mislukte save/sync/readback, gedeeltelijke globale
syncfout waarbij ACTIVE al duurzaam is, compenserende REVOKED en verse boot
uit afzonderlijke duurzame backing. Dit is geen fysieke NVS-foutinjectie.
Als de eerste REVOKED-write zelf niet duurzaam lukt, kan eerdere toestemming
na herstart terugkomen; de UI meldt deze onzekerheid en blokkeert automatisch
toepassen in de huidige sessie. Legacy-enabled profielen zonder geldig journal
zijn fail-closed in de bootregressie getest, niet met een hardwaredowngrade.

Rustige interne-heapreadbacks lagen rond 98–104 kB vrij, minimum-since-boot
39.828 B, grootste block 55–63 kB. Geen releaseveilig geheugenbewijs: identieke
baseline/kandidaatbelasting, taak-stackwatermarks en worst-caseallocatiemarge
blijven open. Actieve compressor/peer-defrost-interleavings zijn niet herhaald.

Lokale evidence: `.tmp/hil/defrost-profiles-2026-09-27T10-33-11-024Z/` bevat
snapshot, geschoonde status-events en de exacte OTA. Geen logs/binary gecommit.

## Eindstate

10:41:57: beide online/fresh/identity_ready, V1.5, 0 Hz, operation mode0,
geen actieve/manual defrost. Fysieke mode0, desired HP1=1/HP2=3, auto=false
beide; `CM Override=Force CM0`. Runner afgesloten met exit0 en lablock vrij.
Gewone testinstellingen zijn op verzoek niet hersteld; faultinjectie bleef uit.
