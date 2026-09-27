# Gerichte vrijgavechecks defrostprofielen — 27 september 2026

Vervolg op [consent-HIL](hil-defrost-consent-result-2026-09-27.md), uitsluitend
op `openquatt-test` en simulator v0.5.0/V1.5 op adressen 1 en 2. Lablock,
snapshots, geserialiseerde writes en begrensde SSE-captures gebruikt.

## Actuatorinterleavings

Op code `863ec8185bcad04df934c19718a63d440466fd0c`, zonder meetoverlay:

- 10:50:32 UTC: HP1 verwarmt in CM2, 39 Hz, fysieke methode0, desired1.
  Save methode3 eindigt 10:50:38 `COMPRESSOR_RUNNING`; methode0 en desired1
  blijven behouden. Een queue-ACK is niet als succes behandeld.
- De vooraf uitgelezen `ODU 1 defrost`-switch is kort aangezet. HP2 is stil
  (0 Hz); zijn save methode1 eindigt 10:51:29 `PEER_DEFROST_ACTIVE`, fysieke
  methode0/desired3 blijven behouden. HP1 `active=true` om 10:51:30.
  De switch is om 10:51:32 aantoonbaar OFF gezet; daarna Force CM0.
- Na stilstand werkt de normale save-route weer. Met de latere serializerfix
  `58413c8f49092e351b1f835d6053e5f44c836de6` zijn beide fysieke wijzigingen
  opnieuw `SAVED/IN_SYNC` teruggelezen (HP1=1, HP2=3, auto=false).

De latere fix verandert alleen HTTP-serialisatie, niet actuator-/saveguards.
Dit is een peer-bitfixture, geen bewijs van een complete automatische ODU-cyclus.

## Vergelijkbare geheugenbelasting en gevonden stackregressie

Baseline `dev` = `378a372141062cce4b60e2e15ea22272624a4fab` en kandidaat zijn
elk via OTA opnieuw gestart met dezelfde `duo_hil.yaml` plus een tijdelijke
observatie-overlay: elke 10 s logt de loop alleen tasknaam en
`uxTaskGetStackHighWaterMark` voor zichzelf en de bestaande `httpd`-task.
ESP-IDF 5.5.5 rapporteert deze waarden in **bytes**, niet woorden.
Geen globale traceconfig, stackgrootte, mallocbeleid of nieuwe worker gewijzigd.
Dit zijn verse applicatieboots, geen fysieke powercycles.

ESPHome 2026.9.0 maakt `httpd` met interne stack4096+256=4352 B.
Modbus/components draaien in de gemeten `loopTask`. Normaal Modbus/OpenTherm-
verkeer bleef actief. Per volledige belastingsronde: 90 s, 3 gelijktijdige
web/API-reads, 24 geslaagde requests in 8 ronden, `/0.css`, `/0.js`, beide
defroststatussen, één begrensde SSE-client, beide parameterloads en idle-saves.
Respectievelijk circa 4,109/4,130/4,130 MB responsecontent ontvangen.
Een eerste baselineproef zonder assets is apart bewaard, niet het vergelijkingsbewijs.

| Gemeten minimum / eindwaarde | Baseline | Oude kandidaat | Serializerfix |
|---|---:|---:|---:|
| Interne heap minimum-since-boot | 39828 B | 39828 B | 39828 B |
| Grootste intern block, eindreadback | 55296 B | 55296 B | 55296 B |
| Loop-stackminimum | 4694 B | 4694 B | 4694 B |
| HTTP-stackminimum | 212 B | **108 B — FAIL** | **412 B — PASS** |
| Actuele interne heap, eindreadback | 99063 B | 99107 B | 98907 B |
| Fragmentatie, eindreadback | 44,181% | 44,206% | 44,093% |

