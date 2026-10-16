# Performance Model Observer

De Duo-diagnostiek werkt verbeterpunten 4 en 7 samen uit. Iedere ODU krijgt voor
zijn actuele voorspelling de eigen gekalibreerde uitlaattemperatuur. Voor een
andere frequentiekeuze berekent de observer een stationaire waterketen
**HP1 → HP2** en vergelijkt hij de volledige kandidaatkeuze met de bestaande
regeling. HP2 vooraan geldt als installatiefout en wordt afgewezen.

## Bediening en effect

- `Performance Model Observer` schakelt de diagnose in. Na iedere boot staat
  deze uit (`ALWAYS_OFF`); er wordt geen voorkeur in NVS opgeslagen.
- `Performance Model Observer Status` publiceert een korte status voor Home Assistant.
- `Performance Model Observer Report` bevat elke 5 s de volledige lokale
  vergelijking. Dit is een interne entiteit, expliciet leesbaar via
  `/openquatt/entities` met domain `text_sensor` en die naam. De lange inhoud
  wordt niet als HA-state geëxporteerd: HA accepteert maximaal 255 tekens
  ([HA core](https://github.com/home-assistant/core/blob/dev/homeassistant/core.py#L188)).
- De observer schrijft geen vraag, topologie, actuator of modelcoëfficiënten.
  De actieve regeling blijft de bestaande performance mapping gebruiken.
- Uitschakelen verwijdert de callbacks en geeft de volledige werkallocatie vrij.
  Allocatie gebeurt uitsluitend in PSRAM; bij ontbreken volgt
  `allocation_failed`, zonder terugval naar interne DRAM.

Dit is de afgeronde observerfase, geen activering van een nieuw regelmodel.
Een echte installatie moet nog aantonen dat de voorspelfout afneemt voordat
nieuwe temperaturen of gekoppelde voorspellingen actief mogen sturen.

## Wat wordt vergeleken?

De bestaande regeling levert één onveranderlijk snapshot van aanvraag,
beschikbaarheid, minimumstilstand, frequentietabellen, vermogenslimieten,
topologie-wachttijden en comparatorinstellingen. De observer herberekent daarmee
het oude besluit en controleert dat dit overeenkomt met het vastgelegde besluit.
Een afwijking geeft `baseline_mismatch`; er verschijnt dan geen vergelijking.

Daarna worden alle toegestane fysieke levelparen met dezelfde selector en
wachttijden doorgerekend. Alleen de vermogensvoorspelling verandert. Power House
houdt ook zijn elektrische soft/peaklimieten. Curve behoudt zijn eigen
vermogensbegrensde continue vraag, Single/Duo-drempels en holdstatus. De fysieke
leveltabellen mogen per ODU verschillen; **90 Hz inclusief** blijft toegestaan,
frequenties erboven komen niet in het raster.

Voor een actuele ODU gebruikt de observer gemeten Hz en de eigen gekalibreerde
Tout. Voor kandidaten lost hij per ODU op:

```text
K = flow_lph × 4186 / 3600                     [W/K]
Q1 = map(hz1, buitenlucht, Tin1 + Q1/K)
Tin2 = Tin1 + Q1/K
Q2 = map(hz2, buitenlucht, Tin2 + Q2/K)
Qduo = Q1 + Q2
Pel_duo = Pel1 + Pel2
```

De bestaande mappings en hun geldigheidsgebied blijven behouden, zonder nieuwe
extrapolatie. Een ODU op 0 Hz voegt geen compressorwarmte toe. Pompwarmte,
leidingverlies, opslag, glycolcorrectie en standbyverbruik zijn niet gemodelleerd.
Bisectie gebruikt hoogstens 24 stappen en maximaal 1 W balansrest per ODU.
Een ontbrekend modelpunt maakt dat paar onbruikbaar; onvolledige rasters worden
nooit als winnaar gepubliceerd.

## Meetcontract en geldigheid

De observer gebruikt oorspronkelijke Modbus-receipts, geen later gepubliceerde
of vastgehouden geselecteerde waarden. Kalibratie wordt op de waarde uit hetzelfde
receipt toegepast; de oorspronkelijke ontvangsttijd blijft intact.
Buitenlucht en flow komen expliciet van HP1. HP2-flow moet vers zijn en binnen
maximaal 50 l/h of 10% overeenkomen. Dit is een controlesignaal voor de
veronderstelde gemeenschappelijke waterstroom, geen bewijs van de installatie.

Een vergelijking vereist:

- beide ODU's online; verse werkmodus-, defrost-, 2108- en 2119-statusreceipts;
- geen defrost, omschakeling, olie-retour of andere actieve 2119-statusbits;
- water/flow/frequentie maximaal 30 s oud, buitenlucht maximaal 30 min oud;
- maximaal 12 s tijdverschil tussen de water/flow/frequentiereceipts;
- flow 300–3000 l/h, HP1-Tout en HP2-Tin binnen 0,5 °C;
- coherente verwarmingsmodus, gemeten Hz en de laatst toegepaste fysieke level;
- maximaal 0,25 °C verschil tussen HP1-buitenlucht en de buitenluchtreferentie
  van het bestaande besluit, zodat alleen de watermodellering verandert;
- minimaal 300 s na een gemeten verwarmingsstart en 60 s na een levelwijziging;
- minimaal zes verschillende volledige ontvangstgroepen over minstens 60 s.

Herhaalde oude waarden vullen het venster niet. De vensterankers tolereren
hoogstens 0,2 °C per watermeting, 0,25 °C buitenlucht, 0,5 Hz en de grootste van
20 l/h of 2% flowverschil. Grotere veranderingen beginnen een nieuw venster.
Wijzigingen van offsets, profiel, tabel, cap of uitsluitingen doen dat ook.
De vergelijkingssleutel begrenst on/off/emergency-holdtellers op hun effectieve
wachttijdgrens. Verder doorgroeien boven die grens verandert geen besluit en
mag een verder onveranderlijk raster niet opnieuw afbreken. De echte regelcontext
blijft onaangeroerd; onderdrempelverschillen en gewijzigde drempels blijven
herkenbaar. Niet-eindige holdwaarden maken de context ongeldig.

Het raster verwerkt één paar per 25 ms en hergebruikt de opgeloste HP1-rij.
Frame, modelvarianten en regelcontext blijven onveranderlijk tijdens de job.
Elke betekenisvolle wijziging van de regelcontext trekt de uitslag in. De
looptijd kan langer zijn dan 25 ms per paar door andere loopwerkzaamheden;
verlopen receiptdeadlines breken de job af. Volledige resultaten worden na
20 s opnieuw berekend. Tijdens een nieuw raster is `valid=0`.

## Rapport lezen

`valid=1` betekent een volledige modelvergelijking binnen dit meetcontract.
Het bewijst geen betere modelnauwkeurigheid of besparing. Het rapport bevat:

- `old_f`/`series_f` en `old_hz`/`series_hz`: volledige gekozen levelparen en Hz;
- `old_pth`/`series_pth`, `old_pel`/`series_pel`: modelvermogens in W;
- `old_target`/`series_target`, `old_cap`/`series_cap` en beide beslisredenen;
- `own`, `own_pel`, `shared`: actuele voorspelling met eigen Tout versus de
  gedeelde referentietemperatuur;
- `pred_out`: opgeloste uitlaten bij het gekozen seriepaar;
- `frame_ms` en `context_ms`: monotone tijden van metingen en regelcontext;
- `samples`, `completed`, `rejected`, `unavailable`, `max_us`, `stack`, `bytes`.

`rejected` telt rasterfouten vastgesteld tijdens de rekenstap. Intrekken door
een veranderde context en ongeldige intervalticks worden niet meegeteld.
`unavailable` telt onbruikbare paren in het laatst begonnen raster. De
berekeningscounters en `max_us` beginnen opnieuw bij inschakelen.
`stack` is de cumulatieve minimum-vrije loopstack sinds boot, geen actuele
stackmeting. `max_us` is de langste observerstap, inclusief eindselectie.
Een ongeldige status trekt alle vergelijkingswaarden in; nullevels daarbij
zijn geen regeladvies.

## Validatie en vervolg

De hostregressie omvat 126 uitvoerbare tests; daarnaast doorlopen 447 Python-contracttests
de suite met twee bestaande skips. De nieuwe regressie test verschillende receiptfouten na een geldig venster,
wijzigingen van context/beleid, volledige PH- en Curve-keuzes, ontbrekende
modelpunten, elektrische limieten en minimumstilstand. Een directe ongecachete
serie-evaluatie controleert de gecachete winnaar. Daarnaast is de verplaatste
Curve-selector voor 20.000 deterministische gevallen vergeleken met de
`dev`-bron, inclusief holdstatus en fallbackgedrag.

De HIL-overlay `scripts/hil/configs/water_performance_duo.yaml` voegt alleen
meet- en onderzoeksinterfaces toe. Eerdere proeven en hun beperkingen staan in
[het onderzoeksverslag](water-performance-shadow.md). De huidige HIL-resultaten
worden hieronder vastgelegd.

Voor actieve aansturing resteert veldvalidatie: stationaire meetvensters van een
correct geïnstalleerde Duo, gecontroleerde sensoroffsets en gemeenschappelijke
flow, onafhankelijke warmte-/elektriciteitsmetingen en foutvergelijking per
buitenlucht, flow, watertemperatuur en frequentiepaar. De simulator gebruikt
verwante modelaannames en kan geen onafhankelijke nauwkeurigheid bewijzen.

### HIL: vaste bron en beperkingen

De fysieke testcontroller is `openquatt-test` op de desktoplabopstelling, met
V2 old model voor HP1 en V2 new model voor HP2. De rekenkern-HIL gebruikt
config-hash `0x1d6d613f` en OTA SHA256
`f62558b9c2da6331ec647a615949c34ef217853e92e361cec6f7314657914be2`.
De Curve-HIL-build met aangepaste HA-export en canonieke Curve-holdtellers heeft config-hash
`0x0a3c230d` en OTA SHA256
`5af3fc54f2518303b55ed11b567db1e570ca50eb3dc902ebab09ddf9af2fdd3a`.
De definitieve build verwijdert daarnaast alleen ongebruikte presentatiemetadata
van het interne rapport: config-hash `0x6842ed41`, OTA SHA256
`270121517bb5be17579993c4592756d95ae9c91eacbec57ac3f98248e578ad18`.
De C++-reken- en regelcode is identiek aan de Curve-HIL-build.
Een config-hash alleen identificeert C++-includes niet; bewaar bij veldmetingen
ook de firmwarebronrevisie en de OTA-hash.

Na OTA bleef de observer uit terwijl hij voor OTA aan stond. Aan/uit-cycli
publiceerden telkens `disabled valid=0`. De 90/90 Hz-rekenproef slaagde. HP2
vooraan gaf `hydraulic_mismatch`; 90,1 Hz gaf `frequency_policy`. Bevroren
receipts verliepen naar `stale` voor beide actuele voorspellingen en de serie.
Deze rekenproef op zichzelf bewijst geen 90 Hz-actuatorreadback.

Een vers gecompileerde baseline (`c0b3e7c3`, hash `0xddd0164b`) en kandidaat
werden na boot onder begrensde SSE- en REST-belasting gemeten, met normale
Modbus/OpenTherm-communicatie. De interne heapmetingen gebruiken
`MALLOC_CAP_INTERNAL`; het minimum is de cumulatieve watermark sinds boot.
De baseline bevatte de eerdere onderzoeksinterfaces en de al gemergede
Hz-bewegingslogica. Zijn compile-time DIRAM-gebruik was 220063 B, dat van de
rekenkernkandidaat 220311 B. De eindbuild met korte HA-status gebruikt
220375 B: **+312 B** tegenover de baseline.

De native API vraagt een andere opgeslagen encryptiesleutel dan de lokale
`api_key`; de verbinding werd geweigerd. Daarom is geen geslaagde nieuwe
Home Assistant/entitysubscription aangetoond. MQTT-brokerbelasting is in deze
run evenmin gekwalificeerd. De hieronder gemeten geheugenmarge geldt voor de
beschreven labbelasting; voor bredere releasekwalificatie blijft een run met
werkende HA- en MQTT-clients nodig.

De algemene Q-Duo-validatie meldt daarnaast de bestaande NVS-budgetfout:
570 geraamde entries op 630 bruikbare, 60 vrij tegenover 100 vereist. Deze
observer voegt geen persistente voorkeur toe; de configuratiecontrole zelf,
style-, docs- en webcontractchecks slagen. De Single-configuratie is ook geldig.

| Identieke begrensde SSE/REST-belasting | Baseline | Kandidaat, observer aan |
| --- | ---: | ---: |
| Actueel intern vrij, laatste sample | 82151 B | 84755 B |
| Minimum intern vrij sinds boot | 39828 B | 39828 B |
| Grootste intern vrij blok | 31744 B | 38912 B |
| Fragmentatie, laatste sample | 61,53% | 54,09% |
| Vrij PSRAM | 5342260 B | 5333296 B |

Dit is een begrensde labvergelijking, geen universeel geheugenbudget. De
baseline verwarmde tijdens de belasting; de kandidaat bevond zich toen in
preflow. De selectie onder actieve verwarming wordt aanvullend gemeten.
De observerstate zelf gebruikt **8768 B** PSRAM. Geen extra matrix wordt op de
loopstack geplaatst; de getargete objectcode toont hoogstens circa 2560 B in
de zichtbare observer-callketen, exclusief scheduler en externe functies.

De contexttijd wordt samen met de volledige uitslag vastgelegd; een nieuwe
pending regelcontext kan die tijd niet achteraf vernieuwen. Een regressie test
publicatie A, pending context B, volgende publicatie B en het wissen bij
invalidatie. De eerdere geheugenmeting hierboven gebruikte de versie vóór
deze correctie, met 8 B minder PSRAMstate.

### Volledige PH-vergelijking onder actieve verwarming

Met -7 °C buitenlucht, 870,144 l/h, Tin1 30 °C en Tin2 35,8 °C liep de
gewone regeling op fysieke levels 10/15 (90/82 Hz). De volledige observer gaf:

| Grootheid | Bestaande gedeelde referentie | Gekoppelde serie |
| --- | ---: | ---: |
| Gekozen Hz | 90/82 | 90/82 |
| Voorspeld thermisch vermogen | 11322,34 W | 10835,01 W |
| Voorspeld elektrisch vermogen | 4090,63 W | 4160,83 W |
| Capaciteit binnen dezelfde gates | 11873,42 W | 11321,78 W |
| Aanvraag / beslisreden | 15000 W / keep_current | 15000 W / keep_current |

De gemeten Tout1/Tout2 waren 35,77/40,75 °C. Hun eigen actuele voorspellingen
waren 5829,51/5002,60 W tegenover 5936,71/5385,62 W met de gedeelde referentie.
Een ongecachete hostberekening met dezelfde framewaarden reproduceerde het
serie-totaal (10835,0059 W), Pel (4160,8311 W), beide uitlaten en eigen
actuele voorspellingen binnen de afgeronde rapportprecisie.

Het raster bevatte 198 toegestane levelparen; 54 paren waren binnen de
conservatieve modelgrenzen onbruikbaar. Twee volledige vergelijkingen werden
achtereen gepubliceerd, met onveranderlijke contexttijden per sequence.
Langste observerstap inclusief eindselectie: **9407 µs**. Minimaal vrije
loopstack sinds boot: **4038 B** van 8192 B. Tijdens actieve verwarming:
85383 B actueel intern vrij, 39828 B minimum sinds boot, 40960 B grootste blok,
52,03% fragmentatie en 5333296 B vrij PSRAM. De beperkte geteste overlap gaf
geen allocatiefout of watchdog. De ODU's rapporteerden `bad_write=0` en `cap=0`.

Dit verschil tussen twee voorspellingen is geen gemeten rendementswinst.
Dezelfde modelaannames in de simulator vervangen geen onafhankelijke velddata.

De actieve belasting leverde vervolgens herhaalde complete uitslagen tot en met
sequence 8. Bij verandering van HP2-Tin van 35,8 naar 37,8 °C werd `valid=0`
met `hydraulic_mismatch` zichtbaar binnen circa 6,3 s, inclusief Modbuspoll en
rapportpublicatie. Na herstel volgde sequence 9 pas na nieuwe stabiele
ontvangstgroepen. Uitschakelen gaf 8964 B extra vrij PSRAM (minstens de volledige
8768 B observerstate), terwijl werkelijke compressorfrequenties 90/82 Hz bleven.

### Volledige Curve-vergelijking

Met dezelfde buitenlucht, flow en Tin1, een tijdelijke HIL-systeemaanvoer van
34,05 °C en een API-aanvoerdoel van 35 °C liep de bestaande Curve-regeling na
de normale opstart op 30/0 Hz. HP2-Tin werd afgestemd op de stabiele HP1-Tout:
31,8 tegenover 31,84 °C. Er werden minstens twee volledige rasters gepubliceerd:

| Grootheid | Bestaande gedeelde referentie | Gekoppelde serie |
| --- | ---: | ---: |
| Gekozen Hz | 30/0 | 30/0 |
| Voorspeld thermisch vermogen | 1781,14 W | 1855,91 W |
| Voorspeld elektrisch vermogen | 603,37 W | 590,10 W |
| Afgeleide aanvraag | 1175,50 W | 1166,01 W |
| Capaciteit binnen dezelfde gates | 11755,00 W | 11321,78 W |
| Capaciteitsmodus | Single | Single |

De aanvraag verschilt omdat Curve dezelfde continue vraag op de capaciteit
van iedere modelvariant toepast. De ongecachete oracle reproduceerde
1855,9133 W en 590,1031 W, met beide voorspelde uitlaten 31,835 °C. De eigen
actuele voorspelling was 1855,7948/0 W. Geen baseline-afwijking of afgewezen
raster; 54 van de 198 paren waren binnen het model onbruikbaar.
Langste stap 3367 µs; minimaal 4006 B loopstack vrij. Intern geheugen:
82931 B actueel vrij, 39828 B minimum sinds boot, grootste blok 40960 B,
50,61% fragmentatie; PSRAM 5333296 B vrij.

De korte HA-status bleef onder 64 tekens en kwam overeen met de eerste twee
velden van het lokale rapport, dat langer dan 255 tekens was. Uitschakelen
publiceerde voor beide `status=disabled valid=0`. Een begrensde logstream
bevatte één Modbus-wachttijdoverschrijding; de normale communicatie en complete
vergelijkingen liepen daarna door.

De eerste PH-wachtproef liet API-kamertemperatuur verlopen; daarop trok de
regeling verwarming en vergelijking in. Een vroege Curve-startcontrole verliep
tijdens de wachtstand. De geslaagde proeven hierboven gebruiken expliciet
ingestelde bronnen, verse API-invoer en de ongewijzigde productiewachttijden.
Na OTA moet de tijdelijke CIC-fixture opnieuw worden ingeschakeld en ingesteld;
deze fixture staat standaard uit en haar waarde wordt niet opgeslagen.

### Eindcontrole

De definitieve metadata-build is via OTA geplaatst en als `0x6842ed41`
teruggelezen. Beide rapporten begonnen met `status=disabled valid=0`.
Twee aanvullende aan/uit-cycli bevestigden de 8768 B werkstate bij inschakelen
en beide disabled-rapporten na uitschakelen, zonder compressorstart.
De afsluitende readback bevestigde CM0, 0/0 Hz, observer en onderzoeksprobe uit,
API-verwarming en -koeling uit, snapshotfault `None`, UART-foutinjectie en
beide handmatige telemetry-overrides uit. De lablock is vrijgegeven.
Dag- en silentgrens bleven 90 Hz; de controller houdt `Force CM0`.

Oordeel: observerfase functioneel en via HIL afgerond, inclusief PH en Curve.
De PR blijft draft voor de ontbrekende gecombineerde HA/API-/MQTT-
geheugenkwalificatie. Veldvalidatie en activering van een nieuw regelmodel
vallen buiten deze observerfase.
