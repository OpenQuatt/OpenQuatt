# MAX31865 settling voor de Q-controller

Lokale override van de [ESPHome 2026.9.0 MAX31865-component](https://github.com/esphome/esphome/tree/2026.9.0/esphome/components/max31865). De meegeleverde `LICENSE` bevat de upstream licenties.

Het Q-profiel activeert afzonderlijke fault-clear, circa 10 ms BIAS-settling, handmatige fault-detect met circa 10 ms tussen de stappen en circa 10 ms settling vóór conversie. De uitleestimeout is 67 ms bij 50 Hz: de extra milliseconde voorkomt dat schedulerkwantisatie de datasheetgrens van 66 ms verkort. De 10ms-wachttijden zijn conservatieve startwaarden voor hardwarevalidatie.

De bestaande foutmaskers, temperatuurconversie en publicatie blijven behouden. Een lopende cyclus accepteert geen overlappende update. Bij detector-timeout wordt BIAS/testmodus uitgeschakeld en `NaN` gepubliceerd; een volgende cyclus kan opnieuw proberen. De extra millisecondenwachttijden gebruiken de ESPHome-scheduler; de begrensde bestaande detectorpoll blijft behouden.

`diagnostics: true` voegt na de gewone meetsequentie dubbele RTD-/fault-reads en timinglogs onder `oq.max31865_diag` toe. Standaard staat dit uit, zodat de gewone Q-firmware geen extra diagnoseverkeer uitvoert. Het aparte `scripts/diagnostics/max31865_duo.yaml` overschrijft de timing voor vergelijking met de oorspronkelijke v0.55.0-sequentie.

De selector, hold, kalibratie, NVS en actuatorregeling worden niet gewijzigd. Valideer op hardware met bekende weerstanden, koud/warm water en echte sensorfouten, inclusief langdurige uitval zonder ODU-fallback. De gekozen wachttijden en runtime-geheugenmarge vereisen fysieke acceptatie vóór een stabiele release.
