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

Een volledig temperatuurframe gebruikt de geselecteerde waarden voor kamer,
setpoint en buiten. De regeling moet in verwarmingsmodus staan, buiten service
en OTA. Een kort ontbrekend temperatuurveld kan onafhankelijk gemeten warmte
laten meetellen via de eigen warmtecursor, inclusief nul en negatieve warmte.

Met ingeschakelde keteloptie en een waargenomen aanvoersensor na de ketel
gebruikt de dagmeting het totale vermogen naar het water. De standaard
keteloptie alleen activeert deze bron niet: zonder waargenomen ketelsensor
blijft het bestaande warmtepomppad met zijn controles op ketelwarmte gelden. Dat volgt uit het gemeten debiet en het temperatuurverschil
tussen de retour bij HP1 en de systeemaanvoer na de ketel. CM3 en CM4 tellen mee
zolang deze meting geldig is, onafhankelijk van de OpenTherm-status. Een
vastgehouden aanvoerwaarde of een fallback naar de warmtepompuitgang levert geen
systeemmeting op. Valt de systeemmeting weg, dan blijft de warmte voor dat
interval onbekend en gelden de meetgatregels. Er is geen terugval op
warmtepompwarmte op basis van de OpenTherm-status. Dat geldt ook bij R1.
Bij stilstaande ODU-pompen kan verse pulsflow van de Q-controller circulatie
aantonen als de bronselectie anders een synthetische nul oplevert. Een stille
pulsingang bewijst op zichzelf geen nuldebiet. De normale stilstandsnul mag
wel meetellen als het bestaande regelcontract bevestigt dat de ketel geen warmte
levert. Aanvoer en retour moeten ook dan actueel zijn. Bij actieve of onbekende
ketelwarmte blijft ontbrekend debiet een meetgat. Bij HA-aanvoer telt de laatste temperatuurontvangst hoogstens
30 seconden mee. De HA-heartbeat houdt de verbinding actueel. Voor de dagmeting
moet de aanvoerbron zelf minstens elke 30 seconden een meetwaarde doorgeven.
Een gewijzigde aanvoerkalibratie bij het terugzetten van een backup begint een
nieuwe dagmeting. Afgeronde dagen blijven bewaard.
Bij actieve systeemmeting accepteert de dagmeting gemeten systeemwarmte tot en
met ±50 kW. Waarden daarbuiten worden afgekeurd, niet afgeknipt. Deze harde
bronfout breekt een lopende dagmeting af. Voor ontbrekende warmte gebruikt de
onzekerheidsberekening 50 kW als ondergrens voor de
vermogensgrens. Een open meetgat bewaart de hoogste toegepaste grens, ook als de
keteloptie tussentijds wordt uitgeschakeld. Zo verlaagt een instellingwijziging
eerder ontstane onzekerheid niet. Voor het accepteren van warmtepompwarmte
blijft de lagere generatie- en topologiegrens gelden.
Het ingestelde ketelvermogen is een regelwaarde en bewijst geen maximum voor
ongemeten ketelwarmte.

CM98 is normale vorstcirculatie en mag, net als stilstand en pompuitloop,
meetellen. Voor pompbedrijf gelden de reguliere broncontroles en pauzes tijdens
service of OTA. Werkelijk gemeten warmte tijdens pompbedrijf wordt met teken
meegenomen.

De woninglijn verzamelt volledige perioden van 24 uur. Normale CM0/CM1-pauzes,
setpointwijzigingen en normale ontdooicycli tellen mee. Geldig nuldebiet levert nul
watervermogen; bij pompuitloop blijft het gemeten vermogen, inclusief een eventuele
negatieve waarde, meetellen. De compressor hoeft niet continu te draaien en er is
geen eis van een constant setpoint of een stabiele watertemperatuur. Na de volledige
dag wordt structuur afzonderlijk van modelkwaliteit beoordeeld. Een structureel
geldige dag blijft bewaard met dekking en warmte-onzekerheid, ook bij te grote
kamertrend of hoge onzekerheid. Zulke dagen krijgen geen modeladviesgewicht.
Bestaande vieruursrecords behouden hun oorspronkelijke kwaliteitscontrole.

De dynamische 1R1C-route verwerkt kortere perioden van opwarmen en afkoelen. Deze
route gebruikt warmtepompwarmte en sluit ketelbedrijf, actieve begrenzing en
beschermingsfasen uit. Beide routes blijven passief.
Ontbrekende metingen worden nooit vervangen door nul. De voorwaarden voor korte
meetonderbrekingen en hervatten na een herstart staan hieronder.

