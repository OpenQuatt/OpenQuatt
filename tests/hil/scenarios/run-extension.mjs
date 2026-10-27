import { asBoolean, asFiniteNumber, entityPath } from '../../../scripts/hil/rest-client.mjs';
import { waitFor, waitValue } from '../../../scripts/hil/wait.mjs';
import { restoreV2SimulatorProfiles } from './v2-performance.mjs';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const close = (a, b, tolerance = 0.025) => a !== null && Math.abs(a - b) <= tolerance;
function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function verified(client) {
  return {
    values: client.values.bind(client),
    value: client.value.bind(client),
    press: client.press.bind(client),
    async setNumber(name, value) {
      const details = await client.request(`${entityPath('number', name)}?detail=all`);
      const minimum = asFiniteNumber(details?.min_value);
      const maximum = asFiniteNumber(details?.max_value);
      const step = asFiniteNumber(details?.step);
      assert(minimum !== null && maximum !== null && step !== null && step > 0, `${name}: missing numeric limits`);
      assert(value >= minimum, `${name}: below minimum`);
      assert(value <= maximum, `${name}: above maximum`);
      // REST formats number states to their trait precision (for example
      // 19.45 reads as 19.5 at step 0.1). Selected inputs are checked separately.
      const tolerance = step / 2 + 0.0001;
      await client.setNumber(name, value);
      await waitFor(async () => close(asFiniteNumber(await client.value('number', name)), value, tolerance),
        `${name}: write confirmed`, { timeoutMs: 10000, intervalMs: 500 });
    },
    async setSelect(name, value) {
      await client.setSelect(name, value);
      await waitValue(client, 'select', name, value, `${name}: selection confirmed`, { timeoutMs: 10000 });
    },
    async setSwitch(name, value) {
      await client.setSwitch(name, value);
      await waitFor(async () => asBoolean(await client.value('switch', name)) === value,
        `${name}: switch confirmed`, { timeoutMs: 10000, intervalMs: 500 });
    },
  };
}

const OBSERVATIONS = [
  ['phase', 'text_sensor', 'Power House run extension status'],
  ['mode', 'sensor', 'HP1 - Working Mode'],
  ['frequency', 'sensor', 'HP1 - Compressor frequency'],
  ['peerFrequency', 'sensor', 'HP2 - Compressor frequency'],
  ['peerMode', 'sensor', 'HP2 - Working Mode'],
  ['baseW', 'sensor', 'Power House run extension base power'],
  ['floorW', 'sensor', 'Power House run extension floor power'],
  ['requestW', 'sensor', 'Strategy requested power'],
  ['stopC', 'sensor', 'Power House run extension comfort stop'],
  ['restartC', 'sensor', 'Power House run extension warm restart'],
  ['roomC', 'sensor', 'Room Temperature (Selected)'],
  ['setpointC', 'sensor', 'Room Setpoint (Selected)'],
  ['outsideC', 'sensor', 'Outside Temperature (Selected)'],
  ['waterFactor', 'sensor', 'Strategy water limit factor'],
  ['offRemainingS', 'sensor', 'HIL HP1 minimum-off remaining'],
  ['runtimeRemainingS', 'sensor', 'HIL HP1 minimum-runtime remaining'],
  ['coupledF1OutletC', 'sensor', 'HIL coupled F1 outlet'],
  ['inletC', 'sensor', 'HP1 - Water in temperature'],
  ['flowLph', 'sensor', 'HP1 - Flow'],
  ['idlePeerReady', 'binary_sensor', 'HIL idle HP2 F1 ready'],
];

async function observe(controller) {
  const raw = await controller.values(OBSERVATIONS.map(([key, domain, name]) => ({ key, domain, name })));
  return Object.fromEntries(Object.entries(raw).map(([key, value]) =>
    [key, key === 'phase' ? String(value) : key === 'idlePeerReady' ? asBoolean(value) : asFiniteNumber(value)]));
}

async function waitState(controller, interrupted, predicate, label, timeoutMs = 120000) {
  let last;
  const match = await waitFor(async () => {
    last = await observe(controller);
    return predicate(last) ? last : false;
  }, label, { timeoutMs, intervalMs: 3000, interrupted }).catch((error) => {
    throw new Error(`${error.message}; last state: ${JSON.stringify(last)}`);
  });
  console.log(`PASS ${label}: ${JSON.stringify(match)}`);
  return match;
}

