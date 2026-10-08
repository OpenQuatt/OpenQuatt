# Beperkte fysieke recovery

Op ESPHome 2026.10.0b1 selecteert de gedebouncete knop een actie **bij loslaten**:
5–10 seconden vraagt een recoveryboot aan; vanaf 10 seconden wordt eerst Wi-Fi
gecontroleerd gewist. Een knop die bij boot al vastzit moet eerst losgelaten worden.
De controller herstart via de bestaande incidentmanager en minimum-off-timepolicy.
Er wordt geen live recoverymodus in een normaal proces geopend.

## Bootgrens en autorisatie

- Een RTC-record van 16 bytes (magic, versie, generatie, CRC) wordt eenmaal
  geconsumeerd en uitsluitend na een software-reset geaccepteerd. Koude boot,
  brownout, watchdog en ongeldige records openen geen fysieke capability.
- De recoveryguard registreert op `WIFI + 3`; web-auth initialiseert zijn
  onveranderlijke credentialbuffers op `WIFI + 2`. Beide gebeuren vóór captive
  portal, Wi-Fi en Ethernet de listener kunnen starten. Een late `on_boot`
  markeert de runtime gereed vóór normale HTTP-routes en resetacties de
  volledig geïnitialiseerde componenten/native API-preferences gebruiken.
- `/recovery` laadt geen SPA, entities, logs of SSE. Een recoveryboot blijft de
  hele boot beperkt; eindigen, de tien minuten termijn en een nieuwe login leiden
  tot een gecontroleerde reboot. Er wordt geen normale login live teruggezet.
- POST vereist dezelfde HTTP-origin, de actieve generatie en een willekeurige
  CSRF-token. Tijdens het fysieke venster kunnen andere apparaten op het lokale
  netwerk de beperkte herstelacties gebruiken. Open normale webtoegang is geen
  beheerautorisatie. Normale beheerders kunnen expliciet API/Wi-Fi resetten;
  deze routes geven na reboot geen fysieke capability.
- HTTP kopieert alleen begrensde actievelden onder de componentmutex en laat de
  lock vóór socketwrites los. Preferences worden uitsluitend in de main loop
  geschreven. Save, sync en loginreadback moeten slagen. `202` bevestigt alleen
  de aanvraag; de frontend meldt geen opslagbewijs op basis van disconnect.
- De actieve web-login blijft dezelfde tot CPU-reset, ook bij normale wijzigingen
  of uitschakelen van login. De volledige lokale `web_server` en `web_server_base`
  kopieën zijn verwijderd. Firmwarepublicatie gebruikt een eigen metadata-endpoint.

## Reset en lopend werk

Na minstens 500 ms en vóór persistence/reboot wordt HTTPD met `queue_work` gedraind.
Daarna passeert een complete scheduler/main-loopronde. Een eerder geselecteerd
request of web-OTA-upload kan hierdoor geen late bodycallback over de resetgrens
heen uitvoeren. Geslaagde OTA heeft voorrang en laat geen recoverymarker achter;
een onderbroken webupload mag na de bewezen barrière veilig opnieuw opstarten.

ODU-settings, runtime-frequency en defrost registreren begrensde restartblockers.
Nieuwe handmatige acties worden bij sluiten geweigerd; voorbereide acties worden
vóór persistence/firstwrite ingetrokken. Begonnen schrijfreeksen/readback doorlopen
hun bestaande afhandeling of timeout. Bestaande autonome profielen blijven tijdens
recovery werken; bij een aangevraagde reboot starten ze geen nieuwe reconciliatie.

`POST /api-security/reset` vereist `confirm=RESET_API_SECURITY`; Wi-Fi reset
vereist `confirm=RESET_WIFI`. De native API-key wordt na setup gecontroleerd gewist.
Alle API-clients worden vóór teardown fatal gemarkeerd. Tussen clear en de synchrone
incidentmanager-reboot zit geen schedulerpass. Home Assistant kan binnen tien
minuten opnieuw provisionen en kan daarbij dezelfde sleutel instellen.

Een opslagfout kan flash al gedeeltelijk hebben veranderd. De huidige boot blijft
beperkt; geen succes, automatische retry of timeoutreboot. Een gebruiker kan een
expliciete resetretry doen. Recovery houdt bij een fout zijn herstelacties beschikbaar.

## Wi-Fi en Improv Serial

De AP blijft zonder opgeslagen STA beschikbaar totdat nieuwe gegevens daadwerkelijk
werken én opgeslagen zijn, onafhankelijk van de API-keytimer en Ethernetvoorkeur.
Captive portal en Improv Serial gebruiken dezelfde staged provisioning. Voor
vervanging van bestaande credentials moet de oude STA eerst aantoonbaar gestopt zijn;
een bestaand SSID alleen is geen bewijs dat het nieuwe wachtwoord werkt. Een checked
clear blokkeert nieuwe credentialwrites voor de rest van de boot, ook bij partiële fout.

De tijdelijke `wifi`, `captive_portal` en `improv_serial` componenten blijven nodig.
Zie hun README's voor verschillen met upstream en voorwaarden voor verwijdering.

## Validatie

`python3 -m unittest scripts.tests.test_web_auth_middleware` compileert productiecode
tegen adapters en injecteert opslag-, autorisatie-, HTTPD- en OTA-interleavings.
`bash scripts/run_host_regression_tests.sh` test knopdrempels, stuck-at-boot,
deadlines, millis-wrap en exclusieve acties. Firmwaretarget is
`configs/heatpump_controller_q/duo_wifi.yaml`.

Vóór vrijgave blijven HIL-tests voor radio/USB, powercuts, sockets/OTA en de interne
heap/stackmarges nodig. De bestaande NVS-budgetfout moet afzonderlijk opgelost worden;
een geslaagde compile bewijst deze grenzen niet.
