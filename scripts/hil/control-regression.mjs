import { asBoolean, asFiniteNumber } from './rest-client.mjs';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const FLAGS = [
  'available_for_start', 'running_confirmed', 'stop_confirmed',
  'stop_unconfirmed', 'stop_unconfirmed_due_to_link_loss', 'must_stop',
];

function require(condition, message) {
  if (!condition) throw new Error(message);
}

export function incidentObservation(payload, hpCount = 2) {
  require([1, 2].includes(hpCount), 'unsupported control regression topology');
  require(payload?.schema_version === 1 && payload?.catalog_version === 1,
    'unsupported incident snapshot');
  require(Number.isInteger(payload.system?.control_mode), 'missing control mode');
  require(typeof payload.system.boiler_command_active === 'boolean', 'missing boiler command');
  require(Array.isArray(payload.heat_pumps) && payload.heat_pumps.length === hpCount,
    `control regression requires exactly ${hpCount} configured heat pumps`);
  const hp = Array.from({ length: hpCount }, (_, i) => i + 1).map((index) => {
    const unit = payload.heat_pumps.find((item) => item.index === index);
    require(unit && ['bootstrap', 'healthy', 'suspect', 'lost', 'recovering'].includes(unit.link_state),
      `missing HP${index} link state`);
    for (const flag of FLAGS) require(typeof unit[flag] === 'boolean', `missing HP${index} ${flag}`);
    // Do not retain the action CSRF token or unrelated incident/user data.
    return Object.fromEntries(['index', 'link_state', ...FLAGS].map((key) => [key, unit[key]]));
  });
  return { mode: payload.system.control_mode, boiler: payload.system.boiler_command_active, hp };
}

export function controlContext({ controller, simulator, interrupted = () => false,
  hpCount = 2, now = Date.now, delay = sleep, timeoutMs = 600000, holdMs = 30000, intervalMs = 1500 }) {
  const samples = [];
  let nextRefresh = 0;
  let heating = false;
  let requireInputs = false;
  async function refresh() {
    if (!heating || now() < nextRefresh) return;
    // Serialized through the shared REST gate, never a racing background timer.
    await controller.setNumber('api_input_outside_temperature', 7);
    await controller.setNumber('api_input_room_temperature', 20);
    await controller.setNumber('api_input_room_setpoint', 24);
    await controller.setNumber('api_input_external_heat_demand', 4000);
    await controller.setSwitch('api_input_heating_enable', true);
    nextRefresh = now() + 45000;
  }
  async function observe() {
    require(!interrupted(), 'HIL run interrupted; starting recovery');
    await refresh();
    const state = incidentObservation(await controller.request('/openquatt/incidents'), hpCount);
    state.boilerActive = asBoolean(await controller.value('binary_sensor', 'Boiler active'));
    if (heating) {
      const inputs = await controller.values([
        { key: 'outside', domain: 'sensor', name: 'Outside Temperature (Selected)' },
        { key: 'room', domain: 'sensor', name: 'Room Temperature (Selected)' },
        { key: 'setpoint', domain: 'sensor', name: 'Room Setpoint (Selected)' },
        { key: 'demand', domain: 'sensor', name: 'External Heat Demand (Selected)' },
        { key: 'enable', domain: 'binary_sensor', name: 'Heating Enable (Selected)' },
        { key: 'valid', domain: 'binary_sensor', name: 'Heating Enable Valid' },
      ]);
      state.inputsReady = Object.entries({ outside: 7, room: 20, setpoint: 24, demand: 4000 })
        .every(([key, expected]) => asFiniteNumber(inputs[key]) !== null &&
          Math.abs(Number(inputs[key]) - expected) < 0.11) &&
        asBoolean(inputs.enable) && asBoolean(inputs.valid);
      require(!requireInputs || state.inputsReady, 'API demand/source selection became invalid or stale');
    }
    require(samples.length < 5000, 'control regression sample limit exceeded');
    samples.push({ at: new Date(now()).toISOString(), ...state });
    return state;
  }
  async function until(label, predicate, invariant = () => {}) {
    const deadline = now() + timeoutMs;
    while (now() < deadline) {
      const state = await observe();
      invariant(state);
      if (predicate(state)) return state;
      await delay(intervalMs);
    }
    throw new Error(`${label} timed out`);
  }
  async function hold(label, invariant) {
    const deadline = now() + holdMs;
    do {
      invariant(await observe());
      await delay(intervalMs);
    } while (now() < deadline);
    return label;
  }
  async function responses(index, enabled) {
    require(!interrupted() || enabled, 'HIL run interrupted before response suppression');
    await simulator.setSwitch(`ODU ${index} responses enabled`, enabled);
    require(asBoolean(await simulator.value('switch', `ODU ${index} responses enabled`)) === enabled,
      `HP${index} response gate readback differs`);
  }
  async function withLoss(indices, body) {
    let failure;
    let result;
    try {
      for (const index of indices) await responses(index, false);
      result = await body();
    } catch (error) {
      failure = error;
    } finally {
      const errors = failure ? [failure] : [];
      // Include gates whose write may have succeeded but whose ACK was lost.
      for (const index of indices) {
        try { await responses(index, true); } catch (error) { errors.push(error); }
      }
      if (errors.length) throw new AggregateError(errors,
        `response-loss stage or cleanup failed: ${errors.map((error) => error.message).join('; ')}`);
    }
    return result;
  }
  async function idle() {
    heating = false;
    requireInputs = false;
    await controller.setSelect('CM Override', 'Force CM0');
    await controller.setSwitch('api_input_heating_enable', false);
    return until('healthy stopped heat pumps', (s) => s.mode === 0 && s.hp.every((h) =>
      h.link_state === 'healthy' && h.stop_confirmed && !h.running_confirmed && !h.must_stop));
  }
  async function demand() {
    heating = true;
    nextRefresh = 0;
    await refresh();
    await until('API demand selected and fresh', (s) => s.inputsReady);
    requireInputs = true;
    await controller.setSelect('CM Override', 'Auto');
  }
  return { controller, simulator, samples, until, hold, responses, withLoss, idle, demand };
}

