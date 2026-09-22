# Beperkte fysieke recovery

Fase 2 van #421, op ESPHome 2026.9.0. Houd de gedebouncete herstelknop 5 seconden
ingedrukt en laat hem los. `/recovery` is een zelfstandige pagina zonder SPA,
entities of logs. Het venster duurt 10 minuten; alleen daar kan een nieuwe
web-login worden opgeslagen. Normale routes blijven met een willekeurige
RAM-only login afgeschermd. Einde/timeout herstelt de laatst succesvol opgeslagen
login (of de oorspronkelijke open modus); openen wijzigt geen credentials in NVS.

## Eigenaarschap en grenzen

- De main loop bezit knop/timing en voert de ene gereserveerde actie uit.
  HTTP kopieert alleen begrensde actievelden onder de componentmutex.
- Activering loopt via één HTTPD-work callback: eerdere handlers voltooien eerst,
  daarna gaat de guard aan en sluiten bestaande sockets op diezelfde HTTPD-task.
  Er wordt geen losse fd naar later werk doorgestuurd.
- Pas na een main-loopbarrière wordt de fysieke capability zichtbaar. Uitgestelde
  webserver-acties controleren recovery en de generatie opnieuw vóór uitvoering.
- POST vereist exact dezelfde HTTP-origin, de actieve generatie en een willekeurige
  CSRF-token. Een open normale webinterface is geen beheerautorisatie.
- Tijdens het fysieke venster kan iedereen op het lokale netwerk de beperkte
  herstelacties gebruiken. Dit is geen per-persoon autorisatie.
- De lockvolgorde is recovery → web-auth → credentials; vóór socketwrites gaat de
  recoverylock los. Geen automatische retries of tweede credentialopslag.
- Een RTC-record (magic, versie, generatie, CRC) wordt eenmaal geconsumeerd en
  uitsluitend na een software-reset geaccepteerd. Fase 3/4 gebruiken dit pas na
  een geslaagde resetactie; een koude start opent nooit automatisch recovery.
- Bij een opslagfout verandert de runtime-login niet. Een mislukte NVS-sync
  bewijst niet dat flash ongewijzigd bleef; daarom wordt nooit succes gemeld of
  automatisch herstart na die fout.

## Fase 3: API-beveiliging resetten

`POST /api-security/reset` accepteert een actieve fysieke capability óf een
geauthenticeerde web-beheerder met web-CSRF, altijd met dezelfde origin en
`confirm=RESET_API_SECURITY`. Open webtoegang is geen beheerautorisatie.

Na `202 Accepted` wacht de main loop 500 ms, wist de native ESPHome Noise-PSK
met gecontroleerde save/sync en herstart veilig. Vóór `safe_reboot()` worden
API-clients voor verwijdering gemarkeerd: API-teardown verwerkt anders nog
set-key-pakketten. Tussen clear, markeren en reboot wordt niet naar de scheduler
teruggekeerd. Alleen een fysieke actie schrijft een RTC-handoff; een adminreset
opent na reboot géén fysieke capability. Bij opslagfout geen reboot of autoretry.

De oude OpenQuatt-key-store wordt sinds de migratie naar native provisioning
niet meer gelezen; geen migratie/tombstone nodig. Native ESPHome is de enige
sleuteleigenaar. De sleutel wordt nergens via HTTP teruggegeven. Home Assistant
krijgt na reboot 10 minuten om opnieuw te provisionen en kan dezelfde sleutel
opnieuw instellen. Alle API-clients worden geraakt.

Wi-Fi-reset komt in fase 4. De 10s-drempel is nog niet aangesloten.

## Validatie

`python3 -m unittest scripts.tests.test_web_auth_middleware` compileert ook de
echte recoverycomponent tegen kleine adapters en injecteert opslag-/autorisatiefouten.
`bash scripts/run_host_regression_tests.sh` test onder andere knopdrempels,
stuck-at-boot, deadline, millis-wrap en exclusieve acties.

Firmware: `esphome compile configs/heatpump_controller_q/duo_wifi.yaml`.
Hardware-/netwerk-interleavings en interne heap/stackmarges moeten vóór merge
nog op de HIL worden gemeten; een geslaagde compile bewijst die niet.
