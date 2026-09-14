import { asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';
import { waitFor, waitNumber, waitValue } from '../../../scripts/hil/wait.mjs';

const PROFILE_APPLY_BUTTON = 'Apply ODU profiles and addresses, then reboot';
const HP1_DETECT_BUTTON = 'HIL Detect HP1 ODU generation';
const HP2_DETECT_BUTTON = 'HIL Detect HP2 ODU generation';
const EXPECTED_POWER_W = 309.97883;
const EXPECTED_FIELD_POWER_W = EXPECTED_POWER_W + 30;
const EXPECTED_PMIN_W = 3072.6185;

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function closeEnough(actual, expected, tolerance) {
  return actual !== null && Math.abs(actual - expected) <= tolerance;
}

export function v2PowerInput({ voltageV, currentA, fanSpeed, status2108 = 0, pumpPowerW = 0 }) {
  return Math.max(
    0,
    5.93 +
      1.02579 * voltageV * currentA -
      0.0133119 * fanSpeed +
      ((status2108 & 0x0800) !== 0 ? pumpPowerW : 0) +
      ((status2108 & 0x0004) !== 0 ? 140 : 0) +
      ((status2108 & 0x0008) !== 0 ? 33.62 : 0),
  );
}

export function heatingPower({ flowLph, inletC, outletC, cp = 4186 }) {
  return (flowLph / 3600) * cp * (outletC - inletC);
}

async function waitSimulatorProfiles(simulator, interrupted, hp1, hp2) {
  return waitFor(async () => {
    const values = await simulator.values([
      { key: 'contract', domain: 'text_sensor', name: 'OpenQuatt Simulator Contract' },
      { key: 'hp1', domain: 'text_sensor', name: 'ODU 1 diagnostics' },
      { key: 'hp2', domain: 'text_sensor', name: 'ODU 2 diagnostics' },
    ]).catch(() => null);
    if (!values || values.contract !== 'openquatt-modbus-opentherm-v2') return false;
    return String(values.hp1).includes(`addr=${hp1.address} profile=${hp1.profile}`) &&
      String(values.hp2).includes(`addr=${hp2.address} profile=${hp2.profile}`)
      ? values
      : false;
  }, `simulator active configuration ${hp1.address}/${hp1.profile}, ${hp2.address}/${hp2.profile}`, {
    timeoutMs: 150000,
    intervalMs: 1500,
    interrupted,
  });
}

async function readOffsets(controller) {
  const values = await controller.values([
    { key: 'hp1In', domain: 'sensor', name: 'HIL HP1 Water in offset' },
    { key: 'hp1Out', domain: 'sensor', name: 'HIL HP1 Water out offset' },
    { key: 'hp2In', domain: 'sensor', name: 'HIL HP2 Water in offset' },
    { key: 'hp2Out', domain: 'sensor', name: 'HIL HP2 Water out offset' },
  ]);
  const offsets = Object.fromEntries(
    Object.entries(values).map(([key, value]) => [key, asFiniteNumber(value)]),
  );
  for (const [key, value] of Object.entries(offsets)) {
    assert(value !== null, `${key} water-temperature offset is unavailable`);
  }
  return offsets;
}

async function setManualTelemetry(simulator, index, fixture) {
  const prefix = `ODU ${index}`;
  await simulator.setSwitch(`${prefix} manual telemetry override`, false);
  await simulator.setNumber(`${prefix} manual working mode 2099`, fixture.workingMode);
  await simulator.setNumber(`${prefix} manual AC voltage`, fixture.voltageV);
  await simulator.setNumber(`${prefix} manual AC current`, fixture.currentA);
  await simulator.setNumber(`${prefix} manual fan speed`, fixture.fanSpeed);
  await simulator.setNumber(`${prefix} manual operating status 2108`, fixture.status2108);
  await simulator.setNumber(`${prefix} manual pump feedback 2137`, fixture.pumpFeedbackRaw);
  await simulator.setNumber(`${prefix} manual water-in temperature`, fixture.waterInC);
  await simulator.setNumber(`${prefix} manual water-out temperature`, fixture.waterOutC);
  await simulator.setNumber(`${prefix} manual flow`, fixture.flowLph);
  await simulator.setSwitch(`${prefix} manual telemetry override`, true);
}

async function setStatusAndWait(controller, simulator, status2108, expectedPowerW, interrupted, label) {
  await simulator.setNumber('ODU 1 manual operating status 2108', status2108);
  return waitNumber(controller, 'HP1 - Power Input', (value) => closeEnough(value, expectedPowerW, 0.15), label, {
    timeoutMs: 70000,
    intervalMs: 1500,
    interrupted,
  });
}

export async function prepareV2PerformanceScenario(controller, simulator, interrupted, snapshot) {
  assert(
    snapshot.simulatorActive.hp1.address === 1 && snapshot.simulatorActive.hp2.address === 2,
    `issue #667 HIL requires active ODU addresses 1/2; received ` +
      `${snapshot.simulatorActive.hp1.address}/${snapshot.simulatorActive.hp2.address}`,
  );
  await controller.setSelect('CM Override', 'Force CM0');
  await controller.setSwitch('Manual Cooling Enable', false);
  await controller.setSwitch('api_input_heating_enable', false);
  await controller.setSwitch('api_input_cooling_enable', false);
  await controller.setSwitch('CIC - Enable polling', false);
  await simulator.setSwitch('ODU 1 defrost', false);
  await simulator.setSwitch('ODU 2 defrost', false);
  await simulator.setSwitch('ODU 1 manual telemetry override', false);
  await simulator.setSwitch('ODU 2 manual telemetry override', false);
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0 start', {
    timeoutMs: 50000,
    interrupted,
  });

  await simulator.setSelect('ODU 1 profile (pending)', 'V2 old');
  await simulator.setSelect('ODU 2 profile (pending)', 'V2 new');
  await simulator.setNumber('ODU 1 Modbus address (pending)', snapshot.simulatorActive.hp1.address);
  await simulator.setNumber('ODU 2 Modbus address (pending)', snapshot.simulatorActive.hp2.address);
  await simulator.press(PROFILE_APPLY_BUTTON);
  await waitSimulatorProfiles(
    simulator,
    interrupted,
    { address: snapshot.simulatorActive.hp1.address, profile: 'V2 old model' },
    { address: snapshot.simulatorActive.hp2.address, profile: 'V2 new model' },
  );
  // Generation is intentionally sampled only at controller boot or by an
  // explicit request. The simulator profile changed after boot, so refresh
  // both validated identities before testing generation-dependent behavior.
  await controller.press(HP1_DETECT_BUTTON);
  await controller.press(HP2_DETECT_BUTTON);
  await waitValue(controller, 'text_sensor', 'HIL HP1 Generation variant', 'V2 old model', 'HP1 V2 identity', {
    timeoutMs: 150000,
    interrupted,
  });
  await waitValue(controller, 'text_sensor', 'HIL HP2 Generation variant', 'V2 new model', 'HP2 V2 identity', {
    timeoutMs: 150000,
    interrupted,
  });
  await waitNumber(controller, 'HIL HP1 Minimum heating frequency', (value) => value > 0,
    'HP1 validated runtime frequency mapping', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await waitNumber(controller, 'HIL HP2 Minimum heating frequency', (value) => value > 0,
    'HP2 validated runtime frequency mapping', { timeoutMs: 90000, intervalMs: 1500, interrupted });

  await controller.setNumber('HIL CIC Water Supply Fixture', 22.5);
  await controller.setSwitch('HIL CIC Water Supply Fixture Enable', true);
  await controller.setSelect('Water Supply Source', 'CIC');
  await waitNumber(
    controller,
    'HIL Water Supply Source Code',
    (value) => value === 3,
    'active CIC water-supply source',
    { timeoutMs: 30000, intervalMs: 1000, interrupted },
  );
  const supplyOffsetC = await waitNumber(
    controller,
    'HIL Water Supply Calibration Offset',
    (value) => Number.isFinite(value),
    'active CIC water-supply calibration offset',
    { timeoutMs: 30000, intervalMs: 1000, interrupted },
  );
  await controller.setNumber('HIL CIC Water Supply Fixture', 22.5 - supplyOffsetC);
  await controller.setSelect('Outside Temperature Source', 'API input');
  await controller.setSelect('Heating Control Mode', 'Power House');
  await controller.setSelect('Flow Source', 'Outdoor unit');
  await controller.setSelect('Outdoor Unit Flow Mode', 'Flowmeter HP1');

  const offsets = await readOffsets(controller);
  await setManualTelemetry(simulator, 1, {
    // Keep verified STOPPED telemetry until the incident manager has earned
    // its real production minimum-off credit. The performance phase switches
    // this fixture to heating only after the candidate-map sample is fresh.
    workingMode: 0,
    voltageV: 230,
    currentA: 1.3,
    fanSpeed: 200,
    status2108: 0,
    pumpFeedbackRaw: 300,
    waterInC: 22.5 - offsets.hp1In,
    waterOutC: 24.53 - offsets.hp1Out,
    flowLph: 1020,
  });
  await setManualTelemetry(simulator, 2, {
    workingMode: 0,
    voltageV: 230,
    currentA: 0,
    fanSpeed: 0,
    status2108: 0,
    pumpFeedbackRaw: 0,
    waterInC: 22.5 - offsets.hp2In,
    waterOutC: 22.5 - offsets.hp2Out,
    flowLph: 1020,
  });
  await waitNumber(controller, 'HP1 - Working Mode', (value) => value === 0,
    'fresh HP1 stopped-mode telemetry before minimum-off test',
    { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await controller.setNumber('api_input_outside_temperature', 12.5);

  await waitValue(controller, 'text_sensor', 'HIL HP1 Power Input quality', 'v2_estimated', 'HP1 V2 power quality', {
    timeoutMs: 90000,
    interrupted,
  });
  await waitValue(controller, 'text_sensor', 'HIL HP2 Power Input quality', 'v2_estimated', 'HP2 V2 power quality', {
    timeoutMs: 90000,
    interrupted,
  });
}

async function testPowerInput(controller, simulator, interrupted) {
  console.log('TEST V2 Power Input raw telemetry and R2108 auxiliary loads');
  const result = {
    baseW: await setStatusAndWait(controller, simulator, 0, EXPECTED_POWER_W, interrupted, 'base V2 Power Input'),
    bottomPlateW: await setStatusAndWait(controller, simulator, 0x0004, EXPECTED_POWER_W + 140, interrupted,
      'bottom-plate heater load'),
    crankcaseW: await setStatusAndWait(controller, simulator, 0x0008, EXPECTED_POWER_W + 33.62, interrupted,
      'crankcase heater load'),
    pumpW: await setStatusAndWait(controller, simulator, 0x0800, EXPECTED_POWER_W + 30, interrupted,
      'pump relay and feedback load'),
  };
  result.restoredBaseW = await setStatusAndWait(controller, simulator, 0, EXPECTED_POWER_W, interrupted,
    'base Power Input restored');

  await simulator.setSwitch('ODU 1 defrost', true);
  await waitValue(controller, 'binary_sensor', 'HP1 - Defrost', true, 'independent defrost telemetry', {
    timeoutMs: 70000,
    interrupted,
  });
  const counterAfterDefrost = asFiniteNumber(
    await controller.value('sensor', 'HIL HP1 Power Input sample counter'),
  );
  assert(counterAfterDefrost !== null, 'Power Input sample counter unavailable after defrost');
  result.defrostSampleCounter = await waitNumber(
    controller,
    'HIL HP1 Power Input sample counter',
    (value) => value > counterAfterDefrost,
    'fresh Power Input sample after defrost',
    { timeoutMs: 70000, intervalMs: 1500, interrupted },
  );
  result.defrostOnlyW = await waitNumber(controller, 'HP1 - Power Input',
    (value) => closeEnough(value, EXPECTED_POWER_W, 0.15),
    'defrost does not add bottom-plate load', { timeoutMs: 70000, intervalMs: 1500, interrupted });
  await simulator.setSwitch('ODU 1 defrost', false);
  console.log('PASS V2 Power Input fixture and defrost separation');
  return result;
}

const FIELD_INPUT_COUNTERS = [
  ['flowCounter', 'HIL Flow sample counter'],
  ['inletCounter', 'HIL HP1 Water in sample counter'],
  ['outletCounter', 'HIL HP1 Water out sample counter'],
  ['powerInputCounter', 'HIL HP1 Power Input sample counter'],
];
const HEAT_POWER_COUNTER = ['heatPowerCounter', 'HIL HP1 Heat Power sample counter'];
const COP_COUNTER = ['copCounter', 'HIL HP1 COP sample counter'];

async function readPerformanceCounters(controller, counters) {
  const values = await controller.values(counters.map(([key, name]) => ({
    key,
    domain: 'sensor',
    name,
  })));
  const result = Object.fromEntries(
    Object.entries(values).map(([key, value]) => [key, asFiniteNumber(value)]),
  );
  assert(Object.values(result).every((value) => value !== null), 'performance sample counter unavailable');
  return result;
}

async function testPerformance(controller, simulator, interrupted) {
  console.log('TEST V2 performance-map point and field heat-power fixture');
  await waitNumber(controller, 'HIL HP1 minimum-off remaining', (value) => value === 0,
    'HP1 production minimum-off window', { timeoutMs: 330000, intervalMs: 2000, interrupted });
  await controller.setNumber('api_input_outside_temperature', 12.6);
  await waitNumber(controller, 'Outside Temperature (Selected)', (value) => closeEnough(value, 12.6, 0.01),
    'selected outside temperature 12.6 C', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  const systemSupplyC = await waitNumber(controller, 'HIL System Supply Temperature',
    (value) => closeEnough(value, 22.5, 0.06),
    'system supply 22.5 C', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  // Capture only after both selected inputs have crossed their barriers. A
  // producer cycle between the API write and source selection may still hold
  // the preceding 12.5 C value and must not satisfy the map assertion.
  const pminCounterBeforeStimulus = asFiniteNumber(
    await controller.value('sensor', 'HIL Pmin producer timestamp'),
  );
  assert(pminCounterBeforeStimulus !== null, 'Pmin producer timestamp unavailable');
  const freshPmin = await waitFor(async () => {
    const state = await controller.values([
      { key: 'pminW', domain: 'sensor', name: 'HIL Low-load Pmin' },
      { key: 'producerTimestamp', domain: 'sensor', name: 'HIL Pmin producer timestamp' },
    ]);
    const numeric = Object.fromEntries(Object.entries(state).map(([key, value]) => [key, asFiniteNumber(value)]));
    if (!Object.values(numeric).every((value) => value !== null)) return false;
    return numeric.producerTimestamp > pminCounterBeforeStimulus ? numeric : false;
  }, 'fresh Pmin producer cycle', {
    timeoutMs: 90000,
    intervalMs: 1500,
    interrupted,
  });

  await simulator.setNumber('ODU 1 manual working mode 2099', 2);
  await simulator.setNumber('ODU 1 manual operating status 2108', 0x0800);
  await simulator.setNumber('ODU 1 manual flow', 1020);
  await waitNumber(controller, 'HP1 - Working Mode', (value) => value === 2,
    'fresh HP1 heating-mode telemetry', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await waitNumber(controller, 'Flow average (Selected)', (value) => closeEnough(value, 1020, 0.7),
    'selected field flow 1020 l/h', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await waitNumber(controller, 'HP1 - Water in temperature', (value) => closeEnough(value, 22.5, 0.06),
    'field inlet 22.5 C', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await waitNumber(controller, 'HP1 - Water out temperature', (value) => closeEnough(value, 24.53, 0.06),
    'field outlet 24.53 C', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  await waitNumber(controller, 'HP1 - Power Input', (value) => closeEnough(value, EXPECTED_FIELD_POWER_W, 0.15),
    'field V2 Power Input', { timeoutMs: 90000, intervalMs: 1500, interrupted });
  const countersBeforeHeating = await readPerformanceCounters(controller, FIELD_INPUT_COUNTERS);
  const freshInputs = await waitFor(async () => {
    const state = await controller.values([
      { key: 'flow', domain: 'sensor', name: 'Flow average (Selected)' },
      { key: 'inlet', domain: 'sensor', name: 'HP1 - Water in temperature' },
      { key: 'outlet', domain: 'sensor', name: 'HP1 - Water out temperature' },
      { key: 'powerInput', domain: 'sensor', name: 'HP1 - Power Input' },
      ...FIELD_INPUT_COUNTERS.map(([key, name]) => ({ key, domain: 'sensor', name })),
    ]);
    const numeric = Object.fromEntries(Object.entries(state).map(([key, value]) => [key, asFiniteNumber(value)]));
    if (!Object.values(numeric).every((value) => value !== null)) return false;
    const fresh = FIELD_INPUT_COUNTERS.every(([key]) => numeric[key] > countersBeforeHeating[key]);
    return fresh ? numeric : false;
  }, 'fresh field-input producer cycle', {
    timeoutMs: 90000,
    intervalMs: 1500,
    interrupted,
  });

  const heatCounterBaseline = (await readPerformanceCounters(controller, [HEAT_POWER_COUNTER])).heatPowerCounter;
  const heatPower = await waitFor(async () => {
    const state = await controller.values([
      { key: 'value', domain: 'sensor', name: 'HP1 - Heat Power' },
      { key: HEAT_POWER_COUNTER[0], domain: 'sensor', name: HEAT_POWER_COUNTER[1] },
    ]);
    const value = asFiniteNumber(state.value);
    const counter = asFiniteNumber(state.heatPowerCounter);
    return value !== null && counter !== null && counter > heatCounterBaseline ? { value } : false;
  }, 'Heat Power sample after fresh flow/water/power inputs', {
    timeoutMs: 90000,
    intervalMs: 1500,
    interrupted,
  });

  const copCounterBaseline = (await readPerformanceCounters(controller, [COP_COUNTER])).copCounter;
  const cop = await waitFor(async () => {
    const state = await controller.values([
      { key: 'value', domain: 'sensor', name: 'HP1 - COP' },
      { key: COP_COUNTER[0], domain: 'sensor', name: COP_COUNTER[1] },
    ]);
    const value = asFiniteNumber(state.value);
    const counter = asFiniteNumber(state.copCounter);
    return value !== null && counter !== null && counter > copCounterBaseline ? { value } : false;
  }, 'COP sample after fresh Heat Power sample', {
    timeoutMs: 90000,
    intervalMs: 1500,
    interrupted,
  });

  const values = { ...freshInputs, pminW: freshPmin.pminW, heatPower: heatPower.value, cop: cop.value };
  const pminW = freshPmin.pminW;
  assert(closeEnough(pminW, EXPECTED_PMIN_W, 2), `V2 20 Hz map Pmin differs: ${pminW}`);
  assert(closeEnough(values.flow, 1020, 0.7), `field flow differs: ${values.flow}`);
  assert(closeEnough(values.inlet, 22.5, 0.06), `field inlet differs: ${values.inlet}`);
  assert(closeEnough(values.outlet, 24.53, 0.06), `field outlet differs: ${values.outlet}`);
  assert(closeEnough(values.powerInput, EXPECTED_FIELD_POWER_W, 0.15), `field Power Input differs: ${values.powerInput}`);
  const expectedHeatW = heatingPower({ flowLph: values.flow, inletC: values.inlet, outletC: values.outlet });
  assert(closeEnough(values.heatPower, expectedHeatW, 3), `heat power ${values.heatPower} differs from ${expectedHeatW}`);
  assert(closeEnough(values.heatPower, 2407.65, 12), `field heat power differs: ${values.heatPower}`);
  assert(closeEnough(values.cop, values.heatPower / EXPECTED_FIELD_POWER_W, 0.06), `COP differs: ${values.cop}`);
  console.log(`PASS V2 performance point ${JSON.stringify(values)}`);
  return { systemSupplyC, pminW, ...values };
}

export async function runV2PerformanceScenarios({ stage, controller, simulator, interrupted }) {
  const result = {};
  if (stage === 'power' || stage === 'all') result.power = await testPowerInput(controller, simulator, interrupted);
  if (stage === 'performance' || stage === 'all') {
    result.performance = await testPerformance(controller, simulator, interrupted);
  }
  return result;
}

export async function restoreV2SimulatorProfiles({ simulator, snapshot, interrupted }) {
  const profileOption = (label) => label === 'V2 old model' ? 'V2 old' : label === 'V2 new model' ? 'V2 new' : label;
  await simulator.setSelect('ODU 1 profile (pending)', profileOption(snapshot.simulatorActive.hp1.profile));
  await simulator.setSelect('ODU 2 profile (pending)', profileOption(snapshot.simulatorActive.hp2.profile));
  await simulator.setNumber('ODU 1 Modbus address (pending)', snapshot.simulatorActive.hp1.address);
  await simulator.setNumber('ODU 2 Modbus address (pending)', snapshot.simulatorActive.hp2.address);
  await simulator.press(PROFILE_APPLY_BUTTON);
  await waitSimulatorProfiles(
    simulator,
    interrupted,
    snapshot.simulatorActive.hp1,
    snapshot.simulatorActive.hp2,
  );
}

export const v2PerformanceScenario = {
  name: 'issue-667-v2-performance',
  prepare: prepareV2PerformanceScenario,
  execute: runV2PerformanceScenarios,
  afterRestore: restoreV2SimulatorProfiles,
};
