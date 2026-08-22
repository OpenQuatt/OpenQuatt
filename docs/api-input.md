# API inputbronnen

OpenQuatt kan externe bronwaarden ontvangen via de lokale ESPHome webserver. Dit is bedoeld voor dezelfde soort dynamische invoer als de MQTT inputbronnen, maar zonder MQTT-broker.

Gebruik deze API alleen vanaf je eigen lokale netwerk en alleen voor waarden die je zelf actueel houdt. Een API-input telt pas mee nadat er een waarde is gezet en blijft daarna beperkt geldig. Na een herstart beginnen alle API-inputbronnen ongeldig totdat je opnieuw een waarde stuurt.

## Basis

De voorbeelden hieronder gebruiken de standaardnaam:

```text
http://openquatt.local
```

Lukt `.local` niet, gebruik dan het IP-adres van de controller:

```text
http://<ip-adres>
```

Als web-login aan staat, gebruik dan dezelfde gebruikersnaam en hetzelfde wachtwoord als voor de web-app. De webserver gebruikt lokale HTTP op poort 80.

## Endpoints

Temperatuur- en vermogenswaarden zijn `number`-entiteiten. Zet ze met een `POST` naar `/number/<naam>/set?value=<waarde>`.

Sommige `curl`-versies vereisen bij `POST` een expliciete `Content-Length`. Gebruik daarom `-H "Content-Length: 0"` in de voorbeelden hieronder.

| Signaal | Endpoint | Geldige waarde |
|---|---|---|
| Koelingsdauwpunt | `/number/api_input_cooling_dew_point/set?value=<temperatuur>` | `-20..35` graden Celsius |
| Buitentemperatuur | `/number/api_input_outside_temperature/set?value=<temperatuur>` | `-40..60` graden Celsius |
| Kamertemperatuur | `/number/api_input_room_temperature/set?value=<temperatuur>` | `0..50` graden Celsius |
| Kamer-setpoint | `/number/api_input_room_setpoint/set?value=<temperatuur>` | `5..35` graden Celsius |
| Warmtevraag | `/number/api_input_heat_demand/set?value=<vermogen>` | `0..15000` watt |

Toestemmingssignalen zijn `switch`-entiteiten. Zet ze met een `POST` naar `/turn_on` of `/turn_off`.

| Signaal | Aan | Uit |
|---|---|---|
| Warmtetoestemming | `/switch/api_input_heating_enable/turn_on` | `/switch/api_input_heating_enable/turn_off` |
| Koeltoestemming | `/switch/api_input_cooling_enable/turn_on` | `/switch/api_input_cooling_enable/turn_off` |

## Voorbeelden

Koelingsdauwpunt op `15.6` graden zetten:

```sh
curl -X POST -H "Content-Length: 0" "http://openquatt.local/number/api_input_cooling_dew_point/set?value=15.6"
```

Buitentemperatuur op `7.2` graden zetten:

```sh
curl -X POST -H "Content-Length: 0" "http://openquatt.local/number/api_input_outside_temperature/set?value=7.2"
```

Warmtetoestemming aanzetten:

```sh
curl -X POST -H "Content-Length: 0" "http://openquatt.local/switch/api_input_heating_enable/turn_on"
```

Koeltoestemming uitzetten:

```sh
curl -X POST -H "Content-Length: 0" "http://openquatt.local/switch/api_input_cooling_enable/turn_off"
```

Met web-login:

```sh
curl --digest -u "gebruikersnaam:wachtwoord" -X POST -H "Content-Length: 0" "http://openquatt.local/number/api_input_room_temperature/set?value=20.5"
```

## Geldigheid

Een API-inputbron wordt pas geldig nadat je de bijbehorende endpoint hebt aangeroepen. Template-startwaarden na boot tellen niet mee als echte input.

De geldigheidsduur is:

- koelingsdauwpunt: 15 minuten;
- buitentemperatuur: 30 minuten;
- kamertemperatuur: 10 minuten;
- kamer-setpoint: blijft geldig tot herstart of nieuwe waarde;
- warmtevraag: 15 minuten;
- warmtetoestemming: blijft geldig tot herstart of nieuwe waarde;
- koeltoestemming: blijft geldig tot herstart of nieuwe waarde.

Stuur temperatuurwaarden daarom periodiek opnieuw, bijvoorbeeld elke minuut of telkens wanneer de bronwaarde verandert. Waarden buiten de geldige range worden niet als bruikbare bron gebruikt.

## Bronselectie

Ga in de web-app naar **Instellingen -> Bronnen / integraties -> Sensorselectie**.

Bij `Koelingsdauwpunt` kies je:

- `Auto`: gebruik de hoogste geldige waarde van Home Assistant, API-invoer en MQTT;
- `API input`: vereis de API-invoerbron.

Bij `Buitentemperatuur` gebruikt `Auto` de laagste geldige waarde uit buitenunit, Home Assistant, API-invoer en MQTT. Bij `Kamertemperatuur` en `Kamer setpoint` kun je `API input` expliciet als bron kiezen.

Bij `Warmtevraag` is `Disabled` de standaard. Kies je `API input`, dan gebruikt `Power House` jouw waarde in plaats van de eigen vermogensschatting, zolang die geldig is. Verloopt de waarde, dan valt de regeling terug op het huismodel en niet op nul.

Bij `Warmtetoestemming` en `Koeltoestemming` telt API-invoer alleen mee als de waarde geldig is. Handmatige koeltoestemming blijft daarnaast een override.

## Verder lezen

- [MQTT inputbronnen](mqtt.md)
- [Web-app gebruiken](web-app.md)
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)
- [Verwarmen en koelen uitgelegd](verwarmen-en-koelen.md)
- [Power House](power-house.md)
