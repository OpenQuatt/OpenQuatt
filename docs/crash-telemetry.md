# Crashtelemetrie

Wanneer gebruiksstatistieken zijn ingeschakeld, bewaart OpenQuatt na een echte
ESP32/ESPHome-crash één begrensd technisch crashrapport. Dit rapport wordt na de
herstart met QoS 1 en zonder retain gepubliceerd op:

```text
<usage_telemetry_topic>/<installation-id>/crash
```

Na een MQTT PUBACK wordt de lokale crashkopie gewist. Tot dat moment blijft het
begrensde record in flash beschikbaar voor een retry. MQTT QoS 1 kan dezelfde
crash opnieuw afleveren wanneer een acknowledgement verloren gaat. Een ontvanger
moet daarom dedupliceren op de combinatie van `installation_id` en `message_id`.

De MQTT-client voor crashpublicatie wordt door een geïsoleerde worker beheerd.
De normale ESPHome-hoofdloop bouwt alleen de begrensde payload op en verwerkt
het resultaat. Een trage of vastlopende MQTT-start of -cleanup mag daardoor de
verwarmingsregeling of controllerhoofdloop niet blokkeren. Het lokale
crashrecord wordt pas gewist nadat de PUBACK is ontvangen én de client volledig
is opgeruimd; blijft de cleanup hangen, dan blijft het record behouden voor een
retry onder hetzelfde `message_id`.

De payload bevat geen gewone runtime-logs, metingen of regelwaarden. Wel bevat
hij de regels uit het ESPHome-crashrapport, het resettype, firmwareversie,
releasekanaal, ESPHome-versie, bronrepository, volledige commit-SHA, exact
buildtarget, release-manifest-URL indien van toepassing, hardwareprofiel,
topologie, verbinding, buildtijd en de volledige ELF-SHA256 van de firmware die
het rapport verstuurt.

De tijdvelden hebben bewust verschillende betekenissen:

- `crash_timestamp` is de laatste geldige UTC Unix-tijd die vóór de reset in een
  RTC-breadcrumb is vastgelegd. Deze wordt normaal iedere 15 seconden vernieuwd,
  maar kan bij een vastgelopen controllerloop ouder zijn. Het veld is `null`
  wanneer geen geldige breadcrumb beschikbaar is.
- `crash_uptime_s` is de uptime die bij dezelfde breadcrumb hoorde en is eveneens
  `null` wanneer de breadcrumb ontbreekt.
- `reported_at` is de geldige UTC Unix-tijd waarop de MQTT-payload na de herstart
  is opgebouwd. OpenQuatt wacht hiervoor maximaal 60 seconden op een tijdsync
  tijdens de huidige boot. Het veld is `null` zonder zo'n sync of wanneer de
  gesynchroniseerde tijd vóór `crash_timestamp` ligt.
- `reporting_build_epoch` is uitsluitend de compileertijd van de rapporterende
  firmware en mag niet als crash- of ontvangsttijd worden gebruikt.

Een server-`received_at` kan bij retries of een opnieuw afgeleverde MQTT-message
veranderen en is daarom evenmin het crashmoment.

ESPHome geeft in zijn replay aan wanneer de adressen bij een andere firmwarebuild
horen. In dat geval staat `captured_by_reporting_build` op `false` en mogen de
adressen niet tegen het huidige of een opnieuw gebouwd ELF worden gesymboliseerd.

Voor normale crashes na een herstart in dezelfde firmware kan een kandidaat-ELF
opnieuw worden gebouwd vanuit de opgenomen bronrepository, commit en het exacte
target, met de opgenomen ESPHome-versie en buildtijd als aanvullende invoer.
Gebruik dit ELF uitsluitend wanneer zijn SHA256 exact gelijk is aan
`reporting_build_id`.

Voor Heatpump Controller Q-builds bewaart GitHub Actions de exacte symbolen uit
dezelfde build als de firmware. Getagde releases krijgen 90 dagen één artifact
`openquatt-q-debug-symbols-<tag>`. Dev-builds krijgen 7 dagen een uniek artifact
`openquatt-q-debug-symbols-<dev-versie>`, zodat oudere symbolen beschikbaar
blijven wanneer `dev-latest` naar een nieuwere build verschuift.

Deze symbolen zijn geen GitHub Release-assets. Ieder artifact bevat per Q-target
de exacte `firmware.elf`, `openquatt.map` en een `index.json`. Het indexbestand
koppelt de bestanden via de ELF-SHA256 rechtstreeks aan `reporting_build_id`,
plus het buildtarget, de broncommit en de gebruikte ESPHome-versie. Andere
hardwareprofielen krijgen geen debug-symbolenartifact.

Bij opt-out bewaart OpenQuatt alleen een kleine pending-tombstone status en
publiceert het een lege retained payload zodra de broker bereikbaar is. Deze
tombstone verwijdert ook crashwaarden die door oudere firmware retained zijn
gepubliceerd; gewone crashberichten zijn niet retained. Alleen een firmware-
upgrade verstuurt geen opruimtombstone; bestaande retained waarden worden
server-side afgehandeld.

## Bewuste begrenzingen van deze eerste versie

- Er wordt lokaal één crash bewaard; een nieuwere crash vervangt een nog niet
  gepubliceerde oudere crash.
- Een pending tombstone gebruikt de op dat moment geconfigureerde broker en
  topicbasis. Migratie over meerdere oude endpoints valt buiten deze versie.
- Voor Q-releasebuilds zijn de exacte symbolen 90 dagen beschikbaar en voor
  Q-dev-builds 7 dagen; daarna blijft reconstructie afhankelijk van de opgenomen
  buildmetadata.
- Wanneer een opnieuw gebouwd ELF niet exact dezelfde SHA256 heeft, blijven de
  adressen ruwe diagnose-informatie en worden ze niet gesymboliseerd.
