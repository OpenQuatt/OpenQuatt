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

## Budgetberekening

Een NVS-pagina bevat 126 entries. Van de zes pagina's blijft één beschikbaar
voor garbage collection: 630 entries voor live records. Een blob kost
`1 + aantal_chunks + ceil(bytes / 32)` entries. Kleine ESPHome-blobs kosten
doorgaans drie; een native klein NVS-getal kost één.

`scripts/check_nvs_budget.py` telt geconfigureerde componentinstances. Custom
records kosten 64 entries voor Q Duo en 55 voor Q Single. Daaronder vallen ook
de defrostprofielen, debug-recorderkeuze en het restart-handoffrecord.

Voor de huidige Q-configuratie wordt 88 entries systeemopslag begroot:
WiFi en fast-connect 9, safe mode 3, factory-resetcounter 3, native API Noise-PSK 3,
drie namespaces 3, PHY-kalibratie 63, PHY-MAC 3 en PHY-versie 1. De PHY-schatting
neemt twee chunks voor 1904 bytes aan. Extra fragmentatie, vendorrecords en
achtergebleven legacykeys kunnen meer ruimte kosten. Dit is geen bovengrens.

| Q Duo WiFi | Entity entries | Custom | Systeem | Totaal | Nominaal vrij |
| --- | ---: | ---: | ---: | ---: | ---: |
| Vóór deze sessiestatuswijziging | 460 | 64 | 88 | 612 | 18 |
| Na deze sessiestatuswijziging | 433 | 64 | 88 | 585 | 45 |

De bestaande grens `REQUIRED_AVAILABLE_ENTRIES = 100` blijft behouden.
Daarom geeft de gecorrigeerde checker voor dit profiel FAIL. Alle waarden zijn
berekeningen bij volledige bezetting, geen apparaatmetingen. Blobvervanging
schrijft nieuwe chunks voordat de oude worden vrijgegeven; 45 vrije entries
zijn minder dan de minimaal 62 voor een gewijzigde volledige PHY-blob.
De huidige Q Single WiFi-configuratie telt 424 entity-entries, 55 custom en
88 systeem: 567 totaal, nominaal 63 vrij. Ook dat profiel haalt de marge niet.

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

De gemeten 169 beschikbare entries gelden voor deze testcontroller met zijn
huidige records en historie. Ze vervangen de volledige-bezettingsschatting van
45 entries voor Q Duo niet; de budgetgate van 100 blijft FAIL.

## Vervolgontwerp voor gebundelde opslag

Bundel alleen waarden met dezelfde eigenaar en lifecycle. Acht 4-byte waarden
kosten afzonderlijk circa 24 entries; een kale 32-byte blob circa 3. Met een
header voor magic, versie, lengte en checksum groeit het record bijvoorbeeld
naar 48 bytes en kost het 4 entries: circa 20 entries winst. De exacte winst
hangt af van de definitieve veldlayout.

Kandidaten voor een aparte inventarisatie zijn samenhangende diagnostische
grenzen en instellingen per feature. Start niet met flow-startcaches,
energietellers of de calibratiemigratie: hun herstel- en downgradegedrag moet
behouden blijven. Verwijder migratiemarkers alleen als hun oorspronkelijke
migratie aantoonbaar niet opnieuw kan worden gestart.

De huidige inventaris geeft deze concrete instellingenclusters. De schatting
gebruikt een header van 12 bytes en 4 bytes per waarde; records blijven per
feature gescheiden. Alle waarden blijven persistent.

| Cluster | Huidige number-preferences | Losse entries | Gebundeld | Mogelijke winst |
| --- | ---: | ---: | ---: | ---: |
| Power House `ph_kp_w_per_k`, beide comfortbanden, demand rise/fall en run-extension stop margin | 6 | 18 | 4 | 14 |
| Heating Curve PID `heating_curve_pid_kp`, `heating_curve_pid_ki`, `heating_curve_pid_kd` | 3 | 9 | 3 | 6 |
| Flow setpoint, cooling flow setpoint, manual iPWM en Flow PI Kp/Ki | 5 | 15 | 3 | 12 |
| Compressor-start warning limits 2h en 72h | 2 | 6 | 3 | 3 |

Deze vier clusters leveren theoretisch samen 35 entries op: Q Duo zou daarmee
van 45 naar 80 nominaal vrij gaan. Ze halen op zichzelf de marge van 100 dus
nog niet. Power House, PID en Flow hebben invloed op de regeling; hun volledige
configuratie moet vóór de eerste controlcyclus beschikbaar zijn. Begin een
prototype met de twee diagnostische grenzen, en beoordeel controlclusters
afzonderlijk voordat ze dezelfde opslagroute gebruiken.

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
