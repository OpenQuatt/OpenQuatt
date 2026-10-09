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
of een vermogensmeting mogelijk is: ontbrekend debiet is ongeldig; geldig nuldebiet
levert tijdens een verwarmingspauze nul watervermogen op.

## Fysieke meetketen

De water-in- en water-uitmetingen van iedere aanwezige warmtepomp, de
compressoractiviteit en beschermingsstatus blijven directe hardwaremetingen. Hun
ontvangststatus beschermt de calorimetrie tegen ontbrekende of ongeldige HP-data.

Het warmtevermogen gebruikt de ΔT per warmtepomp, net als de bestaande Heat Power-
sensoren. Een verschil tussen HP1-uit en HP2-in blokkeert leren niet. IJking van de
watertemperatuursensoren via het servicemenu kan de meetnauwkeurigheid verbeteren.

## Wanneer een record ontstaat

Voor beide leerroutes zijn geldige geselecteerde waarden voor kamer, setpoint,
buiten en flow nodig, met bruikbare water- en HP-statusmetingen voor de calorimetrie.
De regeling moet in verwarmingsmodus staan, zonder service, OTA of actuele
ketelwarmte. Een geldige bron is dus niet hetzelfde als een volledig learning-record:
de vermogensmeting en bedrijfstoestand moeten ook bruikbaar zijn.

De woninglijn verzamelt volledige perioden van 24 uur. Normale CM0/CM1-pauzes,
setpointwijzigingen en normale ontdooicycli tellen mee. Geldig nuldebiet levert nul
watervermogen; bij pompuitloop blijft het gemeten vermogen, inclusief een eventuele
negatieve waarde, meetellen. De compressor hoeft niet continu te draaien en er is
geen eis van een constant setpoint of een stabiele watertemperatuur. Na de volledige
dag wordt nog wel grove kamertemperatuurdrift gecontroleerd; zo'n dag kan worden
afgewezen. Bestaande vieruursrecords behouden hun oorspronkelijke kwaliteitscontrole.

De dynamische 1R1C-route verwerkt kortere perioden van opwarmen en afkoelen. Deze
route sluit actieve begrenzing en beschermingsfasen uit; dat onderbreekt niet
vanzelf de dagmeting voor de woninglijn. Beide routes blijven passief.
Ontbrekende metingen worden nooit vervangen door nul. De voorwaarden voor korte
meetonderbrekingen en hervatten na een herstart staan hieronder.

## Context en opslag

Een wijziging van een geselecteerde bronroute, waterkalibratie of andere fysieke
meetcontext start een nieuwe `context_revision` en onderbreekt alleen lopende
meetintervallen. Afgeronde records en het 1R1C-model blijven behouden.
Een wijziging van regel- of beoordelingsinstellingen herbeoordeelt bestaande records
zonder de fysieke metingen te wissen.

Het journal bewaart afgeronde metingen, de 1R1C-leerstand en een compact checkpoint
van de lopende dag in dezelfde twee flashslots met schema en CRC. Een actieve dag
krijgt iedere 15 minuten een checkpoint; zonder dagprogressie blijft de grens voor
nieuwe modelgegevens eenmaal per uur. Vlak vóór een normale herstart of OTA wordt
extra opgeslagen. Een afgebroken schrijfoperatie laat het vorige geldige slot intact;
een opslagfout wordt gemeld en leidt deze boot niet tot herhaalde schrijfpogingen.
Een dagcheckpoint mag alleen hervatten na een geplande softwareherstart of OTA,
met een eenmalige bevestiging in RTC-geheugen voor de succesvol opgeslagen kopie.
Die bevestiging wordt bij boot verbruikt. Een oude kopie kan daardoor niet opnieuw
hervatten als het verwijderen van een verworpen dag later mislukt. Afgeronde
historie en het 1R1C-model blijven onafhankelijk herstelbaar; leren blijft passief.

Schema 7 voegt het dagcheckpoint toe; schema 4/5/6 blijven leesbaar. Oude firmware
kan schema 7 niet lezen. De eerste OTA vanuit firmware zonder dagcheckpoint kan de
bestaande RAM-dag nog niet bewaren. Afgeronde metingen blijven bij de upgrade behouden.
Herstel vereist geldige UTC en een ondersteund schema en algoritmeversie.

