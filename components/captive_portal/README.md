# OpenQuatt captive portal compatibility patch

ESP32-bronnen uit ESPHome **2026.10.0b1** (`esphome/components/captive_portal`),
tagcommit `33cf262616960c9549252f79ec540d19996715d4`,
onder de upstream MIT-licentie: zie `../web_server_base/LICENSE-MIT`.

Enige functionele wijzigingen: de API-key provisioningtimer sluit de portal/DNS
niet meer, de opslaan-response belooft nog geen persistent succes, en de
recovery-routes blijven buiten de actieve GET-catchall. De lokale
Wi-Fi-component sluit de portal na een geslaagde verbinding/opslag. Geen eigen
portal, credentialsysteem of AP-timer; zie `../wifi/README.md`.
