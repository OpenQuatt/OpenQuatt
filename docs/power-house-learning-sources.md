# Power House-learning: meetbroncontract

Power House learning is passief. De feature leest dezelfde waarden die de bestaande
regeling gebruikt, schrijft geen warmtepomp- of boilercommando en kan geen model
automatisch toepassen.

## Geselecteerde regelwaarden

Voor kamer, setpoint, buiten en flow gebruikt de learner de bestaande geselecteerde
waarde en de bestaande geldigheidsbeslissing. Er is geen tweede learning-specifieke
bronselectie of herkomstcontrole.

Dat geldt voor iedere beschikbare keuze, waaronder OpenTherm, CiC, Home Assistant,
API input en MQTT. Een waarde is voor learning bruikbaar wanneer de regelaar haar als
geldig markeert. De normale freshness- en aanwezigheidseisen blijven dus op één plek:
in de bronresolver van de regelaar.

Een gekozen waarde mag door die resolver ook een vastgehouden of samengestelde waarde
zijn. Dat maakt de bron niet apart ongeldig. De volgende stap bepaalt wel zelfstandig
of een vermogensmeting mogelijk is: nul of ontbrekend debiet levert bijvoorbeeld geen
bruikbare calorimetrie op.

## Fysieke meetketen

De water-in- en water-uitmetingen van iedere aanwezige warmtepomp, de
compressoractiviteit en beschermingsstatus blijven directe hardwaremetingen. Hun
ontvangststatus beschermt de calorimetrie tegen ontbrekende of ongeldige HP-data.

Het warmtevermogen gebruikt de ΔT per warmtepomp, net als de bestaande Heat Power-
sensoren. Een verschil tussen HP1-uit en HP2-in blokkeert leren niet. IJking van de
watertemperatuursensoren via het servicemenu kan de meetnauwkeurigheid verbeteren.

## Wanneer een record ontstaat

Een learning-record vereist:

- geldige geselecteerde waarden voor kamer, setpoint, buiten en flow;
- voldoende bruikbare water- en HP-statusmetingen voor de calorimetrie;
- verwarming zonder actieve begrenzing, service of OTA;
- geen actuele ketelwarmte tijdens CM2;
- een stabiel setpoint en passende comfortstatus voor de structurele batch-fit.

Een geldige bron is dus niet hetzelfde als een volledig learning-record. De eerste stap
accepteert de bestaande controlwaarde; de volgende stappen toetsen alleen voorwaarden
die nodig zijn om werkelijk vermogen en thermisch gedrag te berekenen.

De dynamische 1R1C-route kan een observatie gebruiken wanneer de structurele batch-fit
nog wacht op setpoint-herstel. Beide routes blijven passief.

## Context en opslag

Een wijziging van een geselecteerde bronroute, waterkalibratie of andere fysieke
meetcontext start een nieuwe `context_revision` en onderbreekt alleen lopende
meetintervallen. Afgeronde records en het 1R1C-model blijven behouden.
Een wijziging van regel- of beoordelingsinstellingen herbeoordeelt bestaande records
zonder de fysieke metingen te wissen.

Het journal bewaart batchrecords en de 1R1C-leerstand in twee flashslots met schema en
CRC, maximaal eenmaal per uur bij nieuwe gegevens. Herstel vereist geldige UTC en een
ondersteund schema en algoritmeversie; schema-4-batchrecords blijven leesbaar. De
bronkeuze verhindert herstel niet. Na een reboot staat de opt-in uit. Alle statussen
publiceren `auto_apply_allowed: false`.

## Status en export

De web-app leest onveranderlijke status- en exportsnapshots uit PSRAM:

- `GET /openquatt/learning/status`
- `GET /openquatt/learning/export`

De status toont de gekozen bronroute, geldigheid, blokkaderedenen, voortgang en
geheugendiagnostiek. Een bronroute verklaart welke bestaande controlwaarde is gebruikt;
zij is geen extra meetinstelling.
