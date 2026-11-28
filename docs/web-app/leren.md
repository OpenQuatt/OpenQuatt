# Leren

Open **Leren** om de meetvoortgang, de woninglijn en het model voor opwarmen en
afkoelen te bekijken. Op ondersteunde Heatpump Controller Q Single en Duo gebruik
je onder **Leren → Instellingen** ook de schakelaar **Passief leren**. Het leren verandert je instellingen
niet. **Passief leren** staat standaard aan. De gekozen stand blijft bewaard bij
een herstart of firmware-update, ook als je leren bewust uitschakelt. Bij de eerste
update vanaf de eerdere versie die altijd uit startte, wordt leren ingeschakeld.

- **Uitleg** laat met illustraties zien wat OpenQuatt meet, wat het leert en hoe Vooruitblik rekent.
- **Leren** toont de meetvoortgang, dagmetingen en woninglijn.
- **Vooruitblik** toont het temperatuurscenario en de weersverwachting.
- **Instellingen** groepeert passief leren, de weerinstellingen en de schakelaar voor Meetarchief.
- **Diagnose en data** toont de warmtevraag, broninformatie en opties om leerdata en Meetarchief te beheren.

## Twee meetperioden

- **Woninglijn uit dagmetingen.** De balk loopt over 24 uur. De controller meet
  de afgegeven warmte, ook tijdens verwarmingspauzes en wijzigingen van de
  gewenste kamertemperatuur. Het gemiddelde warmtevermogen en de
  buitentemperatuur vormen samen een meetpunt, omgerekend naar 20 °C in huis.
- **Opwarmen en afkoelen.** De balk loopt normaal over 30 minuten. Na ontdooien
  duren de eerste twee meetperioden elk 15 minuten. Uit geschikte
  meetperioden schat de controller het warmteverlies en hoeveel warmte de woning
  kan opslaan. Tijdens bijvoorbeeld ontdooien kan deze meting wachten terwijl
  de dagmeting doorgaat. Een voorlopige schatting is nog niet onafhankelijk bevestigd.

De controller gebruikt de geselecteerde meetbronnen. OpenTherm is daarvoor geen
vereiste. Een bekend meetgat van maximaal twee minuten kan de controller
overbruggen als de bedrijfstoestand bekend blijft. Bij **Wacht kort op meetwaarde**
blijft de voortgang behouden. De onzekerheid bij het overbruggen van ontbrekende
warmtemetingen wordt apart weergegeven. Op firmware met de huidige kwaliteitsregels
geldt de onzekerheidsgrens voor modeladvies. Een afgeronde dag kan dus wel in de
grafiek staan en buiten het advies vallen.

Cv-ketelwarmte, koelen en service onderbreken de dagmeting. Bevestigd uitsluitend
OpenTherm-tapwaterbedrijf onderbreekt de meting niet. Cv-activiteit en een vlam
zonder duidelijke bestemming blijven uitgesloten.

Na een geplande herstart kan de controller de dagmeting hervatten als er een geldig
meetcheckpoint is, de meetcontext gelijk blijft en de herstart binnen vijf minuten
valt. Als herstel wordt afgewezen, begint een nieuwe dagmeting. De lopende
meetperiode voor opwarmen en afkoelen begint bij een herstart opnieuw. Eerder
opgeslagen leerdata blijven beschikbaar. Zie voor de voorwaarden en
bronafwijzingen het
[meetbron- en kwaliteitscontract](../development/power-house-learning-sources.md).

## Wanneer is er een geleerde woninglijn?

De kaart **Meetgegevens** onder het leerpaneel toont de grafiek en de voorwaarden
voor een woninglijn. De kaart laadt de gegevens vanzelf.
Een plausibele lijn kan vanaf zes bruikbare dagmetingen alvast als **voorlopige
woninglijn** verschijnen. Deze schatting gebruikt alle geschikte dagen, ook de
nieuwste dagen met bekende meetkwaliteit. Betere meetkwaliteit geeft een dag
meer gewicht; oudere dagen met onbekende meetkwaliteit tellen minder zwaar mee.
De groene lijn is volledig gestippeld en loopt over de hele grafiek. Buiten het
gemeten temperatuurbereik is dit een doortrekking van de schatting. Het gekleurde
vlak markeert het bereik van de bruikbare meetpunten. De uitleg vermeldt welke
controles nog ontbreken. Deze schatting stuurt de regeling niet aan.

De balken tonen het aantal dagmetingen en de temperatuurspreiding. De huidige
richtwaarden zijn minstens negen geschikte dagmetingen, waarvan zes om te leren
en drie voor onafhankelijke controle, en minstens 6 °C spreiding tussen P10 en
P90 van de leerdagen. P10 en P90 zijn de grenzen rond het middelste deel van de
temperaturen. De controller kijkt ook naar de verdeling van de temperaturen,
meetkwaliteit en de uitkomst op de controledagen.