export function healthy(s) {
  return s.hp.every((hp) => hp.link_state === 'healthy' && !hp.stop_unconfirmed_due_to_link_loss);
}

export function assertNoFallback(s) {
  require(s.mode !== 4 && !s.boiler && !s.boilerActive, 'partial Duo outage started boiler fallback');
}

export async function prepareControlRegression(controller, simulator, interrupted, snapshot, hpCount = 2) {
  // Validate the actual firmware topology before changing any settings.
  incidentObservation(await controller.request('/openquatt/incidents'), hpCount);
  const indices = Array.from({ length: hpCount }, (_, i) => i + 1);
  for (const index of indices) {
    const key = `hp${index}`;
    require(snapshot?.simulatorActive?.[key]?.address === index,
      `control regression requires active ODU address ${index}`);
    require(['V1.5', 'V2 old model', 'V2 new model'].includes(snapshot.simulatorActive[key].profile),
      'control regression requires simulator ODU flow support (V1.5/V2)');
  }
  const baseline = await controller.values([
    { key: 'setup', domain: 'switch', name: 'Setup Complete' },
    { key: 'enabled', domain: 'switch', name: 'OpenQuatt Enabled' },
    { key: 'aux', domain: 'switch', name: 'Auxiliary heat source connected' },
    { key: 'extension', domain: 'switch', name: 'Power House run extension' },
  ]);
  require(asBoolean(baseline.setup) && asBoolean(baseline.enabled) && asBoolean(baseline.aux),
    'baseline requires setup complete, OpenQuatt enabled and auxiliary source connected');
  require(!asBoolean(baseline.extension), 'disable run extension before control regression');
  const gates = await simulator.values([
    { key: 'simulation', domain: 'switch', name: 'Quatt ODU simulation enabled' },
    { key: 'responses', domain: 'switch', name: 'ODU responses enabled' },
    ...['ODU timeout injection enabled', 'ODU exception injection enabled',
      'ODU reboot on matching request', 'M2 UART fault injection enabled',
      ...indices.map((index) => `ODU ${index} freeze measured frequency`)]
      .map((name) => ({ key: name, domain: 'switch', name })),
  ]);
  require(asBoolean(gates.simulation) && asBoolean(gates.responses), 'simulator is not responding normally');
  for (const [key, value] of Object.entries(gates)) {
    if (key !== 'simulation' && key !== 'responses') require(!asBoolean(value), `active simulator fault: ${key}`);
  }
  require(!interrupted(), 'HIL run interrupted before preparation');
  await controller.setSelect('CM Override', 'Force CM0');
  await controller.setSwitch('api_input_heating_enable', false);
  await controller.setSwitch('api_input_cooling_enable', false);
  await controller.setSwitch('Manual Cooling Enable', false);
  await controller.setSwitch('CIC - Enable polling', false);
  await controller.setSwitch('Boiler assist enabled', false);
  await controller.setSwitch('Boiler fallback on heat-pump fault', true);
  await controller.setSelect('Heating Control Mode', 'Power House');
  await controller.setNumber('Power House demand rise time', 2);
  for (const name of ['Outside Temperature Source', 'Room Temperature Source', 'Room Setpoint Source',
    'External Heat Demand Source', 'Heating Enable Source', 'Cooling Enable Source']) {
    await controller.setSelect(name, 'API input');
  }
  await controller.setSelect('Flow Source', 'Outdoor unit');
  await controller.setSelect('Q Flow Source', 'Outdoor unit');
  await controller.setSelect('Outdoor Unit Flow Mode', 'Local aggregate HP1/HP2');
  await simulator.setSwitch('ODU external system pump flow', true);
  for (const index of indices) await simulator.setNumber(`ODU ${index} water-in temperature`, 22.5);
  await controller.setNumber('HIL CIC Water Supply Fixture', 22.5);
  await controller.setSwitch('HIL CIC Water Supply Fixture Enable', true);
  await controller.setSelect('Water Supply Source', 'CIC');
  const ctx = controlContext({ controller, simulator, interrupted, hpCount });
  await ctx.idle();
  const inputs = await controller.values([
    { key: 'supply', domain: 'sensor', name: 'Water Supply Temp (Selected)' },
    ...indices.map((index) => ({ key: `flow${index}`, domain: 'sensor', name: `HP${index} - Flow` })),
  ]);
  require(asFiniteNumber(inputs.supply) !== null && Math.abs(Number(inputs.supply) - 22.5) < 0.2,
    'supply fixture is not selected and fresh');
  for (const key of indices.map((index) => `flow${index}`)) require(asFiniteNumber(inputs[key]) >= 250,
    `${key} must be at least 250 l/h before testing`);
}
