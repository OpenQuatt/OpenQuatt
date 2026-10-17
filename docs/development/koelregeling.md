# Koelregeling: toestemming, watermarge en compressorselectie

Deze pagina beschrijft het regelalgoritme voor Q Single en Duo. Voor bediening
zie [Koelen](../dagelijks/koelen.md) en [Koelen: technische werking](../koelen-technisch.md).
De gemeenschappelijke request-/actuatorketen staat bij
[Verwarmingsregeling](verwarmingsregeling.md).

## Drie afzonderlijke beslissingen

| Laag | Vraag |
| --- | --- |
| Toestemming en koelvraag | Mag koelen, en is er daadwerkelijk vraag? |
| Waterregeling en begrenzing | Hoeveel vraag past bij de aanvoer en condensmarge? |
| Dispatch en actuator | Welke HP mag die vraag veilig uitvoeren? |

Een actief tijdvenster is een toestemmingsbron. Met de standaard ingeschakelde
`Cooling Room Request Required` blijft kamerkoelvraag nodig. De kamer gebruikt
een ON/OFF-hysterese rond het setpoint. Met die eis uit vormt toestemming zelf
de vraag; geldige watermetingen en de overige guards blijven noodzakelijk.

`OpenQuatt Enabled` blijft een centrale voorwaarde. `Manual Cooling Enable`
kan de geselecteerde toestemmingsbron omzeilen, maar niet die centrale
voorwaarde of de water-/flowguards. De modi zonder werkelijk dauwpunt zijn
afzonderlijke expliciete gebruikerskeuzes, geen effect van die handschakelaar.

Supervisory gebruikt de kernvrijgave om circulatie/CM5 mogelijk te maken. De
volledige `cooling_permitted` voegt de flowvoorwaarde toe. Dit onderscheid
voorkomt dat de pomp pas zou starten nadat al voldoende pompflow gemeten is.

## Van dauwpunt naar een aanvoerdoel

| Guardmodus | Effectieve ondergrens |
| --- | --- |
| Geldig dauwpunt | Geselecteerd dauwpunt plus veiligheidsmarge. `Auto` kiest de hoogste geldige bron. |
| Toegestane benadering zonder dauwpunt | Fallback uit beschikbare buiten-/kamergegevens en eventueel het nachtminimum; zonder geldige fallback blijft koelen geblokkeerd. |
| Expliciete eigen verantwoordelijkheid | Gebruikt `Cooling Minimum Supply Temp`; betrouwbare condensveiligheid wordt daarmee niet aangetoond. |
| Geen bruikbare/toegestane modus | Geen geldige ondergrens en geen koelvrijgave. |

De guardmodus beïnvloedt ook de limiter en herstartlogica. Een wissel reset
onder meer het watercyclusgeheugen, de filtertoestand en de integrator.
Bronkeuze, korte hold en bronveroudering geven geen permanente toestemming om
zonder meetbewijs door te gaan.

`cooling_supply_target` combineert de effectieve ondergrens met de ingestelde
minimumaanvoer. In de gewone dauwpuntmodus geldt:

```text
T_min_safe = T_dew + safety_margin
T_target  = max(Cooling Minimum Supply Temp, T_min_safe)
```

Bij 15 °C dauwpunt, 2 K marge en 18 °C ingestelde minimumaanvoer is het doel
18 °C. Een aanvoer van 20 °C geeft dan 2 K waterzijdige ruimte om te koelen.
Dit is geen meting van de oppervlaktetemperatuur van iedere vloer of leiding.

## Van temperatuurverschil naar vraagstappen

De [demandlogica](../../openquatt/includes/control/oq_cooling_demand_logic.h)
gebruikt het temperatuurverschil in koelrichting:

```text
buffer_gap = gemeten aanvoer - aanvoerdoel       [K]
dew_gap    = gemeten aanvoer - dauwpunt          [K, alleen dauwpuntmodus]
```

