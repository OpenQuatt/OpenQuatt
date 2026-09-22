# Using Codex In OpenQuatt

## Start Small

- Start Codex in the most relevant directory when possible:
  - firmware targets: `configs/`
  - shared ESPHome packages: `openquatt/`
  - custom components: `components/`
  - web UI: `openquatt/web/`
  - docs: `docs/`
  - tooling: `scripts/`
- Use a fresh session for a new unrelated task.
- Name the exact files, target config, component, or docs page you want changed.
- Ask for full builds only when you really want them.

## Good Scoped Prompts

- `Pas alleen configs/heatpump_controller_q/duo.yaml aan en draai alleen config-validatie.`
- `Werk openquatt/oq_thermal_limits.yaml bij; lees alleen direct gekoppelde packages als dat nodig is.`
- `Fix de web UI in openquatt/web/js/src/10-settings.js en rebuild alleen de web bundle.`
- `Controleer docs/web-app.md op deze nieuwe instelling; lees geen lokale referenties.`
- `Pas scripts/check_docs_consistency.py aan en draai alleen die check.`

## Build And Test Requests

- Firmware config-only check:
  - `rtk python3 scripts/dev.py validate --config-only --config configs/<hardware>/<target>.yaml`
- Web bundle:
  - `rtk pnpm run build:web`
- Full local validation:
  - `rtk python3 scripts/dev.py validate`
  - Vraag dit expliciet; het kan veel tijd en output kosten.

## Token Hygiene

- Vraag Codex om output samen te vatten in plaats van volledige logs te tonen.
- Noem gegenereerde of grote mappen alleen als ze echt onderzocht moeten worden.
- Voor debuglogs: plak liever de relevante foutregels of geef een specifiek logbestand.
