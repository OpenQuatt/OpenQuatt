# E-mailmeldingen

OpenQuatt kan je een e-mail sturen als er iets aan de hand is met de installatie,
bijvoorbeeld een storing van de warmtepomp of een ketel die de verwarming overneemt.
De functie staat standaard uit. Zonder volledig ingestelde maildienst verstuurt
OpenQuatt niets.

De controller verstuurt de mail zelf via een dienst die je kiest. Je hebt dus geen
Home Assistant of andere server nodig.

## Instellen

Open de web-app en ga naar **Instellingen → Bronnen / integraties → E-mailmeldingen**. Kies
**Instellen** en vul in:

1. de maildienst;
2. de gegevens die bij die dienst horen (zie hieronder);
3. het afzenderadres;
4. de ontvangers: één veld, meerdere adressen gescheiden door een komma (maximaal 5);
5. welke meldingen je wilt ontvangen;
6. of je ook een mail wilt als een melding weer is opgelost.

Sla op en gebruik daarna **Testmail sturen**. De testmail gebruikt de opgeslagen
instellingen, ook als meldingen nog uit staan. Zet meldingen pas aan als de testmail
binnenkomt.

### Maildiensten

| Dienst | Wat je nodig hebt |
|---|---|
| SMTP | Servernaam, poort (standaard 465), gebruikersnaam en wachtwoord. Alleen SSL/TLS vanaf het begin van de verbinding (poort 465) wordt ondersteund; STARTTLS op poort 587 niet. |
| Postmark | Server API-token en een bevestigd afzenderadres of domein. |
| Mailgun (EU of VS) | Verzenddomein en API-sleutel. Kies de regio waarin het domein bij Mailgun is aangemaakt. |
| Lettermint | Project-API-token (geen team-token) en een geverifieerd afzenderdomein. |
| Resend | API-sleutel en een geverifieerd afzenderdomein. |
| Brevo | API-sleutel en een bevestigd afzenderadres. |

Bij Gmail en andere diensten met tweestapsverificatie heb je voor SMTP meestal een
apart app-wachtwoord nodig. Microsoft 365 en Outlook.com accepteren alleen STARTTLS
op poort 587 en werken daarom niet via SMTP; gebruik daar een van de API-diensten.

De afzender verschijnt als `OpenQuatt <afzenderadres>`. Gebruik een adres dat de
gekozen dienst voor jou mag versturen, anders weigert de dienst de mail.

## Welke meldingen?

| Melding | Wanneer | Standaard |
|---|---|---|
| Storing warmtepomp | De buitenunit meldt een storing, of OpenQuatt kan de resetstatus niet opslaan. | aan |
| Beveiliging of waarschuwing warmtepomp | De buitenunit begrenst het vermogen of blokkeert tijdelijk een start. | uit |
| Warmtepomp niet bereikbaar | De verbinding met de buitenunit is bevestigd weg. | aan |
| Start of stop niet bevestigd | Een start of stop van de compressor wordt niet bevestigd. | aan |
| Ketel neemt verwarming over | De CV-ketel neemt de verwarming over omdat de warmtepomp niet beschikbaar is. | aan |
| Vorstbeveiliging actief | De vorstbeveiliging start. | uit |
| Veel compressorstarts | De waarschuwing voor te veel compressorstarts (pendelen) gaat af. | uit |
| Onverwachte herstart van de controller | De controller is herstart na een crash, watchdog of spanningsdip. | aan |

Met **Ook mailen als het is opgelost** krijg je een tweede mail zodra een storing,
verbindingsverlies, ketelovername of vorstbeveiliging voorbij is. Voor veel
compressorstarts en een herstart bestaat geen herstelmelding. Een storing die pas
na een bevestigde powercycle van de buitenunit vrijkomt, krijgt ook geen
herstelmelding: de warmtepomp blijft geblokkeerd tot je die powercycle in de
web-app bevestigt.

OpenQuatt mailt niet over:

- een vergrendelde storing die na een herstart uit het geheugen wordt teruggezet;
  die is al eerder gemeld;
- de voorverwarming bij een eerste start;
- informatieve statusbits van de buitenunit.

Een mail bevat het tijdstip, de warmtepomp, de melding zelf en, als dat nodig is,
de opmerking dat herstel een bevestigde powercycle van de buitenunit vraagt.
Onderaan staat het adres van de web-app. De mailtekst is altijd Nederlands.

## Verzendgedrag

- **Bundelen:** meldingen binnen een minuut na de eerste komen in één mail. Een
  storing met de ketelovername die erop volgt, levert zo één mail op. Eén mail
  bevat maximaal 12 regels; daarna volgt "en nog N meldingen".
