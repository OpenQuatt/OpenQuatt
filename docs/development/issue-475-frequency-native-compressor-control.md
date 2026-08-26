# Issue #475: frequency-native compressor control

Status: stap 1 softwarematig gereed in draft PR #534; HIL nog vereist  
Target branch: `dev`  
Work branch: `fix/475-v2-compressor-mapping`

## Doel

Maak de fysieke compressoraansturing onafhankelijk van de instelbare F-leveltabellen in de ODU. De regelstrategie mag voorlopig modellevels 0–10 blijven gebruiken; tussen het performance-model en register 1999 wordt het gewenste compressoranker in Hz via de actuele runtime-tabel naar een fysiek F-level vertaald.

## Besluiten en invarianten

- De hardwarefingerprint uit `oq_odu_generation.h` bepaalt uitsluitend de ODU-variant en registerlayout.
- De inhoud, CRC en lengte van een frequentietabel worden nooit gebruikt voor modeldetectie.
- De actuele, gevalideerde runtime-tabel bepaalt de vertaling tussen Hz en F-level.
- V1 en V1.5 hebben dezelfde referentietabellen. Afwijkende EEPROM-dumps zijn runtimewijzigingen.
- Request- en actuatorlaag blijven onafhankelijk begrenzen.
- Een fysiek F-level lekt niet terug naar 0–10-modelboekhouding.
- Automatische regeling extrapoleert niet boven het hoogste onderbouwde performancepunt.
- CM100 blijft een expliciet fysiek servicecommando en wordt niet door de automatische mapper gehaald.
- Bij een stale callback, onvolledige read of ongeldige tabel wordt nooit F11–F20 vrijgegeven.
- Een ontbrekende runtime-tabel blokkeert bestaande V1/V1.5/cooling-aansturing niet; automatische regeling valt terug op fysiek F0–F10.

## Bekende layouts

| Variant | Fingerprint | Layout |
|---|---|---:|
| V1 | `0x0037` / `V001_T25` | F0–F10 |
| V1.5 | `0x0E37` / `V001_T30` | F0–F10 |
| V2 vroeg model | `0x0E37` / `V001_T34` / AMH6 | F0–F10 |
| V2 nieuw model | `0x1037` / `V002_T01` / AMH6 | F0–F20 |

Registerindeling:

- cooling F0–F10: Modbus `3000..3010`;
- heating F0–F10: Modbus `3011..3021`;
- heating F11–F20: Modbus `3050..3059`, alleen na fingerprint `V2_NEW_MODEL`;
- cooling F11–F20: Modbus `3060..3069`, alleen na fingerprint `V2_NEW_MODEL`.

De basisread is één blok van 22 registers. De V2-uitbreiding is één blok van 20 registers. Adressen `3050..3069` hebben op oudere layouts een andere betekenis en mogen daar niet worden gelezen als frequentietabel.

## Referentiegegevens

V1 en V1.5 delen deze oorspronkelijke tabel:

```text
Cooling: 0,30,36,42,47,52,56,61,66,71,74
Heating: 0,30,39,49,55,61,67,72,79,85,90
```

V2 vroeg model:

```text
Cooling: 0,30,36,42,46,48,52,56,61,66,71
Heating: 0,20,26,30,48,55,61,72,80,85,90
```

V2 nieuw model:

```text
Cooling: 0,20,26,30,34,36,38,40,42,44,46,48,52,54,56,58,60,64,66,68,71
Heating: 0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110
```

Referentiewaarden zijn test- en documentatiemateriaal, geen capability-signature of actuatorfallback.

## Validatie

Een tabel is per mode geldig wanneer:

- F0 exact 0 Hz is;
- F1 en hoger tussen 1 en 120 Hz liggen;
- de reeks niet afloopt;
- gelijke frequenties zijn toegestaan.

Een extension is alleen geldig bij `V2_NEW_MODEL`, een volledige response en `F11 >= F10`. Heating en cooling worden onafhankelijk gevalideerd. Een ongeldige extension degradeert alleen die mode naar F0–F10.

## Praktische uitvoering

### Stap 1 — veilige verticale slice in PR #534

- [x] Pure C++-typen met vaste arrays voor tabellen en snapshots.
- [x] Parsers en validatie voor base en V2-extension.
- [x] Deterministische Hz→F-resolver met fail-closed gedrag.
- [x] Read-only Modbus-loader per ODU met request-tokens, timeout en maximaal één retry per minuut.
- [x] Exacte fabriekstabel-signature verwijderen.
- [x] Vaste `V2_HEATING_MODEL_TO_PHYSICAL` verwijderen.
- [x] V2-modellevel omzetten naar bestaand heating-performanceanker in Hz en via runtime-tabel oplossen.
- [x] V1, V1.5, V2 vroeg en automatische cooling op de bekende fabriekstabellen functioneel ongewijzigd houden.
- [x] CM100 per mode tot F20 vrijgeven wanneer configuratie, fingerprint en geldige extension overeenkomen.
- [x] Defrost-hold modellevel en fysiek level gescheiden houden.
- [x] Host-, contract-, web- en firmwareregressies uitvoeren.