De firmware reserveert de nieuwste drie geschikte dagen met bekende meetkwaliteit
voor onafhankelijke controle van het modeladvies. Die aparte adviesberekening
gebruikt de controledagen niet bij het bepalen van de lijn of de spreiding van
de leerdagen. De voorlopige schatting en de adviesberekening kunnen daardoor
verschillende lijnen en temperatuurbereiken hebben. Oudere dagen met onbekende
meetkwaliteit mogen met een lager gewicht helpen bij het leren.
Op oudere firmware zonder voortgang van de fit tonen de balken bij onbekende
meetkwaliteit het aantal en de spreiding van alle opgeslagen dagen.
Dagen met onbekende meetkwaliteit leveren geen onafhankelijke controle.
Klik in **Meetgegevens** op **Vernieuwen** om nieuw opgeslagen punten op te halen.

De blauwe lijn is de ingestelde woninglijn. De punten zijn metingen. Bij een
meetpunt zie je onder meer de meetduur, gemiddelde buitentemperatuur en het
gemiddelde warmtevermogen. Waar beschikbaar zie je ook hoeveel warmte- en
temperatuurmetingen geldig waren, de warmte-onzekerheid en het modelgewicht.
Een dag met een lager modelgewicht telt minder zwaar mee bij het schatten van de
lijn. Oude vieruursmetingen blijven herkenbaar in de grafiek en tellen niet mee
als dagmeting. Na goedkeuring is de groene lijn binnen het gemeten bereik
doorgetrokken en daarbuiten gestippeld. Een voorlopige lijn blijft overal
gestippeld, ook binnen het gemeten bereik.
Gebruik de labels in de legenda om de lijnen, meetpunten en onzekerheidsband
apart te tonen of te verbergen. De webapp onthoudt per browser welke reeksen
je hebt verborgen en welke rekenaanvoer je hebt gekozen.

Open **Bekijk beoordeling** voor de voortgangsbalken en uitleg.

Onder de grafiek staan de geschatte warmtevraag bij −10 °C en de
buitentemperatuur waarbij de geleerde lijn 0 kW bereikt. Buiten het gemeten
bereik rekent de webapp de lijnen door.

### Warmtecapaciteit bij Power House

De paarse lijn schat het maximale thermische vermogen van de geïnstalleerde
Quatt-buitenunits. Kies **35 °C** of **45 °C** naast **Geschat Quatt-maximum**. Deze keuze
geldt alleen voor de grafiek. De berekening gebruikt het ingestelde
verwarmingsdebiet, de daglimiet, de frequentietabellen en uitsluitingen per
buitenunit en de elektrische grens voor langdurig bedrijf. Bij Duo stroomt het
water door beide buitenunits in serie.

Het gekleurde gebied toont hoeveel vermogen er boven de warmtevraag overblijft.
Wijs een punt op de capaciteitslijn aan om het vermogen en de marge te zien.
De markering bij het geschatte omslagpunt noemt de gebruikte woninglijn. Een
voorlopige geleerde woninglijn telt daarvoor nog niet mee. Voor een goedgekeurde
geleerde lijn telt alleen een kruising binnen het bereik met geschikte metingen.

Bij V1 en V1.5 eindigt de capaciteitslijn bij **+7 °C**. Daarboven ontbreken
kaartwaarden om het maximum te schatten. Ontbrekende kaartwaarden op de hoogste
toegestane stand geven een gat in de lijn. De schatting gaat uit van stabiel
verwarmen tussen ontdooicycli. Het omslagpunt geeft aan waar de twee schattingen
elkaar kruisen. Wanneer de ketel daadwerkelijk bijspringt, hangt ook af van de
hybride regeling en de actuele omstandigheden.

### Woninglijnadvies overnemen

Boven de grafiek vergelijkt **Woninglijnadvies** het ingestelde vermogen bij
de koude referentietemperatuur en de stookgrens met het voorstel. Bij een
voorlopig voorstel staat **Schatting**. Als de lijn op aparte dagen is
getoetst, zie je het getoetste temperatuurbereik.

Met **Overnemen** pas je beide instellingen tegelijk aan. Je kunt ook een
voorlopig voorstel kiezen. Met **Vorige waarden terugzetten** maak je de
wijziging ongedaan zolang de overgenomen instellingen nog actief zijn.
Tijdens de nieuwe toets blijven de groene lijn en het laatste voorstel
zichtbaar en is **Overnemen** tijdelijk uitgeschakeld.

## Details en leerdata