Een positieve `buffer_gap` geeft ruimte om verder te koelen. De regelaar heeft
P/I/D-termen; `Ki` integreert de waterfout en wordt begrensd en tegen opwinden
beschermd wanneer de toegestane capaciteit verzadigd is. Ongeldige configuratie
of ongeldige proceswaarden leiden tot nulvraag en reset van de regeltoestand.

`oq_cooling_demand_raw` is een vraagstap `0..10`, verder begrensd door
`Cooling Demand Max` (standaard 4). Het is geen Hz. Ook de kamerfout begrenst
de vraag: minder dan 0,4 K boven het setpoint geeft maximaal stap 1; onder
0,8 K maximaal 2 en onder 1,2 K maximaal 3. Dit staat naast de aparte
aan/uit-hysterese voor de kamerkoelvraag.

## De limiter kijkt verder dan de huidige temperatuur

De limiter gebruikt actuele marge, gefilterde marge en trend. Standaard heeft
de filter een tijdconstante van 60 s, wordt de trend over een venster van 60 s
bepaald en kijkt de voorspelling 3 minuten vooruit:

```text
projected_gap = filtered_gap + trend_K_per_min × horizon_min
```

Dat leidt tot verschillende ingrepen:

- **Vooruit remmen:** een ongunstige voorspelling begrenst de vraag tot een
  lage stap vóór de actuele ondergrens bereikt is.
- **Capaciteit herstellen met hysterese:** dauwpuntmarge en daalsnelheid kunnen
  een maximum opleggen. Verhogen vereist voldoende herstelde marge en stabiele
  trend; de cap springt niet direct terug naar het maximum.
- **Laag doorlopen:** bij specifieke marges kan stap 1 worden vastgehouden
  (`simmer` / `level1_hold`) om pendelen te beperken.
- **Stoppen:** onvoldoende buffer- of dauwpuntmarge kan de vraag nul maken.
  Sommige limiterstops hebben bevestigingstijd; de dauwpuntstop gebruikt
  standaard `oq_cooling_limiter_stop_confirm_s` = 30 s. Deze bevestiging is
  iets anders dan wachten op fysieke compressorstilstand.
- **Fallback:** de benaderingsmodus gebruikt eigen cap-/stopgrenzen en
  vervangt geen werkelijk gemeten dauwpunt.

Opregelen gebeurt normaal hoogstens één stap per 90 s; afregelen heeft een
kortere dwell van 30 s. Een aangescherpte veiligheidslimiet kan direct verder
terugregelen. De normale dwell vertraagt dus geen noodzakelijke capverlaging.

Bij een ODU-olie-retourmelding bestaat een apart herstelvenster met lage
requeststap en begrensde integrator. Bepaalde waterlimiterstops kunnen daarbij
tijdelijk worden gemaskeerd. Ongeldige inputs, kern-/flowverlies en de
request-/actuatorguards blijven afzonderlijke grenzen. De exacte maskerings-
en herstelvoorwaarden staan in `update_oil_return()` en `update_limiter()`;
wijzig die samen met hun regressies en passende HIL-dekking.

## Single/Duo: één automatische koeleigenaar

De [dispatcher](../../openquatt/includes/control/oq_cooling_dispatch_logic.h)
begrenst de vraag met de gedeelde cap `oq_power_cap_f`. De normale automatische
selectie kiest één koeleigenaar; zij gebruikt geen verwarmingsachtige
single/duo-vermogensoptimalisatie. De hieronder genoemde incidenthold kan een
bestaande topologie tijdelijk behouden.

Bij Duo houdt hij een bruikbare, al actieve eigenaar vast. Bij een onbruikbare
eigenaar spelen recente activiteit, de opgeslagen eigenaar, runtime-lead en
beschikbaarheid mee. Incidentstatus, toegestane frequenties en minimum-uit-tijd
kunnen kandidaten uitsluiten. Verdachte communicatie kan de bestaande
topologie tijdelijk laten vasthouden; zo'n hold geeft geen nieuwe HP-start vrij.

