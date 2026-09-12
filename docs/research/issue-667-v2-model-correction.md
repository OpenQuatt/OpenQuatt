# Aanvulling en correctie op het eerste onderzoek

## Ongewijzigd

De huidige OQ-V2-basismatrix combineert de verkeerde brongegevens met de native V2-frequentieas. De referentie bij 20 Hz / A12,6 / W22,5 blijft ongeveer 5.169 W in de oude kaart en 3.073 W in de herstelde native kaart. Er is geen onderbouwing voor een algemene veldcorrectiefactor. De live Power Input-schatting en voorspeld Pel zijn verschillende modellen.

## Gecorrigeerd

De hoge-temperatuurdata is **geen verzameling losstaande ragged tabellen** met 6/13/13 ambientrijen. De eerdere analyse interpreteerde unieke literals als volledige logisch aaneengesloten matrices. De ARM-constructor hergebruikt literals en maakt 8 volledige supply-planes van ieder 17 ambientrijen. De nieuwe pointertracering verifieert 136 Pth-rijen en 136 COP-rijen. Gebruik alleen de nieuwe generator en data voor implementatie; de vroegere research-export blijft een historisch bewijsstuk, niet de bouwbron.

## Nieuw uit de actuele codecontrole

Power House en Heating Curve controleren wel de runtimefrequentiepolicy, maar rekenen het vermogen nog met `model_frequency_hz(level)`. Een geresolveerde 43 Hz wordt daarmee als het 48-Hz-referentieanker doorgerekend. De nieuwe kandidaatadapter gebruikt `automatic_frequency_hz(is_hp1, 2, level)` uit dezelfde vastgelegde context.

## Aangescherpte geschiedenisconclusie

De actuele live Power Input-functie gebruikt de oudere coëfficiënten. Die staan ook direct na de V2-introductie in april 2026 in de YAML. De augustusrefactor neemt ze ongewijzigd over. Een recente refactorregressie is hiermee niet aangetoond; in de gecontroleerde code ontbreekt juist een V2-selectie. Niet-gemergede of tijdelijke branches zijn niet uitputtend onderzocht.

## Implementatiebesluit van de eigenaar

V2 old en new delen het prestatiemodel. De nieuwe kaart mag na technische integratiechecks worden ingezet voor een gecontroleerde praktijkproef. Een langer veldvalidatietraject volgt later. Dit vervangt de eerdere aanbeveling om de kaart voorlopig uitsluitend passief mee te laten rekenen.

## Reproduceren in deze repository

Gebruik uitsluitend de CiC 4.2.0 ELF met SHA256
`3f04ba2bed6c4ec1b4215482f4213e8ea7475c98c305712973b8035838c717b6`.

```bash
python3 scripts/research/build_v2_model.py --elf /path/to/cic_controller.elf --out /tmp/openquatt-v2-model
python3 scripts/research/verify_v2_constructor.py --elf /path/to/cic_controller.elf --out /tmp/openquatt-v2-model/constructor_rows.json
clang-format -i -style=file:.clang-format /tmp/openquatt-v2-model/include/hp_v2_model_data.h
diff -u /tmp/openquatt-v2-model/include/hp_v2_model_data.h openquatt/includes/performance/detail/hp_perf_map_v2_data.h
```

De verificatie moet 136 Pth- en 136 COP-rijen aantonen. De laatste `diff` moet leeg zijn.
