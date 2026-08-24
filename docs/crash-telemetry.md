# Retained crashtelemetrie

Wanneer gebruiksstatistieken zijn ingeschakeld, bewaart OpenQuatt na een echte
ESP32/ESPHome-crash één begrensd technisch crashrapport. Dit rapport wordt na de
herstart met QoS 1 en retain gepubliceerd op:

```text
<usage_telemetry_topic>/<installation-id>/crash
```

Het topic representeert bewust alleen de laatste crash. Een volgende crash
vervangt de vorige retained waarde. Er is geen crashqueue.

De payload bevat geen gewone runtime-logs, metingen of regelwaarden. Wel bevat
hij de regels uit het ESPHome-crashrapport, het resettype, firmwareversie,
releasekanaal, ESPHome-versie, bronrepository, volledige commit-SHA, exact
buildtarget, release-manifest-URL indien van toepassing, hardwareprofiel,
topologie, verbinding, buildtijd en de volledige ELF-SHA256 van de firmware die
het rapport verstuurt.

ESPHome geeft in zijn replay aan wanneer de adressen bij een andere firmwarebuild
horen. In dat geval staat `captured_by_reporting_build` op `false` en mogen de
adressen niet tegen het huidige of een opnieuw gebouwd ELF worden gesymboliseerd.

Voor normale crashes na een herstart in dezelfde firmware kan een kandidaat-ELF
opnieuw worden gebouwd vanuit de opgenomen bronrepository, commit en het exacte
target, met de opgenomen ESPHome-versie en buildtijd als aanvullende invoer.
Gebruik dit ELF uitsluitend wanneer zijn SHA256 exact gelijk is aan
`reporting_build_id`. De firmware bewaart of publiceert niet standaard bij
iedere build een ELF-bestand.

Na een MQTT PUBACK wordt de lokale crashkopie gewist. Tot dat moment blijft het
begrensde record in flash beschikbaar voor een retry. Bij opt-out bewaart
OpenQuatt alleen een kleine pending-tombstone status en publiceert het een lege
retained payload zodra de broker bereikbaar is.

## Bewuste begrenzingen van deze eerste versie

- Er wordt één crash bewaard; een nieuwere crash vervangt een nog niet
  verwerkte oudere crash.
- Een pending tombstone gebruikt de op dat moment geconfigureerde broker en
  topicbasis. Migratie over meerdere oude endpoints valt buiten deze versie.
- De opgenomen buildvelden maken een gerichte rebuild en SHA-controle mogelijk,
  maar vormen geen garantie dat iedere oude toolchain later nog byte-identiek
  beschikbaar is.
- Wanneer een opnieuw gebouwd ELF niet exact dezelfde SHA256 heeft, blijven de
  adressen ruwe diagnose-informatie en worden ze niet gesymboliseerd.
