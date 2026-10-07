# NVS capaciteit en persistentie

De huidige Q-firmware gebruikt een NVS-partitie van 24 KiB. Sessiestatus in RAM
houden en oude keys opruimen geeft ruimte terug. De berekende volledige bezetting
blijft echter onder de projectmarge van 100 vrije entries. Een cleanup alleen
maakt die marge dus niet aantoonbaar voldoende voor nieuwe functies.

## Huidige besparing

`oq_air_purge_return_to_auto` begint na iedere boot op aan. De acht
`oq_compressor_cycling_alert_*`-globals worden alleen tijdens de huidige boot
onthouden. Een compressor-cyclingmelding verdwijnt bij bevestiging of herstart;
de instelbare waarschuwingsgrenzen blijven persistent. Samen vervallen negen
kleine preferences, nominaal 27 entries. Hun oude keys worden gericht opgeruimd.

`oq_flow_last_good_pwm`, `oq_flow_last_good_pwm_cooling`,
`oq_system_thermal_energy_daily`, `oq_system_thermal_energy_cumulative` en
`oq_heating_curve_pid` behouden hun bestaande persistentie.

`oq_cooling_fallback_night_min_last_day_key` wordt alleen in RAM onthouden,
met beginwaarde `-1`. De datum voorkomt herhaald overnemen van een afgeronde
nacht binnen dezelfde boot; hij controleert niet de ouderdom van de temperatuur.
Na een reboot overdag ontbreekt ook het lopende nachtvenster in RAM en wordt
geen nieuw nachtresultaat overgenomen. Na een reboot tijdens de nacht wordt
het nieuwe venster na 06:00 eenmaal overgenomen. De laatste temperatuur
`oq_cooling_fallback_night_min_last_c` blijft persistent voor de
dauwpuntbenadering. De oude datumkey `esphome/1275799272` (vier bytes) wordt
met type- en lengtecontrole opgeruimd: nominaal drie entries extra besparing.
Bij een teruggezette klok kan een opnieuw waargenomen nacht met dezelfde datum
na een reboot opnieuw worden overgenomen; de datum wordt niet over boots heen
onthouden.

De twee eenmalige migraties en hun vlaggen
`oq_flow_cooling_settings_migrated` en `oq_aux_heat_source_policy_migrated`
vervallen. De oude vlagkeys `esphome/515187816` en `esphome/3865822963`
(elk één byte) worden met type- en lengtecontrole opgeruimd. Dit bespaart
nog zes entries; bestaande afzonderlijke instellingen en beide flowcaches
blijven persistent en worden niet overschreven.

**Kleine breaking change bij upgrade vanaf firmware <v0.49.0:** de oude
gecombineerde ketelinstelling wordt niet meer omgezet naar
`oq_aux_heat_source_present`. Ontbreekt die afzonderlijke instelling, dan
begint "Auxiliary heat source connected" standaard op aan. Controleer deze
instelling na de update, vooral als de ketel eerder uitgeschakeld of afwezig
was. Ketelondersteuning en storingsfallback behouden hun eigen voorkeuren;
de standaard aanwezigheidskeuze start op zichzelf geen ketelvraag.
Ontbreken bij firmware van vóór v0.33.0 ook de afzonderlijke koelrecords,
dan begint het koelsetpoint op 800 L/h en de koelstartcache op PWM 440.
De verwarmingswaarden worden niet meer automatisch overgenomen.

Twee aanvullende legacy-blobs kunnen nog ruimte bezetten:

| Namespace en key | Oud record | Blobgrootte | Nominale entries indien aanwezig |
| --- | --- | ---: | ---: |
| `esphome/752195988` | `openquatt_crash_telemetry_record` | 2812 bytes | minstens 90 |
| `esphome/1156115452` | `openquatt_api_security_store` | 40 bytes | 4 |

De cleanup controleert key, blobtype en lengte zonder de inhoud te lezen of te
loggen. Bij een afwijkend type, andere lengte of leesfout blijft de key staan.
Wissen en commitfouten worden gelogd; een volgende boot probeert het opnieuw.
De huidige crashrecords in `openquatt_data`, de afzonderlijke crashstatus en
native ESPHome Noise-PSK `88491486` blijven behouden.

De gedeelde retirementfunctie wordt vanuit beide bestaande boot-hookvormen
aangeroepen. ESPHome kan bij package-samenvoeging een dict door een lijst
vervangen of omgekeerd. De regressietest controleert daarom de daadwerkelijk
gevalideerde configuraties van Q Single, Q Duo en `duo_hil.yaml`; losse
YAML-fragmenten bewijzen niet dat een hook wordt uitgevoerd.