async function holdState(controller, interrupted, predicate, label, durationMs) {
  const end = Date.now() + durationMs;
  let last;
  do {
    if (interrupted()) throw new Error('run-extension interrupted');
    last = await observe(controller);
    assert(predicate(last), `${label}: ${JSON.stringify(last)}`);
    await sleep(4000);
  } while (Date.now() < end);
  console.log(`PASS ${label}: ${JSON.stringify(last)}`);
  return last;
}

async function outside(controller, simulator, value, interrupted) {
  await simulator.setNumber('ODU 1 outside temperature', value);
  await controller.setNumber('api_input_outside_temperature', value);
  await waitState(controller, interrupted, (s) => close(s.outsideC, value), `outside ${value} C selected`);
}

export async function prepareRunExtension(controller, simulator, interrupted, snapshot) {
  controller = verified(controller);
  simulator = verified(simulator);
  assert(snapshot.simulatorActive.hp1.address === 1 && snapshot.simulatorActive.hp2.address === 2,
    'run-extension requires the documented desktop addresses 1/2');
  await controller.setSelect('CM Override', 'Force CM0');
  await controller.setSwitch('OpenQuatt Enabled', true);
  await controller.setSelect('Silent Mode Override', 'Off');
  await controller.setSwitch('api_input_heating_enable', false);
  await controller.setSwitch('api_input_cooling_enable', false);
  await controller.setSwitch('Manual Cooling Enable', false);
  await controller.setSwitch('Power House run extension', false);
  await controller.setSwitch('Controlled warmup enabled', false);
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0', { timeoutMs: 480000, interrupted });
  await simulator.setSelect('ODU 1 profile (pending)', 'V2 old');
  await simulator.setSelect('ODU 2 profile (pending)', 'V2 new');
  await simulator.setNumber('ODU 1 Modbus address (pending)', snapshot.simulatorActive.hp1.address);
  await simulator.setNumber('ODU 2 Modbus address (pending)', snapshot.simulatorActive.hp2.address);
  await simulator.press('Apply ODU profiles and addresses, then reboot');
  await waitFor(async () => {
    const hp1 = await simulator.value('text_sensor', 'ODU 1 diagnostics');
    const hp2 = await simulator.value('text_sensor', 'ODU 2 diagnostics');
    return String(hp1).includes('addr=1 profile=V2 old model') &&
      String(hp2).includes('addr=2 profile=V2 new model');
  }, 'active V2 old/new simulator profiles', { timeoutMs: 150000, intervalMs: 2000, interrupted });
  await controller.press('HIL Detect HP1 ODU generation');
  await controller.press('HIL Detect HP2 ODU generation');
  await waitValue(controller, 'text_sensor', 'HIL HP1 Generation variant', 'V2 old model', 'HP1 V2 old identity',
    { timeoutMs: 180000, interrupted });
  await waitValue(controller, 'text_sensor', 'HIL HP2 Generation variant', 'V2 new model', 'HP2 V2 new identity',
    { timeoutMs: 180000, interrupted });
  await controller.setSwitch('CIC - Enable polling', false);
  await controller.setNumber('HIL CIC Water Supply Fixture', 30.66);
  await controller.setSwitch('HIL CIC Water Supply Fixture Enable', true);
  await controller.setSelect('Water Supply Source', 'CIC');
  await controller.setSelect('Outside Temperature Source', 'API input');
  await controller.setSelect('Room Temperature Source', 'API input');
  await controller.setSelect('Room Setpoint Source', 'API input');
  await controller.setSelect('External Heat Demand Source', 'API input');
  await controller.setSelect('Heating Enable Source', 'API input');
  await controller.setSelect('Cooling Enable Source', 'API input');
  await controller.setSelect('Heating Control Mode', 'Power House');
  await controller.setSelect('Flow Source', 'Outdoor unit');
  await controller.setSelect('Outdoor Unit Flow Mode', 'Flowmeter HP1');
  // The lab pump model tops out at 900 l/h. Fixed ordinary iPWM avoids
  // spending the protected-run window waiting for the PI to saturate.
  await controller.setNumber('Manual iPWM', 50);
  await controller.setSelect('Flow Control Mode', 'Manual PWM');
  await simulator.setSwitch('ODU external system pump flow', true);
  await simulator.setNumber('ODU 1 water-in temperature', 27.9);
  await simulator.setNumber('ODU 2 water-in temperature', 27.9);
  await controller.setNumber('HP1 - Excluded frequency minimum', 0);
  await controller.setNumber('HP1 - Excluded frequency maximum', 0);
  // Keep the Duo peer observable but ineligible for dispatch, using the normal
  // supported frequency exclusion controls rather than injected faults.
  await controller.setNumber('HP2 - Excluded frequency minimum', 20);
  await controller.setNumber('HP2 - Excluded frequency maximum', 120);
  await controller.setNumber('Minimum runtime', 180);
  await controller.setNumber('Maximum heating outdoor temperature', 25);
  await controller.setNumber('Power House temperature reaction', 0);
  await controller.setNumber('Power House demand rise time', 2);
  await controller.setNumber('Power House demand fall time', 1);
  await controller.setNumber('Power House comfort below setpoint', 0.1);
  await controller.setNumber('Power House comfort above setpoint', 0.3);
  await controller.setNumber('Power House run extension stop margin', 0.4);
  await controller.setNumber('Power House run extension restart cooldown', 0.2);
  await controller.setNumber('api_input_room_setpoint', 19.5);
  await controller.setNumber('api_input_room_temperature', 19.82);
  await controller.setNumber('api_input_external_heat_demand', 1000);
  await outside(controller, simulator, 13.5, interrupted);
  await waitState(controller, interrupted, (s) => close(s.roomC, 19.82) && close(s.setpointC, 19.5),
    'room and setpoint inputs selected');
  await controller.setSelect('CM Override', 'Auto');
  await controller.setSwitch('api_input_heating_enable', true);
  await waitFor(async () => {
    const values = await controller.values([
      { key: 'valid', domain: 'binary_sensor', name: 'Heating Enable Valid' },
      { key: 'selected', domain: 'binary_sensor', name: 'Heating Enable (Selected)' },
    ]);
    return asBoolean(values.valid) === true && asBoolean(values.selected) === true;
  }, 'valid selected heating enable', { timeoutMs: 90000, interrupted });
}

