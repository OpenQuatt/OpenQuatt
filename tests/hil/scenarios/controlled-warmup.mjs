import { asBoolean, asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';
import { waitFor, waitNumber, waitValue } from '../../../scripts/hil/wait.mjs';
import { startNativeHaFixture } from '../../../scripts/hil/native-ha-fixture.mjs';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const assert = (condition, message) => { if (!condition) throw new Error(message); };
export const warmupNumber = (value) => !['number', 'string'].includes(typeof value) || (typeof value === 'string' && value.trim() === '') ? null : asFiniteNumber(value);
export const warmupClose = (actual, expected, tolerance = 0.025) => warmupNumber(actual) !== null && Math.abs(warmupNumber(actual) - expected) <= tolerance;
const close = warmupClose;
const prefix = 'Controlled warmup ';
export const warmupExtraSettings = [
  { key: 'enabled', domain: 'switch', name: `${prefix}enabled` },
  ...[
    ['trigger', 0.5, 5], ['step', 0.1, 0.5], ['step time', 5, 120],
    ['maximum offset', 0.1, 2], ['maximum duration', 1, 24],
  ].map(([name, min, max]) => ({ key: name, domain: 'number', name: prefix + name, min, max })),
  { key: 'supplySource', domain: 'select', name: 'Heating Supply Target Source', options: ['Heating curve', 'OT thermostat', 'HA input', 'API input', 'MQTT'] },
  { key: 'curveProfile', domain: 'select', name: 'Heating Curve Control Profile', options: ['Comfort', 'Balanced', 'Stable'] },
  { key: 'comfortBelow', domain: 'number', name: 'Power House comfort below setpoint', min: 0, max: 2 },
  { key: 'oqEnabled', domain: 'switch', name: 'OpenQuatt Enabled' },
  { key: 'coolingRoomRequired', domain: 'switch', name: 'Cooling Room Request Required' },
  { key: 'thermostatDemand', domain: 'switch', name: 'Thermostat CH demand', simulator: true },
];

export function validateWarmupExtra(extra) {
  assert(extra && [1, 2].includes(extra.schema), 'missing controlled-warmup recovery settings');
  for (const setting of warmupExtraSettings) {
    // Initial HIL snapshots did not touch the cooling-demand permission.
    // Leave it unchanged if absent in schema 1; schema 2 always requires it.
    if (extra.schema === 1 && setting.key === 'coolingRoomRequired' && !(setting.key in (extra.values || {}))) continue;
    const value = extra.values?.[setting.key];
    if (setting.domain === 'number') {
      assert(Number.isFinite(value) && value >= setting.min && value <= setting.max, `invalid recovery ${setting.name}`);
    } else if (setting.domain === 'switch') assert(typeof value === 'boolean', `invalid recovery ${setting.name}`);
    else assert(setting.options.includes(value), `invalid recovery ${setting.name}`);
  }
  return extra;
}

async function captureExtra({ controller, simulator }) {
  await assertIdleBaseline(controller);
  return readExtra({ controller, simulator });
}

async function readExtra({ controller, simulator }) {
  const values = {};
  for (const setting of warmupExtraSettings) {
    const raw = await (setting.simulator ? simulator : controller).value(setting.domain, setting.name);
    values[setting.key] = setting.domain === 'number' ? asFiniteNumber(raw) : setting.domain === 'switch' ? asBoolean(raw) : raw;
  }
  return validateWarmupExtra({ schema: 2, values });
}

export async function assertIdleBaseline(controller) {
  const mode = await controller.value('text_sensor', 'Control Mode');
  assert(typeof mode === 'string' && ['CM0', 'CM1', 'CM2', 'CM3', 'CM4', 'CM5', 'CM98', 'CM100'].includes(mode), 'control mode baseline is unavailable or unknown');
  const service = await controller.request('/openquatt/service/status');
  const entities = service.entities;
  assert(entities && typeof entities === 'object', 'service baseline is unavailable');
  const active = ['cm100Active', 'boilerPowerTestActive', 'airPurgeActive', 'manualFlowActive', 'manualHpActive', 'hpWaterCalibrationActive']
    .some((key) => asBoolean(entities[key]?.value));
  assert(mode !== 'CM100' && mode !== 'CM98' && !active, 'active service cannot be restored from a settings snapshot; HIL refuses to modify it');
}

async function write(client, domain, name, value) {
  if (domain === 'number') await client.setNumber(name, value);
  else if (domain === 'select') await client.setSelect(name, value);
  else await client.setSwitch(name, value);
  await waitFor(async () => {
    const raw = await client.value(domain, name);
    return (domain === 'number' ? close(raw, value) : domain === 'switch' ? asBoolean(raw) === value : raw === value) ? { value: raw } : false;
  }, `${name} write/readback`, { timeoutMs: 30000, intervalMs: 1500 });
}

async function restoreExtra({ controller, simulator, snapshot }) {
  const extra = validateWarmupExtra(snapshot.extra);
  await write(controller, 'switch', prefix + 'enabled', false);
  // Restore permission last, after parameters and source selectors. Never resume a session.
  for (const setting of warmupExtraSettings.filter((s) => s.key !== 'enabled' && s.key in extra.values)) {
    await write(setting.simulator ? simulator : controller, setting.domain, setting.name, extra.values[setting.key]);
  }
  await write(controller, 'switch', prefix + 'enabled', extra.values.enabled);
  await sleep(2000);
  const actual = await readExtra({ controller, simulator });
  for (const setting of warmupExtraSettings.filter((s) => s.key in extra.values)) {
    assert(setting.domain === 'number' ? close(actual.values[setting.key], extra.values[setting.key]) : actual.values[setting.key] === extra.values[setting.key], `warmup post-settle readback failed: ${setting.name}`);
  }
}

export const warmupStateEntities = [
  { key: 'active', domain: 'binary_sensor', name: prefix + 'active' },
  { key: 'target', domain: 'sensor', name: prefix + 'effective target' },
  { key: 'offset', domain: 'sensor', name: prefix + 'offset' },
  { key: 'status', domain: 'text_sensor', name: prefix + 'status' },
  { key: 'room', domain: 'sensor', name: 'Room Temperature (Selected)' },
  { key: 'setpoint', domain: 'sensor', name: 'Room Setpoint (Selected)' },
  { key: 'heatEnable', domain: 'binary_sensor', name: 'Heating Enable (Selected)' },
  { key: 'mode', domain: 'text_sensor', name: 'Control Mode' },
  { key: 'request', domain: 'sensor', name: 'Power House – P_req' },
  { key: 'curveWater', domain: 'sensor', name: 'Heating Curve Supply Target' },
  { key: 'effectiveWater', domain: 'sensor', name: 'Heating Supply Target (Effective)' },
  { key: 'applied1', domain: 'sensor', name: 'HP1 applied control level' },
  { key: 'applied2', domain: 'sensor', name: 'HP2 applied control level' },
  { key: 'boiler', domain: 'binary_sensor', name: 'Boiler command active' },
  { key: 'boilerHeat', domain: 'binary_sensor', name: 'Boiler command heat request' },
];

async function state(controller, label) {
  controller.haFixture?.assertHealthy();
  const value = await controller.values(warmupStateEntities);
  console.log(`STATE ${label} ${JSON.stringify(value)}`);
  return value;
}

async function waitState(controller, expected, label, interrupted, timeoutMs = 40000) {
  return waitFor(async () => {
    controller.haFixture?.assertHealthy();
    const value = await controller.values(warmupStateEntities);
    return Object.entries(expected).every(([key, expectedValue]) => typeof expectedValue === 'number' ? close(value[key], expectedValue) : typeof expectedValue === 'boolean' ? asBoolean(value[key]) === expectedValue : value[key] === expectedValue) ? value : false;
  }, label, { timeoutMs, intervalMs: 1500, interrupted });
}

async function selected(controller, name, value, interrupted) {
  return waitNumber(controller, name, (actual) => { controller.haFixture?.assertHealthy(); return close(actual, value); }, `${name}=${value}`, { timeoutMs: 45000, interrupted });
}

async function setRoom(controller, simulator, source, value, interrupted) {
  if (source === 'HA input') await controller.haFixture.send({ room: value });
  else await write(source === 'API input' ? controller : simulator, 'number', source === 'API input' ? 'api_input_room_temperature' : 'Thermostat room temperature', value);
  await selected(controller, 'Room Temperature (Selected)', value, interrupted);
}

async function setpoint(controller, simulator, source, value, interrupted) {
  if (source === 'HA input') await controller.haFixture.send({ setpoint: value });
  else await write(source === 'API input' ? controller : simulator, 'number', source === 'API input' ? 'api_input_room_setpoint' : 'Thermostat room setpoint', value);
  await selected(controller, 'Room Setpoint (Selected)', value, interrupted);
}

async function establish(controller, simulator, interrupted, { source = 'API input', strategy = 'Power House', room = 19, goal = 18, time = 5, maximum = 1 } = {}) {
  await write(controller, 'number', 'api_input_outside_temperature', 8);
  await write(controller, 'switch', prefix + 'enabled', false);
  await write(controller, 'select', 'CM Override', 'Auto');
  await write(controller, 'select', 'Flow Control Mode', 'Flow Setpoint');
  await write(controller, 'select', 'Heating Control Mode', strategy);
  await write(controller, 'select', 'External Heat Demand Source', 'Disabled');
  await write(controller, 'select', 'Heating Supply Target Source', 'Heating curve');
  await write(controller, 'select', 'Room Temperature Source', source);
  await write(controller, 'select', 'Room Setpoint Source', source);
  await write(controller, 'number', prefix + 'trigger', 1.5);
  await write(controller, 'number', prefix + 'step', 0.1);
  await write(controller, 'number', prefix + 'step time', time);
  await write(controller, 'number', prefix + 'maximum offset', 0.3);
  await write(controller, 'number', prefix + 'maximum duration', maximum);
  await write(controller, 'number', 'Power House comfort below setpoint', 0.1);
  await write(controller, 'select', 'Heating Curve Control Profile', 'Comfort');
  await setRoom(controller, simulator, source, room, interrupted);
  await setpoint(controller, simulator, source, goal, interrupted);
  await write(controller, 'switch', prefix + 'enabled', true);
  await sleep(2500);
  await waitState(controller, { active: false, target: goal }, 'first value only establishes baseline', interrupted);
}

async function start(controller, simulator, interrupted, options = {}) {
  await establish(controller, simulator, interrupted, options);
  await setpoint(controller, simulator, options.source || 'API input', 21, interrupted);
  return waitState(controller, { active: true, target: (options.room ?? 19) + 0.1, offset: 0.1, setpoint: 21 }, 'warmup activated on selected thermostat edge', interrupted);
}

async function prepare(controller, simulator, interrupted) {
  await write(controller, 'select', 'CM Override', 'Force CM0');
  await write(controller, 'switch', 'OpenQuatt Enabled', true);
  await write(controller, 'switch', 'OpenTherm Enabled', true);
  await write(controller, 'switch', 'CIC - Enable polling', false);
  await write(controller, 'switch', 'Manual Cooling Enable', false);
  await write(controller, 'switch', 'api_input_cooling_enable', false);
  await write(controller, 'select', 'Heating Enable Source', 'OT thermostat');
  await write(simulator, 'switch', 'Thermostat CH demand', false);
  await write(simulator, 'switch', 'ODU external system pump flow', true);
  await write(controller, 'select', 'Outside Temperature Source', 'API input');
  await write(controller, 'number', 'api_input_outside_temperature', 8);
  await waitValue(controller, 'binary_sensor', 'Heating Enable (Selected)', false, 'no thermostat heat permission', { interrupted });
}

async function boundaries(controller, simulator, interrupted) {
  for (const source of ['API input', 'OT thermostat', ...(controller.haFixture ? ['HA input'] : [])]) {
    await establish(controller, simulator, interrupted, { source });
    await setpoint(controller, simulator, source, 19.5, interrupted);
    await waitState(controller, { active: false, target: 19.5 }, `${source} exact threshold does not activate`, interrupted);
    await start(controller, simulator, interrupted, { source });
    await waitState(controller, { heatEnable: false, applied1: 0, applied2: 0, boiler: false, boilerHeat: false }, 'warmup does not grant actuator/boiler heat permission', interrupted);
    await setRoom(controller, simulator, source, 18.5, interrupted);
    await waitState(controller, { active: true, target: 19.1 }, 'cooler room does not lower fixed target', interrupted);
    await setRoom(controller, simulator, source, 19.5, interrupted);
    await waitState(controller, { active: true, target: 19.6, setpoint: 21 }, 'reached step advances; true comfort goal stays intact', interrupted);
    await setpoint(controller, simulator, source, 21.5, interrupted);
    await waitState(controller, { active: true, target: 19.6, setpoint: 21.5 }, 'further raise retains session/step', interrupted);
    await setpoint(controller, simulator, source, 21, interrupted);
    await waitState(controller, { active: false, target: 21, status: 'Setpoint lowered' }, 'lower setpoint cancels', interrupted);
    await start(controller, simulator, interrupted, { source });
    await write(controller, 'switch', prefix + 'enabled', false);
    await waitState(controller, { active: false, target: 21 }, 'disable hands back immediately', interrupted);
    await write(controller, 'switch', prefix + 'enabled', true);
    await sleep(2500);
    await waitState(controller, { active: false }, 'enabling does not replay old edge', interrupted);
    await start(controller, simulator, interrupted, { source });
    await setRoom(controller, simulator, source, 21, interrupted);
    await waitState(controller, { active: false, target: 21, status: 'Comfort reached' }, 'real thermostat comfort ends session', interrupted);
    console.log(`PASS ${source} threshold, fixed step, raise/lower, switch, true comfort`);
  }

  await start(controller, simulator, interrupted);
  await write(controller, 'switch', 'OpenQuatt Enabled', false);
  await waitState(controller, { active: false, applied1: 0, applied2: 0, boiler: false }, 'disabled controller hands back and does not heat', interrupted);
  await write(controller, 'switch', 'OpenQuatt Enabled', true);
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  await write(simulator, 'number', 'Thermostat room setpoint', 4.5);
  await waitState(controller, { active: false, status: 'Input unavailable' }, 'invalid OT setpoint cannot retain warmup through selected hold', interrupted);
  if (controller.haFixture) {
    await start(controller, simulator, interrupted, { source: 'HA input' });
    await controller.haFixture.send({ room: 'unavailable' });
    await waitState(controller, { active: false, status: 'Input unavailable' }, 'invalid HA room cannot retain warmup through selected hold', interrupted);
    await controller.haFixture.send({ room: 19 });
    await selected(controller, 'Room Temperature (Selected)', 19, interrupted);
    await waitState(controller, { active: false, target: 21 }, 'valid HA room recovery establishes baseline only', interrupted);
  }
  await start(controller, simulator, interrupted);
  await write(simulator, 'switch', 'Thermostat CH demand', true);
  await waitState(controller, { active: true, heatEnable: true }, 'delayed CH permission preserves already started session', interrupted);
  await write(simulator, 'switch', 'Thermostat CH demand', false);
  await waitState(controller, { active: true, heatEnable: false }, 'withdrawing CH permission keeps limiter without granting heat', interrupted);
  await write(controller, 'select', 'Room Setpoint Source', 'OT thermostat');
  await waitState(controller, { active: false }, 'A to B invalidates warmup', interrupted);
  await write(controller, 'select', 'Room Setpoint Source', 'API input');
  await waitState(controller, { active: false, target: 21 }, 'B to A does not replay old raise', interrupted);
  await start(controller, simulator, interrupted);
  await write(controller, 'number', prefix + 'trigger', 2);
  await waitState(controller, { active: false }, 'parameter changes invalidate warmup', interrupted);
  await start(controller, simulator, interrupted);
  await write(controller, 'select', 'External Heat Demand Source', 'API input');
  await waitState(controller, { active: false }, 'external power demand bypasses warmup', interrupted);
  await start(controller, simulator, interrupted);
  await controller.press('CM100 Start');
  await waitState(controller, { active: false, mode: 'CM100' }, 'actual service container invalidates warmup', interrupted);
  await controller.press('Manual Flow Start');
  await waitFor(async () => asBoolean((await controller.request('/openquatt/service/status')).entities.manualFlowActive.value), 'actual manual flow task is active', { interrupted, timeoutMs: 30000 });
  await waitState(controller, { active: false, mode: 'CM100' }, 'actual manual flow service bypasses warmup', interrupted);
  await controller.press('Manual Flow Abort');
  await controller.press('CM100 Stop');
  await waitFor(async () => {
    const value = await controller.value('text_sensor', 'Control Mode');
    return value !== 'CM100' ? { mode: value } : false;
  }, 'manual flow service left CM100', { interrupted, timeoutMs: 50000 });
  await start(controller, simulator, interrupted);
  await write(controller, 'select', 'CM Override', 'Force CM98');
  await waitState(controller, { active: false }, 'service control override bypasses warmup', interrupted);

  await curve(controller, simulator, interrupted);
  console.log('PASS source roundtrip, delayed permission, settings, external power/water, manual, service and Heating Curve');
}

async function curve(controller, simulator, interrupted) {
  await start(controller, simulator, interrupted, { strategy: 'Water Temperature Control (heating curve)' });
  // Warmup diagnostics and the curve sensor use different scheduling ticks.
  await healthySleep(controller, 15000, interrupted);
  const before = await state(controller, 'curve settled active');
  await setRoom(controller, simulator, 'API input', 19.5, interrupted);
  await waitState(controller, { active: true, target: 19.6 }, 'curve progresses same shared steps', interrupted);
  await healthySleep(controller, 15000, interrupted);
  const after = await state(controller, 'curve settled reached step');
  assert(close(after.curveWater, before.curveWater, 0.11), 'curve warmup unexpectedly changed weather-based water target');
  await write(controller, 'select', 'Heating Supply Target Source', 'API input');
  await waitState(controller, { active: false }, 'external water target bypasses warmup', interrupted);
  console.log(`PASS Heating Curve shared steps and steady water target, external water bypass ${JSON.stringify({ before, after })}`);
}

async function healthySleep(controller, milliseconds, interrupted) {
  const end = Date.now() + milliseconds;
  while (Date.now() < end) {
    controller.haFixture?.assertHealthy();
    if (interrupted()) throw new Error('HIL interrupted');
    await sleep(Math.min(10000, end - Date.now()));
  }
  controller.haFixture?.assertHealthy();
}

async function regulation(controller, simulator, interrupted) {
  assert(controller.haFixture, 'regulation stage requires the actual encrypted native HA fixture for bench supply temperature');
  await controller.haFixture.send({ supply: 25, room: 19, setpoint: 18, heat: false, cool: false });
  await write(controller, 'select', 'Water Supply Source', 'HA input');
  await selected(controller, 'Water Supply Temp (Selected)', 25, interrupted);
  await write(controller, 'number', 'Power House demand rise time', 2);
  await write(controller, 'number', 'Power House temperature reaction', 3000);
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  await waitState(controller, { heatEnable: false, applied1: 0, applied2: 0, boiler: false, boilerHeat: false }, 'default warmup does not grant heat while CH is off', interrupted);
  await write(simulator, 'switch', 'Thermostat CH demand', true);
  await waitState(controller, { active: true, heatEnable: true }, 'default step accepts later real thermostat CH permission', interrupted);
  // Allow the unmodified product demand ramp to settle; capture the computed
  // request and actual dispatched levels rather than relying on diagnostics.
  await healthySleep(controller, 140000, interrupted);
  const limited = await state(controller, 'Power House default step request');
  assert(asFiniteNumber(limited.request) !== null, 'limited Power House demand is unavailable');
  await write(controller, 'switch', prefix + 'enabled', false);
  await waitState(controller, { active: false, target: 21 }, 'normal thermostat target handed back', interrupted);
  await healthySleep(controller, 140000, interrupted);
  const normal = await state(controller, 'Power House unrestricted request');
  assert(asFiniteNumber(normal.request) !== null && Number(normal.request) > Number(limited.request) + 100, 'controlled target did not reduce actual computed Power House request');
  console.log(`PASS Power House demand comparison ${JSON.stringify({ limited, normal })}`);
  await write(simulator, 'switch', 'Thermostat CH demand', false);
  await waitState(controller, { heatEnable: false, applied1: 0, applied2: 0, boiler: false, boilerHeat: false }, 'withdrawn CH stops actual dispatched heat/boiler', interrupted, 60000);

  await regulationTail(controller, simulator, interrupted);
}

async function regulationTail(controller, simulator, interrupted) {
  assert(controller.haFixture, 'regulation tail requires actual native HA supply input');
  await controller.haFixture.send({ supply: 25, room: 19, setpoint: 18, heat: false, cool: false });
  await write(controller, 'select', 'Water Supply Source', 'HA input');
  await selected(controller, 'Water Supply Temp (Selected)', 25, interrupted);
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  await write(controller, 'number', 'Power House comfort below setpoint', 0.3);
  await write(controller, 'number', prefix + 'maximum offset', 0.2);
  await write(controller, 'number', 'api_input_outside_temperature', 30);
  await selected(controller, 'Outside Temperature (Selected)', 30, interrupted);
  await waitFor(async () => {
    controller.haFixture.assertHealthy();
    const settled = await controller.values(warmupStateEntities);
    const request = warmupNumber(settled.request);
    if (settled.mode === 'CM0' && asBoolean(settled.heatEnable) === false && warmupNumber(settled.applied1) === 0 && warmupNumber(settled.applied2) === 0 && asBoolean(settled.boiler) === false && asBoolean(settled.boilerHeat) === false && request !== null && request >= 0 && request < 100) {
      console.log(`STATE custom-band settled standstill ${JSON.stringify(settled)}`);
      return settled;
    }
    return false;
  }, 'custom-band actual standstill and settled near-zero demand before CH edge', { timeoutMs: 360000, intervalMs: 2000, interrupted });
  // Parameter changes cancel; establish a genuine new thermostat edge.
  await setpoint(controller, simulator, 'OT thermostat', 18, interrupted);
  await sleep(2500);
  await setpoint(controller, simulator, 'OT thermostat', 21, interrupted);
  await waitState(controller, { active: true, target: 19.1 }, 'custom comfort band retains small limiting step', interrupted);
  await write(simulator, 'switch', 'Thermostat CH demand', true);
  await healthySleep(controller, 90000, interrupted);
  const low = await waitState(controller, { active: true, applied1: 0, applied2: 0, boiler: false, boilerHeat: false }, 'low-load custom comfort has no forced minimum compressor/boiler start', interrupted);
  const lowRequest = warmupNumber(low.request);
  assert(lowRequest !== null && lowRequest >= 0 && lowRequest < 100, 'low-load demand is unavailable or unexpectedly material');
  await write(simulator, 'switch', 'Thermostat CH demand', false);
  await write(controller, 'number', 'api_input_outside_temperature', 8);
  console.log(`PASS custom comfort .3/step .1/max .2 low-load no forced floor ${JSON.stringify(low)}`);
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  await write(controller, 'select', 'Cooling Dew Point Source', 'API input');
  await write(controller, 'number', 'api_input_cooling_dew_point', 10);
  await write(controller, 'switch', 'Cooling Room Request Required', false);
  await write(controller, 'select', 'Cooling Enable Source', 'API input');
  await write(controller, 'switch', 'api_input_cooling_enable', true);
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM5', 'real cooling mode entry', { timeoutMs: 100000, interrupted });
  const cooling = await waitState(controller, { active: false, status: 'Mode changed' }, 'real cooling transition cancels warmup', interrupted);
  await write(controller, 'switch', 'api_input_cooling_enable', false);
  await waitFor(async () => {
    const mode = await controller.value('text_sensor', 'Control Mode');
    return mode !== 'CM5' ? { mode } : false;
  }, 'cooling permission withdrawn', { timeoutMs: 100000, interrupted });
  console.log(`PASS actual CM5 cooling invalidates warmup ${JSON.stringify(cooling)}`);
}

async function timing(controller, simulator, interrupted) {
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  const started = Date.now();
  let lastOffset = 0.1;
  while (Date.now() - started < 620000) {
    if (interrupted()) throw new Error('HIL interrupted');
    const value = await state(controller, 'five-minute timeout');
    assert(asBoolean(value.active), 'warmup stopped before real timeout test finished');
    assert(warmupNumber(value.offset) !== null && warmupNumber(value.target) !== null, 'timeout telemetry is unavailable');
    assert(Number(value.offset) <= 0.31, 'offset exceeded configured maximum');
    assert(Number(value.target) >= 19.075, 'timeout target moved backwards');
    const offset = Number(value.offset);
    if (offset > lastOffset + 0.05) {
      assert(Date.now() - started >= (offset < 0.25 ? 280000 : 570000), 'warmup timeout advanced too early');
      lastOffset = offset;
    }
    if (offset >= 0.29) break;
    await sleep(15000);
  }
  assert(lastOffset >= 0.29, 'two real five-minute timeouts did not reach max offset');
  console.log(`PASS two real 5 min timeouts, elapsed=${Date.now() - started}ms`);
}

async function stale(controller, simulator, interrupted) {
  await start(controller, simulator, interrupted, { time: 120 });
  const started = Date.now();
  await waitState(controller, { active: false, status: 'Input unavailable' }, 'real API room freshness expiry cancels (no held values)', interrupted, 660000);
  assert(Date.now() - started >= 570000, 'room freshness expired earlier than production ten minutes');
  // The five-minute selected-input hold may still display the old value. It
  // must not preserve a session or allow a timeout boost.
  await write(controller, 'number', 'api_input_room_temperature', 19);
  await selected(controller, 'Room Temperature (Selected)', 19, interrupted);
  await waitState(controller, { active: false, target: 21 }, 'freshness recovery establishes new baseline only', interrupted);
  console.log(`PASS real 10 min freshness expiry and recovery, elapsed=${Date.now() - started}ms`);
}

async function restart(controller, simulator, interrupted, waitForProfile) {
  await start(controller, simulator, interrupted, { source: 'OT thermostat' });
  const saved = await captureExtra({ controller, simulator });
  await sleep(2500);
  await controller.press('Restart');
  await sleep(6000);
  await waitForProfile();
  // API input samples are runtime-only. Refresh the bench's outside sample so
  // the existing conservative frost gate can return to idle after the reboot.
  await write(controller, 'number', 'api_input_outside_temperature', 8);
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'fresh bench outside input clears reboot frost gate', { timeoutMs: 50000, interrupted });
  const actual = await captureExtra({ controller, simulator });
  for (const setting of warmupExtraSettings) assert(JSON.stringify(saved.values[setting.key]) === JSON.stringify(actual.values[setting.key]), `restart lost ${setting.name}`);
  await waitState(controller, { active: false, target: 21 }, 'restart retains settings but never resumes warmup/edge', interrupted);
  console.log('PASS real reboot: six settings persisted; runtime session and previous setpoint reset');
}

