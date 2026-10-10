# HIL-resultaat: recordergeheugen, 10 oktober 2026

De gerichte Q Duo WiFi-run liep van 9 oktober 15:10:49 tot 10 oktober
02:24:35 UTC. Resultaat: **PASS_TESTED_SCOPE** voor opname, eviction en
range-export. Geheugen-/stackreleasekwalificatie blijft **PARTIAL**.

## Firmware en meetopzet

Beide testbuilds gebruiken `configs/heatpump_controller_q/duo_wifi.yaml`,
de afzonderlijke `openquatt-test`-identiteit en dezelfde toegevoegde
geheugen-/stackinstrumentatie. De recorder behoudt de echte 10 s sampling.
Alle ODU- en OpenTherm-peers zijn gesimuleerd; er is geen productiecontroller
gebruikt. OTA/restarts zijn uitgevoerd, geen fysieke koude power-cycle.

| Artifact | Commit | Config-hash | OTA-bytes |
|---|---|---|---:|
| Baseline | `62fe49b1402d8b4a0032ae664b07183739ea96e6` | `0xf76a44cb` | 2454928 |
| Kandidaat | `e89acd325d1ed102cb274160106a556898ffe313` | `0xa6b5d87c` | 2457040 |
| Hersteld PR #823-profiel | `aff49006faa045895c6705a2b1f1b50f4da35e92` | `0x62138aa2` | 2452736 |

SHA256 van de geüploade artifacts:

- Baseline: `fe4a6509754aeb55af2b672150661437b32ac909deeab7d005158b7659b6e2ac`.
- Kandidaat: `23c457eaf77a31dff3504f7aa7ff22dc0e94fb72c96c1a969b512aa118297dc4`.
- Herstel: `722806c5dec855c89d8e2569c504df8693fa7768c9122436366b059b0ee09944`.

De latere CI-fix `d4a82702` is niet als firmware in deze run getest. De
HH:MM-formatbegrenzing verandert de geldige uitvoer en geheugenlayout niet;
de twee andere gewijzigde bestanden zijn hosttests. Dat vervangt geen
runtimevalidatie van een eventuele volgende functionele wijziging.

## Opname en exports

| Contract | Baseline | Kandidaat |
|---|---:|---:|
| Ringbuffer | 1048576 B | 2097152 B |
| Gevulde samples / capaciteit | 1374 / 1374 | 2637 / 2637 |
| Rijgrootte / kolommen | 763 B / 248 | 795 B / 258 |
| Daadwerkelijk behouden historie | 13741 s | 26382 s |
| Vooruitgang oudste / nieuwste sample na eviction | 70024 / 70024 ms | 70121 / 70121 ms |
| Geslaagde exportprobes | 9 | 9 |

Per firmware zijn 15 min, 2 uur, 6 uur en volledige historie geëxporteerd bij
korte en volle historie, plus een volledige export vóór de evictioncheck.
Dezelfde opname-id en volle capaciteit bleven behouden. De opgeslagen
uptimekolom bevestigt onafhankelijk dat oudste én nieuwste samples circa
70 s opschoven. Het verschil met de nominale capaciteit komt door echte
samplingjitter. Er waren geen ontbrekende velden, stringpool-overflow of
configuratiesnapshot-overflow in de kandidaat.

Bij volle historie duurden de 15-minutenexports 233 / 164 ms en de laatste
volledige exports 2181 / 3221 ms, baseline / kandidaat. De eerste volledige
exports vóór eviction duurden 7058 / 7953 ms. Dit zijn afzonderlijke
HTTP-meetpunten, geen latencypercentielen of geïsoleerde mutexmetingen.
Elke firmware had twee begrensde SSE-captures van 45 s met HTTP 200.

## Geheugen en timing

Onderstaande waarden zijn bytes, behalve fragmentatie (%) en lockduur (ms).
'Laatste' is het
laatste meetpunt van die firmware vóór de volgende OTA, niet de huidige
ruimte op de herstelde controller. Er zijn 918 baseline- en 1756
kandidaatwaarnemingen. De mediane afstand tussen polls was 15,100 / 15,094 s;
de grootste afstand 40,111 / 40,084 s, baseline / kandidaat. Dit is de
meetpollcadans, niet het 10 s opname-interval. Polls kunnen korte dalingen missen.