Onder **Diagnose en data → Bronnen en technische details** vind je de bronstatus, watertemperaturen
en modelwaarden. Bij de woninglijn geeft `H` aan hoeveel extra vermogen per
graad kouder nodig is. `T₀` is de buitentemperatuur waarbij de geschatte
warmtevraag nul wordt. De regeling gebruikt die waarde niet als schakelgrens.

Onder **Diagnose en data → Leerdata** kun je de leerdata downloaden als JSON.
**Leerdata wissen** wist daar de leerhistorie en pauzeert passief leren. Het
Meetarchief blijft bewaard. Bij een fysieke ombouw van Single naar Duo of
omgekeerd wis je de leerdata voordat je de nieuwe opstelling gaat beoordelen. Een gewone bronwissel vereist deze reset niet.
Bij ingeschakelde technische statistieken wordt alleen de aan/uit-stand van
passief leren gedeeld.

### Meetarchief (experimenteel)

Op Heatpump Controller Q Single en Duo staat het Meetarchief bij een nieuw
archief standaard aan. De controller bewaart de blokken lokaal, ook als de
browser dicht is. Onder **Instellingen → Meetarchief** kun je de opname uitzetten.
Eerder bewaarde blokken blijven beschikbaar. Je keuze blijft bewaard bij een
herstart of update.

Onder **Diagnose en data → Meetarchief** staan de opnamestatus en de bewaarde meetperiode. Met **Hele archief
downloaden** krijg je één JSON-bestand met alle bewaarde meetblokken en de
weerdata bij die periode. De browser haalt de dagen één voor één op. Laat de
pagina open tot de download start. Onder **Eén dag downloaden** kun je ook
een losse dag (UTC) kiezen.

Onder **Wat bewaart Meetarchief?** staat wat de controller vastlegt. Per blok
bewaart hij de binnentemperatuur aan het begin en einde, de gemiddelde
buitentemperatuur, de ingestelde kamertemperatuur, de aan het water afgegeven
warmte en de bedrijfstoestand. Het bestand bevat ook de eindtijd, de blokduur
en de geldige meetduur voor warmte en temperatuur. Met Weer aan staan de
opgehaalde verwachtingen voor buitentemperatuur, windsnelheid, windrichting en
zoninstraling apart in de export, samen met beschikbare actuele windgegevens.
Zoninstraling staat in `solar` met dezelfde ophaaltijd als de bijbehorende
weerupdate. De vijf waarden zijn globale instraling op een horizontaal vlak
in W/m². Elke waarde geldt voor het uur dat op het vermelde tijdstip eindigt.
Een ontbrekende waarde staat als `null`. Koppel een zonrij aan een bestaande
weerupdate op ophaaltijd. Een oudere weerupdate heeft geen zonrij. Bij een
onleesbaar zonarchief blijft de gewone export beschikbaar en krijgt
`solar_available` de waarde `false`. Het bronnummer
verwijst per dag naar de meetroute in `heat_contexts`. Bij een bronwissel
bewaart de controller het oude blok als
verkorte periode. Ontbrekende warmte heeft een eigen waarde en is te
onderscheiden van gemeten nul. De dagen in de volledige export hebben
dezelfde opbouw als een losse dag. Meetarchief hergebruikt bronregistraties
zodra de bijbehorende blokken uit de opslag zijn gerold. Als alle slots in
gebruik zijn, blijft de opname lopen. Een nieuwe meetbron staat dan als
`unknown` in de export tot er weer ruimte vrijkomt.

Een fabrieksreset wist de opname-instelling. De firmware schermt oude
meetblokken daarna af. Voor een nieuw archief moet de archiefruimte eerst
worden gewist. Dit kan via USB op een Q-controller met de standaard
16 MiB-partitie-indeling. De wisopdracht verwijdert de oude meetopname
definitief. Controleer eerst de partitie-indeling van de aangesloten controller
met esptool 5 en het partitiescript van ESP-IDF. Vul de USB-poort van de
controller in bij `OQ_USB_PORT`.

```sh
OQ_USB_PORT=/dev/tty.usbmodem…
python -m esptool --chip esp32s3 --port "$OQ_USB_PORT" read-flash 0x8000 0x1000 partitions.bin
python "$IDF_PATH/components/partition_table/gen_esp32part.py" partitions.bin
```

Controleer dat `openquatt_data` start op `0xA20000` en `6016K`
(`0x5E0000`) groot is, zoals in
[`openquatt_16mb.csv`](../../openquatt/partitions/openquatt_16mb.csv).
Gebruik de volgende opdracht alleen bij die indeling. Die wist uitsluitend
de gereserveerde archiefruimte.

```sh
python -m esptool --chip esp32s3 --port "$OQ_USB_PORT" erase-region 0xC00000 0x29D000
```

Herstart de controller. Het nieuwe Meetarchief begint automatisch met opnemen.

## Vooruitblik

