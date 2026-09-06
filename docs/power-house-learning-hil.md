# HIL-plan: passief leren met de OpenQuatt Simulator

## Doel

Deze proef toont op een Q Duo aan dat de bestaande firmware een geldig, passief
meetpunt kan maken uit echte ontvangsten van twee gesimuleerde ODU's. Het
acceptatiepunt is de status `collecting` op
`/openquatt/learning/status`, met geldige room-, setpoint-, outside- en
flowbronnen en een niet-lege `last_sample_epoch`.

De proef verandert geen regelparameter en schrijft geen model terug. In iedere
status moet `auto_apply_allowed` daarom `false` blijven.

De simulator modelleert geen woning, hydrauliek of echte warmteproductie. Een
geldige snapshot bewijst dus de controller-, Modbus-, bron- en
calorimetrieketen; hij bewijst geen bruikbare 1R1C-fit, H/T0-advies of
energiebesparing. Een record ontstaat pas na vier uur aaneengesloten geldige
metingen. Die duur wordt voor HIL niet verkort. `records: 0` is bij deze proef
dus verwacht.

## Opstelling

Gebruik de normale Q Duo-firmware die al op de controller staat. Deze proef
vereist geen testprofiel, bronwijziging, CM-override of OTA.

Verbind uitsluitend de primaire ODU-RS485 van de controller met `M2` van de
simulator: A→A, B→B en GND→GND. De bus is 19200 8E1. Er mag geen echte ODU met
adres 1 of 2 op die bus staan. De simulatorhandleiding beschrijft terminatie
en de overige aansluitvoorwaarden in
[OpenQuatt-Simulator](https://github.com/OpenQuatt/OpenQuatt-Simulator).

Deze proef test geen ketel of OpenTherm-boilerpad. Laat de bestaande
thermostaatverbinding voor kamer- en setpointmetingen intact. Sluit `OTB` van
de controller niet op `OTT` van de simulator aan voor deze proef: de
OpenTherm-functie van de simulator emuleert een ketel en vervangt geen
thermostaat. In CM2 is een vers keteltelemetriebericht geen voorwaarde voor
leren; het bestaande CM2-contract zonder ketelvraag is dat wel.

De simulator draaide bij voorbereiding op `http://192.168.2.63/`; gebruik
morgen het actuele DHCP-adres of `http://hcq-system-simulator.local/`.

## Simulatorconfiguratie

Kies voor de eerste proef `V2 old` voor ODU 1 en `V2 new` voor ODU 2, beide op
hun standaardadres (1 en 2), en druk daarna op **Apply ODU profiles and
addresses, then reboot**. Dit is de bestaande Duo-HIL-baseline; het is geen
bewering over de generatie van de geïnstalleerde ODU's.

Zet vóór de meetrun alle protocol- en foutinjecties uit. De twee ODU-responses
en de algemene simulatie staan aan. Zet voor deze Q-controllerproef **ODU
external system pump flow** aan, zodat de simulator de door de controller
gevraagde iPWM-flow volgt zonder een ODU-pomprelais te vereisen. Zet `force no
flow`, defrost, frequentie-freeze, response delay en fast simulation uit. Druk
op **Reset ODU diagnostics**.

De ODU's in de simulator zijn thermisch onafhankelijk. Daarom maakt de
simulator de seriekoppeling HP1-uit → HP2-in niet zelf. Stel beide
buitentemperaturen gelijk in, bijvoorbeeld 7 °C, en begin met een HP1
water-in-temperatuur van 30 °C. Zodra HP1 tijdens verwarmen een stabiele
water-uit-temperatuur toont, stel je **ODU 2 water-in temperature** op die
waarde in, afgerond op 0,1 °C. Controleer na een verse Modbus-poll dat het
verschil HP1-uit minus HP2-in hoogstens 1,0 °C is. Dit is alleen een
simulatorfixture; het is geen controllerinstelling of aanname over de echte
installatie.

## Uitvoering

1. Controleer eerst zonder wijzigingen de simulator en de controller:

   ```bash
   node scripts/hil/run-input-sources.mjs \
     --controller http://openquatt.local \
     --simulator http://SIMULATOR-IP \
     --stage smoke
   ```

   Deze bestaande rooktest is read-only. Hij controleert het simulatorcontract
   `openquatt-modbus-opentherm-v1`, legt geheugenwaarden vast en toont de
   ODU-diagnostiek.

2. Wacht na de simulatoreboot tot beide `req`-tellers oplopen en de controller
   beide ODU's als verse bronnen ziet. Controleer dat **Passief leren** aan
   staat. De opt-in wordt niet door deze proef veranderd.

3. Laat room- en setpointbron de bestaande fysieke thermostaat gebruiken. Kies
   op die thermostaat tijdelijk een stabiel setpoint dat warmtevraag geeft én
   nog binnen de ingestelde comfortband valt. Houd het setpoint daarna minstens
   één uur onveranderd. De batchcollector sluit het eerste uur na een
   setpointwijziging bewust uit.

4. Laat de gewone regeling zelf CM2 bereiken; forceer CM2 niet. Controleer
   daarbij dat er geen ketelvraag is. De simulator vervangt alleen de ODU-bus;
   dit is geen boilerproef.

5. Zodra beide gesimuleerde ODU's verwarmen en flow rapporteren, voer de
   seriekoppelstap hierboven uit. Wacht daarna op nieuwe, verse ODU-frames en
   lees `http://openquatt.local/openquatt/learning/status` uit.

6. Leg de volledige status-JSON vast, plus de ODU 1- en ODU 2-diagnostiek van
   de simulator. De passieve status moet aan de volgende voorwaarden voldoen:

   | Waarde | Verwacht resultaat |
   | --- | --- |
   | `enabled` | `true` |
   | `status` | `collecting` |
   | `invalid_reasons` | lege lijst |
   | `sources.room/setpoint/outside/flow.valid` | alle vier `true` |
   | `sources.outside.route` en `sources.flow.route` | een verse ODU-route of `HP1/HP2 composition` |
   | `last_sample_epoch` | UTC-tijdstip, niet `null` |
   | `auto_apply_allowed` | altijd `false` |
   | ODU-diagnostiek | beide `req` oplopend; `drop`, `exc`, `bad_addr` en `bad_write` nul |

## Gerichte diagnose

| Status of symptoom | Actie |
| --- | --- |
| `series_junction_mismatch` | Stel HP2 water-in opnieuw gelijk aan de actuele HP1 water-uit en wacht op een nieuwe poll. |
| Flowbron ongeldig of nul | Controleer **ODU external system pump flow**, iPWM-activiteit en dat `force no flow` uit staat. |
| Room of setpoint ongeldig | Herstel de bestaande thermostaatroute; vervang hem niet door API-, HA- of handmatige testwaarden. |
| `setpoint_recovery` | Wacht één uur vanaf de laatste fysieke setpointwijziging. |
| `external_heat_not_excluded` | Wacht op een stabiele CM2-cyclus zonder ketelvraag; voeg geen OpenTherm-ketelsimulator toe. |
| Andere `invalid_reasons` | Leg eerst status en simulatordiagnostiek vast. Wijzig geen learner- of regelgrens om de proef te laten slagen. |

## Afronden en vervolg

Herstel de thermostaat op zijn oorspronkelijke setpoint en zet **ODU external
system pump flow** terug uit. Bewaar de status-JSON en simulatordiagnostiek
vóór het resetten van tellers. Er is geen OTA nodig en de passieve opt-in blijft
zoals hij vóór de proef stond.

Na een geslaagde `collecting`-proef volgen afzonderlijk:

1. een langere HIL-run onder web/API/MQTT/Modbus/OpenTherm-belasting, met
   geheugen- en tickmetingen;
2. een echte vieruursmeetperiode voor één opgeslagen record, gevolgd door
   reboot-, journal- en power-interruptiontesten;
3. wintervalidatie van batch en 1R1C met echte woningrespons.

Geen van deze stappen staat automatisch toepassen toe.
