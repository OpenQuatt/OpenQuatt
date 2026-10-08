# OpenQuatt captive portal compatibility patch

ESP32-bronnen uit ESPHome **2026.10.0b1**, tagcommit
`33cf262616960c9549252f79ec540d19996715d4`, onder de upstream MIT-licentie:
zie `LICENSE-MIT`.

Gerichte delta:

- De API-key provisioningtimer sluit portal/DNS niet meer. De lokale Wi-Fi-
  component sluit deze pas na geslaagde verbinding en gecontroleerde opslag.
- Bij `USE_OPENQUATT_CAPTIVE_PORTAL_ROUTER` wordt portalpolicy atomisch actief
  vóór `base_->init()` de listener kan starten, en inactief vóór teardown.
  HTTPD `canHandle()` leest die snapshot. Dit voorkomt het early-startvenster
  waarin de upstream `/wifisave` eerder dan de eigen handler zou winnen.

De eigen router registreert vóór portalsetup en bezit `/wifisave` en de checked
statusroute. De save-responsepatch en recovery-exclusies in deze kopie zijn
verwijderd: routes en betrouwbaar succes horen in eigen OpenQuatt-handlers.
Deze kopie blijft nodig tot upstream de aparte Wi-Fi-provisioninglifecycle en
voor-listener/voor-teardownhooks ondersteunt. Geen extra credentialstore.
