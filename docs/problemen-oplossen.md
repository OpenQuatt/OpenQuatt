# Problemen oplossen

Deze pagina helpt je rustig zoeken als OpenQuatt niet zichtbaar is, waarden vreemd lijken of het systeem anders reageert dan verwacht. De hoofdregel: eerst kijken welke informatie OpenQuatt gebruikt, pas daarna instellingen aanpassen.

## Eerst dit controleren

Controleer in deze volgorde:

1. OpenQuatt is online.
2. De web-app opent via `openquatt.local` of via het IP-adres.
3. De warmtepompgegevens worden bijgewerkt.
4. De gekozen bronwaarden lijken logisch.
5. Flow en aanvoertemperatuur verversen.
6. Er is geen handmatige override actief.

Als een stap niet klopt, los die eerst op voordat je naar tuning kijkt.

Gebruik je Home Assistant? Controleer dan daarnaast:

1. Home Assistant ziet het OpenQuatt-apparaat en de entiteiten ontvangen waarden.
2. Je gebruikt de dashboardvariant die bij `Single` of `Duo` past.

## OpenQuatt is niet bereikbaar

Probeer dit:

- controleer of het apparaat stroom heeft;
- kijk in je router of OpenQuatt een IP-adres heeft;
- open de web-app via het IP-adres in plaats van `openquatt.local`;
- controleer of je computer of telefoon op hetzelfde netwerk zit;
- herstart de OpenQuatt-module.

Net na het flashen kan OpenQuatt ook nog op het fallback access point zitten:

- SSID: `OpenQuatt`
- wachtwoord: `openquatt`

## Home Assistant ziet OpenQuatt niet

Controleer eerst of de web-app wel bereikbaar is. Als de web-app werkt maar Home Assistant niets ziet:

- controleer of Home Assistant op hetzelfde netwerk zit;
- voeg de ESPHome-integratie handmatig toe met het IP-adres;
- controleer of API-encryptie in Home Assistant overeenkomt met de web-app;
- herstart Home Assistant of herlaad de ESPHome-integratie.

## Dashboardkaarten melden ontbrekende entiteiten

Controleer een ontbrekende OpenQuatt-entiteit via **Instellingen -> Apparaten & diensten -> Entiteiten**. Begint de `entity_id` met een area, zoals `sensor.zolder_openquatt_flow`, dan is die area waarschijnlijk geselecteerd tijdens de eerste toevoeging in Home Assistant 2026.6 of nieuwer. Het dashboard verwacht `sensor.openquatt_flow`.