Bij stoppen kan werkmodusregister `2099` al uitstand melden terwijl
compressorfrequentie `2103` nog boven nul staat. Als alle overige metingen en
bedrijfsinformatie geldig zijn, behoudt een lopende dag deze korte overgang.
Het volledige frame blijft tijdens die overgang ongeldig. Onafhankelijk geldige
calorimetrie telt wel mee, inclusief gemeten nulvermogen; alleen werkelijk
ontbrekende warmte wordt bij herstel geïnterpoleerd en conditioneel begrensd.
Een hoge onzekerheidslast wist de dag niet, maar kan modeladvies uitsluiten. Het hele
interval tussen de geldige meetpunten mag maximaal 120 seconden duren. Een andere
gelijktijdige meetfout of een te lange overgang onderbreekt de dag wel. Deze
uitzondering start geen nieuwe dag met tegenstrijdige gegevens; de 1R1C-route en
de strengere bedrijfstoestandcontrole bij herstel na boot blijven ongewijzigd.

## Context en opslag

Automatische en handmatige bronwissels voor kamer, setpoint, buiten en flow laten
de lopende dagmeting en het 1R1C-interval doorlopen. Ook een wissel tussen een
actuele en geldig vastgehouden waarde is geen nieuwe meetcontext. De learner
gebruikt vanaf de volgende normale bronselectie de nieuwe geselecteerde waarde;
ontbrekende of ongeldige metingen blijven onder de normale meetgat- en
kwaliteitscontroles vallen.
Waterkalibratie, flowmeterkalibratie of een wijziging van de hardwaremeetcontext
start wel een nieuwe `context_revision` en onderbreekt lopende meetintervallen.
Afgeronde records en het 1R1C-model blijven behouden.
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

Schema 9 bewaart kwaliteitscodes per dag en de onafhankelijke warmtecursor in het
checkpoint. Schema 4–8 blijven leesbaar, met behoud van geordende recordwaarden,
1R1C-toestand, laatste geaccepteerde UTC en instellingen. Oude dekking blijft
onbekend. Schema 9 gebruikt dezelfde 128×52-byte-recordruimte en een 300-byte
dagcheckpoint; de maximale slotinhoud is 8168 bytes binnen de bestaande 8192 bytes.
Oude firmware kan schema 9 niet lezen. Na twee saves kunnen beide slots schema 9
zijn: dit is geen rollbackgarantie of algemene OTA-vrijgave. De eerste OTA vanuit
firmware zonder dagcheckpoint kan een bestaande RAM-dag niet achteraf bewaren.
Herstel vereist geldige UTC en een ondersteund schema en algoritmeversie.

De opgeslagen dag wacht na boot op geldige UTC, ontbrekende geselecteerde
meetwaarden en dezelfde fysieke meetcontext. Alleen een geverifieerd eenmalig
softwareherstartticket, gebonden aan journalsequentie, checkpointtijd en volledige
CRC, geeft maximaal 300 seconden vanaf het opgeslagen geldige meetpunt tot herstel,
inclusief upload, reboot en bronopstart. Tijdens waargenomen WAIT blijven de gewone
maximaal 60 s-ticks vereist. Een gewone expliciete brononderbreking blijft maximaal
120 s; de OTA-voortgangslease blijft 120 s. Een onverwachte reset krijgt geen extra
hersteltijd. Bekende ongeschikte bedrijfsvoering, ontvangen ongeldige telemetrie,
klokbreuken en contextwijzigingen blijven harde veto's.

Tijdens geautoriseerd wachten blijft nieuw geldig warmtevermogen in een eigen
PSRAM-toestand; de opgeslagen journalinhoud wordt niet herschreven. Na herstel is
alleen werkelijk ontbrekende warmte onzeker. Een hoge opgebouwde onzekerheid wist
het checkpoint niet meer. Zij blijft bij de complete dag beschikbaar voor de
aparte kwaliteitsbeoordeling. Temperaturen worden geïnterpoleerd; hiervoor bestaat
geen afzonderlijke foutgarantie. Bij te lange uitval of een hard veto begint een
nieuwe dag. De 1R1C-leerstand blijft behouden; het onvoltooide 30-minuteninterval begint
opnieuw. Een ticket wordt deze boot eenmaal gebruikt en mag niet worden hergebruikt.
Het ticket bewaart ook of een systeemaanvoersensor al is waargenomen; een
herstart tijdens sensoruitval schakelt daardoor niet naar warmtepompwarmte.
Tickets van firmware zonder dit bronbewijs hervatten de onvoltooide dag niet.
Afgeronde metingen en de 1R1C-leerstand blijven behouden.

