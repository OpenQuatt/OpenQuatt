# Koelen: technische werking

Voor de dagelijkse bediening zie [Koelen](dagelijks/koelen.md). Deze naslag beschrijft voorwaarden, wachttijden en bronbewaking.


## Wat betekent koeling binnen OpenQuatt?

Koeling is niet simpelweg "verwarmen maar dan andersom". Bij koeling is vooral het risico op condens belangrijk.

Daarom werkt OpenQuatt bij koeling terughoudend:

- er moet echt een koelvraag zijn;
- de flow moet bruikbaar zijn;
- de minimale veilige watertemperatuur moet bewaakt worden;
- dauwpuntinformatie is normaal gesproken nodig.

Standaard gebruikt OpenQuatt de kamertemperatuur en het setpoint om vast te stellen of er echt koelvraag is. Een kleine marge voorkomt dat koeling steeds kort aan en uit schakelt rond het setpoint.

Bij koelvraag kijkt OpenQuatt vervolgens naar de watertemperatuur. De regeling start rustig, bouwt alleen op als dat nodig is en remt af of stopt wanneer de aanvoer dicht bij de veilige ondergrens komt.

Voor het opnieuw starten na een koelstop kun je kiezen tussen voldoende opwarming van het water en een vaste minimale uit-tijd. Die uit-tijd geldt bij Duo voor beide warmtepompen, zodat de tweede pomp niet direct de gestopte koelcyclus overneemt. Ook bij Single blijft de vaste minimale uit-tijd van de compressor (4 minuten) altijd gelden: OpenQuatt start pas wanneer alle relevante wachttijden en voorwaarden zijn vrijgegeven. De condens-, flow- en andere veiligheidsbewaking blijft altijd gelden.

### Koelen binnen een dagelijks tijdvenster

Wil je bijvoorbeeld alleen overdag koelen, zet dan onder **Instellingen → Koelen** het blok **Dagelijks koelvenster** aan en stel de start- en eindtijd in. Het tandwiel bij **Koeltoestemming** op het overzicht opent dezelfde instellingen. Inschakelen kiest intern `Schedule` als `Cooling Enable Source`; uitschakelen kiest `Disabled`. Het schema geeft alleen toestemming om te koelen. Standaard blijft `Cooling Room Request Required` aan en begint koeling dus pas als de kamertemperatuur daadwerkelijk om koeling vraagt. Zet je die instelling bewust uit, dan vormt een actief tijdvenster zelf de koelvraag. De dauwpunt-, water- en flowbeveiligingen en `OpenQuatt Enabled` blijven in beide gevallen leidend.

De starttijd hoort bij het venster, de eindtijd niet: `08:00-20:00` is actief vanaf 08:00 tot vlak voor 20:00. Een venster mag over middernacht lopen, bijvoorbeeld `20:00-07:00`. Zijn start en einde gelijk, dan staat het venster uit; de veilige standaard `00:00-00:00` activeert na een update dus niets onverwacht.

Het schema gebruikt de lokale klok van de controller. Na een herstart zonder geldige netwerktijd blijft de schematoestemming veilig uit. Zodra SNTP de tijd heeft gesynchroniseerd, loopt de lokale klok op de controller door en wordt het venster automatisch opnieuw beoordeeld.

Aan het einde van het venster trekt OpenQuatt de koeltoestemming gecontroleerd in. Een nog lopende minimale compressortijd kan de compressor kort na de eindtijd laten doorlopen; daarna kan de pomp voor de normale postflow actief blijven. Een harde veiligheidsingreep mag de minimale looptijd wel doorbreken.

Wil je de exacte koelinstellingen, marges en begrenzingen begrijpen of wijzigen? Gebruik dan de technische naslag [Instellingen en meetwaarden](instellingen-en-meetwaarden.md).

### Waarom is dauwpunt zo belangrijk?

Bij vloerkoeling of andere watergedragen koeling wil je voorkomen dat oppervlakken te koud worden en vocht uit de lucht erop condenseert.

Daarom kijkt OpenQuatt bij koeling niet alleen naar comfort, maar ook naar veiligheid:

- is de lucht in huis vochtig;
- wat is dan de veilige ondergrens voor de watertemperatuur;
- mag cooling op dit moment dus wel of niet vrijgegeven worden.

Een dauwpunt kan uit Home Assistant, API-invoer of MQTT komen. In de web-app kies je de bron. Bij `Auto` gebruikt OpenQuatt de hoogste geldige dauwpuntwaarde, omdat die voor koeling de veiligste ondergrens geeft. Voor Home Assistant geldt de centrale heartbeat (`sensor.openquatt_ha_ingress_heartbeat`): een constante waarde blijft bruikbaar zolang die heartbeat binnenkomt. Bij een verouderde of ontbrekende waarde valt OpenQuatt terug op een andere geldige bron of blokkeert het koelen. Zie [API inputbronnen](api-input.md) en [MQTT inputbronnen](mqtt.md) voor de technische geldigheidsduur.

### Wat doet `Manual Cooling Enable`?

Die schakelaar geeft extra handmatige toestemming en omzeilt daarmee de gekozen `Cooling Enable Source`, dus ook een gesloten of nog niet geldige `Schedule`. Met de standaardinstelling `Cooling Room Request Required` blijft nog steeds een normale koelvraag nodig. De schakelaar omzeilt nooit `OpenQuatt Enabled`, dauwpunt-, water- of flowbeveiligingen.

`Manual Cooling Enable` is geen automatisch aflopende override. De gebruikte herstelmodus `RESTORE_DEFAULT_OFF` betekent dat een opgeslagen stand na een herstart terugkomt; alleen zonder opgeslagen stand is de standaard uit. Zet de schakelaar daarom zelf weer uit wanneer de handmatige toestemming niet meer nodig is.

Kort gezegd:

- handmatig toestaan is niet hetzelfde als onbeperkt mogen koelen.