Een downgrade naar firmware die de verwijderde legacy-blobs nog gebruikt,
vindt het oude crashrapport en de oude eigen API-beveiligingskeuze niet meer.
Controleer API-beveiliging afzonderlijk bij zo'n historische downgrade.
Teruggewonnen legacyruimte telt niet nogmaals als verlaging van de hieronder
berekende huidige dataset.

## Aanvullende legacycleanup

De drie oude globals `oq_water_supply_temp_calibration_source_code`,
`oq_water_supply_temp_calibration_source_fingerprint` en
`oq_water_supply_temp_calibration_checksum` worden niet meer aangemaakt of
bij een nieuwe kalibratie bijgewerkt. Dit verlaagt de huidige dataset met negen
entries. De vier brongebonden kalibratierecords en de offsetnumber blijven behouden.

De oude keys `esphome/2609287369`, `esphome/3358605580` en
`esphome/909863605` (elk vier bytes) worden met type- en lengtecontrole
opgeruimd. Er is geen import, opslagmigratie of migratievlag meer. Onverwachte
typen en groottes blijven behouden; bij een wis- of commitfout probeert een
volgende boot de cleanup opnieuw. Gedeeltelijke cleanup wijzigt geen huidig
brongebonden record.

**Breaking change voor uitsluitend legacy-aanvoerkalibratie:** een kalibratie
die alleen in het oude formaat bestaat vervalt bij de update. Zonder geldig
brongebonden record gebruikt de huidige bron een offset van 0 °C. Het ontbreken
van een record zet niet automatisch de melding "kalibratie vereist" aan;
gebruikers met een oude kalibratie moeten de aanvoerkalibratie opnieuw uitvoeren.
Bestaande geldige brongebonden kalibraties blijven behouden, inclusief hun
offset en bronbinding.

De voormalige `oq_ram_log_history_switch` (`esphome/306736601`, één byte) kan
nog drie entries gebruiken. RAM-loghistorie staat al permanent aan. De oude
vorststatus `oq_cm_frost_prev` (`esphome/2881445393`, één byte) kan eveneens
drie entries gebruiken. Beide keys worden met type- en lengtecontrole in de
gedeelde bootactie opgeruimd. De oude afzonderlijke vorsthook viel bij
package-samenvoeging uit de Q Duo WiFi-configuratie weg.

Behoud van kalibratie bij een downgrade naar uitsluitend het oude formaat
wordt niet ondersteund.
De 61 gemeten WiFi-driverentries, PHY-opslag en
keys met onbewezen herkomst vallen buiten deze cleanup.

## Budgetberekening

Een NVS-pagina bevat 126 entries. Van de zes pagina's blijft één beschikbaar
voor garbage collection: 630 entries voor live records. Een blob kost
`1 + aantal_chunks + ceil(bytes / 32)` entries. Kleine ESPHome-blobs kosten
doorgaans drie; een native klein NVS-getal kost één.

`scripts/check_nvs_budget.py` telt geconfigureerde componentinstances. Custom
records kosten 67 entries voor Q Duo en 58 voor Q Single. Daaronder vallen ook
de defrostprofielen, debug-recorderkeuze en het restart-handoffrecord.

Voor de huidige Q-configuratie wordt 88 entries systeemopslag begroot:
WiFi en fast-connect 9, safe mode 3, factory-resetcounter 3, native API Noise-PSK 3,
drie namespaces 3, PHY-kalibratie 63, PHY-MAC 3 en PHY-versie 1. De PHY-schatting
neemt twee chunks voor 1904 bytes aan. Extra fragmentatie, vendorrecords en
achtergebleven legacykeys kunnen meer ruimte kosten. Dit is geen bovengrens.

| Q Duo WiFi | Entity entries | Custom | Systeem | Totaal | Nominaal vrij |
| --- | ---: | ---: | ---: | ---: | ---: |
| Vóór deze sessiestatuswijziging | 463 | 64 | 88 | 615 | 15 |
| Na de sessiestatuswijziging | 436 | 64 | 88 | 588 | 42 |
| Na uitfasering van de drie kalibratieglobals | 427 | 64 | 88 | 579 | 51 |
| Na verplaatsing van de nachtminimumdatum naar RAM | 424 | 64 | 88 | 576 | 54 |
| Na uitfasering van de twee migratievlaggen | 418 | 64 | 88 | 570 | 60 |
| Met nieuwe Power House restart cooldown | 421 | 64 | 88 | 573 | 57 |
| Na bundeling van beide compressorwaarschuwingsgrenzen | 415 | 67 | 88 | 570 | 60 |

