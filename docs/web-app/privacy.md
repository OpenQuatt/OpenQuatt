# Gegevens delen en privacy

Je kiest afzonderlijk of je gebruiksstatistieken en prestatiemetingen deelt. OpenQuatt bedien je lokaal; voor deze keuzes heb je geen cloudaccount nodig.

## Wat kun je delen?

| Keuze | Wat wordt gedeeld? | Standaard bij een nieuwe Quick Start |
| --- | --- | --- |
| Gebruiksstatistieken | Technische systeemstatus, firmwareversie, gekozen functies en communicatiebetrouwbaarheid. Bij een echte firmwarecrash kan ook een technisch crashrapport worden verstuurd. | Aan; je kunt dit vóór afronden uitzetten. |
| Prestatiemetingen | Stabiele verwarmingsmetingen voor validatie van het prestatiemodel. | Uit; je kunt dit zelf aanzetten. |

Het uurbericht met gebruiksstatistieken bevat geen wachtwoorden, Wi-Fi-netwerknaam, lokale IP-adressen, ingestelde temperaturen of gewone logregels. De installatie krijgt een willekeurig ID om berichten van dezelfde installatie te herkennen. De loggingserver ziet technisch het bron-IP-adres van een verbinding; OpenQuatt slaat dit IP-adres niet op.

Deze opsomming gaat over het uurbericht. Voor de inhoud en verwerking van crashrapporten zie [Retained crashtelemetrie](../crash-telemetry.md). Prestatiemetingen zijn een afzonderlijke keuze en bevatten juist wel verwarmingsmetingen.

## Delen aan- of uitzetten

1. Open **Instellingen → Systeem** in de web-app.
2. Open **Gebruiksstatistieken** of **Prestatiemetingen**, afhankelijk van de keuze die je wilt wijzigen.
3. Bekijk de toelichting en, bij gebruiksstatistieken, **Welke gegevens worden gedeeld?**.
4. Zet de gewenste keuze aan of uit. Controleer beide keuzes als je helemaal geen van deze gegevens wilt delen.

Tijdens een nieuwe Quick Start kun je deze keuzes al maken voordat je afrondt. Gebruiksstatistieken worden pas na afronden verzonden. Bestaande installaties krijgen zonder opgeslagen keuze geen automatische toestemming; na migratie van de eerste telemetryversie kan opnieuw inschakelen nodig zijn.

## Eenmalige uitnodiging voor prestatiedata

Bestaande gebruikers krijgen bij het openen van de web-app een uitnodiging om
warmtepompprestaties te delen voor het verbeteren van de vermogens- en COP-modellen.
De popup verschijnt alleen als delen uitstaat, de controllerstatus bekend is en de
uitnodiging nog niet is afgehandeld. Wie al deelt, krijgt geen popup. Tijdens Quick
Start, updates, herverbinden of een andere dialog wordt de vraag niet getoond.

- **Inschakelen — ik help mee** zet delen aan en onthoudt je keuze.
- **Niet inschakelen** laat delen uit en voorkomt dat de vraag terugkomt.
- **Later**, het kruisje, Escape en klikken buiten de popup stellen de vraag uit
  voor deze tabsessie, ook na herladen. Bij een nieuwe tabsessie kan de vraag terugkomen.

Een definitieve keuze wordt op de controller bewaard en geldt voor alle browsers,
ook na herstart en OTA-update. De popup sluit pas na bevestiging van de opgeslagen
keuze. Uitstellen schrijft niets naar de controller. Als browseropslag niet beschikbaar
is, geldt het uitstel tot het herladen van de pagina. Onder **Welke gegevens worden
gedeeld?** vind je uitleg over de metingen, het verzendinterval en het bron-IP-adres.
Je kunt delen later altijd wijzigen via **Instellingen → Systeem**.

## Wat gebeurt er daarna?

Bij ingeschakelde gebruiksstatistieken verstuurt OpenQuatt na afronden vrijwel direct en daarna ongeveer ieder uur een bericht. Uitzetten stopt nieuwe uurberichten; er wordt geen wachtrij voor later opgebouwd. Uitzetten verwijdert niet automatisch eerder ontvangen berichten uit de loggingserver. Het installatie-ID blijft bewaard wanneer je delen uitzet of de firmware bijwerkt.

Bij ingeschakelde prestatiemetingen worden maximaal 15 complete minuutrecords per bericht iedere 15 minuten vanaf de controllerstart verstuurd. De planning volgt de verstreken tijd sinds opstarten, niet de kwartieren van de klok.

### Zo controleer je dat het gelukt is

Open de gekozen instelling opnieuw en controleer de getoonde aan/uit-stand. Controleer gebruiksstatistieken en prestatiemetingen afzonderlijk. Een lege waarde in de gegevenspreview betekent niet dat delen uitstaat: sommige gegevens zijn alleen bij de echte verzending beschikbaar.

## Technische details

Zie [Gegevens delen: technische naslag](../gegevens-delen-technisch.md) voor de exacte velden, het installatie-ID, migratie en verzendgedrag van het uurbericht. Zie [MQTT inputbronnen](../mqtt.md) voor het verschil tussen deze centrale verzending en je eigen MQTT-koppeling.

De popupstatus gebruikt één bit in het bestaande 8-byte NVS-record van de
prestatiedatakeuze, zonder extra NVS-key. Eén interne statusentiteit maakt dit
uitleesbaar. Bij upgrade blijft de bestaande toestemming behouden; bestaande
uit-keuzes krijgen de uitnodiging eenmaal. Een fabrieksreset wist de status.
Downgrade naar firmware met de oude opslagindeling zet delen uit en wist de keuze
en popupstatus; na opnieuw upgraden kan de uitnodiging terugkomen.
