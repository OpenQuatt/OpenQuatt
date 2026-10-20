# Energie en resultaten

Gebruik **Energie** om actuele prestaties te bekijken en **Resultaten** om perioden te vergelijken.

| Waarde | Betekenis |
| --- | --- |
| Elektrisch vermogen (W) | Hoeveel elektriciteit de warmtepomp op dit moment gebruikt |
| Thermisch vermogen (W) | Hoeveel warmte de warmtepomp op dit moment levert |
| Energie (kWh) | Verbruik of geleverde warmte over een periode |
| COP | Verhouding tussen geleverde warmte en elektrisch verbruik bij verwarmen |
| EER | Vergelijkbare verhouding bij koelen |

De actuele COP toont `—` in CM1 (voorloop/naloop), tijdens opstart en ontdooien, of als bedrijfstelemetrie ontbreekt of verouderd is. Buiten CM1 verschijnt COP zodra de compressor bij verwarmen draait en de gemeten warmteafgifte positief is. Negatief thermisch vermogen blijft zichtbaar en telt mee in de netto energiebalans.

Vergelijk dezelfde periode en let op verschillen in buitentemperatuur en gebruik. Eén kort moment vertelt weinig over de prestaties van een hele dag.

## Resultaten

`Resultaten` bundelt opgeslagen resultaten en historie. Gebruik dit scherm om prestaties over een langere periode te vergelijken. Voor een snelle diagnose van het actuele regelgedrag is `Diagnose` geschikter.

COP is geleverde warmte gedeeld door **elektrisch verwarmen**; EER is geleverde koeling gedeeld door **elektrisch koelen**. Beide verbruikstellers bevatten ook pomp, voorloop/naloop en standby. Standby volgt de laatst gebruikte verwarmings- of koelmodus, ook na een herstart. Een nieuwe installatie begint bij verwarmen totdat de eerste koelsessie start.

Handmatig verwarmen of koelen in CM100 bepaalt hetzelfde energiedoel; service zonder gekozen thermische modus behoudt het vorige doel.

De bestaande ESPHome-tellers voor totaal, verwarmen en koelen blijven behouden. De nieuwe toerekening geldt vanaf de update; opgeslagen tellerstanden worden niet aangepast. Dag- en cumulatieve COP/EER kunnen daardoor bij bestaande metingen nog afwijken van de herberekende Resultaten. In de grafiektooltip staan alleen elektrisch verwarmen en elektrisch koelen.

Voor COP in Resultaten wordt elektrisch verwarmen berekend als totaal min koelen, ook voor opgeslagen historie. Hiermee telt eerder niet-toegerekende pomp/standby mee zonder de opgeslagen gegevens te wijzigen. Achteraf is niet betrouwbaar te bepalen welk deel daarvan bij koelen hoorde.

## Energie

`Energie` geeft inzicht in vermogen en rendement. Gebruik dit vooral om richting te krijgen, niet als gecertificeerde energiemeter.

Voorbeelden:

- elektrisch vermogen van de warmtepomp;
- thermisch vermogen;
- COP bij verwarmen;
- EER bij koelen, als koeling actief en ondersteund is;
- dag- en totaalwaarden wanneer die entiteiten beschikbaar zijn.

### Elektrisch vermogen

Het elektrische vermogen is een schatting op basis van spanning, stroom en
ventilatorsnelheid. De circulatiepomp telt mee via de pompfeedback wanneer het
pomprelais actief is. Ingeschakelde verwarmingen voegen per buitenunit het
volgende toe:

| Buitenunit | Bodemplaat-/afvoerverwarming | Carterverwarming |
| --- | ---: | ---: |
| V1 / V1.5 | 150,06 W | 40 W |
| V2, oud en nieuw model | 280 W (2 × 140 W) | 33,62 W |

V2 heeft twee parallel geschakelde heaters achter hetzelfde relais. De
aanduiding `Bottom plate heater` geldt daar voor beide elementen samen.
Zie [Bodemplaatverwarming](../bodemplaatverwarming.md) voor de aansturing.

Oudere OpenQuatt-versies telden bij V2 slechts 140 W voor deze heatergroep.
De correctie verhoogt het geschatte verbruik met 140 W per buitenunit zolang
de heaters actief zijn; bij dezelfde warmteafgifte wordt de COP daardoor lager.
De extra 140 W wordt niet achteraf aan opgeslagen energie toegevoegd.

## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