export async function waitDurationHandback(controller, interrupted, timeoutMs = 20000) {
  return waitState(controller, { active: false, status: 'Time limit reached', target: 21 }, 'coherent duration deadline handback', interrupted, timeoutMs);
}

async function duration(controller, simulator, interrupted) {
  await start(controller, simulator, interrupted, { source: 'OT thermostat', time: 120, maximum: 1 });
  const started = Date.now();
  const heap = [];
  while (Date.now() - started < 3700000) {
    if (interrupted()) throw new Error('HIL interrupted');
    await write(controller, 'number', 'api_input_outside_temperature', 8);
    const value = await state(controller, 'one-hour duration');
    const diagnostics = await controller.values([
      { key: 'free', domain: 'sensor', name: 'Heap Free' },
      { key: 'minimum', domain: 'sensor', name: 'Heap Min Free' },
      { key: 'largest', domain: 'sensor', name: 'Heap Max Block' },
      { key: 'fragmentation', domain: 'sensor', name: 'Heap Fragmentation' },
      { key: 'psram', domain: 'sensor', name: 'PSRAM Free' },
    ]);
    heap.push({ elapsedMs: Date.now() - started, ...diagnostics });
    console.log(`MEMORY ${JSON.stringify(heap.at(-1))}`);
    if (asBoolean(value.active) === false) {
      const firstInactiveElapsedMs = Date.now() - started;
      await waitDurationHandback(controller, interrupted);
      assert(firstInactiveElapsedMs >= 3550000, 'one-hour duration ended early');
      console.log(`PASS real 1 h deadline; elapsed=${Date.now() - started}ms`);
      return { firstInactiveElapsedMs, durationElapsedMs: Date.now() - started, heap };
    }
    assert(close(value.target, 19.1), 'fixed target moved during room-constant one-hour soak');
    await sleep(30000);
  }
  throw new Error('real one-hour maximum duration did not terminate warmup');
}

