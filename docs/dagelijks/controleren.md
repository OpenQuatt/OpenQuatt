# Dagelijkse controle

Gebruik het scherm **Overzicht** om kort te controleren of OpenQuatt doet wat je verwacht. Je hoeft daarvoor niet iedere dag instellingen aan te passen.

## Overzicht

Begin hier als je wilt weten of alles normaal oogt.

Let vooral op:

- OpenQuatt is online;
- Quatt-data wordt ververst;
- flow, aanvoertemperatuur, buitentemperatuur en kamertemperatuur zijn geloofwaardig;
- er is geen onverwachte override actief;
- de gekozen strategie past bij wat je in huis verwacht.

Zie je hier al vreemde waarden, ga dan niet meteen tunen. Controleer eerst de bronkeuze onder **Instellingen → Bronnen / integraties → Sensorselectie** en, als je Home Assistant gebruikt, de aangeleverde Home Assistant-bronnen.

## Wat is normaal?

De compressor hoeft niet voortdurend te draaien. OpenQuatt kan wachten op warmtevraag, water laten circuleren of een minimale uit-tijd respecteren. Bij Duo hoeven ook niet altijd beide buitenunits actief te zijn. Kijk naar de getoonde status en reden voordat je instellingen wijzigt.

## Wat doe je bij afwijkingen?

- Zijn bronwaarden niet logisch? Controleer [Bronnen en integraties](../web-app/bronnen.md).
- Is het te koud, te warm of schakelt het systeem onrustig? Begin bij [Problemen oplossen](../problemen-oplossen.md).
- Wil je begrijpen waarom de regeling een keuze maakt? Gebruik [Diagnose en logboeken](../web-app/diagnose.md).
- Wil je prestaties over een langere periode bekijken? Open [Energie en resultaten](../web-app/energie.md).

Verander pas instellingen als je weet welke afwijking je wilt oplossen. Zie [Verwarmen en comfort](verwarmen.md) en [Koelen](koelen.md) voor de dagelijkse keuzes.