De Q Duo-tabel bevat ook de drie entries van de geneste switch
`oq_ot_slave_enabled`. De checker telt deze geneste entity nu ook mee.

De bestaande grens `REQUIRED_AVAILABLE_ENTRIES = 100` blijft behouden.
Daarom geeft de gecorrigeerde checker voor dit profiel FAIL. Alle waarden zijn
berekeningen bij volledige bezetting, geen apparaatmetingen. Blobvervanging
schrijft nieuwe chunks voordat de oude worden vrijgegeven; 60 vrije entries
zijn minder dan de minimaal 62 voor een gewijzigde volledige PHY-blob.
De Single-begroting moet bij vervolgstappen opnieuw uit de actuele configuratie
worden bepaald. Ook de kleine bundelproef lost het totale ruimtetekort niet op.

## Meting op de testcontroller

Gebruik de [HIL-procedure](hil-testing.md), uitsluitend `openquatt-test`, met
de gedeelde lablock. Begin nadat de andere run gereed is en de lock vrij is.
Een vrije lock alleen bewijst niet dat een handmatige labrun klaar is. Gebruik
een afzonderlijke buildmap; start geen OTA of reboot tijdens een andere run.

1. Leg firmwareversie, commit/config-hash, partitiegrootte en stabiele
   controllerinstellingen vast. Maak eerst een read-only nulmeting.
2. Meet NVS `used_entries`, `free_entries`, `available_entries` en namespaces.
   Inventariseer alleen namespace, key, type en bloblengte. Log geen
   WiFi-, MQTT-, webauth- of API-credentialwaarden en geen crashinhoud.
3. Leg aanwezigheid van de negen retired preferences en beide legacy-blobs
   vast. Zonder aanwezige legacyblob is nul extra legacywinst het juiste resultaat.
4. Plaats de kandidaat via de normale OTA-route na toestemming voor die concrete
   run. Gebruik `configs/heatpump_controller_q/duo_hil.yaml` als basis: dezelfde
   Q Duo-firmware met de hostname `openquatt-test` en write-interval 1 s.
5. Meet opnieuw na alle boot-cleanups en na stabiele communicatie. Vergelijk
   dezelfde keys en systeemrecords; PHY- of configuratiewijzigingen kunnen de
   totale delta beïnvloeden. Controleer dat huidige instellingen en API-pairing
   behouden zijn. Bevestig bootidentiteit en uptime, niet alleen een HTTP-ack.
6. Wijzig een representatieve blijvende instelling, wacht op de preferences-flush
   en verifieer haar na een toegestane echte restart. Controleer de nieuwe
   RAM-defaults, flow-startcaches en energietellers; gebruik metadata/instrumentatie
   waar een interne waarde niet via een bestaande entity zichtbaar is.
7. Meet na een tweede boot. Er mogen geen verwijderde keys opnieuw verschijnen
   en geen verdere cleanup-writes nodig zijn. Een herhaalde gelijke instelling
   moet persistent blijven. Registreer eventuele write-, commit- of restorefouten.
8. Vergelijk ook actuele en minimale interne heap, grootste vrije blok en
   relevante stack-watermarks onder dezelfde rustige en gecombineerde belasting.
   Gebruik één begrensde logstream. Bewaar het rapport onder `.tmp/hil/` en leg
   de uiteindelijke controller- en simulatorstatus vast.

Vul geen kunstmatige legacyrecords in op de controller voor deze meting. De
hosttests injecteren ontbrekende keys, verkeerde types/groottes, open-/lees-/
wis-/commitfouten en retries. Die tests bewijzen de foutafhandeling van de
productiehelper met NVS-stubs; ze vervangen geen fysieke persistentiemeting.

## HIL-resultaat 6 oktober 2026

De Q Duo-testcontroller is via normale OTA voorzien van de kandidaat met
ESPHome 2026.9.0, config-hash `0x719eba15`. Binnen dezelfde boot zijn de
NVS-statistieken vóór en na de gerichte cleanup vergeleken:

| Moment | Used | Free | Available | Namespaces |
| --- | ---: | ---: | ---: | ---: |
| Vóór cleanup | 488 | 268 | 142 | 5 |
| Na cleanup | 461 | 295 | 169 | 5 |
| Na eerste herstart | 461 | 295 | 169 | 5 |
| Na tweede herstart | 461 | 295 | 169 | 5 |