export async function testRunExtension({ controller, simulator, interrupted }) {
  controller = verified(controller);
  simulator = verified(simulator);
  const result = {};
  const maximumWater = asFiniteNumber(await controller.value('number', 'Maximum water temperature'));
  assert(maximumWater !== null, 'water-temperature limit must be captured before the hot-water case');
  await controller.setSwitch('Power House run extension', true);
  result.idle = await holdState(controller, interrupted,
    (s) => close(s.roomC, 19.82) && s.frequency === 0 && s.floorW === 0 && s.phase === 'inactive',
    'enabling extension above setpoint does not start the idle compressor', 20000);

  await controller.setNumber('api_input_room_temperature', 19.0);
  result.started = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.0) && s.mode === 2 && close(s.frequency, 20, 0.5),
    'cold room starts on F1', 420000);
  // Working-mode feedback can arrive before the production request loop has
  // observed its rising edge. Wait for that loop's confirmed runtime anchor.
  result.runtimeReady = await waitState(controller, interrupted,
    (s) => s.mode === 2 && close(s.frequency, 20, 0.5) && s.runtimeRemainingS > 90,
    'HIL minimum-runtime clock observes the confirmed start');
  await controller.setNumber('api_input_external_heat_demand', 1000);
  await controller.setNumber('api_input_room_temperature', 19.82);
  result.f1 = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.82) && s.phase === 'extending' && s.floorW > 2000 && close(s.stopC, 19.9) &&
      close(s.frequency, 20, 0.5) && s.waterFactor >= 0.999,
    'F1 may fill the configured extension margin');

  await outside(controller, simulator, 14.6, interrupted);
  result.warmF1 = await waitState(controller, interrupted,
    (s) => close(s.outsideC, 14.6) && s.phase === 'extending' && close(s.stopC, 19.9) &&
      close(s.frequency, 20, 0.5) && s.floorW > 2000 && s.floorW < 4000 && s.peerFrequency === 0,
    'V2 old keeps F1 above 14 C using the shared V2-new map');
  await holdState(controller, interrupted,
    (s) => s.phase === 'extending' && close(s.frequency, 20, 0.5) && close(s.stopC, 19.9),
    'V2 old warm-weather F1 remains stable', 12000);
  await outside(controller, simulator, 20, interrupted);
  result.mildF1 = await waitState(controller, interrupted,
    (s) => close(s.outsideC, 20) && s.phase === 'extending' && close(s.frequency, 20, 0.5) &&
      close(s.stopC, 19.9) && s.floorW > 3000 && s.floorW < 4000,
    'V2 old also extends on F1 at 20 C outside');
  await holdState(controller, interrupted,
    (s) => close(s.outsideC, 20) && s.phase === 'extending' && close(s.frequency, 20, 0.5) &&
      close(s.stopC, 19.9) && s.floorW > 3000 && s.floorW < 4000,
    'V2 old 20 C map output remains fresh over complete control cycles', 12000);
  await outside(controller, simulator, 14.6, interrupted);
  await controller.setNumber('Maximum water temperature', 60);
  await simulator.setNumber('ODU 1 water-in temperature', 47.0);
  await controller.setNumber('HIL CIC Water Supply Fixture', 48.94);
  result.hotWaterF1 = await waitState(controller, interrupted,
    (s) => s.phase === 'extending' && close(s.frequency, 20, 0.5) && close(s.stopC, 19.9) &&
      close(s.inletC, 47.0, 0.05) && close(s.flowLph, 900, 2) && close(s.coupledF1OutletC, 48.944, 0.02) &&
      s.floorW > 1900 && s.floorW < 2200 && s.peerFrequency === 0 && s.waterFactor >= 0.999,
    'F1 remains available with the supported water-balance root just below 49 C');
  await holdState(controller, interrupted,
    (s) => s.phase === 'extending' && close(s.frequency, 20, 0.5) && s.floorW > 1900 && s.floorW < 2200 &&
      close(s.inletC, 47.0, 0.05) && close(s.flowLph, 900, 2) && close(s.coupledF1OutletC, 48.944, 0.02),
    'near-boundary F1 output stays fresh over complete control cycles', 12000);
  await simulator.setNumber('ODU 1 water-in temperature', 27.9);
  await controller.setNumber('HIL CIC Water Supply Fixture', 30.66);
  await waitState(controller, interrupted,
    (s) => s.phase === 'extending' && s.floorW > 2700 && s.floorW < 3100,
    'normal water temperature and F1 power restored');
  await controller.setNumber('Maximum water temperature', maximumWater);
  // The broader model no longer raises Pmin at 14 C. Exercise the earlier
  // stop policy with an ordinary user exclusion that makes F3 the minimum.
  await controller.setNumber('api_input_room_temperature', 19.0);
  await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.0) && ['normal', 'extending'].includes(s.phase),
    'cold room is selected before raising the minimum frequency');
  await controller.setNumber('HP1 - Excluded frequency minimum', 20);
  await controller.setNumber('HP1 - Excluded frequency maximum', 26);
  result.f3Running = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.0) && s.mode === 2 && close(s.frequency, 30, 0.5) &&
      s.peerFrequency === 0 && ['normal', 'extending'].includes(s.phase),
    'active unit reaches F3 before testing its comfort stop');
  // The peer can serve F1, but is idle: its lower minimum must not extend HP1's run.
  await controller.setNumber('HP2 - Excluded frequency minimum', 0);
  await controller.setNumber('HP2 - Excluded frequency maximum', 0);
  result.peerReady = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.0) && s.mode === 2 && s.idlePeerReady && s.peerFrequency === 0 &&
      close(s.frequency, 30, 0.5) && ['normal', 'extending'].includes(s.phase),
    'idle peer has a valid F1 model and is allowed to start');
  await controller.setNumber('api_input_room_temperature', 19.82);
  result.f3Stop = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.82) && s.phase === 'comfort_stop' && s.floorW === 0 && close(s.stopC, 19.5) &&
      close(s.restartC, 19.4) && s.requestW === 0 && s.peerFrequency === 0 && s.idlePeerReady,
    'active F3 requests the earlier stop even with an available idle F1 peer');
  assert(result.f3Stop.runtimeRemainingS > 10, 'test missed the production minimum-runtime window');
  assert(result.f3Stop.frequency > 0 && result.f3Stop.mode === 2,
    'comfort stop bypassed the production minimum-runtime hold');
  result.idlePeer = await holdState(controller, interrupted,
    (s) => close(s.roomC, 19.82) && s.phase === 'comfort_stop' && s.requestW === 0 && s.floorW === 0 &&
      close(s.stopC, 19.5) && s.peerFrequency === 0 && s.idlePeerReady,
    'available idle peer cannot restore the extension floor', 8000);
  await controller.setNumber('HP2 - Excluded frequency minimum', 20);
  await controller.setNumber('HP2 - Excluded frequency maximum', 120);
  const earlyStops = [];
  result.stopped = await waitState(controller, interrupted,
    (s) => {
      // Allow five seconds for independently published Modbus/sensor samples.
      if (s.runtimeRemainingS > 5 && (s.frequency === 0 || s.mode !== 2)) earlyStops.push(s);
      return s.frequency === 0 && s.mode === 0 && s.floorW === 0 &&
        s.requestW === 0 && close(s.stopC, 19.5) && close(s.restartC, 19.4) &&
        // While the minimum-off guard removes all candidates, the public
        // status can mask wait_warm_restart as blocked because Pmin is absent.
        ['comfort_stop', 'wait_warm_restart', 'blocked'].includes(s.phase);
    },
    'compressor stops after the minimum runtime', 480000);
  assert(earlyStops.length === 0, `minimum runtime bypassed: ${JSON.stringify(earlyStops)}`);
  result.minimumRuntimeHeld = true;
  assert(result.stopped.offRemainingS > 10, 'stopped compressor has no observable minimum-off window');
  const restartNotBefore = Date.now() + (result.stopped.offRemainingS - 5) * 1000;
  // The protected first run is proved; use the HIL floor for subsequent runs.
  await controller.setNumber('Minimum runtime', 120);

  await controller.setNumber('api_input_room_temperature', 19.45);
  await waitState(controller, interrupted, (s) => close(s.roomC, 19.45), 'hysteresis room input selected');
  result.hysteresis = await holdState(controller, interrupted,
    (s) => close(s.roomC, 19.45) && s.frequency === 0 && s.floorW === 0 &&
      s.requestW === 0 && close(s.stopC, 19.5) && close(s.restartC, 19.4),
    'room above the reduced restart threshold stays stopped', 20000);
  await controller.setNumber('api_input_room_temperature', 19.39);
  await controller.setNumber('api_input_external_heat_demand', 1000);
  const restartPending = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.39), 'restart room input selected');
  assert(restartPending.offRemainingS > 10 && restartPending.frequency === 0,
    'test missed the production minimum-off window');
  const earlyRestarts = [];
  result.restart = await waitState(controller, interrupted,
    (s) => {
      // The guard invalidates its credit when starting, so its live remaining
      // value is meaningful only while stopped. Pin the observed stop deadline.
      if (Date.now() < restartNotBefore && s.frequency > 0) earlyRestarts.push(s);
      return close(s.roomC, 19.39) && s.mode === 2 && close(s.frequency, 30, 0.5) && s.peerFrequency === 0;
    },
    'cold room restarts on the permitted F3 after minimum off-time', 420000);
  assert(earlyRestarts.length === 0, `minimum off-time bypassed: ${JSON.stringify(earlyRestarts)}`);
  assert(Date.now() >= restartNotBefore, 'F3 restart bypassed minimum off-time');
  result.minimumOffTimeHeld = true;

  await controller.setNumber('api_input_external_heat_demand', 5000);
  await waitState(controller, interrupted, (s) => s.baseW >= 4500,
    'genuine demand reaches at least 4.5 kW');
  await controller.setNumber('api_input_room_temperature', 19.7);
  result.realDemand = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.7) && s.baseW >= 4500 && s.mode === 2 &&
      s.phase === 'normal' && close(s.stopC, 19.9) && s.floorW === 0,
    'genuine demand above minimum keeps heating above setpoint');
  await controller.setNumber('api_input_external_heat_demand', 1000);
  result.lowDemand = await waitState(controller, interrupted,
    (s) => s.baseW < 4000 && s.phase === 'comfort_stop' && s.requestW === 0 &&
      s.floorW === 0 && close(s.stopC, 19.5),
    'falling demand restores the earlier stop at the higher minimum', 180000);

  await waitState(controller, interrupted,
    (s) => s.frequency === 0 && s.mode === 0 && s.peerFrequency === 0,
    'first unit completes its protected run before testing V2 new', 480000);
  await controller.setNumber('HP1 - Excluded frequency minimum', 20);
  await controller.setNumber('HP1 - Excluded frequency maximum', 120);
  await controller.setNumber('HP2 - Excluded frequency minimum', 0);
  await controller.setNumber('HP2 - Excluded frequency maximum', 0);
  await simulator.setNumber('ODU 2 outside temperature', 14.6);
  await controller.setNumber('api_input_room_temperature', 19.0);
  result.newV2Started = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.0) && s.frequency === 0 && s.peerMode === 2 && close(s.peerFrequency, 20, 0.5),
    'V2 new starts on F1 with the same shared map', 420000);
  await controller.setNumber('api_input_room_temperature', 19.82);
  result.newV2F1 = await waitState(controller, interrupted,
    (s) => close(s.roomC, 19.82) && close(s.outsideC, 14.6) && s.frequency === 0 &&
      s.peerMode === 2 && close(s.peerFrequency, 20, 0.5) && s.phase === 'extending' &&
      close(s.stopC, 19.9) && s.floorW > 2000 && s.floorW < 4000,
    'V2 new also extends on F1 above 14 C');
  await holdState(controller, interrupted,
    (s) => s.frequency === 0 && close(s.peerFrequency, 20, 0.5) && s.phase === 'extending',
    'V2 new warm-weather F1 remains stable', 20000);
  await controller.setNumber('api_input_room_temperature', 20.0);
  result.timings = { minimumRuntimeS: 180, laterRuntimeS: 120, minimumOffTimeS: 60, acceleratedHilOnly: true };
  return result;
}

