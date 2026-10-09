# Diagnose en logboeken

## Welk hulpmiddel kies je?

| Vraag | Hulpmiddel |
| --- | --- |
| Welke waarden veranderen tijdens het probleem? | Diagnose: actuele waarden en grafieken |
| Waarom schakelt of begrenst de regeling? | Beslislog: regelkeuzes en redenen |
| Welke technische meldingen verschijnen? | Logboek: live meldingen |
| Welke gegevens moet ik voor support bewaren? | Systeemrecorder: downloadbaar diagnosebestand |

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


## Systeemrecorder voor support

De Systeemrecorder bewaart continu recente systeemgegevens, dus je hoeft een opname niet vooraf te starten:

1. Open **Diagnostiek → Systeemrecorder**.
2. Kies het venster dat het probleem afdekt: laatste 15, 30 of 60 minuten, 2 of 6 uur, of alles wat beschikbaar is.
3. Download het diagnosebestand.
4. Voeg het gedownloade `.oqdebug.json`-bestand toe aan je Discord-vraag of GitHub-issue. Via **Open analyser** kun je het bestand zelf alvast bekijken op OpenHeatPumps; er wordt niets automatisch verzonden.

De recorder gebruikt een ringbuffer van 2 MiB in PSRAM en neemt elke 10 seconden een sample. Met de volledige standaardveldenset past er ongeveer 7 uur en 19 minuten historie in; de beschikbare duur staat in de popup. De recorder blijft doorlopen en vervangt de oudste samples zodra de buffer vol is. Uitschakelen bewaart de huidige historie nog tot een herstart van het apparaat; **Nieuwe opname** wist de historie. Na een apparaatherstart begint de historie opnieuw.

De opname wordt lokaal in het apparaatgeheugen opgeslagen en niets wordt automatisch verzonden. Deel het bestand alleen binnen het supportverzoek waarvoor je het hebt gemaakt.

Bij **Langer doorverwarmen** bevat de opname de aan/uitstand, de stopgrens boven de gewenste temperatuur en de afkoeling voor een warme herstart. Deze instellingen staan samen met gekozen temperatuurbronnen en compressorbegrenzingen in een configuratiesnapshot. Het bestand bewaart de startsituatie en wijzigingen die bij volgende samples worden waargenomen; de sampleperiode is 10 seconden.

De cyclusstatus wordt afzonderlijk opgenomen, bijvoorbeeld doorverwarmen, comfortstop of wachten op warme herstart. Ook het basisvermogen, de doorverwarmvloer, de herstelcorrectie en de laatst berekende stop- en herstarttemperatuur worden bewaard. De ingestelde afkoeling is een temperatuurverschil in °C; de effectieve herstartgrens houdt ook rekening met de comfortondergrens. De regelstatus en berekende grenzen beschrijven de laatst gepubliceerde evaluatie en kunnen kort achterlopen op een zojuist gewijzigde instelling.

Niet-beschikbare instellingen worden expliciet als ontbrekend vastgelegd. De configuratiesnapshot bevat geen wachtwoorden, tokens of live API-ingangwaarden. Dit diagnosebestand is bedoeld om het gedrag te verklaren; het wijzigen of herstellen van instellingen gebeurt via de normale bediening.

Vanuit de OpenHeatPumps-analyser kun je met een deep link terug naar de Systeemrecorder-popup: `http://<device-ip>/?view=settings&section=system&modal=systeemrecorder` (kort: `http://<device-ip>/#systeemrecorder`). De popup opent dan automatisch.

## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
