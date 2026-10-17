# Instellingen permanent bewaren in de buitenunit

**Experimenteel:** permanent schrijven is beperkt tot de V2 met control-board-item
`0x1037`, PCB-programma `0x0201` en een uitleesbaar serienummer. Het vereist een
werkelijke NVS-partitie van minimaal 64 KiB. De Q-uitbreiding staat in
[PR #847](https://github.com/OpenQuatt/OpenQuatt/pull/847). De module vergroot of
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
herhaalt de commit niet automatisch. Laat een onverwachte of corrupte inhoud
onderzoeken; een backup wordt nooit automatisch over een onbekende buitenunit
geschreven. Herstel vanuit een willekeurig geüpload EEPROM-bestand wordt niet
ondersteund.

Bij een opslagfout mag de UI geen geslaagde EEPROM-opslag melden. Herstart
OpenQuatt om de duurzaam opgeslagen opdracht opnieuw te laden en controleer het
resultaat. Voer tijdens een onafgeronde opdracht geen downgrade uit naar firmware
zonder deze EEPROM-blokkering. Een gewone instellingenbackup is geen backup van
dit hersteljournaal.