export const controlledWarmupScenario = {
  name: 'controlled-warmup', captureExtra, prepare, afterRestore: restoreExtra,
  preflight({ stage }) {
    if (['regulation', 'regulation-tail'].includes(stage)) assert(process.env.OQ_HIL_NATIVE_API_KEY, 'regulation requires a private native HA fixture key and matching lab-only firmware before any device mutation');
  },
  validateSnapshot(snapshot) { validateWarmupExtra(snapshot.extra); },
  async beforeRestore({ controller }) {
    const errors = [];
    const attempt = async (operation) => { try { await operation(); } catch (error) { errors.push(error); } };
    // An unacknowledged abort must not prevent independent safe writes. OTA
    // remains blocked unless every operation and the actual CM0 gate succeed.
    await attempt(() => write(controller, 'select', 'CM Override', 'Force CM0'));
    await attempt(() => controller.press('Manual Flow Abort'));
    // Manual Flow Abort retains the neutral CM100 container even when idle.
    // Explicitly close it before requiring actual standby for recovery OTA.
    await attempt(() => controller.press('CM100 Stop'));
    await attempt(() => write(controller, 'number', 'api_input_outside_temperature', 8));
    await attempt(() => write(controller, 'switch', 'Manual Cooling Enable', false));
    await attempt(() => write(controller, 'switch', 'api_input_cooling_enable', false));
    await attempt(() => write(controller, 'switch', 'api_input_heating_enable', false));
    await attempt(() => waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'manual service cleared; CM0 before recovery OTA', { timeoutMs: 50000 }));
    if (errors.length) throw new AggregateError(errors, 'warmup recovery safety writes failed; OTA blocked');
  },
  async execute({ stage, controller, simulator, interrupted, waitForProfile }) {
    if (process.env.OQ_HIL_NATIVE_API_KEY) {
      controller.haFixture = await startNativeHaFixture(new URL(controller.baseUrl).hostname);
    }
    try {
      const skipped = [];
      if (stage === 'regulation') await curve(controller, simulator, interrupted);
      for (const [name, test] of [['boundaries', boundaries], ['curve', curve], ['regulation', regulation], ['regulation-tail', regulationTail], ['timing', timing], ['stale', stale]]) {
        if ((stage === 'all' && !['curve', 'regulation-tail'].includes(name)) || stage === name || (['timers', 'short-timers'].includes(stage) && ['timing', 'stale'].includes(name))) {
          if (name === 'regulation' && !controller.haFixture) {
            skipped.push('regulation: native HA supply-temperature fixture unavailable');
            console.log('SKIP actual regulation/CM5: native HA fixture unavailable');
          } else await test(controller, simulator, interrupted);
        }
      }
      if (stage === 'all' || ['timers', 'short-timers'].includes(stage) || stage === 'restart') await restart(controller, simulator, interrupted, waitForProfile);
      if (stage === 'all' || stage === 'timers' || stage === 'duration') return { ...await duration(controller, simulator, interrupted), nativeHa: Boolean(controller.haFixture), untestedTransports: ['MQTT', 'CIC', ...(!controller.haFixture ? ['native HA'] : [])], skipped };
      return { stage, completed: true, nativeHa: Boolean(controller.haFixture), skipped };
    } finally {
      await controller.haFixture?.stop();
      delete controller.haFixture;
    }
  },
};
