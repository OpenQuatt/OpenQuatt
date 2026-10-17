# Instellingen permanent bewaren in de buitenunit

**Experimenteel:** permanent schrijven ondersteunt de vier bekende uitvoeringen:

| Buitenunit | Control-board-item / PCB-programma | Frequentietabellen | Backupbinding |
|---|---|---|---|
| V1 | `0x0037 / 0x0119` | F0–F10 | HP-aansluiting en model/firmware |
| V1.5 | `0x0E37 / 0x011E` | F0–F10 | HP-aansluiting en model/firmware |
| V2 old | `0x0E37 / 0x0122` | F0–F10 | Inclusief serienummer |
| V2 new | `0x1037 / 0x0201` | F0–F20 | Inclusief serienummer |

Andere firmwarecombinaties worden geweigerd. Het vereist een werkelijke
NVS-partitie van minimaal 64 KiB. De Q-uitbreiding uit
[PR #847](https://github.com/OpenQuatt/OpenQuatt/pull/847) is opgenomen in `dev`.
De EEPROM-module vergroot of
migreert zelf geen partities. De hardwarevrijgave staat nog open; zie het
[ontwikkel- en testplan](development/odu-eeprom-opslag.md).

Onder **Instellingen → Service → Permanent bewaren in buitenunit** kun je per HP
bodemplaatinstellingen, de ontdooimethode en de frequentietabellen voor verwarmen
en koelen opslaan. De bestaande knoppen **Opslaan en toepassen** blijven hun
eigen tijdelijke ODU-writes en eventuele OpenQuatt-profielopslag gebruiken.

EEPROM-opslag blijft na spanningsuitval bestaan zonder dat OpenQuatt de waarden
opnieuw hoeft toe te passen. Een latere fabrikantupdate van de EEPROM kan deze
instellingen vervangen. Er wordt nooit automatisch opnieuw naar EEPROM geschreven.

## Eerst de oorspronkelijke backup

1. Breng de installatie veilig naar stand-by. Bij Duo moeten beide compressoren
   stilstaan. Actieve defrost, incidenten, andere servicetaken en onbekende of
   verouderde terugmeldingen blokkeren de voorbereiding.
2. Kies bij de bedoelde HP **Oorspronkelijke backup voorbereiden**. OpenQuatt
   bewaart de opdracht en blokkeert de normale aansturing van beide HP's.
   Automatisch opnieuw toepassen van bodemplaat- en defrostprofielen voor de
   bedoelde HP wordt duurzaam ingetrokken. Veiligheidsstops blijven mogelijk.
3. Maak uitsluitend de bedoelde buitenunit volledig spanningsloos en schakel haar
   daarna weer in. Wacht totdat OpenQuatt de identiteit en verse telemetrie weer
   heeft vastgesteld. Bevestig de uitgevoerde spanningsonderbreking en kies
   **Uitlezen na spanningsonderbreking**.
4. Alleen een volledige, geldige uitlezing van dezelfde buitenunit wordt als
   oorspronkelijke backup bewaard. Download deze backup ook naar je computer.
   Hij bevat identiteit, de oorspronkelijke 512 bytes en de oorspronkelijke CRC.

V1 en V1.5 leveren geen bruikbaar serienummer. De UI meldt daarom expliciet dat
de backup bij de betreffende HP-aansluiting en model-/firmware-identiteit hoort.
Verplaats of vervang de buitenunit niet zolang je deze backup gebruikt: twee
gelijke oudere units zijn hiermee niet uniek van elkaar te onderscheiden.

De gewone EEPROM-export leest het **runtime-shadowgeheugen**. Na tijdelijke
wijzigingen is die export geen bewezen kopie van de oorspronkelijke EEPROM.
Daarom vraagt deze functie eerst een gecontroleerde spanningsonderbreking,
terwijl hertoepassing geblokkeerd blijft. Alleen een communicatieonderbreking
of een herstart van OpenQuatt is onvoldoende. OpenQuatt kan het verschil niet
zelf fysiek vaststellen; jouw bevestiging van de echte spanningsonderbreking
maakt deel uit van de controle.

## Wijzigingen opslaan

Pas gewenste waarden eerst met de bestaande editors tijdelijk toe. De
frequentietabellen moeten monotoon zijn en binnen het frequentiebereik van de
oorspronkelijke buitenunit blijven. Extra defrostdrempels en -tijden zijn geen
schrijfbare instellingen in deze functie.

Selecteer daarna de groepen die je permanent wilt bewaren en kies
**Wijzigingen controleren**. De tabel toont per register de laatst bevestigde
opgeslagen waarde, de actieve runtimewaarde en de doelwaarde. Ongeselecteerde
tijdelijke wijzigingen worden naar hun getoonde opgeslagen waarde teruggezet.
Onbekende verschillen buiten de toegestane instellingen blokkeren schrijven.

Bevestig alle getoonde wijzigingen en kies **Permanent opslaan**. De backend leest
de identiteit, veiligheidsstatus en broninhoud opnieuw. Verouderde voorbereidingen
worden geweigerd. Er volgt één overdracht van het volledige EEPROM-beeld, één
afzonderlijke commit en teruglezing. Een ontbrekend antwoord veroorzaakt geen
automatische herhaling.

Een bevestigde Modbus-write of overeenkomende runtime-CRC bewijst nog geen
permanente opslag. De aansturing blijft geblokkeerd. Voer na de bevestigde
schrijfopdracht opnieuw de echte spanningsonderbreking en uitlezing uit. Alleen
als de volledige inhoud daarna overeenkomt, verschijnt **Permanent behoud
bevestigd**. Blijkt de vorige opgeslagen inhoud teruggekomen, dan meldt de UI
dat de wijziging niet permanent is opgeslagen.

## Herstel en onderbrekingen

Een voorbereiding zonder ODU-writes kan worden geannuleerd. De optie
**Oorspronkelijke backup voorbereiden** bij een bestaande backup maakt een nieuwe
controleerbare schrijfvoorbereiding om die oorspronkelijke waarden terug te
zetten; ook dat vereist de afzonderlijke bevestiging en herstartcontrole.

Bij een onzekere of gedeeltelijk uitgevoerde schrijfopdracht blijft de normale
aansturing geblokkeerd, ook na een herstart van OpenQuatt. OpenQuatt hervat of
herhaalt de commit niet automatisch. **Houd de buitenunit onder spanning** zolang
de UI niet heeft vastgesteld dat de writer is gestopt. Kies **Schrijfstatus
controleren**; deze actie leest uitsluitend de identiteit en schrijfstatus en
herhaalt geen write. Een foutstatus kan optreden terwijl verdere EEPROM-chunks
nog worden geschreven. Alleen status `4` (afgerond) of `7` (CRC geweigerd vóór
schrijven), of een in dezelfde controllersessie bewezen nog niet gestarte writer,
geven daarna de instructie voor een spanningsonderbreking. Status `0` of `6`
bewijst na een onzekere opdracht geen stilstaande writer. Na een herstart van
OpenQuatt wordt die kennis niet verondersteld. Als de buitenunit al werkelijk
spanningsloos is geweest, blijft uitlezen met disconnectbewijs en expliciete
bevestiging mogelijk. Laat een onverwachte of corrupte inhoud
onderzoeken; een backup wordt nooit automatisch over een onbekende buitenunit
geschreven. Herstel vanuit een willekeurig geüpload EEPROM-bestand wordt niet
ondersteund.

Bij een opslagfout mag de UI geen geslaagde EEPROM-opslag melden. Herstart
OpenQuatt om de duurzaam opgeslagen opdracht opnieuw te laden en controleer het
resultaat. Voer tijdens een onafgeronde opdracht geen downgrade uit naar firmware
zonder deze EEPROM-blokkering. Een gewone instellingenbackup is geen backup van
dit hersteljournaal. De resetknop en de automatische fabrieksreset na snelle
powercycles zijn tijdens een onafgeronde opdracht geblokkeerd.