Voorbeeld: na 18 uur meten volgt een bevestigde geplande softwareherstart. Bij geldige
bronnen na 180 seconden kan dezelfde dag hervatten. Bij stroomuitval, fysieke reset,
crash of mislukte opslag vóór herstart begint de lopende dag opnieuw. Afgeronde
metingen blijven onafhankelijk herstelbaar. Een mislukte OTA kan de bestaande
RAM-dag hervatten onder de gewone 120 s-meetgatvoorwaarden.
Tijdens verzamelen zijn maximaal 96 periodieke checkpointwrites per dag nodig, plus
geplande herstarts/OTA; het journal blijft beperkt tot de bestaande twee 8 KiB-slots.
Het dagcheckpoint is gekoppeld aan hardware en meetkalibratie, niet aan de
bronkeuze. Een andere automatische of handmatige bronkeuze verhindert herstel dus
niet. Bestaande schema-7-checkpoints met broninstellingen in hun meetcontext blijven
hervatbaar wanneer de fysieke meetcontext gelijk is. Opnieuw opslaan gebruikt
schema 9 en een nieuwe CRC; dit verandert de inhoudelijke bronselectie niet.
De overige voorwaarden voor hervatten, waaronder de RTC-bevestiging en meetgatgrens,
blijven gelden.
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
`batch_resume_reason` verklaart een afgewezen dagherstel tijdens de eerste
vervangende leerdag. Zodra die dag eindigt of opnieuw begint, vervallen de
herstelstatus en reden naar `none`.
De gecombineerde `source_status` kan daarnaast een beperking van het 1R1C-model tonen.
Een bronroute verklaart welke bestaande controlwaarde is gebruikt;
zij is geen extra meetinstelling.

De export bevat naast de laatste 60 meetregels een optioneel `daily_collection`:
verloren dagvoortgang, bronreden, meetgatduur en onzekerheidslast vóór afbreken.
Gemiste ticks, te lange gaten en klokbreuken hebben eigen oorzaken. De oude
budgetoorzaak blijft herkenbaar; hoge warmte-onzekerheid onderbreekt de nieuwe
dagcollector niet meer. De tellers gelden sinds initialiseren/resetten van de collector en
worden niet over een herstart bewaard. Wachten zonder actieve dag telt geen
verloren dag. Een onbekende last of duur is `null`, niet nul.

`collection_state` bewaart in dezelfde export de actuele dagvoortgang,
`tick_epoch`, uptime, numerieke ESP-IDF `boot_reset_reason`, laatste afwijzing,
bronstatus en herstelreden. De bootreden beschrijft de controllerherstart;
`batch_resume_reason` verklaart afzonderlijk waarom dagherstel werd geweigerd.
Context-, decode- en kwaliteitsweigeringen zijn uitgesplitst. Een vóór import
afgewezen bootcheckpoint valt buiten de actieve-dagtellers.

## Dagkwaliteit en modeladvies

De bestaande tien `record_columns` blijven ongewijzigd. Exportschema1 voegt
`record_quality_policy: 1`, vijf `record_quality_columns` en een aligned
`record_quality`-array toe: warmtedekking, temperatuurdekking, gemiddelde
warmtegat-onzekerheid, servergeschiktheid en gewicht. Dekking is naar beneden in
basispunten afgerond; 10000 is 100%. Onzekerheid wordt conservatief naar boven
vastgelegd als gemiddeld watt over 24 uur. Onbekende historische waarden blijven
`null`; overloop is `"overflow"` en geeft geen modelgewicht. Geen ontbrekende
kwaliteit wordt als nul onzekerheid of volledige dekking gepresenteerd.

Bewaren en modelgebruik zijn aparte beslissingen. Nieuw bekende dagen tellen
voor advies alleen mee bij beide dekkingen ≥99%, gemiddelde warmtegat-onzekerheid
≤100 W en de bestaande dagkwaliteitstoetsen, waaronder kamertrend ≤0,20 K/h.
Het gewicht is `warmtedekking × temperatuurdekking / (1 + onzekerheid_W / 100)`;
dit blijft in alle Huber-/leave-one-day-out-fits aanwezig. Ongeschikte dagen blijven
opgeslagen en zichtbaar met gewicht 0. Oude geschikte dagen zonder kwaliteitscodes
mogen trainen met gewicht 0,5, maar leveren nooit onafhankelijke controledagen.
Vrijgave wacht op drie nieuwe bruikbare dagen met bekende kwaliteit en blijft ook
aan de bestaande aantallen, spreiding, verdeling en stabiliteitscriteria gebonden.