| Meetwaarde | Baseline | Kandidaat |
|---|---:|---:|
| Intern vrij: laatste / laagste poll | 89195 / 80831 | 88759 / 86203 |
| Intern minimum sinds boot | 39828 | 39828 |
| Grootste interne blok: laatste / laagste poll | 45056 / 32768 | 45056 / 34816 |
| Interne fragmentatieproxy: laatste / hoogste poll (%) | 49,49 / 59,46 | 49,24 / 61,27 |
| Exportmeetpunten: intern vrij / grootste interne blok, minima | 69667 / 31744 | 69759 / 31744 |
| PSRAM vrij: laatste / minimum sinds boot | 6647264 / 5447456 | 5342232 / 3144868 |
| Grootste PSRAM-blok: laatste / laagste poll | 6553600 / 6553600 | 5242880 / 5242880 |
| Exportmeetpunten: PSRAM vrij / grootste PSRAM-blok, minima | 5447704 / 5373952 | 3144868 / 3080192 |
| App-stackwatermark | 3958 | 3990 |
| HTTP-stackwatermark: korte / volle historie | 528 / 464 | 528 / 464 |
| Maximale recorderlockduur sinds boot | 61,253 ms | 101,755 ms |

De heapwatermarks zijn cumulatieve minima sinds boot. De exportwaarden zijn
minima van toegevoegde meetpunten tijdens exports; zij bewijzen niet dat
iedere tijdelijke allocatiepiek is waargenomen. Een grootste vrije blok is
afzonderlijk van de totale vrije ruimte. De fragmentatieproxy is
`100 * (1 - grootste interne blok / intern vrij)` uit dezelfde observatierij;
dit is geen volledige allocatietrace of zelfstandig gemeten fragmentatiewatermark.

De laatste PSRAM-waarneming verschilt 1305032 B tussen beide builds. De
gerapporteerde recorder-`storage_size` groeit van 1200512 naar 2505536 B,
een verschil van 1305024 B. De extra live PSRAM-belasting komt daarmee overeen
met de grotere recorderbuffers; dit kwalificeert de piekbelasting niet.

De HTTP-stackmarge was aanvankelijk **528 B**, maar daalde bij volledige
exports naar **464 B in beide builds**. Dat is een bestaande, krappe marge;
geen bewezen veilige ruimte voor extra callback-/netwerkbelasting. De
recorderlock groeide naar **101,755 ms**. Dit is wall-time van alle gemeten
recorderkritieke secties; exportkopie en control-looplatency zijn niet
afzonderlijk gekwalificeerd. De App-loop probeert de recorderlock zonder te
wachten (`lock_state_(0)`), dus dit maximum bewijst geen even lange blokkade van de
warmtepompregeling. Beide punten blijven open voor de mergegate.

## Herstel en resterende mergegate

`restored=true` is bevestigd met profiel
`power-house-startup-pr823-aff49006` en config-hash `0x62138aa2`.
De teruggelezen firmware-identiteit en alle opgeslagen controllerinstellingen
zijn gelijk aan het snapshot van vóór de run.
Een afzonderlijke read-only HIL-rooktest op 10 oktober 08:20:32 UTC bevestigt
dezelfde herstelde identiteit en twee bereikbare V1.5-ODU-simulatoren met
contract `openquatt-modbus-opentherm-v2`.

Deze run bewijst nog geen veilige releasegeheugenmarge. Vóór merge blijven
nodig:

- Een identieke baseline/kandidaatvergelijking via fysieke koude power-cycle
  op de relevante Q Single- en Q Duo-profielen; Single is niet runtimegemeten.
- Realistische gelijktijdige native HA/API-, web/SSE-, MQTT-, Modbus-,
  OpenTherm- en OTA-belasting voor zover het profiel die paden gebruikt,
  inclusief CIC/TLS waar actief. De run gebruikte REST-inputs, HTTP-exports
  en gesimuleerde buspeers, niet die volledige overlap.
- Een afgesproken intern-heap-, grootste-blok-, fragmentatie- en stackbudget
  voor de grootste gelijktijdige allocaties, met aantoonbare reserve. Vrije
  PSRAM van circa 3 MiB tijdens een volledige export is daarvoor niet genoeg
  bewijs; de tekstpool is ook niet tot alle capaciteitsgrenzen gevuld.
- Verklaring of correctie van de krappe HTTP-stackmarge en kwalificatie van
  de langere recorderlock onder die belasting, inclusief looplatency.

De ruwe lokale evidence blijft in de genegeerde runmap
`.tmp/hil-memory/runs/2026-10-09T15-10-49-779Z-recorder-memory/` van de
`debug-recorder-memory-hil`-worktree: `report.json`, `snapshot.json`,
`base-memory.jsonl`, `test-memory.jsonl`, exports en begrensde SSE-captures.
Firmwarebinaries, lokale configs en volledige recordings worden niet aan Git
toegevoegd. Dit document bewaart alleen de relevante meetwaarden en grenzen.
