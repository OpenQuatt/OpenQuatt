# Gegevens delen: technische naslag

Deze naslag beschrijft het uurbericht met gebruiksstatistieken. Zie [Gegevens delen en privacy](web-app/privacy.md) voor de gebruikerskeuzes. Crashrapporten en optionele prestatiemetingen zijn afzonderlijke berichten.

## Gebruiksstatistieken en privacy


Tijdens een nieuwe Quick Start staat delen standaard aan en verschijnt de opt-out vóór het afronden. De keuze wordt pas opgeslagen wanneer die stap werkelijk wordt geopend. Je kunt de keuze later wijzigen via **Instellingen → Systeem → Gebruiksstatistieken**. Zolang Quick Start niet is afgerond, wordt niets verzonden. Daarna, of wanneer je delen later zelf aanzet, verstuurt OpenQuatt vrijwel direct en vervolgens ongeveer elk uur één klein bericht naar de centrale OpenQuatt-loggingserver.

Een ontbrekende telemetrykeuze geldt nooit als toestemming. Bestaande installaties starten na de introductie van deze functie daarom met delen uit, ook wanneer hun oude Quick Start-status ontbreekt. Als correctie op de eerste telemetryversie wordt de oude opslagindeling eenmalig naar uit gemigreerd; het willekeurige installatie-ID blijft behouden. Ook wie delen in die korte eerste versie bewust had aangezet, moet het daardoor eenmalig opnieuw inschakelen. Nieuwe installaties krijgen de standaard-aan opt-out alleen wanneer ze de gebruiksstatistiekenstap van Quick Start werkelijk openen.

Het bericht bevat uitsluitend:

- een willekeurig installatie-ID;
- de Unix-timestamp van de momentopname in seconden, of `null` zolang de klok nog niet is gesynchroniseerd;
- uptime;
- firmwareversie en releasekanaal;
- hardwareprofiel en, als beschikbaar, hardwarerevisie;
- `Single` of `Duo` en `Wi-Fi` of `Ethernet`;
- `quatt_hybrid_generation_config`: `v1`, `v1_5` of `v2` volgens de ingestelde Quatt Hybrid-versie;
- `flow_source_config`: `cic`, `controller_local` of `outdoor_unit`, afgeleid uit de algemene en (bij Q) Q-specifieke flowselectie;
- `heating_strategy`: `power_house` of `heating_curve`;
- de gekozen regelbronnen in `room_temperature_source`, `room_setpoint_source`, `outside_temperature_source`, `heating_enable_source`, `cooling_enable_source`, `cooling_dew_point_source`, `external_heat_demand_source` en `heating_supply_target_source`, genormaliseerd naar vaste waarden zoals `auto`, `local`, `outdoor_unit`, `cic`, `opentherm`, `home_assistant`, `api_input`, `mqtt`, `cic_or_home_assistant`, `schedule`, `disabled` en `heating_curve`;
- vrij heapgeheugen, het minimum sinds de start, het grootste vrije heapblok en vrij PSRAM;
- maximale looptijd van de firmwareloop, ESP-chiptemperatuur en reden van de laatste herstart;
- vier Modbus-betrouwbaarheidstellers, rechtstreeks op de primaire ODU-bus geteld en onafhankelijk van het ingestelde logniveau: partial responses, parse failures, succesvol herstelde responses met voor- of naloopruis en offline-transities. Per bericht wordt de toename sinds de vorige succesvolle publicatie verstuurd;
- bij Wi-Fi: de signaalsterkte in dBm;
- of CiC JSON-feed inlezen, Quatt-app via CiC en de OpenTherm-thermostaatkoppeling aanstaan;
- `boiler_assist_enabled`: of CV-ketel-/boilerondersteuning aanstaat;
- `boiler_connection`: `on_off` voor de `R1`-aansluiting en `opentherm` voor OTB; firmware zonder OTB-keuze rapporteert automatisch `on_off`;
- of MQTT inputbronnen als geheel aanstaan;
- of RAM-trends, flashtrends, beslisloghistorie en lifetime-energiehistorie aanstaan; RAM-loghistorie wordt altijd als `true` gerapporteerd omdat die permanent actief is.

