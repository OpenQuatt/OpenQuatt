# Koelen

Bij watergedragen koeling moet je condens voorkomen. OpenQuatt bewaakt daarvoor onder meer dauwpunt, watertemperatuur en waterdoorstroming. Toestemming om te koelen betekent daarom niet dat de compressor meteen start.

## Koelen binnen een dagelijks tijdvenster

### Koelen aanzetten

1. Controleer dat je installatie geschikt is voor koeling en dat de gekozen dauwpuntbron geldige gegevens levert. Zie [Bronnen en integraties](../web-app/bronnen.md).
2. Open **Instellingen → Koelen → Dagelijks koelvenster**.
3. Zet het venster aan en kies een start- en eindtijd, bijvoorbeeld 08:00–20:00. Sla ieder tijdveld op door het te verlaten of op Enter te drukken.
4. Controleer op **Overzicht** de koeltoestemming, kamertemperatuur en gewenste kamertemperatuur.

Standaard begint koeling alleen als de kamer ook daadwerkelijk om koeling vraagt. De veiligheidsbewaking blijft altijd gelden. Gebruik je een externe bron voor koeltoestemming, kies die dan bewust bij de [koelinstellingen](../web-app/instellingen.md#koelen).

## Wat kun je verwachten?

Binnen het tijdvenster mag OpenQuatt koelen wanneer er koelvraag is en de voorwaarden kloppen. Buiten het venster trekt het de toestemming in. De compressor kan nog kort doorlopen om zijn minimale looptijd af te maken; daarna kan de circulatiepomp nog draaien.

Een venster mag over middernacht lopen. Gelijke start- en eindtijden betekenen dat het venster uitstaat. Na een herstart wacht het schema op geldige netwerktijd.

### Zo controleer je dat het gelukt is

Controleer dat je tijden na opnieuw openen bewaard zijn en dat **Overzicht** de verwachte koeltoestemming toont. Kijk daarnaast naar de status en reden: een geldige toestemming hoeft zonder koelvraag niet tot koeling te leiden.

## Waarom start koelen niet?

| Wat zie je? | Wat controleer je? |
| --- | --- |
| Geen koeltoestemming | Staat het venster aan, valt de huidige tijd erin en is de controllerklok geldig? |
| Wel toestemming, geen koelvraag | Is de kamer warmer dan de gewenste temperatuur? Rond het setpoint voorkomt een kleine marge steeds aan/uit schakelen. |
| Koeling geblokkeerd | Is de dauwpuntbron geldig, is de waterdoorstroming bruikbaar en is het water niet al te koud? Lees de getoonde reden. |
| Koeling is net gestopt | De compressor kan een wachttijd hebben voordat hij opnieuw mag starten. |

Blijft de oorzaak onduidelijk, volg [Problemen oplossen](../problemen-oplossen.md) en [Diagnose en logboeken](../web-app/diagnose.md).

## Wat doet `Manual Cooling Enable`?

Handmatige koeltoestemming kan ook buiten het gekozen venster gelden. Zij schakelt de veiligheidsbewaking niet uit en vereist standaard nog steeds koelvraag van de kamer. Deze keuze verloopt niet vanzelf en kan na een herstart blijven aanstaan; zet haar zelf weer uit wanneer je klaar bent.

## Verder lezen

- [Instellingen aanpassen](../web-app/instellingen.md#koelen) voor bediening en bronkeuze.
- [Koelen: technische werking](../koelen-technisch.md) voor exacte voorwaarden, wachttijden en bronbewaking.

## Waarom is dauwpunt zo belangrijk?

Dauwpunt is de temperatuur waaronder vocht uit de lucht kan condenseren. Een geldige dauwpuntbron helpt OpenQuatt voorkomen dat het koelwater te koud wordt. Zie [Dauwpuntbeveiliging](../web-app/instellingen.md#dauwpuntbeveiliging).