Alle negen oude sessiepreferences waren aanwezig en zijn verwijderd: precies
27 entries winst. De twee legacy-blobs waren al afwezig, dus hier is nul extra
legacywinst gemeten. De native API-Noise-key bleef aanwezig als 32-byte blob;
een daadwerkelijke HA-pairinghandshake is in deze run niet getest.

De compressor-startwaarschuwingsgrens is gewijzigd van 6 naar 7 en bleef na
beide herstarts op 7. De air-purgekeuze is vóór de eerste herstart uitgezet en
begon daarna weer op aan. Beide flowcaches bleven 153/440; thermische energie
bleef dagelijks 3.663273 kWh en cumulatief 56.68042 kWh. Cyclingstatus begon
op false. Na beide herstarts waren de negen keys nog afwezig en traden geen
verdere retirement-writes op.

De oorspronkelijke firmware met config-hash `0x57f4228d`, waarschuwingsgrens
6, air-purgekeuze en vastgelegde controllerinstellingen zijn teruggezet en
geverifieerd. De gedeelde lablock is vrijgegeven. Gedetailleerde metadata en
het rapport staan lokaal onder
`.tmp/hil/2026-10-06T19-00-59-011Z-nvs-persistence/`.

De actuele interne vrije heap van de kandidaat was circa 98–99 KiB,
het minimum-sinds-boot circa 39 KiB en het grootste vrije blok 58 KiB.
Dit was een rustige CM0-persistentietest: gelijktijdige HA/web/API/MQTT/bus/OTA-
belasting en stack-watermarks zijn niet gemeten. Hieruit volgt geen bewezen
runtime-geheugenmarge voor een release.

De gemeten 169 beschikbare entries gelden voor de testcontroller met de
records en firmware van die eerdere HIL-run. Ze vervangen de huidige
volledige-bezettingsschatting van 60 vrije entries voor Q Duo niet; de
budgetgate van 100 blijft FAIL.

## Proef: gebundelde compressorwaarschuwingsgrenzen

`openquatt_compressor_limits` bewaart uitsluitend
`oq_compressor_starts_warning_limit_2h` en
`oq_compressor_starts_warning_limit_72h` samen in `esphome/oq_cycle_limits`.
IDs, namen, sliderstap en grenzen blijven gelijk: 1–20 met default 6 en
1–120 met default 40. Dit zijn diagnostische waarschuwingsgrenzen; startlimieten,
actuatoraansturing en de overige instellingen gebruiken hun bestaande opslag.

Het record is 16 bytes: magic (4), schemaversie (2), lengte (2), beide floats
(8). Het kost drie entries in plaats van zes. Een aparte migratievlag of
ESPHome-corewijziging is niet nodig. NVS verzorgt de blob-CRC.

Bij boot wordt eerst de bundel rechtstreeks uit NVS gelezen. Ontbreekt die,
dan worden de twee oude TemplateNumber-blobs gelezen via dezelfde entityhash
(inclusief eventuele device ID). Ontbrekende individuele keys krijgen hun
bestaande default; onleesbare, verkeerd getypeerde of ongeldige waarden blokkeren
de overzetting. Na een geslaagde write, commit en exacte raw readback worden
alleen de oude vier-byte blobs verwijderd. Een herstart vóór die write herhaalt
de import; daarna blijft de bundel autoritatief, ook tijdens gedeeltelijke cleanup.
Voor deze eerste write zijn tijdelijk drie extra entries nodig.

De component herstelt vóór de diagnostische intervalcallbacks. De actuele
waarden en NVS blijven eigendom van de main task. Andere taken schrijven alleen
naar twee vaste slots onder een korte `portMUX`-lock. Main-task setters blijven
synchroon; off-task wijzigingen worden op de volgende loop verwerkt. Er komen
geen worker task, groeiende callbackwachtrij of lange buffers bij.

Writes worden samengevoegd: `write_interval` is standaard 60 s en bepaalt ook
de retryperiode. Gewijzigde waarden worden bij gecontroleerde shutdown/OTA
nogmaals opgeslagen; de mailbox sluit eerst en verwerkt alle geaccepteerde
requests. Bij harde stroomuitval kunnen wijzigingen sinds de laatste geslaagde
write vervallen. Een opslagfout geeft componentstatus/logging en behoudt de
pending waarden voor een volgende poging. Acceptatie van een setter bewijst
dus geen duurzame opslag.

Een aanwezige bundel met onbekend schema, verkeerde lengte of ongeldige waarden
blijft staan. De waarschuwingen gebruiken dan defaults, setters worden geweigerd
en de component meldt een restorefout; stale legacywaarden worden niet geïmporteerd.
Factory reset wist ook deze key. Downgrade wordt niet ondersteund: oudere firmware
kan na cleanup voor deze twee grenzen terugvallen op defaults.