De opgeslagen dag wacht na de boot op geldige UTC, ontbrekende geselecteerde
meetwaarden en dezelfde meetcontext. Tijdens de eerste bronopstart na een boot mag
het checkpoint kort wachten op nog ontbrekende bedrijfstelemetrie; dit zijn geen
geldige metingen en de grens van 120 seconden wordt niet verlengd. Een daadwerkelijk
waargenomen ongeldige bedrijfstoestand of ontvangen ongeldige bedrijfstelemetrie
breekt het herstel af; latere verbetering herstelt die dag niet alsnog. Dit geldt
ook voor ontvangen ongeldige meetwaarden vóór de klok is gesynchroniseerd.
De hele onderbreking vanaf het laatste opgeslagen geldige meetpunt tot
de eerste geldige nieuwe meting mag maximaal 120 seconden zijn, inclusief upload,
herstart en het beschikbaar komen van de bronnen. De ontbrekende warmte wordt
begrensd met dezelfde onzekerheidscontrole als andere korte dagmeetgaten: maximaal
100 W onzekerheid gemiddeld over 24 uur, samen met eerdere meetgaten. Temperaturen
worden geïnterpoleerd; hiervoor bestaat geen afzonderlijke foutgarantie.
Bij te lange uitval, gewijzigde meetcontext, ongeldige actuele bedrijfstoestand of
uitgeput onzekerheidsbudget begint een nieuwe dag. De 1R1C-leerstand blijft behouden,
maar het onafgeronde 30-minuteninterval begint opnieuw.

Voorbeeld: na 18 uur meten volgt een herstart. Zijn na 90 seconden weer geldige
metingen beschikbaar en past het meetgat binnen het budget, dan loopt dezelfde dag
verder tot 24 uur. Bij stroomuitval, een fysieke reset of crash begint de lopende
dag opnieuw; ook bij een ontbrekende bevestiging na een firmwarewissel. Afgeronde
leerdata blijven behouden. Een mislukte opslag vóór de geplande herstart geeft
geen hervatbevestiging en begint eveneens een nieuwe dag.
Tijdens verzamelen zijn maximaal 96 periodieke checkpointwrites per dag nodig, plus
geplande herstarts/OTA; het journal blijft beperkt tot de bestaande twee 8 KiB-slots.
Bij automatische bronkeuze wordt alleen de selectorconfiguratie opgeslagen, niet
de effectieve bronroute van de vorige boot. Identieke selectie garandeert daarom
geen identieke effectieve route over een herstart; echte routewijzigingen die deze
boot al zijn waargenomen verhinderen herstel wel.
Een afgebroken webupload mag het leren niet blijvend pauzeren: zonder nieuwe
OTA-voortgangsmelding vervalt de interne pauze na circa twee minuten.
De normale controllerpauze voor native/HTTP-request OTA blijft onafhankelijk gelden.

De bronkeuze verhindert herstel van afgeronde metingen en het 1R1C-model niet.
Passief leren staat standaard aan; de gekozen schakelaarstand blijft na een reboot
behouden. Alle statussen publiceren `auto_apply_allowed: false`.

## Status en export

De web-app leest onveranderlijke status- en exportsnapshots uit PSRAM:

- `GET /openquatt/learning/status`
- `GET /openquatt/learning/export`

De status toont de gekozen bronroute, geldigheid, blokkaderedenen, voortgang en
geheugendiagnostiek. `collection.batch_restore_pending`, `batch_resume_status` en
`batch_missing_energy_uncertainty_wh` tonen dagherstel en de onzekerheid van
meetgaten (onbekend zolang het checkpoint nog niet hersteld is).
`collection.batch_source_status` toont afzonderlijk de geldigheid voor de dagmeting;
`batch_resume_reason` bewaart de reden waarom het laatste dagherstel werd afgewezen.
De gecombineerde `source_status` kan daarnaast een beperking van het 1R1C-model tonen.
Een bronroute verklaart welke bestaande controlwaarde is gebruikt;
zij is geen extra meetinstelling.
