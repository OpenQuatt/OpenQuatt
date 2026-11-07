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
Een plausibele lijn kan vanaf zes bruikbare leerdagen alvast als **voorlopige
woninglijn** verschijnen. De groene lijn is dan volledig gestippeld en beperkt
tot het gemeten temperatuurbereik van die leerdagen. De uitleg vermeldt welke
controles nog ontbreken. Deze schatting stuurt de regeling niet aan.

De balken tonen het aantal dagmetingen en de temperatuurspreiding. De huidige
richtwaarden zijn minstens negen geschikte dagmetingen, waarvan zes om te leren
en drie voor onafhankelijke controle, en minstens 6 °C spreiding tussen P10 en
P90 van de leerdagen. P10 en P90 zijn de grenzen rond het middelste deel van de
temperaturen. De controller kijkt ook naar de verdeling van de temperaturen,
meetkwaliteit en de uitkomst op de controledagen.

De firmware reserveert de nieuwste drie geschikte dagen met bekende meetkwaliteit
voor onafhankelijke controle. Ze tellen niet mee bij het berekenen van de lijn
of de spreiding van de leerdagen. Oudere dagen met onbekende meetkwaliteit mogen
met een lager gewicht helpen bij het leren.
Op oudere firmware zonder voortgang van de fit tonen de balken bij onbekende
meetkwaliteit het aantal en de spreiding van alle opgeslagen dagen.
Dagen met onbekende meetkwaliteit leveren geen onafhankelijke controle.
Open **Meetgegevens** opnieuw om nieuw opgeslagen punten op te halen.

De blauwe lijn is de ingestelde woninglijn. De punten zijn metingen. Bij een
meetpunt zie je onder meer de meetduur, gemiddelde buitentemperatuur en het
gemiddelde warmtevermogen. Waar beschikbaar zie je ook hoeveel warmte- en
temperatuurmetingen geldig waren, de warmte-onzekerheid en het modelgewicht.
Een dag met een lager modelgewicht telt minder zwaar mee bij het schatten van de
lijn. Oude vieruursmetingen blijven herkenbaar in de grafiek en tellen niet mee
als dagmeting. Na goedkeuring is de groene lijn binnen het gemeten bereik
doorgetrokken. Alleen bij die goedgekeurde lijn loopt het gestippelde deel ook
buiten het gemeten temperatuurbereik.

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
