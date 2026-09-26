# Bodemplaatverwarming per buitenunit

De instellingen voor bodemplaatverwarming staan in de [lokale web-app](web-app.md)
bij **Instellingen buitenunit**. OpenQuatt bepaalt per buitenunit welke regelmethoden
beschikbaar zijn. Bij een Duo worden HP1 en HP2 afzonderlijk beoordeeld.

| Buitenunit | Kiesbare regelmethoden | Standaard |
|---|---|---|
| V1 | 1: buitentemperatuur; 2: ontdooien + naloop | 1 |
| V1.5 | 1, 2 en 3: automatische combinatie | 3 |
| V2, oud en nieuw model | 1, 2 en 3 | 3 |
| Nog niet herkend | Geen wijziging mogelijk | Geen profiel toepassen |

## Opgeslagen referentieconfiguratie per generatie

De actuele dumps maken het verschil tussen de **opgeslagen configuratie** en een
tijdelijke runtimewaarde expliciet:

| Instelling | V1 | V1.5 | V2 |
|---|---:|---:|---:|
| Bodemplaatmodus | **1 — buitentemperatuur** | **3 — automatische combinatie** | **3 — automatische combinatie** |
| Inschakel-/bovengrens | **4 °C** | **4 °C** | **4 °C** |
| Hysterese / stopdelta | **3 K** | **3 K** | **3 K** |

Voor V1 is de opgeslagen modus dus **1**. Een eerder waargenomen V1-waarde
**2** kwam uit een tijdelijke runtime-override tijdens testen en was niet de
opgeslagen V1-configuratie. Die eerdere vergelijking mag daarom niet worden
gelezen als “V1 werkt standaard alleen tijdens ontdooien”.

OpenQuatt sluit hier al op aan: de generatie-default is voor V1 methode 1 en
voor V1.5/V2 methode 3, met 4 °C en 3 K als referentiewaarden.

**Regelmethode 3 is niet beschikbaar op V1.** OpenQuatt blokkeert deze keuze niet
alleen in de web-app, maar ook bij opslaan, backupherstel, automatisch opnieuw
toepassen en vlak vóór een Modbus-write.

Een bestaande waarde 3 op een V1 kan nog wel worden uitgelezen voor diagnose.
OpenQuatt behandelt die waarde daar niet als een geldige instelling en schrijft
hem niet opnieuw.

Regelmethode 0 betekent **uit**. Die waarde blijft herkenbaar bij uitlezen en in
bestaande geldige backups, maar wordt bewust niet als nieuwe dropdownkeuze
aangeboden.

## Wat doen de regelmethoden?

### Methode 1 — Buitentemperatuur

De bodemplaatverwarming schakelt in als de buitentemperatuur onder de ingestelde
**inschakelgrens** komt. Uitschakelen gebeurt pas wanneer de buitentemperatuur
weer voldoende is gestegen volgens de ingestelde **hysterese**.

Voorbeeld: bij een inschakelgrens van **4 °C** en een hysterese van **3 K** kan
de verwarming onder 4 °C inschakelen en weer uitschakelen nadat de
buitentemperatuur tot ongeveer 7 °C is gestegen.

De temperatuurregeling wordt niet direct bij compressorstart actief; er geldt
eerst een interne looptijdvoorwaarde. De temperatuurgrens alleen zegt dus niet
dat de bodemplaat op dat moment ook daadwerkelijk aan staat.

### Methode 2 — Ontdooien + naloop

De bodemplaatverwarming ondersteunt de buitenunit tijdens een defrost en blijft
daarna nog gedurende een interne nalooptijd actief. Een overgang naar stand-by
kan die naloop beëindigen.

Bij deze methode zijn geen temperatuurgrens of hysterese instelbaar.

### Methode 3 — Automatische vorst- en ontdooiregeling

Deze methode combineert temperatuurafhankelijke vorstbeveiliging met
ondersteuning tijdens ontdooien.

Bij normaal verwarmen wordt de bodemplaat rond het vriespunt gebruikt. Tijdens
een defrost kan zij ook boven het vriespunt actief zijn, tot de ingestelde
**bovengrens bij ontdooien**. Bij zeer lage buitentemperaturen gelden aanvullende
interne voorwaarden.

Methode 3 gebruikt niet dezelfde instelbare hysterese als methode 1 en is alleen
beschikbaar op V1.5 en V2.

## Een oude V1-instelling met methode 3

Een eerder opgeslagen V1-profiel met methode 3 wordt niet automatisch opnieuw
toegepast en ook niet stilzwijgend naar methode 1 of 2 omgezet.

Bij backupherstel wordt zo'n ongeldig V1-profiel overgeslagen. Geldige profielen
voor andere buitenunits kunnen wel gewoon worden hersteld.

Als een V1 momenteel waarde 3 meldt:

1. lees de actuele waarden uit;
2. kies bewust methode 1 of 2;
3. controleer de bijbehorende temperatuurinstellingen;
4. kies **Opslaan en toepassen**.

Alleen het openen van het scherm, een taalwissel of het wijzigen van een lokale
keuze schrijft niets naar de buitenunit.

## Opslaan en opnieuw toepassen

**Opslaan en toepassen** bewaart het gekozen profiel in OpenQuatt en schrijft de
waarden naar het werkgeheugen van de buitenunit. Daarna worden de waarden
teruggelezen om te controleren of ze zijn overgenomen.

De fysieke EEPROM van de buitenunit blijft daarbij ongewijzigd.

Met **Na herstart opnieuw toepassen** bewaart OpenQuatt het profiel en biedt het
na een herstart opnieuw aan dezelfde herkende buitenunit aan. Zonder die optie
valt de buitenunit na een herstart terug op zijn eigen opgeslagen waarden.