Hernoem de betrokken entity-ID's en verwijder alleen de area-prefix. De area mag toegewezen blijven, omdat Home Assistant bestaande entity-ID's niet opnieuw wijzigt wanneer je een area later aanpast. Zie [Dashboard installeren](dashboard/README.md#area-was-al-geselecteerd) voor de volledige herstelroute.

## Ik zie geen warmtepompgegevens

Ga niet afstellen zolang basisdata ontbreekt.

Controleer:

- RS485-bekabeling;
- gekozen hardwareprofiel;
- `Single` of `Duo`;
- voeding en massa;
- of de gebruikte module past bij de installatie.

Gebruik je een Heatpump Controller Q-edition? Maak voor een supportverzoek een scherpe foto van de verbinding tussen de HCQ en de warmtepomp. Zorg dat `M1`, de klemmarkeringen `GND/A/B`, de adervolgorde en de aangesloten Modbuskabel zichtbaar zijn. Maak niets los om de foto te nemen.

Als de warmtepompdata ontbreekt, kunnen dashboard en regeling niet betrouwbaar verklaren wat er gebeurt.

## Waarden lijken niet logisch

Kijk eerst naar de gekozen bronnen. In het Home Assistant-dashboard heet dat vaak `Sensorconfiguratie`; in de web-app zie je de gebruikte waarden vooral in `Overzicht` en `Instellingen`.

Controleer vooral:

- buitentemperatuur;
- kamertemperatuur;
- setpoint;
- aanvoertemperatuur;
- flow.

De waarde met `Gekozen` of `Selected` is de waarde die OpenQuatt echt gebruikt. Als die niet klopt, reageert OpenQuatt logisch op verkeerde informatie.

## Quick Start selecteert automatisch OpenTherm

Controleer onder **Instellingen -> Installatie** hoe de CV-ketel fysiek is
aangesloten. Wanneer de veilige opstartcontrole tijdens Quick Start een
OpenTherm-ketel vindt, kiest OpenQuatt automatisch `OpenTherm (OTB)` en toont
het daarvan een melding. Is de onboarding al afgerond, dan verandert OpenQuatt
de opgeslagen keuze niet automatisch en blijft R1 uit totdat je de aansluiting
handmatig corrigeert.

## Het huis wordt niet warm genoeg

Controleer eerst:

1. klopt het setpoint;
2. klopt de gemeten kamertemperatuur;
3. klopt de buitentemperatuur;
4. is er voldoende flow;
5. is de maximale watertemperatuur niet te laag;
6. is stille modus of een maximum level actief.

Pas daarna heeft het zin om naar `Power House`, stooklijn of PID-instellingen te kijken.

## Het wordt te warm

Te warm gedrag komt vaak door bronwaarden of te agressieve instellingen.

Controleer:

- kamerwaarde en setpoint;
- of de gekozen verwarmingsstrategie past bij je verwachting;
- of `Power House temperature reaction` niet te sterk staat;
- of de stooklijn niet te hoog staat;
- of het systeem niet nog in minimum runtime zit.

Wijzig daarna hooguit een instelling tegelijk.

## Het systeem schakelt onrustig

Onrust komt meestal door een combinatie van bronwaarden, flow, minimum runtime en strategie.

Kijk eerst:

- wisselt de flow sterk;
- wisselt de warmtevraag snel;
- klopt de geselecteerde kamertemperatuur;
- is er kort geleden veel aangepast;
- staat `CM Override` op `Auto`.

Draai bij twijfel terug naar de laatste instelling waarbij het systeem logisch werkte.

## Koeling blijft geblokkeerd

Controleer:

- is er echt een koelvraag;
- welke `Cooling Enable Source` is gekozen;
- bij `Schedule`: ligt de lokale tijd binnen het venster en zijn start en einde niet gelijk;
- bij een recente herstart: heeft de controller al via SNTP een geldige netwerktijd gekregen;
- staat `Manual Cooling Enable` bewust goed;
- is flow beschikbaar;
- is dauwpuntinformatie beschikbaar;
- blokkeert de veilige minimale watertemperatuur;
- staat de koelbeveiliging bewust op `Dauwpuntmeting vereist`, `Dauwpuntsbenadering` of `Expliciet toestaan`.

Koeling is bewust terughoudend. Zonder goede dauwpuntinformatie kan blokkeren precies het veilige gedrag zijn.

Een schema met gelijke start- en eindtijd staat bewust uit. De starttijd is inbegrepen en de eindtijd niet; een begintijd na de eindtijd betekent een geldig venster over middernacht. Na een offline herstart blijft `Schedule` veilig ongeldig totdat de tijd is gesynchroniseerd. Daarna beoordeelt de controller het venster automatisch opnieuw met zijn lokale klok.

Loopt een compressor kort door na de ingestelde eindtijd, dan kan dat de nog geldige minimale compressortijd zijn. Is de compressor al uit maar draait de pomp nog, dan kan dat de normale postflow zijn. Blijft koeling onverwacht toegestaan, controleer dan ook `Manual Cooling Enable`: deze override wordt opgeslagen en kan na een herstart terugkomen. Hij omzeilt alleen de toestemmingsbron, nooit `OpenQuatt Enabled` of de dauwpunt-, water- en flowbeveiligingen.

Gebruik je dauwpuntbronnen uit Home Assistant, volg dan de actuele
[handleiding voor dynamische koelbronnen](https://github.com/OpenQuatt/home-assistant-openquatt/blob/main/docs/cooling.md).

Gebruik je MQTT voor het dauwpunt, controleer dan ook:

- staan MQTT inputbronnen aan in de web-app;
- is de broker verbonden;
- publiceer je op het topic dat de web-app bij **MQTT sensoren** toont;
- is de payload een geldige Celsius-waarde, zoals `15.6` of `{"value":15.6}`;
- komt er minstens elke 15 minuten een nieuwe waarde binnen.

Zie [MQTT inputbronnen](mqtt.md) voor topic en payload.

## Firmware-update lijkt mislukt

- laat de web-app open zolang er nog voortgang zichtbaar is en onderbreek de voeding dan niet;
- wacht enkele minuten op de herstart en probeer daarna `openquatt.local` en het bekende IP-adres;
- controleer in je router of OpenQuatt na de herstart een ander IP-adres kreeg;
- opent de web-app weer, controleer dan onder **Instellingen -> Systeem -> Updates** welke versie actief is;
- blijft de controller onbereikbaar, sluit hem via USB aan en gebruik de normale installer of [Handmatige installatie](handmatige-installatie.md) als herstelroute.

Start een update pas opnieuw als de controller weer stabiel bereikbaar is.

## Wanneer niets veranderen?

Verander bij voorkeur niets als:

- de woning comfortabel is;
- OpenQuatt rustig draait;
- de bronwaarden logisch zijn;
- je alleen een korte piek of dip ziet;
- je niet weet welke instelling bij het probleem hoort.

Te veel wijzigen maakt diagnose vaak moeilijker.

## Als je toch iets aanpast

Gebruik deze werkwijze:

1. schrijf de oude waarde op;
2. wijzig een instelling;
3. wacht lang genoeg;
4. kijk naar comfort, rust en energie;
5. draai terug als het slechter wordt.

Begin bijna altijd met bronkeuze en flow. Strategie-instellingen komen daarna pas.

## Waar meld ik wat?

| Situatie | Juiste route |
|---|---|
| Gebruiksvraag of hulp bij diagnose | Vraag het in het [OpenQuatt Discord-kanaal](https://discord.com/channels/1176602554885492786/1464174190788874427). |
| Reproduceerbare fout in OpenQuatt | Meld die als [GitHub-issue](https://github.com/OpenQuatt/OpenQuatt/issues/new/choose). |
| Fysiek probleem met de warmtepomp, lekkage of een mogelijk onveilige situatie | Stop en neem contact op met een vakbekwaam installateur of Quatt. |

Twijfel je wat er gebeurt? Verander dan geen nieuwe instellingen, noteer de actuele toestand en verzamel eerst de informatie hieronder.

Vermeld bij een hulpvraag of bugmelding:

- OpenQuatt-versie en hardwareprofiel;
- Quatt-generatie, `Single` of `Duo`, en Wi-Fi of Ethernet;
- wat je verwachtte en wat er daadwerkelijk gebeurde;
- het tijdstip en de stappen om het probleem te herhalen;
- relevante screenshots uit `Diagnose` of `Beslislog` en recente wijzigingen;
- bij een reproduceerbaar probleem: een [debugopname uit de web-app](web-app.md#debugopname-voor-support);
- bij een ontbrekende warmtepompverbinding met de HCQ: een scherpe foto van `M1` en de Modbusverbinding met de warmtepomp.

Deel nooit Wi-Fi-wachtwoorden, API-sleutels of andere geheimen.

## Verder lezen

- [Web-app gebruiken](web-app.md)
- [Dashboard gebruiken](dashboardoverzicht.md)
- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)
