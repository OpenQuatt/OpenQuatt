# OpenQuatt Wi-Fi compatibility patch

ESP32-bronnen uit ESPHome **2026.9.0** (`esphome/components/wifi`), onder de
upstream MIT-licentie: zie `../web_server_base/LICENSE-MIT`. Formatting volgt
de repository; niet-ESP32 platformimplementaties zijn niet meegenomen.

Functionele delta voor #421, fase 4:

- Bind native preferences éénmaal vóór runtime-STA wordt geladen. Anders kan
  disable/enable de sleutel veranderen van `88491487` naar de configuratiehash.
- Wis native STA en fast-connect metadata met gecontroleerde save/sync/readback.
  Ook zonder `fast_connect` wordt de bijbehorende flashslot gewist. Geen tweede
  credentialstore of herstelmarker in flash.
- Zonder opgeslagen STA blijft provisioning actief. Nieuwe gegevens staan eerst
  alleen in RAM; pas na verbinding met het ingediende SSID én gecontroleerde
  opslag sluit de AP. Een gewijzigde credential-pair moet opnieuw verbinden,
  ook bij hetzelfde SSID; alleen de reeds bewezen pair van Improv wordt hergebruikt.
  Bij verkeerde gegevens/stroomuitval blijft de lege store
  dus herkenbaar. Opslagfalen vereist opnieuw indienen; geen automatische retry.
- De API-key provisioningtimer sluit niet langer de Wi-Fi-AP. Bij bekende maar
  onbereikbare Wi-Fi geldt de native `ap_timeout` van 90 seconden; zonder STA start de
  AP meteen. `captive_portal` heeft dezelfde gerichte timerpatch.

Dit ondersteunt runtime-provisioned Wi-Fi op de officiële profielen; recovery
weigert gecompileerde `wifi.networks`. De ESPHome preferencebackend is eigenaar
van caching/flash. Een mislukte sync bewijst niet dat flash ongewijzigd bleef;
dan volgt geen succes/reboot. Een readback is geen power-cut-test.

Bij een ESPHome-upgrade opnieuw vergelijken met upstream. HIL moet AP/DNS,
Improv, stroomonderbreking, Ethernetvoorkeur en interne heap/stackmarges toetsen.
