# Herstelpagina gebruiken

De herstelpagina is een afzonderlijke pagina op de controller voor als je niet meer kunt inloggen, Home Assistant opnieuw wilt koppelen of Wi-Fi wilt herstellen. Je hoeft hiervoor het bestaande wachtwoord van de OpenQuatt-webinterface niet te weten. Je opent de herstelacties met de fysieke knop op de controller.

## Herstelpagina openen

1. Houd op de Heatpump Controller Q de **linker van de twee knoppen 5 seconden** vast en laat hem los. Houd hem niet tot 10 seconden vast: daarmee wis je direct de Wi-Fi-gegevens.
2. Open `http://openquatt.local/recovery` of `http://<IP-adres>/recovery`. Gebruik bij een aangepaste apparaatnaam de bijbehorende hostnaam.
3. Kies de herstelactie die bij je probleem past. Het herstelvenster blijft maximaal **10 minuten** open.

Zonder actief herstelvenster toont de pagina hoe je de fysieke knop moet bedienen. Het openen van de pagina of het activeren van het venster wist op zichzelf geen instellingen. Zit de knop bij opstarten al ingedrukt, laat hem dan eerst los voordat je deze stappen uitvoert.

| Probleem | Actie op de herstelpagina | Gevolg |
| --- | --- | --- |
| Gebruikersnaam of wachtwoord van de webinterface vergeten | [Web-login herstellen](#web-login-herstellen) | Sla een nieuwe gebruikersnaam en wachtwoord op; die gelden zodra je herstel afsluit of het venster verloopt. |
| Home Assistant kan niet koppelen door een onbekende of afwijkende API-sleutel | [API-beveiliging resetten](#api-beveiliging-resetten) | Wis alleen de ESPHome API-sleutel en herstart; koppel daarna binnen 10 minuten opnieuw. |
| Opgeslagen Wi-Fi-gegevens werken niet meer | [Wi-Fi opnieuw instellen](#wi-fi-opnieuw-instellen) | Wis alleen de Wi-Fi-gegevens en herstart; stel daarna je netwerk opnieuw in via het OpenQuatt access point. |

**Is de controller helemaal niet bereikbaar?** Dan kun je de herstelpagina nog niet openen. Voor Wi-Fi-herstel kun je de linker knop **10 seconden** vasthouden, loslaten en daarna verbinden met het OpenQuatt access point. Volg [Wi-Fi opnieuw instellen](#wi-fi-opnieuw-instellen). Gebruik je Ethernet, controleer dan ook de kabel en het IP-adres. De huidige Q-firmware ondersteunt daarnaast Wi-Fi-herstel; oudere firmware met alleen Ethernet heeft geen access point.

## Herstel afsluiten

Tijdens herstel is de gewone webinterface afgeschermd. Verschijnt daar een browser-inlogvenster, annuleer het en open rechtstreeks `/recovery`.

Kies **Herstel afsluiten** om terug te gaan naar de normale webinterface, of wacht tot het herstelvenster verloopt. Open daarna de gewone controllerpagina zonder `/recovery`. Na een API- of Wi-Fi-reset via de herstelpagina komt het herstelvenster na de herstart nog één keer terug; sluit het ook dan af zodra je klaar bent.

> [!NOTE]
> Het herstelvenster en het Home Assistant-koppelvenster duren allebei 10 minuten, maar zijn verschillende functies. Het herstelvenster geeft toegang tot de herstelacties. Na een API-reset begint bij de herstart het venster om Home Assistant opnieuw te koppelen. Het Wi-Fi-instelvenster blijft beschikbaar totdat nieuwe Wi-Fi-gegevens werken en zijn opgeslagen.

Gebruik de herstelpagina op een vertrouwd lokaal netwerk: tijdens het fysiek geopende venster kunnen ook andere apparaten op dat netwerk de beperkte herstelacties uitvoeren. Deze gerichte acties zijn geen factory reset; je hoeft hiervoor niet alle OpenQuatt-instellingen te wissen.

## Web-login herstellen

Op de HeatPump Controller Q edition is de herstelknop de **linker van de twee knoppen**.

Houd de fysieke herstelknop **5 seconden** vast en laat hem los. Open daarna
`http://openquatt.local/recovery` of `http://<IP-adres>/recovery`.
Gebruik bij een aangepaste apparaatnaam de bijbehorende hostnaam, bijvoorbeeld
`http://openquatt-test.local/recovery` voor een testcontroller.
De herstelpagina is zonder bestaande web-login bereikbaar, maar herstelacties
worden pas beschikbaar nadat je de fysieke knop hebt bediend. Zonder actief
herstelvenster toont de pagina de instructie om de knop in te drukken.
Je hebt 10 minuten om een nieuwe gebruikersnaam en wachtwoord op te slaan.
Sluit herstel daarna af; pas dan, of na afloop van het venster, geldt de nieuwe login.
Bij een opslagfout blijft de bestaande runtime-login behouden en kun je opnieuw proberen.

De gewone webinterface, REST-acties en webstreams zijn tijdens herstel afgeschermd.
Een nog geopende gewone webpagina kan daardoor een browser-inlogvenster tonen.
Annuleer dat venster en open rechtstreeks `/recovery`; de gewone login geeft
tijdens herstel geen toegang tot de normale webinterface.
Herstel opent geen algemene onbeveiligde beheeromgeving en wist geen andere instellingen.
Iedereen op hetzelfde netwerk kan tijdens het fysiek geopende venster de beperkte
herstelpagina gebruiken: voer dit alleen op een vertrouwd netwerk uit.
Een knop die tijdens boot al ingedrukt is moet eerst worden losgelaten.
Met **Herstel afsluiten**, of automatisch na 10 minuten, sluit het herstelvenster.
Open daarna de gewone webinterface zonder `/recovery`. Heb je geen nieuwe login
opgeslagen, dan blijft de eerdere web-login of open toegang gelden.

## API-beveiliging resetten

Gebruik deze reset als Home Assistant niet meer kan verbinden doordat de opgeslagen API-sleutel onbekend is of niet overeenkomt. Hiermee wis je alleen de sleutel voor de ESPHome-verbinding. Je Wi-Fi-instellingen, overige instellingen en de gebruikersnaam en het wachtwoord van de webinterface blijven behouden.

**Via de herstelpagina**

1. Houd op de Heatpump Controller Q de linker knop **5 seconden** ingedrukt en laat hem los. Daarmee open je het herstelvenster van 10 minuten.
2. Open `http://openquatt.local/recovery` of `http://<IP-adres>/recovery`. Hiervoor hoef je niet in te loggen op de webinterface.
3. Kies **API-beveiliging resetten** en bevestig het wissen van de sleutel en het herstarten van de controller.
4. Wacht tot de controller opnieuw bereikbaar is. De herstelpagina wordt na deze herstart nog één keer beschikbaar. Sluit het herstel af wanneer je klaar bent.
5. Rond het opnieuw koppelen in Home Assistant binnen 10 minuten na de herstart af.

**Via de normale instellingenpagina**

Heb je een gebruikersnaam en wachtwoord ingesteld voor de OpenQuatt-webinterface en ben je daarmee ingelogd? Dan kun je ook **API-beveiliging resetten** kiezen onder **Instellingen -> Systeem -> Toegang & Beveiliging**. Bevestig de reset en rond na de herstart het opnieuw koppelen binnen 10 minuten af. Deze route opent de herstelpagina niet.

Is de webinterface zonder wachtwoord toegankelijk, of ben je het wachtwoord vergeten? Gebruik dan de fysieke herstelroute hierboven. Alleen toegang tot een onbeveiligde webinterface is niet voldoende om de API-sleutel te wissen.

**Opnieuw koppelen in Home Assistant**

De reset verbreekt alle ESPHome API-verbindingen. Home Assistant kan daarna automatisch een sleutel instellen; dat kan dezelfde sleutel zijn als voorheen.

Bij een bestaande koppeling kan Home Assistant melden dat het apparaat transportencryptie heeft uitgeschakeld en vragen de oude sleutel te verwijderen. Bevestig dit alleen als je zelf deze reset hebt gestart en het juiste apparaat wordt genoemd. Controleer na het koppelen in **Instellingen -> Systeem -> Toegang & Beveiliging** dat API-encryptie weer actief is.

Meldt OpenQuatt dat het wissen niet is gelukt? De controller herstart dan niet automatisch. Controleer de melding voordat je opnieuw probeert. Voor verdere koppelproblemen zie [OpenQuatt koppelen aan Home Assistant](../dashboard/koppelen.md#problemen-met-koppelen).

## Wi-Fi opnieuw instellen

Kies met een ingestelde web-login **Connectiviteit → Wi-Fi wissen en herstarten**,
of gebruik de fysieke herstelpagina na 5 seconden indrukken. Zonder browser kan
het ook: houd de herstelknop **10 seconden** vast. Laat hem bij 5 seconden los
als je alleen web-login/API wilt herstellen. Een knop die bij opstart al vastzit
moet eerst losgelaten worden.

De reset wist alleen opgeslagen Wi-Fi-gegevens en fast-connect metadata.
Web-login, API-beveiliging, de verbindingsvoorkeur en overige instellingen blijven
behouden. Na succesvolle opslag herstart de controller. Verbind met het OpenQuatt
access point en stel Wi-Fi in; de AP blijft beschikbaar totdat de nieuwe gegevens
werken én zijn opgeslagen, ook bij Ethernetvoorkeur en na een stroomonderbreking.
Dit instelvenster staat los van het 10 minuten durende API-koppelvenster.
Targets zonder Wi-Fi tonen deze actie niet.

Na het wissen van Wi-Fi:

1. Verbind je telefoon of laptop met het Wi-Fi-netwerk **OpenQuatt**, wachtwoord **`openquatt`**.
2. Kies zo nodig om verbonden te blijven als je apparaat meldt dat dit netwerk geen internet heeft.
3. Open het instelscherm dat verschijnt, of ga naar **`http://192.168.4.1`** terwijl je met dit netwerk verbonden bent.
4. Kies je eigen Wi-Fi-netwerk en voer daar het bijbehorende wachtwoord in.
5. Verbind je telefoon of laptop weer met je eigen netwerk. Open de controller via zijn hostnaam of het IP-adres in je router; dit adres kan veranderd zijn.

Bij verkeerde Wi-Fi-gegevens blijft het instelnetwerk beschikbaar om ze te corrigeren.
Een reset vanuit fysieke recovery opent na de herstart opnieuw een herstelvenster;
gebruik `/recovery` om dit af te sluiten of wacht tot het verloopt. Een reset vanuit
normaal beheer opent dit venster niet. Controleer na terugkeer ook of de bestaande
Home Assistant-koppeling weer werkt. Wi-Fi wissen is geen factory reset.

Wijzigingen aan beveiliging kunnen een herstart nodig hebben. Bewaar nieuwe gegevens goed, want Home Assistant moet dezelfde API-sleutel gebruiken als API-encryptie actief is.


## Verder

- [Overzicht van de web-app](../web-app.md)
- [Problemen oplossen](../problemen-oplossen.md)