De stackregressie is daadwerkelijk gevonden en opgelost vóór vrijgave.
`write_status()` gebruikt nu één buffer192 in plaats van640, kleinere
snprintf-chunks en controleert truncatie/sendfouten. Bufferinhoud wordt door
ESP-IDF synchroon verbruikt vóór hergebruik. Er is geen gedeelde scratch,
nieuwe heapallocatie of PSRAM-fallback. Snapshot en token blijven per request
immutable/eigen storage. Werkelijke winst ten opzichte van de mislukte
kandidaat: 304 B; de theoretische bufferwinst448 B is niet als meting geclaimd.

Gerichte guardrail voor dit gewijzigde pad: geen achteruitgang ten opzichte
van de gemeten baseline (HTTP minimaal212 B, heapminimum39828 B en grootste
block55296 B onder deze belasting). De fix overtreft de HTTP-baseline met200 B.
De nieuwe profile/journalrecords zijn vaste16-byte records per HP; statische
RAM-delta met identieke overlay is112 B. De serializer voegt geen dynamische
allocatie toe; hosttests bewijzen maximale int/float-/enumchunks, volledige
JSON, nietfinite→null en onmiddellijke stop bij send-/truncatiefout.

Dit is **geen universele worst-casekwalificatie** van de hele webserver.
De bestaande algemene HTTP-stackmarge is beperkt. Geen representatieve
HA/MQTT/TLS-simultaanbelasting, fysieke cold power-on of alle andere workers
gemeten; geen willekeurig allocatiebudget voor die niet-uitgevoerde belasting
geclaimd. Deze grenzen zijn onderscheiden van de nu gesloten regressie in
het gewijzigde defrostpad. Er waren geen crashes of allocatiefouten in deze runs.

## Artifactidentiteit

Config-hash meetoverlay kandidaat blijft `0x3418dde3` bij C++-wijzigingen;
de OTA-SHA256, niet alleen config-hash, onderscheidt de binaries:

- Baselineprobe `0xf0e95753`, 2.335.840 bytes:
  `ec52d06f31d392ae648d274bcce8b9ac91ed241f304068ca59219efc811d3963`.
- Oude kandidaatprobe, 2.340.272 bytes:
  `bc11f1ae0fab038560746be3dc0b1b60de581e7f0471d7e956ede49b285da798`.
- Gefixte probe `58413c8`, 2.340.640 bytes:
  `7b21df9c5c85fccce20e284969f63ae571964df34be1c87dbf921312af7bb90e`.
- Gewone kandidaat zonder overlay, `duo_hil.yaml`, 2.340.000 bytes,
  config-hash `0xcb8f8bdb`:
  `7920c9227d158a6492c85d3b5bbe072f591bc3785a95d76ab99039a3f24ff8d3`.

Lokale evidence: `.tmp/hil/defrost-profiles-2026-09-27T10-47-30-815Z/`
(baseline, mislukte kandidaat, actuatorguards) en
`.tmp/hil/defrost-profiles-2026-09-27T11-09-06-336Z/` (fix, SHA's, captures).
Probeoverlays en binaries worden niet in productie of Git opgenomen.

## Eindstate zonder meetoverlay

De gewone kandidaat is om11:13:56 via OTA geïnstalleerd; verse loads na de
nieuwe boot bevestigen HP1 fysieke/desired1 en HP2 fysieke/desired3, beide
auto=false/IN_SYNC. Om11:15:52: Force CM0, beide0 Hz, online/fresh/identity_ready,
V1.5, geen actieve/manual defrost, injectieswitch OFF; runner exit0/lock vrij.
Deze afzonderlijke eindreadback is heap97347 B, minimum39828 B, grootste
block51200 B, fragmentatie47,405%. Dit is een andere netwerktiming dan de
identieke probe-belasting; daarom geen blanketclaim dat elk moment55296 B
beschikbaar is. Gewone instellingen zijn op verzoek niet hersteld.

Hostregressies82, defrostcontracts17, C++format en docschecks PASS.
Onafhankelijke cold review van serializer/failurepaden: geen codeblocker.