De V2 heating-performanceankers blijven in deze stap:

```text
0,20,26,30,48,55,61,72,80,85,90 Hz
```

Op de bekende nieuwe V2-tabel resulteert dat in F0/F1/F2/F3/F7/F9/F10/F13/F15/F16/F17. Bij een aangepaste geldige tabel kiest de resolver het dichtstbijzijnde beschikbare frequentiepunt. Een gelijke afstand kiest de lagere frequentie en geen enkele automatische kandidaat mag boven het hoogste onderbouwde performanceanker van 90 Hz liggen.

### Stap 2 — runtime-editor F0–F20

- Editor toont 11 of 21 levels op basis van de fingerprint.
- Schrijfpad blijft gescheiden van het read-only regelpad.
- Alleen schrijven bij stilstaande ODU, met maintenance-lock en volledige readback.
- Een gedeeltelijke of afwijkende write blokkeert automatisch hervatten.

### Stap 3 — frequentiebeleid

- Day- en Silent-cap per mode in Hz.
- Excluded frequentieranges per mode.
- Nieuwe entity-ID's en uitgestelde migratie op basis van een geldige runtime-tabel.
- Requestlaag filtert en actuatorlaag valideert opnieuw.

### Stap 4 — performance-optimalisatie

- Performance-maps op Hz indexeren.
- Tussen bekende performancepunten interpoleren.
- De optimizer alle daadwerkelijk beschikbare runtimelevels laten beoordelen.
- Niet extrapoleren boven onderbouwde vermogen- en COP-data; V2 heating F18–F20 blijft tot die tijd CM100-only.

## Verificatiematrix stap 1

- gedeelde V1/V1.5-referentietabel;
- gewijzigde V1- en V1.5-runtimewaarden, inclusief dubbele frequenties;
- V2 vroeg model met 11 levels;
- V2 nieuw model met geldige 21-leveltabellen;
- extension-adressen nooit interpreteren op een legacy-layout;
- incomplete, aflopende en buiten-bereik responses;
- heating/cooling extension onafhankelijk geldig of ongeldig;
- request-token invalidatie en timeout houden F11–F20 gesloten;
- aangepaste V2-tabel gebruikt de dichtstbijzijnde kandidaat met een lagere tie-break;
- automatische V2-heating blijft maximaal 90 Hz;
- CM100 vereist configuratie, fingerprint en een geldige mode-extension;
- defrost-hold behoudt modellevel en fysiek level afzonderlijk;
- Single en Duo configvalidatie; standaard compiletarget is `configs/heatpump_controller_q/duo_wifi.yaml`.

## Werklog

- 2026-08-26: plan bijgewerkt na bevestiging dat V1 en V1.5 dezelfde oorspronkelijke heating- en coolingtabel gebruiken. Runtimegewijzigde EEPROM-tabellen blijven geldige regressiefixtures.
- 2026-08-26: stap 1 gestart in de bestaande issue-475-worktree.
- 2026-08-26: Hz-resolver vastgelegd als dichtstbijzijnde kandidaat met lagere tie-break en een onafhankelijk maximum van 90 Hz voor automatische V2-heating.
- 2026-08-26: runtime-snapshot vastgelegd in 46 bytes per ODU; control callbacks werken op een lokale immutable kopie zonder heapallocaties.
- 2026-08-26: incomplete identity- of tabelreads behouden de laatst gevalideerde snapshot. Een fingerprintwijziging invalidateert deze snapshot voordat een nieuwe layout wordt gelezen.
- 2026-08-26: de bestaande experimentele F0–F10-editor invalidateert de snapshot vóór een write en laadt hem pas opnieuw na volledige readback. F11–F20-editorondersteuning blijft stap 2.
- 2026-08-26: koude regressie-audit: de tijdelijke afhankelijkheid van een geldige tabelread voor alle automatische starts verwijderd. Alleen V2-heating gebruikt de Hz-mapping; ontbrekende data behoudt het bestaande F0–F10-pad.
- 2026-08-26: softwarevalidatie groen: 39 hosttests, 95 Python-contracttests, 252 webtests, C++-formatcheck en volledige ESPHome-compile van `configs/heatpump_controller_q/duo_wifi.yaml`.
- 2026-08-26: resterende releasegate is HIL op V1/V1.5, V2 vroeg en V2 nieuw, inclusief boot/reconnect, incomplete reads, aangepaste EEPROM-tabellen, defrost-hold en CM100 F11–F20.