Hosttests injecteren volle NVS, open-/type-/lengte-/commit-/readback-/erasefouten,
herstarts rond de migratie, callbackinterleavings, off-task bursts en shutdown
zonder tussenliggende loop. Een factory-resettest controleert dat shutdown
geen gewiste data herschrijft. OTA/reboot en interne heap/stackmarges moeten
nog op de testcontroller worden bevestigd; eerdere HIL-metingen hierboven
behoren niet bij deze bundelproef.

## Vervolgontwerp voor overige gebundelde opslag

Bundel alleen waarden met dezelfde eigenaar en lifecycle. Acht 4-byte waarden
kosten afzonderlijk circa 24 entries; een kale 32-byte blob circa 3. Met een
header voor magic, versie, lengte en checksum groeit het record bijvoorbeeld
naar 48 bytes en kost het 4 entries: circa 20 entries winst. De exacte winst
hangt af van de definitieve veldlayout.

Kandidaten voor een aparte inventarisatie zijn samenhangende diagnostische
grenzen en instellingen per feature. Start niet met flow-startcaches,
energietellers of brongebonden kalibratierecords: hun herstelgedrag en
bronbinding moeten behouden blijven. Verwijder migratiemarkers alleen als hun oorspronkelijke
migratie aantoonbaar niet opnieuw kan worden gestart.

De huidige inventaris geeft deze concrete instellingenclusters. De schatting
gebruikt een header van 12 bytes en 4 bytes per waarde; records blijven per
feature gescheiden. Alle waarden blijven persistent.

| Cluster | Huidige number-preferences | Losse entries | Gebundeld | Mogelijke winst |
| --- | ---: | ---: | ---: | ---: |
| Power House `ph_kp_w_per_k`, beide comfortbanden, demand rise/fall en run-extension stop margin/restart cooldown | 7 | 21 | 4 | 17 |
| Heating Curve PID `heating_curve_pid_kp`, `heating_curve_pid_ki`, `heating_curve_pid_kd` | 3 | 9 | 3 | 6 |
| Flow setpoint, cooling flow setpoint, manual iPWM en Flow PI Kp/Ki | 5 | 15 | 3 | 12 |

Deze drie vervolgclusters leveren theoretisch samen 35 entries op: Q Duo zou daarmee
van 60 naar 95 nominaal vrij gaan. Ze halen op zichzelf de marge van 100 dus
nog niet. Power House, PID en Flow hebben invloed op de regeling; hun volledige
configuratie moet vóór de eerste controlcyclus beschikbaar zijn. Beoordeel deze
controlclusters afzonderlijk na de proef met de twee diagnostische grenzen.

Een mogelijke OpenQuatt-component houdt het complete record in RAM, herstelt
het vóór de afhankelijke entities en schrijft alleen bij echte wijzigingen.
Die aanpak vereist geen ESPHome-corewijziging. Iedere setter valideert eerst;
een mislukte save mag niet als duurzaam opgeslagen worden teruggelezen.

Voor een migratie gelden de volgende grenzen:

- Maak eerst een geldig nieuw record uit de bestaande afzonderlijke waarden;
  behoud bestaande defaults voor ontbrekende keys.
- Schrijf, sync en lees het record terug voordat oude keys worden verwijderd.
  Een herstart tussen die stappen moet veilig opnieuw kunnen migreren.
- Budgetteer tijdelijk oude én nieuwe records. Juist een bijna volle NVS kan
  onvoldoende ruimte hebben voor de eerste migratiewrite.
- Behoud de oude keys zolang downgradeondersteuning dat vereist. Nieuwe-only
  writes maken die oude keys stale; definieer daarom vooraf welke oudere
  firmwareversies ondersteund blijven. Dubbel schrijven geeft voorlopig geen
  ruimtewinst.
- Een nieuw ongeldig record mag niet stilzwijgend stale legacywaarden herstellen
  nadat de migratie al afgerond is. Definieer herstelbeleid en een duurzame
  migratiestatus; neem ook hun opslagkosten mee.
- Test ontbrekende/ongeldige records, volle NVS, sync-/readbackfouten,
  tussentijdse herstarts en upgrade/downgrade voordat de backend wordt ingevoerd.

Bundeling is een afzonderlijke vervolgstap. De huidige cleanup wijzigt geen
partitietabel en gebruikt geen tweede NVS-partitie.