De onafhankelijke controle begrenst de slechtste verbetering voor warmte binnen
`gemeten ± U`: minimaal 50 W en 5% tegenover de foutbovengrens van de ingestelde lijn.
Dit is geen gekalibreerde betrouwbaarheidskans; temperatuurfouten, zon, interne
winsten en sensorbias worden hiermee niet volledig afgedekt. Het opgeslagen
1R1C-/RLS-model wordt niet vervangen en `auto_apply_allowed` blijft false. Fysieke
context-/algoritmeversie 4 blijft gelijk; kwaliteitspolicy 1 heeft een eigen versie.

Status en export benoemen de vroegere 2400 Wh-grens met
`batch_budget_scope: "model_advice_only"`: een adviespoort, geen afbreekgrens voor
het bewaren van de dag. Voor exact 100 W is geen empirische woningonderbouwing;
deze eerste adviespolicy blijft bewust apart van de feitelijke metingen.

Voor warmtepompwarmte komt de conditionele ontbrekende-warmtegrens uit het volledige performancedomein:
V1/V1.5 Single 6115,28 W / Duo 12230,56 W; V2 Single 9172,15 W / Duo 18344,30 W.
De effectieve grens is het maximum van tabelbasis, geldige absolute eindvermogens
en waargenomen piekvloer. Voor een gat is U begrensd met `B × duur + integraal(abs(Q))`;
negatieve warmte mag onzekerheid niet wegstrepen. De tabel is geen bewezen maximum
van alle werkelijke thermische/defrostpieken. Bij onbekende generatie gebruikt de
runtime de hoogste ondersteunde tabelbasis voor dezelfde Single/Duo-topologie.
Alleen oude schema7-checkpoints zonder opgeslagen piekvloer behouden voor de
eerste overbrugging de conservatieve 50 kW-terugval; reeds opgeslagen U wordt
nooit achteraf verlaagd.

Het plausibiliteitsfilter voor warmtepompwarmte gebruikt
`tabelmaximum × 1,20`: V1/V1.5 Single 7338,34 W / Duo 14676,67 W en V2 Single
11006,58 W / Duo 22013,16 W. Exact de grens is toegestaan; positief en negatief
worden symmetrisch behandeld. Dit is een engineeringmarge, geen gemeten bewijs
dat iedere werkelijke waterzijdige piek binnen 20% blijft. Onbekende generatie
gebruikt ook hier de hoogste ondersteunde generatie voor de ingestelde topologie.
Deze grens geldt zonder keteloptie en voor de dynamische 1R1C-meting. De status publiceert de grens van de
dagmeting als `collection.batch_heat_measurement_limit_w`.

Een eindige uitschieter boven deze grens wordt pas na bron-/unit-/tijdcontroles
`heat_out_of_range`. Hij telt niet als warmte of RLS-meting en wordt niet naar
nul of de grens afgeknipt. Een kort gat mag onder dezelfde bedrijfsvoorwaarden
overbruggen; de afgekeurde absolute piek vergroot daarbij conservatief de
onzekerheidsvloer. Langdurige gaten en harde bedrijfs-/bronfouten blijven blokkeren.
Bij geverifieerd bootherstel kan zo'n uitschieter wachten binnen de bestaande
300 s-totaalgrens en 60 s-tickgrens; ticket, klok en andere veto's blijven gelden.
Een uitschieter vóór geldige UTC/journalherstel blijft een hard bootveto: er is
dan nog geen herstelaccumulator waarin zijn piekevidence behouden kan worden.

Voor uitsluitend warmtepompwarmte blijft de oude structurele 50 kW-grens voor
compatibiliteit met eerder opgeslagen records, checkpoints, piekvloeren en U.
Daardoor wist een lagere nieuwe meetgrens geen echte historie of RLS. Waarden
buiten dat oude domein blijven hard ongeldig; ze leveren geen bruikbare
piekevidence. De systeemmeting met ingeschakelde keteloptie gebruikt daarnaast
nog steeds de actieve ±50 kW-meetgrens en de onzekerheidsgrens hierboven.

De webgrafiek bewaart alle punten en toont de kwaliteit in de tooltip. Alleen
servergeschikte punten beïnvloeden de adviesvoortgang; historische kwaliteit blijft
zichtbaar onbekend. Oudere firmware zonder kwaliteitsmetadata behoudt de bestaande
presentatie. Leerdata-backup/herstel blijft toekomstig onderzoek, niet inbegrepen.

De drie serializer-/export-/requestscratchbuffers groeien van 32 naar 37 KiB, samen
15 KiB extra strikt PSRAM zonder interne-RAMfallback. De statusbuffer blijft 4 KiB;
de HTTPD-stack wordt niet vergroot. Host-/JSON-capaciteitstests bewijzen geen
ESP32-geheugenmarge, flashdurabiliteit of timing. Representatieve HIL-, upgrade- en
bewuste downgradevalidatie blijven vereist vóór een eventuele installatie.