Een niet-ondersteunde functie, tijdelijk nog niet geïnitialiseerde keuze, onbekende keuze of niet-beschikbare sensor krijgt de waarde `null`; `false` betekent dat de functie beschikbaar maar uitgeschakeld is. Dit geldt ook afzonderlijk voor de nieuwe configuratievelden. `flow_source_config` is `null` zolang de benodigde flowselectie nog geen bekende toestand heeft. Zo is de Wi-Fi-signaalsterkte bij Ethernet `null`. `boiler_connection` is alleen `null` wanneer de OTB-select bestaat maar tijdelijk nog geen geldige toestand heeft, of een onbekende optie bevat.

Het bericht bevat nooit een MAC-adres, lokaal IP-adres, wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of andere inloggegevens. Ook MQTT-servergegevens, topics, ontvangen MQTT-waarden, ingestelde temperaturen of grenzen, verwarmingsmetingen, regelwaarden, Modbus-frames en gewone logregels gaan niet mee. Alleen de hierboven genoemde communicatiebetrouwbaarheidstellers worden gedeeld. De OpenQuatt-loggingserver ziet bij een netwerkverbinding technisch wel het bron-IP-adres, maar dit staat niet in de payload en OpenQuatt slaat het niet op. In de web-app staat onder **Welke gegevens worden gedeeld?** (in Quick Start **Wat gaat er mee?**) een eenmalige momentopname van de JSON-vorm. De vier Modbus-tellers worden rechtstreeks uit de ODU-bus gelezen bij de echte verzending en zijn bewust geen web-/Home Assistant-entiteiten; daarom staan alleen deze vier velden in de lokale preview op `null`. Het getoonde `message_id` en `timestamp_s` worden voor een echte verzending opnieuw bepaald; `reset_reason` is niet via de lokale web-API beschikbaar en staat in deze preview eveneens op `null`.

Wanneer delen voor het eerst actief wordt, maakt de controller met de hardware-randomgenerator een UUIDv4 aan en bewaart die lokaal. Een UUIDv4 heeft 122 willekeurige bits; zelfs bij één miljoen installaties is de kans op minstens één dubbel ID kleiner dan ongeveer `10^-25`. Dit ID blijft gelijk na een OTA-update en wanneer je delen tijdelijk uitzet. Je kunt het bekijken via **Instellingen → Systeem → Gebruiksstatistieken**. Een fabrieksreset maakt een nieuw ID. De keuze en het ID worden niet via een instellingenbackup naar een andere controller gekopieerd. Uitzetten stopt nieuwe berichten direct; er wordt geen wachtrij voor later opgeslagen. Na een mislukte verzending maakt iedere retry een verse momentopname, maar behoudt binnen dezelfde retryreeks het `message_id` zodat een verloren QoS 1-bevestiging kan worden gededupliceerd.

De statistiekenclient staat los van de configureerbare [MQTT inputbronnen](mqtt.md): hij publiceert alleen dit ene bericht, subscribed nergens op en schakelt ESPHome MQTT-discovery, entiteitspublicaties en logexport niet in. Het JSON-bericht wordt met QoS 1 en zonder retain gepubliceerd op `openquatt/devices/<installation-id>/telemetry`. De broker bewaart het daardoor niet als retained state voor later verbindende subscribers; de loggingserver slaat ieder ontvangen bericht zelf op. Een eerder door oude firmware retained opgeslagen payload wordt door een non-retained publicatie niet gewist en moet zo nodig eenmalig op de centrale broker worden verwijderd. Een build zonder geconfigureerde centrale loggingserver maakt ook wanneer delen aanstaat geen externe verbinding.

