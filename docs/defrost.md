# Ontdooien

De buitenunit bepaalt wanneer een ontdooicyclus begint en eindigt. In OpenQuatt kun je de cyclus per buitenunit volgen, een ondersteunde methode kiezen of een extra ontdooicyclus aanvragen.

## Ontdooimethoden

- [Modus 0 met wisselende wachttijd](#modus-0-met-wisselende-wachttijd)
- [Modus 1 met 45 minuten draaitijd](#modus-1-met-45-minuten-draaitijd)
- [Modus 3 met eerdere metingen](#modus-3-met-eerdere-metingen)
- [Modus 4 met temperatuurverschil](#modus-4-met-temperatuurverschil)

Modus 2 is gereserveerd en ontbreekt in de methodekeuze.

## Modus 0 met wisselende wachttijd

De buitenunit begint met een interval van 50 minuten compressordraaitijd en kan dit aanpassen binnen een bereik van 45 tot 120 minuten. De teller loopt op terwijl de compressor draait. Hoelang het ontdooien zelf duurde bepaalt het volgende interval. Duurt een ontdooibeurt bijvoorbeeld 2 minuten, dan gaat een interval van 50 naar 70 minuten.

| Duur van de vorige ontdooibeurt | Volgende wachttijd |
| --- | --- |
| 1 s t/m 3 min | 20 minuten erbij, maximaal 120 minuten |
| 3 min 1 s t/m 4 min 59 s | Blijft gelijk |
| 5 min t/m 7 min 59 s | 10 minuten eraf, minimaal 45 minuten |
| Vanaf 8 min | Terug naar 45 minuten |

Bij ongeveer −3 tot +3 °C buiten kan een vaste wachttijd van 45 minuten voorrang krijgen, afhankelijk van de instelling. Voor de volgende ontdooibeurt moet ook de verdamper koud genoeg zijn. OpenQuatt kan de actuele verdampergrens uitlezen en schat hoelang de verdamper lokaal aan die voorwaarde voldoet. De buitenunit houdt de gekozen wachttijd zelf bij.

Bij droog winterweer kan een langere wachttijd onnodige onderbrekingen voorkomen. Trekt er mist op rond het vriespunt, dan kan rijp ineens sneller aangroeien en kan de gekozen wachttijd te lang zijn. Ook de beschikbare warmte uit het cv-water beïnvloedt hoelang het ontdooien duurt.

## Modus 1 met 45 minuten draaitijd

Voor een gewone ontdooiing moet de compressor in totaal 45 minuten hebben gedraaid. De verdamper moet ook lang genoeg onder zijn temperatuurgrens blijven. Staat de compressor tussendoor stil, dan pauzeert de teller. Met 30 minuten draaien, 10 minuten stilstand en nog 15 minuten draaien bereikt de unit na 55 minuten kloktijd zijn 45 minuten draaitijd.

Neem een temperatuurgrens van −3 °C. Bij −2 °C blijft de buitenunit verwarmen. Zakt de verdamper naar −4 °C en blijft hij lang genoeg onder −3 °C, dan kan de buitenunit ontdooien. OpenQuatt gebruikt ongeveer 3 minuten als schatting voor de tijd onder de grens. De draaitijdteller loopt op tot 50 minuten. Ook daarna kan de buitenunit blijven verwarmen zolang de verdamper te warm is.

De eerste ontdooiing kan vóór 45 minuten beginnen als de verdamper lang genoeg kouder dan −7 °C blijft en de compressor minstens 5 minuten heeft gedraaid. Bij mist kan rijp de luchtstroom al beperken vóór de gewone draaitijd is bereikt. Bij droge vorst kan de verdamper lang koud blijven terwijl er weinig rijp zit.

## Modus 3 met eerdere metingen

Draait de ventilator vrijwel op het gevraagde toerental, dan kan de buitenunit 60 metingen van de omvormer verzamelen. Hij vergelijkt het gemiddelde met een eerder opgeslagen waarde voor dezelfde ventilatorstand. In de uitgelezen profielen telt een afwijking van minstens 400 naar boven of beneden mee bij een ontdooiaanvraag. Dat getal hoort bij de interne schaal van het signaal. Welke fysieke grootheid de waarde weergeeft, is onbekend.

In die profielen schakelt de buitenunit de vergelijking in bij 8 °C buiten of kouder en uit bij 14 °C of warmer. Tussen die grenzen houdt hij zijn vorige stand vast. Bij 11 °C kan de vergelijking dus actief zijn als het eerder 6 °C was, of uitstaan als het eerder 15 °C was. De buitenunit houdt ook bij hoelang de verdamper onder zijn temperatuurgrens blijft. Voor een ontdooiaanvraag moet de compressor minstens 5 minuten hebben gedraaid.

Op een vochtige ochtend rond het vriespunt kan rijp aangroeien terwijl de ventilator op dezelfde snelheid blijft draaien. De buitenunit kan in die periode veranderingen in de omvormerwaarde meewegen. Wisselt de ventilator vaak van stand, dan kan het langer duren voordat er genoeg vergelijkbare metingen zijn. OpenQuatt kan het toerental en de verdampertemperatuur uitlezen. De referentiewaarde blijft in de buitenunit.

## Modus 4 met temperatuurverschil

Tijdens het verwarmen neemt het verdampende koudemiddel warmte op uit de buitenlucht. De buitenunit trekt de verdampingstemperatuur (`Tevap`) af van de buitenluchttemperatuur (`Ta`). Dat verschil telt mee bij de beslissing om te ontdooien.

Bij +3 °C buiten en een Tevap van −6 °C is het verschil 9 K. Daalt Tevap naar −9 °C terwijl het buiten +3 °C blijft, dan loopt het verschil op naar 12 K. Daarmee wordt de verschilgrens voor +3 °C in beide profielen hieronder bereikt.

### Temperatuurgrenzen bij modus 4

Onder +9 °C buiten kiest de buitenunit de verschilgrens op basis van de buitentemperatuur. De tabel toont het minimale verschil uit twee uitgelezen buitenunits, één V1.5 en één V2. De instellingen op jouw buitenunit kunnen afwijken.

| Buitenlucht (Ta) | Quatt V1.5 | Quatt V2 |
| --- | ---: | ---: |
| 0 tot onder +9 °C | 12 K | 12 K |
| −5 tot onder 0 °C | 11 K | 11 K |
| −10 tot onder −5 °C | 11 K | 10 K |
| −15 tot onder −10 °C | 10 K | 10 K |
| −20 tot onder −15 °C | 9 K | 9 K |
| −23 tot onder −20 °C | 7 K | 6 K |
| Onder −23 °C | 6 K | 5 K |

Bij −8 °C buiten ligt de grens in deze profielen op 11 K voor de V1.5 en 10 K voor de V2. Tevap moet dan bij de V1.5 −19 °C of kouder zijn en bij de V2 −18 °C of kouder.

Van +9 °C tot onder +15 °C buiten geldt in deze twee profielen een vaste grens voor Tevap zelf. Tevap moet dan −4 °C of kouder zijn. Vanaf +15 °C geldt −1 °C of kouder. Bij Tevap onder ongeveer −29,4 °C kan een aparte temperatuurvoorwaarde gelden.

### Tijdsvoorwaarden bij modus 4

In de beschreven instellingen geldt gewoonlijk een tijdgrens van 45 minuten en moet de compressor minstens 5 minuten hebben gedraaid. Rond +7 °C buiten of kouder kan voor de eerste ontdooibeurt 25 minuten gelden. De teller voor de tijdgrens loopt tijdens compressorbedrijf op tot maximaal 180 minuten. De buitenunit controleert ook hoelang het temperatuurverschil groot genoeg blijft. Na het bereiken van de tijdgrens kan hij blijven verwarmen zolang de temperatuurvoorwaarden nog uitblijven.

Bij sommige buitenunits kan de tijdgrens ook na de eerste ontdooibeurt korter worden als het temperatuurverschil bij koud weer lang genoeg groot blijft. Bij lang compressorbedrijf kan tot ongeveer +12 °C buiten een aparte temperatuurvoorwaarde meespelen.

Bij vochtig weer rond het vriespunt kan rijp de warmteopname bemoeilijken. Een oplopend temperatuurverschil kan daarop wijzen. Een harder draaiende compressor of minder lucht langs de lamellen kan het verschil ook vergroten. Bij droge vorst kunnen beide temperaturen juist laag zijn terwijl het verschil klein blijft. Bij −6 °C buiten en een Tevap van −12 °C is dat bijvoorbeeld 6 K.

## V1 tegenover V1.5

Handmatig ontdooien en defrostmethode 4 zijn twee verschillende zaken. De handmatige actie vraagt één extra cyclus aan; die blijft ook voor V1 beschikbaar wanneer alle normale veiligheidsvoorwaarden kloppen. De methodekeuze bepaalt daarentegen hoe de buitenunit normale ontdooicycli kwalificeert. De beschikbare methoden verschillen per buitenunitvariant.

De V1.5 heeft aanvullende mode-4-instellingen voor het verschil tussen buitentemperatuur en verdampingstemperatuur. Die uitbreiding ontbreekt op V1. Ook wanneer de actieve methode op mode 0 staat, kan V1.5 eigen instellingen voor mode 4 hebben. Mode 4 op V1.5 gebruikt het eigen V1.5-profiel; er hoeft en mag geen V2-profiel naar de V1.5 worden gekopieerd.

Ook beide ondersteunde V2-varianten gebruiken de mode-4-instellingen van de aangesloten buitenunit. OpenQuatt leest de actuele waarden uit wanneer je de instellingen opvraagt.

## Bediening

Open **Instellingen buitenunit → Ontdooien** en kies **Instellingen uitlezen**. Bij een Duo kies je de buitenunit die je wilt bekijken.

### Handmatig ontdooien

Kies **Handmatig ontdooien** en bevestig de aanvraag. De knop is beschikbaar als de buitenunit verwarmt, de compressor draait en er voldoende water door het systeem stroomt. Een actieve ontdooiing, een blokkering of een andere serviceactie kan de aanvraag tegenhouden. Bij een Duo moet OpenQuatt ook kunnen vaststellen dat de andere buitenunit niet aan het ontdooien is.

OpenQuatt stuurt één aanvraag. De buitenunit beslist of de cyclus start. Volg de status in de web-app om te zien of de buitenunit daadwerkelijk ontdooit.

### Ontdooimethode wijzigen

Het info-icoon naast **Ontdooimethode** legt de beschikbare modi uit. V1 biedt modus 0, 1 en 3. V1.5 en de ondersteunde V2-buitenunits bieden ook modus 4.

Kies een methode als de compressor stilstaat. Klik op **Toepassen** en bevestig de wijziging. Onder **Uitgelezen** zie je welke methode de buitenunit terugmeldt. Als OpenQuatt vraagt om opnieuw uit te lezen, kies je eerst **Instellingen uitlezen**.

Met **Na herstart opnieuw toepassen** kan OpenQuatt je keuze voor deze buitenunit bewaren. Na een herstart van OpenQuatt of na herstel van de verbinding leest OpenQuatt de methode opnieuw uit. Daarna controleert het ongeveer eens per tien minuten of die nog overeenkomt met je opgeslagen keuze. Als er een verschil is, wacht OpenQuatt tot wijzigen veilig kan. Meldt de web-app dat de keuze niet is opgeslagen, pas haar dan opnieuw toe en controleer de bevestiging.

### Metingen en cyclusduur

Onder **Technische metingen** staan de buitenluchttemperatuur, de verdampertemperatuur, de verdampingstemperatuur van het koudemiddel en de compressorfrequentie. Tijdens het ontdooien zie je de verstreken tijd. Na een waargenomen ontdooiing zie je hoelang die duurde en hoeveel tijd sindsdien is verstreken. Na een herstart kan die geschiedenis ontbreken. De buitenunit bepaalt zelf wanneer de volgende cyclus begint.