- **Daglimiet:** hoogstens 24 meldingsmails per 24 uur, ook over herstarts heen.
  Zo levert een controller die steeds opnieuw opstart geen stroom aan mails op.
  Daarna wachten nieuwe meldingen tot er weer ruimte is; ze komen dan samen in
  één mail. Testmails tellen niet mee.
- **Klok:** meldingsmails gaan pas weg als de controller de tijd kent (via
  internet of Home Assistant). Dat is meestal binnen een minuut na het opstarten.
- **Opnieuw proberen:** lukt verzenden niet, dan probeert OpenQuatt het na 1, 2, 4,
  8 en 16 minuten opnieuw, maximaal zes keer. Weigert de dienst de mail
  (bijvoorbeeld een verkeerde sleutel of een niet-bevestigde afzender), dan stopt
  OpenQuatt meteen; nog een poging lost dat niet op.
- **Geen netwerk:** zonder netwerkverbinding wacht de mail. Meldingen die nog niet
  verstuurd zijn, gaan verloren bij een herstart of firmware-update.
- **Firmware-updates en servicetaken:** zolang OpenQuatt het uitlezen pauzeert,
  bijvoorbeeld tijdens een update, verstuurt het niets. Een lopende verzending
  wordt afgebroken en daarna opnieuw geprobeerd.

In **Instellingen → Bronnen / integraties → E-mailmeldingen** zie je de laatste verzending,
hoeveel meldingen nog wachten en hoeveel mails er de afgelopen 24 uur zijn verstuurd.

## Privacy en beveiliging

- De mail gaat via de gekozen dienst naar je ontvangers. Die dienst ziet de
  inhoud: de apparaatnaam, de meldingen met tijdstip en het lokale adres van de
  web-app. Er worden geen meetreeksen, wifi-gegevens of andere instellingen
  meegestuurd.
- De verbinding met de dienst is versleuteld met TLS. OpenQuatt controleert het
  certificaat van de server met de ingebouwde certificatenbundel.
- Het wachtwoord of de API-sleutel staat, net als het MQTT-wachtwoord, onversleuteld
  in het flashgeheugen van de controller. De web-app toont het nooit terug; je kunt
  het alleen vervangen of wissen. Gebruik bij voorkeur een sleutel die alleen mag
  versturen.
- Bij een andere dienst kiezen wist OpenQuatt het opgeslagen wachtwoord of de
  sleutel, zodat een sleutel nooit naar een andere dienst gaat.
- De e-mailinstellingen zitten niet in de backup van de web-app. Stel ze na een
  herstel opnieuw in.

## Problemen oplossen

| Melding in de web-app | Wat je kunt doen |
|---|---|
| Sleutel geweigerd (HTTP 401 of 403) | Controleer de API-sleutel of het token. Bij Lettermint werkt alleen een project-token. |
| Afzender of ontvanger geweigerd (HTTP 422) | Bevestig het afzenderadres of verifieer het domein bij de dienst. |
| Gebruikersnaam of wachtwoord geweigerd (SMTP) | Controleer de inloggegevens; gebruik bij Gmail een app-wachtwoord. |
| Geen beveiligde verbinding | Controleer de servernaam en of de server SSL/TLS op poort 465 aanbiedt. |
| Servernaam niet gevonden | Controleer de servernaam en de DNS-instellingen van je netwerk. |
| Tijdslimiet bereikt | De dienst reageerde niet binnen 20 seconden. OpenQuatt probeert het later opnieuw. |

De ESPHome-log toont per verzending het resultaat onder `openquatt.mail`, met de
foutcode van de dienst.

## Technische details

- Configuratie: één NVS-record van 764 bytes in de namespace `oq_mail`. Na opslaan
  leest OpenQuatt het record terug om het te controleren. De daglimiet staat in een
  tweede record van 108 bytes, dat na iedere verstuurde meldingsmail wordt
  bijgewerkt (hoogstens 24 keer per dag).
- HTTP-endpoints (met de normale web-authenticatie en CSRF-token):
  `GET /openquatt/mail/status`, `POST /openquatt/mail/save` en
  `POST /openquatt/mail/test`.
- De bron van de meldingen is de [beslislog](web-app.md). De e-mailfunctie leest die
  alleen en verandert niets aan de regeling.
- Verzenden gebeurt in een aparte taak, die pas bij de eerste opslag of mail wordt
  gestart. De buffers staan in PSRAM. Mail deelt met de weersverwachting en
  firmware-updates één HTTPS-verbinding tegelijk; een verzending duurt hoogstens 20
  seconden.
