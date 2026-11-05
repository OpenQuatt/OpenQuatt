# Leren

Open **Leren** om de meetvoortgang, de woninglijn en het model voor opwarmen en
afkoelen te bekijken. Op ondersteunde Heatpump Controller Q Single en Duo gebruik
je hier ook de schakelaar **Passief leren**. Het leren verandert je instellingen
niet. De gekozen stand blijft bewaard bij een herstart of firmware-update.

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

Na een geplande herstart kan de controller de dagmeting hervatten als er een geldig
meetcheckpoint is, de meetcontext gelijk blijft en de herstart binnen vijf minuten
valt. Als herstel wordt afgewezen, begint een nieuwe dagmeting. De lopende
30-minutenmeting begint bij een herstart opnieuw. Eerder opgeslagen leerdata
blijven beschikbaar. Zie voor de voorwaarden en bronafwijzingen het
[meetbron- en kwaliteitscontract](../development/power-house-learning-sources.md).

## Wanneer is er een geleerde woninglijn?

Open **Meetgegevens** voor de grafiek en de voorwaarden voor een woninglijn.
De balken tonen het aantal dagmetingen en de temperatuurspreiding. De huidige
richtwaarden zijn minstens negen geschikte dagmetingen, waarvan zes om te leren
en drie voor onafhankelijke controle, en minstens 6 °C spreiding tussen P10 en
P90 van de leerdagen. P10 en P90 zijn de grenzen rond het middelste deel van de
temperaturen. De controller kijkt ook naar de verdeling van de temperaturen,
meetkwaliteit en de uitkomst op de controledagen.

Als oudere dagen een onbekende meetkwaliteit hebben, tonen de balken het aantal
en de spreiding van alle opgeslagen dagen. Die waarden helpen je de meetgeschiedenis
te begrijpen. Dagen met onbekende meetkwaliteit leveren geen onafhankelijke controle.
Open **Meetgegevens** opnieuw om nieuw opgeslagen punten op te halen.

De blauwe lijn is de ingestelde woninglijn. De punten zijn metingen. Bij een
meetpunt zie je onder meer de meetduur, gemiddelde buitentemperatuur en het
gemiddelde warmtevermogen. Waar beschikbaar zie je ook hoeveel warmte- en
temperatuurmetingen geldig waren, de warmte-onzekerheid en het modelgewicht.
Een dag met een lager modelgewicht telt minder zwaar mee bij het schatten van de
lijn. Oude vieruursmetingen blijven herkenbaar in de grafiek en tellen niet mee
als dagmeting. Een groene lijn toont de geleerde woninglijn. Het gestippelde
deel loopt buiten het gemeten temperatuurbereik.

## Details en leerdata

Onder **Bronnen en technische details** vind je de bronstatus, watertemperaturen
en modelwaarden. Bij de woninglijn geeft `H` aan hoeveel extra vermogen per
graad kouder nodig is. `T₀` is de buitentemperatuur waarbij de geschatte
warmtevraag nul wordt. De regeling gebruikt die waarde niet als schakelgrens.

Via **Diagnostische leerdata downloaden** bewaar je een lokale JSON-export.
**Leerdata wissen** pauzeert het leren en wist de leerhistorie. Bij een fysieke
ombouw van Single naar Duo of omgekeerd wis je de leerdata voordat je de
nieuwe opstelling gaat beoordelen. Bij ingeschakelde technische statistieken
wordt alleen de aan/uit-stand van passief leren gedeeld.


## Vooruitblik

Met de optionele weerproef op een Heatpump Controller Q verschijnt naast **Leren**
het tabblad **Vooruitblik**. Vul een Nederlandse plaatsnaam, postcode of
coördinaten in en zet **Weersverwachting gebruiken** aan. De controller stuurt
de locatie naar Open-Meteo om de plaats en de KNMI-weersverwachting op te halen.
Locatie en toestemming blijven lokaal op de controller bewaard en staan buiten
de instellingenback-up.

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

Bij een goedgekeurde geleerde woninglijn toont **Warmtevraag vergelijken** hoeveel
basiswarmte Power House met de ingestelde en met de geleerde lijn zou vragen.
Beide berekeningen gebruiken hetzelfde rekenmoment, dezelfde metingen en
dezelfde toestand van de regeling. Het verschil is de geleerde vraag min de
ingestelde vraag, in watt.

De vergelijking verschijnt binnen het temperatuurbereik waarin de geleerde lijn
is getoetst. Tijdens bijvoorbeeld opwarmen, externe warmtevraag of service
laat het scherm zien waarom de waarden tijdelijk wegvallen. Starten, stoppen
en verdelen volgen daarna hun eigen regels. De vergelijking past de woninglijn
of de aansturing niet aan.
