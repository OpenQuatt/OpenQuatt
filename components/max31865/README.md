# MAX31865 settling voor de Q-controller

Lokale override van de [ESPHome 2026.9.0 MAX31865-component](https://github.com/esphome/esphome/tree/2026.9.0/esphome/components/max31865). De meegeleverde `LICENSE` bevat de upstream licenties.

Het Q-profiel activeert afzonderlijke fault-clear, circa 10 ms BIAS-settling, handmatige fault-detect met circa 10 ms tussen de stappen en circa 10 ms settling vóór conversie. De uitleestimeout is 67 ms bij 50 Hz: de extra milliseconde voorkomt dat schedulerkwantisatie de datasheetgrens van 66 ms verkort. De 10ms-wachttijden zijn conservatieve startwaarden voor hardwarevalidatie.

De bestaande foutmaskers, temperatuurconversie en publicatie blijven behouden. Een lopende cyclus accepteert geen overlappende update. Bij detector-timeout wordt BIAS/testmodus uitgeschakeld en `NaN` gepubliceerd; een volgende cyclus kan opnieuw proberen. De extra millisecondenwachttijden gebruiken de ESPHome-scheduler; de begrensde bestaande detectorpoll blijft behouden.

`diagnostics: true` leest RTD (`0x01/0x02`), fault (`0x07`) en configuratie (`0x00`) tweemaal vóór en tweemaal na BIAS-uit. Elke cyclus krijgt twee INFO-regels onder `oq.max31865_diag`, gekoppeld met `sample`. De eerste RTD-read vóór BIAS-uit en de eerste fault-read erna blijven bepalend voor de bestaande foutafhandeling; afwijkende diagnosereads worden alleen gelogd. Er wordt geen extra clear of retry uitgevoerd. De logs volgen na BIAS-uit en de registerreads. De componentdefault is uit; **in deze test-PR staat diagnose tijdelijk aan voor Q Single en Duo**.

`pre`/`post` tonen hexadecimale registerparen in leesvolgorde. Bij een afgeronde 50Hz-conversie zijn `cfg=81/81` vóór en `cfg=01/01` na BIAS-uit te verwachten. `conv_us` loopt van de conversiestart tot de eerste RTD-read; `pre_us` van die read tot de BIAS-uit-opdracht; `post_us` van die opdracht tot het einde van de post-snapshot. `detect_us` meet alleen de laatste detectorpoll, niet de handmatige wachttijd. `total_us` omvat de cyclus tot het einde van de snapshots, zonder logverwerking. `faults` telt cycli met een afwijking in de oorspronkelijke RTD-/fault-read, inclusief waarschuwingen.

De extra SPI-reads verlengen de BIAS-aan-periode kort; `pre_us` maakt die meetinvloed zichtbaar. Dit is diagnostische firmware. Het aparte `scripts/diagnostics/max31865_duo.yaml` gebruikt standaard exact dezelfde timing (clear=1, bias/manual/post=10 ms, read_margin=1 ms). Verander bij vervolgvergelijkingen één factor tegelijk. Een verschil tussen reads kan zowel een nieuwe analoge fout als een onbetrouwbare read zijn; alleen dubbele reads bewijzen geen SPI-betrouwbaarheid.

De selector, hold, kalibratie, NVS en actuatorregeling worden niet gewijzigd. Valideer op hardware met bekende weerstanden, koud/warm water en echte sensorfouten, inclusief langdurige uitval zonder ODU-fallback. De gekozen wachttijden en runtime-geheugenmarge vereisen fysieke acceptatie vóór een stabiele release.
