# Ontwikkelaarsdocumentatie

Deze documenten beschrijven onderhoud, architectuur, validatie en releasewerk voor
bijdragers. Ze zijn bewust geen onderdeel van de gebruikerssite.

- [Ontwikkelen op Mac](ontwikkelen-op-mac.md): lokale setup en validatie.
- [Testomgeving](../../tests/README.md): testindeling, gedeelde runner, vereisten en sanitizerchecks.
- [System overview](system-overview.md): runtime-architectuur en subsystemownership.
- [Verwarmingsregeling](verwarmingsregeling.md): Power House, stooklijn, dispatch en de start-/stopketen.
- [Koelregeling](koelregeling.md): koelvraag, watermarge, limiter, HP-keuze en herstart.
- [Heating strategy development](heating-strategy-development.md): strategy-grenzen en uitbreiding.
- [Heating strategy template](heating-strategy-template.yaml): kopieerbaar YAML-startpunt.
- [Beslislog: contract en vragenmatrix](control-replay-firmware.md): eventcontract tussen firmware en web-app.
- [Hardware-in-the-loop-tests](hil-testing.md): veiligheidscontract en HIL-runs.
- [Defrostprofielen testen](hil-defrost-profiles.md): herbruikbaar HIL-testplan.
- [NVS capaciteit en persistentie](nvs-persistence.md): opslagbudget, migratie en herstel.
- [Netwerkdiagnostiek](netwerkdiagnostiek.md): DNS-herstel en socketbegroting.
- [Power House Auto-Tuning](power-house-autotuning-development.md): implementatie en Adaptive-roadmap.
- [Power House-learningbronnen](power-house-learning-sources.md): meetbroncontract.
- [Release process](releaseproces.md): branches, builds en releases.
