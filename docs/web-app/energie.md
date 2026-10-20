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


## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
