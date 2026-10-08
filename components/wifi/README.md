# OpenQuatt Wi-Fi compatibility patch

ESP32-bronnen uit ESPHome **2026.10.0b1** (`esphome/components/wifi`), tagcommit `33cf262616960c9549252f79ec540d19996715d4`, onder de
upstream MIT-licentie: zie `LICENSE-MIT`. Formatting volgt
de repository; niet-ESP32 platformimplementaties zijn niet meegenomen.

Functionele delta voor #421, fase 4:

- Bind native preferences éénmaal vóór runtime-STA wordt geladen. Anders kan
  disable/enable de sleutel veranderen van `88491487` naar de configuratiehash.
- Wis native STA en fast-connect metadata met gecontroleerde save/sync/readback.
  Ook zonder `fast_connect` wordt de bijbehorende flashslot gewist. Geen tweede
  credentialstore of herstelmarker in flash.
- Elke nieuwe credential-pair (fresh install, reset of vervanging) wordt eerst
  alleen in RAM getest. De bewezen flashrecord wordt pas na een nieuwe verbinding
  én gecontroleerde save/sync/readback vervangen. Ook bij hetzelfde SSID wordt
  de STA eerst gestopt; de main loop wacht op bevestigde `WIFI_EVENT_STA_STOP`
  voordat de candidate opnieuw verbindt. Dit scheidt oude connect/IP-events
  van het bewijs voor het nieuwe wachtwoord. Een ontbrekende STOP-ack (3 s)
  meldt `CONNECT_FAILED`, zonder verbinding/persistence van de candidate.
- `begin_wifi_provisioning()`/`cancel_wifi_provisioning()` zijn main-loop-API's.
  `get_provisioning_status()` is één atomisch 32-bit snapshot met 28-bit generation
  en 4-bit status; HTTPD leest geen muterende strings. `SAVED` betekent verbinding
  plus gecontroleerde persist. Fouten houden provisioning/AP beschikbaar en
  vereisen opnieuw indienen; geen automatische persistence-retry. Generation
  voorkomt dat een oudere Improv/portal-transactie de nieuwe transactie annuleert.
- Improv Serial en `wifi.configure(save: true)` gebruiken dezelfde status en
  melden pas succes bij `SAVED`. `save_wifi_sta()` blijft als compatibele void
  wrapper, maar bewijst geen voltooide opslag aan zijn caller.
- Een pending/failed candidate maakt de AP weer beschikbaar, ook als een nieuwe
  STA-link al werkt maar persist faalt. Succesvolle AP-close reset de setupflag,
  zodat een volgende provisioning de AP opnieuw kan starten.
- De API-key provisioningtimer sluit niet langer de Wi-Fi-AP. Bij bekende maar
  onbereikbare Wi-Fi geldt de native `ap_timeout` van 90 seconden; zonder STA start de
  AP meteen. `captive_portal` heeft dezelfde gerichte timerpatch.

Dit ondersteunt runtime-provisioned Wi-Fi op de officiële profielen; recovery
weigert gecompileerde `wifi.networks`. De ESPHome preferencebackend is eigenaar
van caching/flash. Een mislukte sync bewijst niet dat flash ongewijzigd bleef;
dan volgt geen succes/reboot. Een readback is geen power-cut-test.
Bij cancelled verbinden wordt de eerdere flashpair in RAM hersteld, met dezelfde
STA-stopbarrière. Een STORAGE_FAILED-resultaat claimt geen rollback.
Disable/start en externe STA-vervanging trekken een lopende generation in;
reload van de bewezen pair mag nooit als succes voor de oude candidate tellen.
Een selective-clearpoging bevriest alle STA/fast-connect writers tot reboot,
ook bij partial failure en tijdens `safe_reboot()`-teardownloops. Een expliciete
clearretry blijft toegestaan; provisioning wordt pas bij een nieuwe boot heropend.

Selectief wissen betreft uitsluitend de twee eenmaal gebonden native slots;
legacy config-hashaliases worden niet blind op recordgrootte verwijderd. Hun
inventarisatie/migratie blijft een aparte releasecheck. De tijdelijke Improv-kopie
staat in `../improv_serial`; deze vervangt uitsluitend de persist-resultaatsemantiek.

Bij een ESPHome-upgrade opnieuw vergelijken met upstream. HIL moet AP/DNS,
Improv, stroomonderbreking, Ethernetvoorkeur en interne heap/stackmarges toetsen.
