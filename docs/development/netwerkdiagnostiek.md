# Netwerkdiagnostiek en socketbegroting

Onderzoek van de uitgaande verbindingsfouten op `v0.53.0-dev.872+be17fcb`,
27 september 2026. Een herstart herstelde een manifestcheck; de oorzaak is niet
bewezen. De socketlimiet is niet gewijzigd.

## DNS-race bij vertraagde Ethernet-start

De STOP-bevestiging in PR #762 kan de Ethernet-herstart uitstellen tot na de
WiFi-DHCP-configuratie. ESP-IDF wist bij DHCP-start op Ethernet de globale
MAIN/BACKUP-DNS, ook zonder Ethernet-link. De WiFi-route en de per-interface
DNS-cache kunnen daarbij geldig blijven. ESPHome controleert de default route,
maar herstelt DNS niet wanneer die route al correct is.

`openquatt_network` herstelt nu eenmaal per seconde alleen ontbrekende
MAIN/BACKUP-adressen uit de huidige standaardinterface. Selectie, routecontrole
en DNS-herstel gebeuren samen in de TCP/IP-context. Geldige globale DNS en
FALLBACK blijven behouden; STOP/PHY-beveiligingen blijven actief.

De hosttests reproduceren beide opstartvolgordes en testen herstel bij DHCP-renewal,
failover en fouten. De daadwerkelijke DNS-toestand op de gemelde controllers is
niet gemeten; dit is een aangetoond codepad dat de symptomen kan verklaren.

Bronnen: [ESP-IDF DHCP/DNS](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_netif/lwip/esp_netif_lwip.c),
[ESPHome routecontrole](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/network/network_component.cpp).

## Begroting van Q Single en Q Duo

ESPHome 2026.9.0 registreert voor de Q-configuratie 21 sockets:

| Type | Consumenten | Aantal |
| --- | --- | ---: |
| TCP-clients | API 3, webserver 5, captive portal 3, CiC 1, MQTT-input 1, usage/performance-telemetrie 1, crashtelemetrie 1 | 15 |
| UDP | mDNS 2, captive DNS 1 | 3 |
| TCP-listeners | API 1, native OTA 1, webserver 1 | 3 |

Dit is een registratie van verwachte behoeften, geen reservering per component.
Alle BSD-sockets delen `CONFIG_LWIP_MAX_SOCKETS`.

De toegestane gelijktijdigheid is groter dan deze registratie:

- API laat standaard 5 clients toe; de registratie rekent met 3.
- ESP-IDF HTTPD laat 7 clients toe; `web_server` registreert 5.
- HTTPD gebruikt daarnaast een listener en twee interne UDP-sockets.
- `http_request` registreert geen afzonderlijke manifestsocket.
- De native OTA-listener heeft bij een upload ook een geaccepteerde clientsocket.

Daar staat extra reserve tegenover: captive portal registreert 3 TCP-clients
naast `web_server`, terwijl beide dezelfde HTTP-server gebruiken. De twee
OpenQuatt-logstreams vallen binnen de 7 HTTPD-clients; tel ze niet nogmaals op.
Performance-telemetrie deelt de usage-transportclient.

Een conservatieve bovengrens is 24 sockets met de fallback-AP uit:
10 voor HTTPD, 6 voor API, 1 native OTA-listener, 2 mDNS, 3 MQTT-clients,
1 CiC-client en 1 HTTP-download. Een actieve native OTA-upload voegt 1 toe;
actieve captive DNS voegt nog 1 toe. Dit is een bovengrens, geen gemeten
bezetting: niet alle verbindingen zijn normaal tegelijk actief en IPv6 kan
uitstaan. DHCP en lwIP-SNTP gebruiken raw UDP-PCBs, geen BSD-sockets.

De budgetaudit bewijst dus geen socketuitputting op het gemelde apparaat.
Verhoog de limiet pas na meting of een reproduceerbare failure-injectiontest,
met controle van interne RAM-kosten en maximale gelijktijdigheid.

Bronnen: [ESPHome socketregistratie](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/socket/__init__.py),
[API](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/api/__init__.py),
[webserver](https://github.com/esphome/esphome/blob/2026.9.0/esphome/components/web_server/__init__.py),
[HTTPD-configuratie](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_http_server/include/esp_http_server.h).

## Wat de bestaande firmware kan tonen

`Debug Level` past alleen het ESPHome-loglevel aan. De gegenereerde SDK-configuratie
gebruikt `CONFIG_LOG_DEFAULT_LEVEL_ERROR=y`,
`CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=n` en `CONFIG_LOG_TAG_LEVEL_IMPL_NONE=y`.
Runtime `DEBUG` schakelt dus geen volledige ESP-IDF TCP/TLS-debuglogging in.
Usage-telemetrie heeft op DEBUG al heapmetingen bij MQTT-start en cleanup en
task-stackmetingen. Bewaar alleen kort een gerichte opname; zet terug naar INFO.

Bij `MQTT_EVENT_ERROR` logt usage-telemetrie voortaan op WARN:

- TCP-transport: socket-`errno`, ESP-TLS-code, TLS-stackcode en certificaatflags;
- brokerweigering: de MQTT-CONNACK-code;
- ontbrekende foutdetails of een ander type: een expliciete melding;
- interne vrije bytes, minimum sinds boot, grootste vrij blok en vrije PSRAM.

Alleen de velden die bij het fouttype horen worden gelezen. De eventpointer
wordt niet bewaard; credentials, installatie-ID, topic en payload worden niet
gelogd. Retrybeleid, consent en controlesturing blijven ongewijzigd.

## Reproduceer zonder aparte diagnostic build

1. Bewaar firmwareversie, uptime, bestaande foutlogs en actieve clients.
2. Bij een storing: sluit extra webtabs/logstreams, laat de controller draaien
   en probeer één manifestcheck. Noteer of herstel zonder reboot optreedt.
3. Leg bij de volgende fout de transportcode en resourcewaarden samen vast.
   Een lage minimumwatermark is geen bewijs van de huidige vrije marge.
4. Reproduceer in de simulator/HIL met gecontroleerde clients en netwerkfouten.
   Controleer dat de regeling veilig doorloopt en vrijgegeven verbindingen
   werkelijk opnieuw bruikbaar zijn. Een geslaagde manifestcheck bewijst geen OTA.

Een automatische reboot of failover bij uitsluitend een onbereikbare externe
dienst is geen herstelcriterium: de interface kan gewoon verbonden zijn.
