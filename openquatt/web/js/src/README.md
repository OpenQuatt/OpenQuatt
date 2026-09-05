`openquatt/web/js/src/` contains the ES module source for the bundled web app.

Current split:
- `app.js`: bundle entrypoint; imports and starts `boot()`
- `core/`: shared config, state/runtime, entity store, sync, actions, browser helpers and small formatting/domain helpers
- `features/`: feature flows such as firmware update, header status, security, MQTT, quickstart, storage/history, webserver logs and debug recording
- `settings/`: settings shell helpers and domain renderers for storage, heating, water, installation, integrations, security, service, silent mode and cooling
- `views/`: overview, energy, heat pump and root shell rendering

The deployed/runtime file remains:
- `openquatt/web/js/openquatt-app.js`

Rebuild the bundle with:
- `rtk npm run build:web`

Run the local web smoke checks with:
- `rtk npm run smoke:web`

Settings select controls use `settings/field-models.js` for entity availability,
draft-aware values, firmware options (`option` or `options`) and busy state.
Pass the same model to choice cards in a field; keep presentation copy and
specialized capability/source filters in their domain renderer.

`settings/controls.js` owns rendering and live patches for these controls.
`data-oq-select-model="true"` opts into shared disabled-state updates and, for
native selects, option updates. Only add it when the shared model owns those
decisions. An explicit choice `busy` override retains its custom disabled gate;
filtered selects without the marker retain their own options and gates. Focused
native menus defer value/option patches until focus leaves. Service and focused
integration panels retain their existing full-render fallback.

The ODU editors share modal/panel/action markup in `features/odu-editor-ui.js`
and numeric input markup in `core/number-controls.js`. Service-owned inputs omit
`data-oq-field` and pass their own disabled gate; they are not ESPHome entities.
The bottom-plate editor uses one model for initial rendering and live draft
validation. Its identity/write rules remain separate from the frequency table's
arm, standby and 0 Hz requirements.