Request control selecteert de koelrequests in CM5 en past gedeelde guards toe.
De actuator controleert de laatste vrijgave en verzorgt mode-/levelopdrachten.
Een stopopdracht, een bevestigde fysieke stop en een vrijgegeven volgende start
zijn drie afzonderlijke gebeurtenissen.

## Stoppen en opnieuw beginnen

Er zijn twee koelherstartmodi:

- **Wateropwarming:** na een waterzijdige stop moet de gefilterde marge genoeg
  herstellen ten opzichte van de opgeslagen stopmarge. `Cooling Restart Delta`
  bepaalt de benodigde opwarming mede. Dauwpunt- en projectieherstel hebben
  daarnaast eigen voorwaarden; ingetrokken kamerkoelvraag is een andere
  stopreden dan een waterzijdige limiterstop.
- **Minimum-uit-tijd:** een werkelijk gestopte koelcyclus start de gezamenlijke
  timer uit `Cooling Minimum Off Time`. Bij Duo geldt die voor beide HP's;
  de tweede HP kan de wachtperiode niet omzeilen. Zolang stopbevestiging
  ontbreekt, mag geen fictieve resterende tijd worden afgeteld.

Los hiervan geldt per compressor de gewone minimum-uit-tijd (standaard 240 s).
Een normale stop kan door minimumlooptijd worden uitgesteld; harde
veiligheidsredenen hebben voorrang. Bij sluiten van een koelvenster zijn daarom
nog kort compressorbedrijf en vervolgens CM1-postflow mogelijk.

## Diagnose en wijzigingen

| Situatie | Volgorde van onderzoek |
| --- | --- |
| Venster aan, geen koelvraag | `cooling_enable_selected`, klokvaliditeit, kamerhysterese en `Cooling Room Request Required`. |
| Wel vraag, geen vrijgave | Guardmodus, dauwpuntbron, effectieve minimumaanvoer, flow en `cooling_block_reason`. |
| CM5, vraag begrensd | `oq_cooling_demand_raw`, limiterreden, actuele/gefilterde/voorspelde marge en powercap. |
| Geen HP-start ondanks vraag | Dispatch-owner, incidentstatus, frequenties, stopbevestiging en beide minimum-uit-tijden. |

Live diagnostics en de [beslislog](control-replay-firmware.md) hebben
verschillende detaillering. Een limiterstap kan in diagnostics zichtbaar zijn
zonder een nieuw afzonderlijk tijdlijnevent.

De bronverdeling staat in
[koelveiligheid](../../openquatt/includes/control/oq_cooling_safety_logic.h),
[policy](../../openquatt/includes/control/oq_cooling_safety_policy.h),
[limiter](../../openquatt/includes/control/oq_cooling_limiter_logic.h) en
[runtime](../../openquatt/includes/control/oq_cooling_runtime.h).
Actuele YAML-parameters staan in
[de koelstrategie](../../openquatt/oq_cooling_strategy.yaml) en
[de substituties](../../openquatt/oq_substitutions_common.yaml).

Controleer bij wijzigingen minimaal de regressies voor
[demand](../../tests/host/cooling_demand_logic_test.cpp),
[limiter](../../tests/host/cooling_limiter_logic_test.cpp),
[dispatch](../../tests/host/cooling_dispatch_logic_test.cpp),
[veiligheid](../../tests/host/cooling_safety_policy_test.cpp) en
[startstatus](../../tests/host/cooling_start_status_test.cpp). Test foutieve/
verouderde bronnen, guardwissels, flowverlies, olie-retour en onbevestigde stops
afzonderlijk. Hosttests vervangen geen fysieke condens-, timing- of
geheugenkwalificatie. Runverslagen horen bij de betreffende PR.
