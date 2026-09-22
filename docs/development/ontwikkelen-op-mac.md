# Ontwikkelen Op Mac

Deze repo gebruikt macOS als primaire lokale ontwikkelomgeving.
De GitHub Actions blijven op Linux draaien; lokaal gebruik je dezelfde Python- en shell-entrypoints.

## Setup

1. Clone de repo in je normale home-directory.
2. Zorg dat Python 3.12 of nieuwer als `python3` beschikbaar is.
3. Installeer `clang-format` voor lokale C/C++ formattingchecks:

```bash
brew install uv
uv tool install --force clang-format==22.1.8
clang-format --version
```

4. Maak de lokale ESPHome-omgeving aan:

```bash
python3 scripts/dev.py bootstrap
```

De `bootstrap`-stap maakt een lokale `.venv` aan en installeert de gepinde ESPHome-versie uit de repo.
Voer `python3 scripts/dev.py bootstrap` opnieuw uit nadat `/.github/requirements-esphome.txt`
is aangepast; de bootstrap ververst een bestaande `.venv` dan opnieuw naar de gepinde versie.

## Hoofdworkflow

Gebruik deze commando's als primaire lokale workflow:

```bash
python3 scripts/dev.py validate
python3 scripts/dev.py validate --config-only
python3 scripts/dev.py preview-pages --no-serve
pnpm run check:cpp-format
```

Gebruik `pnpm run fix:cpp-format` om C/C++ formatting lokaal toe te passen voordat je een pull request bijwerkt; CI faalt als de formatter-check verschillen vindt. De vereiste versie staat in `.clang-format-version`; de check weigert een andere versie en controleert ook nieuwe, nog niet getrackte C/C++-bestanden. Als `clang-format` niet automatisch in `PATH` staat, kun je `CLANG_FORMAT_BIN` op het door `uv tool dir --bin` getoonde pad naar `clang-format` zetten.

De bash-wrappers blijven beschikbaar voor de meest gebruikte taken:

```bash
./scripts/bootstrap_esphome_local.sh
./scripts/validate_local.sh --config-only
./scripts/preview_pages_local.sh --no-serve
```

## Parallel Bouwen

Je kunt parallel bouwen met bijvoorbeeld:

```bash
python3 scripts/dev.py validate --jobs 2
```

Begin bij voorkeur met `--jobs 2`. Meer parallelisme kan sneller zijn, maar gebruikt ook meer CPU, RAM en schijfcache.
De eerste full-validate na een lege of opgeschoonde cache kan tijdelijk sequentieel lopen; de helper doet dat automatisch om ESP-IDF component-cache races te vermijden.

## Geheugenvalidatie

Een geslaagde compile en het linker-RAM-percentage bewijzen niet dat de runtimeheap veilig is. Meet bij nieuwe runtimefeatures ook de actuele en minimale interne heap, het grootste vrije block, fragmentatie, vrij PSRAM en relevante task-stack-watermarks op representatieve hardware.

Vergelijk dezelfde releaseconfiguratie vóór en na de wijziging, vanaf een koude boot en onder gecombineerde HA-, web-, API-, MQTT-, Modbus-, OpenTherm- en waar relevant OTA/flashbelasting. Zie `CONTRIBUTING.md` en `system-overview.md` voor de allocatieregels, meetmethode en releasecriteria.

## Flashen En Hardware

Gebruik voor ESPHome upload- en logtaken de ESPHome executable uit de lokale venv:

```bash
.venv/bin/esphome upload configs/heatpump_controller_q/duo.yaml
.venv/bin/esphome logs configs/heatpump_controller_q/duo.yaml
```

Voor browser-based flashen kun je de lokale Pages preview of de gepubliceerde installer gebruiken.
