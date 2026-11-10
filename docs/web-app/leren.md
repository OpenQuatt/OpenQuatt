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
- **Instellingen** groepeert passief leren, leerdata wissen en de weerinstellingen.
- **Diagnose en data** toont modelwaarden, broninformatie, warmtevraag en de export van leerdata.

## Twee meetperioden

- **Woninglijn uit dagmetingen.** De balk loopt over 24 uur. De controller meet
  de afgegeven warmte, ook tijdens verwarmingspauzes en wijzigingen van de
  gewenste kamertemperatuur. Het gemiddelde warmtevermogen en de
  buitentemperatuur vormen samen een meetpunt, omgerekend naar 20 °C in huis.
- **Opwarmen en afkoelen.** De balk loopt over 30 minuten. Uit geschikte
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
30-minutenmeting begint bij een herstart opnieuw. Eerder opgeslagen leerdata
blijven beschikbaar. Zie voor de voorwaarden en bronafwijzingen het
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

Open **Bekijk beoordeling** voor de voortgangsbalken en uitleg. Met **Band rond
de lijn** zie je hoe dicht de gebruikte dagmetingen bij de woninglijn lagen.
Buiten hun temperatuurbereik waaiert de band uit volgens een rekenregel. De
werkelijke foutmarge is daar onbekend. Onder de grafiek staan schattingen bij
−10 °C en bij de buitentemperatuur waar de geleerde lijn op 0 kW uitkomt.

## Details en leerdata

Onder **Diagnose en data → Bronnen en technische details** vind je de bronstatus, watertemperaturen
en modelwaarden. Bij de woninglijn geeft `H` aan hoeveel extra vermogen per
graad kouder nodig is. `T₀` is de buitentemperatuur waarbij de geschatte
warmtevraag nul wordt. De regeling gebruikt die waarde niet als schakelgrens.

Via **Diagnose en data → Leerdata downloaden** bewaar je een lokale JSON-export.
**Instellingen → Leerdata wissen** pauzeert het leren en wist de leerhistorie. Bij een fysieke
ombouw van Single naar Duo of omgekeerd wis je de leerdata voordat je de
nieuwe opstelling gaat beoordelen. Een gewone bronwissel vereist deze reset niet.
Bij ingeschakelde technische statistieken wordt alleen de aan/uit-stand van
passief leren gedeeld.


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

Het scherm toont 36 uur aan buitentemperatuur en wind. Wijs in de grafiek een uur
aan, gebruik de pijltjestoetsen of open **Bekijk uurwaarden**.

**Temperatuur in huis** geeft een voorlopig scenario voor 30, 60 en 180 minuten.
De berekening neemt de actuele kamertemperatuur als beginpunt, houdt de gemeten
warmteafgifte constant en gebruikt de verwachte buitentemperatuur. Wind is
extra informatie in de grafiek. Het scenario stuurt de regeling niet aan.

Na een herstart zijn actuele metingen, een geldige weersverwachting en een
actueel beoordeeld woningmodel nodig. De controller kan een opgeslagen schatting
herstellen. Het scenario wacht op een nieuwe geldige meetperiode van 30 minuten.
Bij een korte onderbreking kan het laatst ontvangen scenario tijdelijk zichtbaar
blijven. De waarden verdwijnen na 30 seconden zonder bevestiging of zodra hun
tijdstip is verstreken.

## Warmtevraag vergelijken

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
