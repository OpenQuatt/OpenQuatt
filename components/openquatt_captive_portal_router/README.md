# OpenQuatt captive portal router

Eigen handler, vóór CaptivePortal setup geregistreerd. `/` delegeert aan de
upstream portal; GET `/wifisave` en `/wifi/provisioning/status` blijven bereikbaar
via AP-provisioning. De portal publiceert zijn actieve policy atomisch vóór
listenerstart en trekt deze vóór teardown in. HTTPD leest geen native mutable
portalstatus.

Een save-request valideert same-originheaders indien aanwezig, rejects lege/te
lange credentials en NUL (ook de upstream `%00`-truncatie), en kopieert uitsluitend
naar één begrensde mutexqueue (SSID 33, password 65 bytes). HTTPD voert geen NVS-
of radiowerk uit en houdt de mutex niet vast tijdens responseverzending.
`202 Connecting. Request N accepted` betekent uitsluitend aangenomen.

De main loop start de gedeelde Wi-Fi-provisioninggeneration. De JSON-status bevat
`generation`, `state` (`QUEUED`, `CONNECTING`, `SAVED`, `STORAGE_FAILED`,
`CONNECT_FAILED`, `CANCELLED`) en `saved`; alleen checked `SAVED` geeft true.
Een tweede HTTP-job krijgt 409 zolang de eerste loopt; Improv kan de Wi-Fi-
generation superseden en de oude portaljob wordt dan CANCELLED. Na 90 s annuleert
de router uitsluitend zijn eigen generation. Verdwijnen van de AP vóór execution
annuleert de queue, zonder radio-/opslagactie. Geen eigen credentialstore.

HIL blijft nodig voor de echte AP/DNS/listener/driverovergangen, power cuts en
HTTP-taskgeheugenmarges. Hosttests voeren de echte handler/queue/loop uit met de
echte Wi-Fi-opslagmethoden, incl concurrente HTTP-requests en partial writes.