De standaardfirmware voor Heatpump Controller Q Single en Duo bevat
**Vooruitblik** onder **Leren**. Op een telefoon staat die
navigatie bovenaan. Vul onder **Leren → Instellingen** een Nederlandse plaatsnaam,
postcode of coördinaten in en zet **Weersverwachting gebruiken** aan. De controller stuurt
de locatie naar Open-Meteo om de plaats en de KNMI-weersverwachting op te halen.
Locatie en toestemming blijven lokaal op de controller bewaard en staan buiten
de instellingenback-up. De controller stopt het ophalen zodra je de functie
uitzet en bevestigt de keuze na opslag. Wacht vóór een herstart of firmware-update
tot **Uit** is bevestigd. Zolang de opslag loopt of als die mislukt, kan na een
herstart de vorige opgeslagen stand weer gelden. Probeer het opnieuw bij een
opslagfout. Bij de eerste upgrade vanaf de eerdere weerproef staat de functie
uit. Zet haar opnieuw aan als je wilt blijven delen met Open-Meteo.

De controller voert firmwarecontroles, weeropvragen en firmwaredownloads na elkaar uit.
Een handmatige firmwarecontrole kan daardoor even wachten op Open-Meteo.
Bij een firmwaredownload wacht de controller maximaal twee seconden op de vorige
verbinding. Is die dan nog bezet, dan start de download niet; probeer de update
later opnieuw.

Het scherm toont 36 uur aan buitentemperatuur en windsnelheid, met windrichting
en zoninstraling waar die beschikbaar zijn. In de windgrafiek wijzen de pijlen
waarheen de wind waait. Bij het gekozen uur staat uit welke richting de wind komt.
Zoninstraling staat in W/m² en is het gemiddelde over het uur dat op het
aangegeven tijdstip eindigt. Wijs in de grafiek een uur aan, gebruik de
pijltjestoetsen of open **Bekijk uurwaarden**.

**Temperatuur in huis** geeft een voorlopig scenario voor 30, 60 en 180 minuten.
De berekening neemt de actuele kamertemperatuur als beginpunt, houdt de gemeten
warmteafgifte constant en gebruikt de verwachte buitentemperatuur. Wind en
zoninstraling geven extra informatie bij de weersverwachting. Het scenario
stuurt de regeling niet aan.

Na een herstart zijn actuele metingen, een geldige weersverwachting en een
actueel beoordeeld woningmodel nodig. De controller kan een opgeslagen schatting
herstellen. Het scenario wacht op een nieuwe geldige meetperiode, normaal
30 minuten. Bij een korte onderbreking kan het laatst ontvangen scenario zichtbaar
blijven. De waarden verdwijnen na 30 seconden zonder bevestiging of zodra hun
tijdstip is verstreken.

Onder **Nog geen scenario** staat waar Vooruitblik op wacht. Als de controller
weet welke meting ontbreekt, zie je die daar ook.

Tijdens ontdooien blijft de vorige berekening zichtbaar onder **Laatst berekend**
zolang de doeltijden nog in de toekomst liggen. Bij geldige metingen rekent
Vooruitblik verder met het al beoordeelde model. Het model zelf leert weer zodra
er 15 minuten aaneengesloten geldige metingen zijn. Na het tweede geldige
kwartier duurt een meetperiode weer 30 minuten.

## Warmtevraag vergelijken

Met een voorlopige woninglijn toont **Diagnose en data → Warmtevraag vergelijken**
een schatting uit beide lijnen bij de laatst ontvangen buitentemperatuur en
20 °C binnen. Onder de waarden staat de stand van de beoordeling. Bij een
ongeldige ingestelde woninglijn of ongeldige leerinstellingen toont de kaart
alleen de reden. Buiten het meetbereik vermeldt de kaart de grenzen van de
voorlopige lijn. Die zijn net als de dagmetingen in de grafiek omgerekend
naar 20 °C binnen.

Bij een goedgekeurde geleerde woninglijn toont **Diagnose en data → Warmtevraag vergelijken** hoeveel
basiswarmte Power House met de ingestelde en met de geleerde lijn zou vragen.
Beide berekeningen gebruiken hetzelfde rekenmoment, dezelfde metingen en
dezelfde toestand van de regeling. Het verschil is de geleerde vraag min de
ingestelde vraag, in watt.

Binnen het getoetste temperatuurbereik toont het scherm de vergelijking van
de regelaar. Als de buitentemperatuur daarbuiten valt en de model- en
meetgegevens geldig zijn, toont het scherm een schatting uit beide woninglijnen
bij de laatst ontvangen buitentemperatuur. Tijdens opwarmen, externe warmtevraag of service
laat het scherm zien waarom de waarden tijdelijk wegvallen. Starten, stoppen
en verdelen volgen daarna hun eigen regels. De vergelijking past de woninglijn
of de aansturing niet aan.
