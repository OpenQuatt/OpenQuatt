# OpenQuatt in Homey

De Homey-app verbindt Homey Pro rechtstreeks met OpenQuatt op je eigen netwerk. De controller wordt automatisch gevonden en stuurt zijn waarden live door over de lokale verbinding. Er is geen cloudaccount voor nodig en de Quatt-app blijft er buiten.

Homey is optioneel voor OpenQuatt zelf, net als Home Assistant. Gebruik het als je vanuit Homey wilt meekijken, wilt automatiseren, of OpenQuatt wilt voeden met sensoren die je al in Homey hebt.

De app is open source en heeft een eigen repository: [OpenQuatt/homey-openquatt](https://github.com/OpenQuatt/homey-openquatt).

## Wat je nodig hebt

- Homey Pro met firmware `12.3.0` of nieuwer. De app draait lokaal op Homey Pro en is niet beschikbaar voor Homey Cloud.
- OpenQuatt online op hetzelfde netwerk, met Quick Start afgerond.

## Installeren en koppelen

1. Installeer [OpenQuatt](https://homey.app/a/nl.openquatt/) uit de Homey App Store.
2. Voeg in Homey een apparaat toe en kies OpenQuatt. Homey vindt de controller via mDNS, dezelfde route waarmee `openquatt.local` werkt.
3. Blijft de lijst leeg, vul dan het adres van de controller in bij de apparaatinstellingen onder `Host / IP-adres (handmatig)`. Dat is ook de oplossing als mDNS in je netwerk niet doorkomt, bijvoorbeeld over een gast-VLAN of tussen twee subnetten.

Verandert het IP-adres later, dan volgt de app dat vanzelf zolang je het veld leeg laat.

## Wat je in Homey ziet

| Waarde | Toelichting |
|---|---|
| Aanvoer-, buiten- en kamertemperatuur | De waarden zoals de controller ze zelf geselecteerd heeft. |
| Kamer-setpoint en koelingsdauwpunt | Idem, dus inclusief de gekozen bron. |
| Vermogen, warmteafgifte en koelvermogen | Live uit de regeling. |
| COP en EER | Live rendement. |
| Flow | Waterdebiet in liter per uur. |
| Regelmodus | Als leesbaar label en als getal, zodat Homey er een grafiek van kan maken. |
| Warmte- en koeltoestemming | Of de regeling verwarmen en koelen op dit moment toestaat. |
| Hulprelais (R2) | Functie en status. |

Homey logt elke waarde in Inzichten, dus je kunt ze zonder verdere configuratie over de tijd terugkijken. Luchtvochtigheid ontbreekt, want OpenQuatt rekent met het dauwpunt en heeft zelf geen kamervochtigheid. Zet daarvoor je eigen vochtsensor in de grafiek.

Naast de losse waarden heeft de app een dashboardwidget met de status van de installatie in één oogopslag.

## Wat je kunt schakelen

Direct vanaf de apparaatpagina schakel je de OpenQuatt-regeling, de handmatige koelvrijgave en het hulprelais (R2). De functie van R2 kies je uit dezelfde vijf standen die de firmware kent.

## Flow-kaarten

Triggers voor het volgen van de installatie: de regelmodus is veranderd, verwarmen of koelen gestart en gestopt, ontdooien gestart en gestopt per warmtepomp, de CV-ketel in- en uitgeschakeld, stille modus aan en uit, een storing gedetecteerd of opgelost, en het dauwpunt beschikbaar gekomen of weggevallen.

Condities om een flow te laten afhangen van wat OpenQuatt doet: is aan het verwarmen, koelen of ontdooien, is er een storing actief, draait de CV-ketel, staat stille modus aan, is er een dauwpunt beschikbaar, is koelen toegestaan, en staat het hulprelais op een bepaalde functie.

Acties om bij te sturen: regelmodus forceren, stille-modus-override zetten, CV-ketel-assist aan of uit, de OpenQuatt-regeling aan of uit, handmatige koelvrijgave, en het hulprelais schakelen of van functie wisselen.

Daarnaast zijn er acties om de controller te voeden. Die staan hieronder.

## OpenQuatt voeden vanuit Homey

Sensoren die je al in Homey hebt, kun je als bron aan OpenQuatt doorgeven. Elk signaal heeft een eigen flow-kaart en gaat rechtstreeks naar de [API-invoer](api-input.md) van de controller, met [MQTT](mqtt.md) als terugvaloptie.

| Flow-kaart | Bereik | Blijft geldig |
|---|---|---|
| Stuur dauwpunt naar de controller | `-20..35` graden | tot je flow stopt met verversen |
| Stuur buitentemperatuur naar de controller | `-40..60` graden | tot je flow stopt met verversen |
| Stuur kamertemperatuur naar de controller | `0..50` graden | tot je flow stopt met verversen |
| Stuur kamer-setpoint naar de controller | `5..35` graden | tot je een nieuwe waarde stuurt |
| Zet warmtetoestemming | aan of uit | tot je een nieuwe waarde stuurt |
| Zet koeltoestemming | aan of uit | tot je een nieuwe waarde stuurt |

De app stuurt alles wat hij voedt elke minuut opnieuw, zodat de geldigheidsduur van de controller niet onder je flow vandaan verloopt. Blijft een meetwaarde langer weg dan `Maximale leeftijd sensorwaarde` (standaard 60 minuten), dan stopt de app met versturen en valt de controller terug op zijn eigen bron. Dat is met opzet: liever de eigen meting dan een kamertemperatuur van vanochtend.

Het setpoint en de twee toestemmingen zijn commando's in plaats van metingen. Die blijven staan tot je ze vervangt, en de app stuurt ze opnieuw na een herstart.

Zet aan de kant van de controller wel de juiste bron. Ga naar **Instellingen -> Bronnen / integraties -> Sensorselectie** in de web-app en sta `API input` toe voor het signaal dat je voedt. `Auto` dekt de meeste gevallen al.

> [!NOTE]
> `Zet koeltoestemming` is het normale toestemmingssignaal en weegt dus mee in de bronselectie. `Zet handmatige koelvrijgave` is iets anders: dat is de override, die koelen toestaat ongeacht de gekozen bron.

## Dauwpunt uit je eigen kamersensoren

OpenQuatt koelt alleen als het het dauwpunt binnen kent. Heb je een temperatuur- en vochtsensor per kamer, dan kan Homey het dauwpunt uitrekenen en doorgeven, met dezelfde Magnus-formule en hoogste-kamer-wint-logica als het dynamische koelpakket voor Home Assistant.

Maak per gekoelde kamer een flow: *als* de temperatuur of luchtvochtigheid verandert, *dan* `Werk het dauwpunt van [kamer] bij met temperatuur en luchtvochtigheid`, met de tags van je sensor. De app aggregeert de kamers, laat oude waarden vervallen en stuurt de hoogste elke minuut opnieuw.

Heb je al een berekend dauwpunt, gebruik dan de kaart `Stuur dauwpunt naar de controller`.

## MQTT als terugvaloptie

Draait er nog firmware zonder API-invoer, dan werkt MQTT. Zet **MQTT inputbronnen** aan onder **Instellingen -> Bronnen / integraties**, wijs een broker op je netwerk aan en vul dezelfde broker in bij de apparaatinstellingen in Homey, onder `Externe waarden - MQTT-fallback`.

Heb je geen broker, dan kan Homey Pro er zelf een zijn met de app [MQTT Server](https://homey.app/a/net.weejewel.mqttserver/). Die accepteert geen anonieme verbindingen, dus vul aan beide kanten de gebruikersnaam en het wachtwoord uit de app-instellingen in.

De app probeert altijd eerst de API-invoer en gebruikt MQTT alleen als dat niet lukt. Je hoeft dus niets om te zetten als je later naar nieuwere firmware gaat.

## Als het niet werkt

- Het apparaat is niet beschikbaar in Homey: controleer of `http://openquatt.local` nog opent. Lukt dat wel en Homey niet, vul dan het IP-adres handmatig in bij de apparaatinstellingen.
- Een gevoede waarde komt niet aan: kijk in de web-app onder **Instellingen -> Bronnen / integraties -> Sensorselectie** of het signaal `API input` als bron mag gebruiken.
- De hulprelais-functie blijft leeg: die bestaat alleen op firmware met R2-ondersteuning.

Blijft het onduidelijk, kijk dan verder bij [Problemen oplossen](problemen-oplossen.md).

## Verder lezen

- [API inputbronnen](api-input.md)
- [MQTT inputbronnen](mqtt.md)
- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Web-app gebruiken](web-app.md)
- [Problemen oplossen](problemen-oplossen.md)
