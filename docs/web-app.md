# Overzicht van de web-app

De OpenQuatt web-app is de lokale bedienings- en instellingenpagina van je OpenQuatt-module. Zodra de controller online is, is dit de eerste plek waar je naartoe gaat: open `http://openquatt.local` en rond Quick Start af. Home Assistant komt eventueel daarna.


## Openen

Probeer eerst:

```text
http://openquatt.local
```

Lukt dat niet, zoek dan het IP-adres van OpenQuatt in je router of in Home Assistant en open:

```text
http://<ip-adres>
```

De web-app draait lokaal op je eigen netwerk. Je gebruikt dus geen cloudaccount en hoeft niets externs open te zetten.

Wil je de interface eerst rustig bekijken zonder echte hardware, open dan de [web-app demo op GitHub Pages](https://openquatt.github.io/OpenQuatt/demo/). Die gebruikt dezelfde look-and-feel in mockmodus.

## Taal kiezen

De web-app is volledig beschikbaar in het Nederlands en Engels. Open rechtsboven het paneel **Weergave en systeem** en kies onder **Taal / Language** voor `Nederlands` of `English`. De keuze wordt lokaal in de browser bewaard en blijft na herladen en een controllerherstart actief. Zonder opgeslagen keuze blijft Nederlands de standaardtaal.

De taalkeuze vertaalt de interface, meldingen, statussen en datum-/getalopmaak. Technische firmwarewaarden, entitynamen en API-/MQTT-waarden blijven ongewijzigd zodat koppelingen en backupbestanden compatibel blijven.

## Hoofdschermen

De web-app heeft zes hoofdschermen.

| Scherm | Gebruik |
|---|---|
| `Overzicht` | Live zien wat OpenQuatt nu doet en of de belangrijkste waarden logisch zijn. |
| `Energie` | Vermogen, energie, COP en EER bekijken. |
| `Resultaten` | Opgeslagen energie- en resultaathistorie over een langere periode bekijken. |
| `Beslislog` | Terugzien welke regelbeslissingen OpenQuatt nam en waarom. Deze functie is nog beta. |
| `Diagnose` | Live waarden en korte trendhistorie naast elkaar bekijken om gedrag te onderzoeken. |
| `Instellingen` | OpenQuatt configureren, bijwerken en beheren. |

Voor dagelijks kijken is `Overzicht` meestal genoeg. Ga pas naar `Instellingen` als je bewust iets wilt veranderen.

## Warmtecapaciteit op het overzicht

**Geschatte beschikbare warmtecapaciteit** toont bij Power House én
Stooklijnregeling het geschatte maximale thermische vermogen van de beschikbare
warmtepomp(en). De berekening gebruikt de actuele buiten- en aanvoertemperatuur,
het bevestigde warmtepompmodel, de frequentiegrenzen en de elektrische piekgrens.
Een geblokkeerde warmtepomp telt niet mee; tijdens ontdooien geldt een
modelcorrectie. De waarde kan daardoor veranderen zonder dat de warmtevraag verandert.

Dit is een modelschatting, geen meting van het geleverde vermogen of garantie op
het startmoment. Vraagregeling, waterbeveiligingen en startvoorwaarden blijven
bepalen wat werkelijk wordt geleverd. Ontbreken benodigde model-, frequentie-
of temperatuurgegevens, dan staat er **—**. **0 W** betekent dat de berekening
met geldige gegevens geen beschikbare capaciteit vindt. De sensornaam
`HP capacity (W)` blijft gelijk; bij koelen is deze verwarmingsschatting niet beschikbaar.

## Wat wil je doen?

| Onderwerp | Handleiding |
| --- | --- |
| OpenQuatt voor het eerst configureren | [Eerste keer instellen](web-app/quick-start.md) |
| Comfort en installatie-instellingen aanpassen | [Instellingen aanpassen](web-app/instellingen.md) |
| Kiezen welke sensoren en externe waarden de regeling gebruikt | [Bronnen en integraties](web-app/bronnen.md) |
| Vermogen, rendement en historie bekijken | [Energie en resultaten](web-app/energie.md) |
| Onverwacht gedrag onderzoeken en gegevens bewaren | [Diagnose en logboeken](web-app/diagnose.md) |
| Firmware bijwerken of instellingen veiligstellen | [Updates en backups](web-app/onderhoud.md) |
| Kiezen welke gegevens je deelt | [Gegevens delen en privacy](web-app/privacy.md) |
| Wachtwoord, Home Assistant-koppeling of Wi-Fi herstellen | [Herstelpagina gebruiken](web-app/herstel.md) |

Voor normaal gebruik begin je bij [Dagelijkse controle](dagelijks/controleren.md). Voor uitleg van comfortkeuzes zie [Verwarmen en comfort](dagelijks/verwarmen.md) en [Koelen](dagelijks/koelen.md).

## Bij problemen

Ga naar [Problemen oplossen](problemen-oplossen.md). Voor een eerste Home Assistant-koppeling zie [OpenQuatt koppelen](dashboard/koppelen.md).