export async function restoreRunExtensionProfiles(context) {
  const { controller, snapshot, interrupted } = context;
  await restoreV2SimulatorProfiles(context);
  await waitValue(controller, 'select', 'CM Override', 'Force CM0', 'safe override before identity recovery',
    { interrupted });
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0 before identity recovery',
    { interrupted });
  // Normal firmware boots before the simulator profiles are restored. A short
  // simulator reboot can escape offline detection, so explicitly resample both
  // identities through a controller restart while the persisted CM0 is active.
  const { uptime } = await waitFor(async () => {
    const value = asFiniteNumber(await controller.value('sensor', 'Uptime raw'));
    return value !== null && value > 10 ? { uptime: value } : false;
  }, 'controller uptime before identity recovery', { timeoutMs: 90000, interrupted });
  await controller.press('Restart');
  let rebootSeen = false;
  await waitFor(async () => {
    const values = await controller.values([
      { key: 'uptime', domain: 'sensor', name: 'Uptime raw' },
      { key: 'firmware', domain: 'text_sensor', name: 'ESPHome Version' },
      { key: 'mode', domain: 'text_sensor', name: 'Control Mode' },
      { key: 'hp1', domain: 'text_sensor', name: 'HP1 - ODU generation variant' },
      { key: 'hp2', domain: 'text_sensor', name: 'HP2 - ODU generation variant' },
    ]).catch(() => {
      rebootSeen = true;
      return null;
    });
    if (!values) return false;
    const restartedUptime = asFiniteNumber(values.uptime);
    if (restartedUptime !== null && restartedUptime < uptime) rebootSeen = true;
    return rebootSeen && values.firmware === snapshot.firmware &&
      values.mode === 'CM0' && values.hp1 === snapshot.simulatorActive.hp1.profile &&
      values.hp2 === snapshot.simulatorActive.hp2.profile;
  }, 'normal controller reboot and restored ODU identities', { timeoutMs: 180000, intervalMs: 2000, interrupted });
  console.log('PASS restored simulator profiles and normal controller ODU identities');
}

export const runExtensionScenario = {
  name: 'run-extension',
  prepare: prepareRunExtension,
  execute: testRunExtension,
  afterRestore: restoreRunExtensionProfiles,
};
