# Diagnose en logboeken

## Diagnose

`Diagnose` combineert actuele waarden met korte historie. Dat helpt bij vragen zoals:

- loopt de aanvoertemperatuur rustig op;
- blijft de flow stabiel;
- schakelt het systeem vaak;
- reageert de regeling logisch op setpoint en kamertemperatuur.

Gebruik bij een probleem dat je opnieuw kunt veroorzaken ook het **Logboek**. Nieuwe regels verschijnen daar live; valt de verbinding kort weg, dan vult OpenQuatt de gemiste recente regels weer aan. Het logboek is vluchtige diagnose-informatie: bewaar voor support daarnaast altijd een Systeemrecorder-diagnosebestand.

Via `Instellingen → Systeem → Gegevens bewaren` beheer je welke historie OpenQuatt bewaart. OpenQuatt maakt daarbij onderscheid tussen twee soorten geheugen:

- **PSRAM (tijdelijk, vluchtig)** — snelle opslag voor recente diagnosegegevens en RAM-logs. Deze historie is direct beschikbaar zolang de controller online is en verdwijnt na een herstart.
- **Flash-partitie `openquatt_data` (persistent)** — blijft normaal bewaard na een herstart of update. Hier staan energie-dagtotalen (standaard aan, 180 dagen uurdetail), beslislog (standaard aan, maximaal 7 dagen, per uur gebundeld naar flash) en diagnosehistorie (standaard aan, maximaal 30 dagen). Bij de overstap van diagnosehistorie-formaat v1 naar v2 wordt alleen de oude diagnosehistorie eenmalig gewist; de andere archieven blijven staan.

Tijdelijke PSRAM-historie is op alle ondersteunde profielen standaard aan en wordt niet als aparte keuze in Quick Start getoond; ontbrekende PSRAM wijst op een hardware- of profielprobleem. Persistente flash-historie kun je per domein (Diagnose / Beslislog / Energie) aan of uit zetten onder Gegevens bewaren. Zet je een flash-optie uit, dan blijft bestaande flashhistorie gewoon staan — OpenQuatt stopt alleen met nieuw wegschrijven. Met `Nu opslaan` kun je vóór een herstart of update alvast een extra opslagmoment forceren. De technische opslagdetails tonen voor diagnosehistorie ook de langste volledige opslagactie, sector-erase, flashwrite en index-update sinds de laatste start.

## Beslislog

`Beslislog` laat zien welke regelkeuze OpenQuatt maakte en welke signalen daarbij meespeelden. Gebruik dit scherm vooral om een onverwachte omschakeling of begrenzing te verklaren. De functie is nog beta; combineer de uitleg daarom met de actuele waarden in `Diagnose`.


## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
