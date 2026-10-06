# Bronnen en integraties

Een bron levert een waarde aan OpenQuatt, bijvoorbeeld de kamertemperatuur. Het inschakelen van een verbinding en het kiezen van die verbinding als bron zijn twee afzonderlijke stappen.

## Een bron instellen en controleren

1. Open **Instellingen -> Bronnen / integraties** en stel de benodigde verbinding in.
2. Open **Sensorselectie** en kies de bron voor het betreffende signaal.
3. Controleer of de getoonde waarde geldig en aannemelijk is. De gekozen waarde is wat de regeling daadwerkelijk gebruikt.
4. Verander één bron tegelijk. Zie je een terugval naar een andere bron, controleer dan de verbinding en de geldigheid van de aangeleverde waarde.

`Auto` kiest volgens de regels van het betreffende signaal; het betekent niet dat altijd dezelfde integratie voorrang heeft. Een dauwpuntbron gebruikt bijvoorbeeld de hoogste geldige waarde, terwijl buitentemperatuur de laagste geldige waarde gebruikt. Zie [Instellingen en meetwaarden](../instellingen-en-meetwaarden.md#5-bronselectie) voor de bronregels.

## Bronnen / integraties

Hier beheer je de directe gegevensbronnen en integraties:

- `OpenTherm`: zet de lokale OpenTherm-thermostaatkoppeling aan of uit;
- `CiC JSON-feed inlezen`: haalt gegevens uit de CiC op via je lokale netwerk; open `Adres aanpassen` onder deze schakelaar om het feed-adres in te stellen;
- `MQTT inputbronnen`: configureer een broker voor externe MQTT-bronwaarden zoals dauwpunt, buiten- en kamerwaarden, het aanvoertarget en toestemmingssignalen, en zet ongebruikte topics uit;
- `API inputbronnen`: lever dezelfde externe bronwaarden via lokale HTTP-endpoints aan;
- `Quatt-app via CiC`: geeft alleen buitenunitgegevens via de Modbusverbinding op M2 door aan de CiC, zodat de Quatt-app kan meekijken.

## CiC: kies de functie die je echt nodig hebt

De CiC is de originele Quatt-controller. Je kunt hem op twee manieren blijven gebruiken:

| Als je dit wilt | Schakel in | Wat gebeurt er? | Niet nodig voor |
|---|---|---|---|
| CiC-waarden als bron gebruiken | `CiC JSON-feed inlezen` | OpenQuatt leest de lokale JSON-feed van de CiC. Daaruit kunnen onder meer setpoint, kamerwaarden, aanvoertemperatuur en flow beschikbaar komen. | De Quatt-app behouden. |
| Buitenunitgegevens in de Quatt-app blijven bekijken | `Quatt-app via CiC` | OpenQuatt geeft via Modbus op M2 alleen buitenunitgegevens door aan de CiC. Thermostaatgegevens gaan niet mee. | CiC-waarden als bron gebruiken. |

Je kunt één functie inschakelen, beide combineren, of beide uit laten. Gebruik je geen CiC meer, laat beide schakelaars uit.

Voor **CiC JSON-feed inlezen** open je **Adres aanpassen** en vul je het lokale feed-adres van je CiC in, bijvoorbeeld `http://<ip-adres>:<poort>/beta/feed/data.json`. Zet deze schakelaar alleen aan als je ook werkelijk één of meer CiC-bronnen kiest onder **Sensorselectie**. De infoknop naast iedere verbinding geeft extra uitleg.

Voor **Quatt-app via CiC** verbind je `M2` met een aparte RS485-kabel met de vrijgekomen Modbuspoort van de CiC. Dit is alleen beschikbaar op de Heatpump Controller Q. Deze Modbusverbinding geeft uitsluitend buitenunitgegevens door; thermostaatgegevens zoals kamertemperatuur en kamer-setpoint gaan niet naar de CiC. OpenQuatt blijft de warmtepomp regelen; besturingscommando's via deze M2-koppeling worden niet overgenomen. De CiC heeft zijn eigen voeding en netwerkverbinding nodig om gegevens aan Quatt door te geven. Deze functie heette eerder **CiC-compatibiliteit**. Zie voor de aansluiting [Q-edition aansluiten](../q-edition.md#welke-kabel-gaat-waarheen).

## Optionele externe regeldoelen

Onder `Sensorselectie` in dezelfde groep kies je per signaal welke bron OpenQuatt gebruikt. Naast de kaarten voor buiten-, kamer- en aanvoerwaarden staat daar `Externe warmtevraag (Power House)`: een optionele externe vermogensvraag, alleen voor de Power House-strategie, standaard op `Niet gebruiken`. Zet je die op Home Assistant of API-invoer, dan vervangt jouw waarde uitsluitend de vermogensschatting van het huismodel; de kaart laat zien of Power House die externe waarde daadwerkelijk gebruikt of is teruggevallen op het model. Zie [Power House](../power-house.md).

Daarnaast staat er `Aanvoertarget (stooklijn)`: een optionele externe aanvoertemperatuur, alleen voor de stooklijnregeling, standaard op `Stooklijn`. Zet je die op OpenTherm-thermostaat, Home Assistant, API-invoer of MQTT, dan vervangt jouw waarde uitsluitend het berekende stooklijntarget; de kaart laat zien of de regeling dat externe target daadwerkelijk gebruikt of is teruggevallen op de stooklijn. Zie [Water Temperature Control](../water-temperature-control.md#extern-aanvoertarget-optioneel).

## Warmtetoestemming

Voor `Warmtetoestemming` (`Heating Enable Source`) betekent `Niet gebruiken`: geen externe toestemmingsvoorwaarde; de strategie bepaalt zelf of warmte nodig is. Tijdens Quick Start vervangt een strategieswitch deze keuze automatisch door `Niet gebruiken` voor `Power House`, of door de gekoppelde en actieve thermostaatbron voor `Water Temperature Control`. Buiten Quick Start toont `Instellingen → Verwarmen` alleen een advies met knop en wordt de instelling niet stil overschreven. Afwijkende combinaties (zone-regeling, volledig weersafhankelijk) blijven mogelijk. De buitentemperatuur staat normaliter op `Auto` en gebruikt de buitenunit.

Dezelfde groep toont compacte diagnostiek voor OpenTherm en CIC, zoals linkstatus, JSON-feedstatus, kamertemperatuur, setpoint, flow en waterdruk voor zover de aangesloten apparaten deze waarden leveren.

Laat dit met rust zolang OpenQuatt logisch werkt. Verander liever een instelling per keer en kijk daarna wat het systeem doet.


## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
