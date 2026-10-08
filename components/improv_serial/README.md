# OpenQuatt Improv Serial persist patch

Bronnen en codegen gekopieerd uit ESPHome **2026.10.0b1**, tagcommit
`33cf262616960c9549252f79ec540d19996715d4`, onder de upstream MIT-licentie:
zie `LICENSE-MIT`. UART/USB, protocol, URL-opbouw en
scanrespons blijven upstream; formatting volgt de repository.

Deze tijdelijke override behoudt USB-Wi-Fi-installatie voor nieuwe devices en
credentialwijzigingen. Upstream meldt `STATE_PROVISIONED` direct nadat een
`void save_wifi_sta()` is aangeroepen. Verbinding is daar geen bewijs van
permanente opslag; bovendien bewijst een bestaand SSID het nieuwe wachtwoord niet.

Gerichte delta:

- `WIFI_SETTINGS` gebruikt Wi-Fi's staged `begin_wifi_provisioning()` in plaats
  van eerst een eigen verbinding en daarna een onbevestigde save.
- `STATE_PROVISIONED` en de succesvolle `WIFI_SETTINGS`-RPC-response volgen pas
  op `SAVED` voor dezelfde generation: nieuwe verbinding plus checked persist.
- Storage-/connectfailure, cancel of een superseding portalgeneration geven
  `ERROR_UNABLE_TO_CONNECT` en `STATE_AUTHORIZED`, zonder succesresponse.
- Een timeout annuleert alleen de eigen generation. Hij wist geen bewezen
  credentials en kan geen nieuwere portal-/Improv-transactie annuleren.

`ERROR_UNABLE_TO_CONNECT` is de bestaande Improv-protocolfout; deze onderscheidt
op de draad opslagfalen niet van netwerkfalen. De installer mag daarom geen
permanente opslag beloven bij een fout. OpenQuatt's portalstatus kan dit wel.

Verwijdercriterium: upstream Wi-Fi en Improv bieden staged credentials,
verbindingsbewijs na credentialwissel en een gecontroleerd persist-resultaat,
plus failure-injectiontests voor oude/superseding transacties en partial writes.
Tot die tijd bij upgrades vergelijken met exact de gepinde upstreamversie.
Hosttests voeren de echte parse/loop/timeout-methods uit tegen de echte
Wi-Fi-opslagmethoden met een failure-injecting backend. Driver/USB/power-cut-
integratie en geheugenmarges blijven firmware/HIL-validatie.
