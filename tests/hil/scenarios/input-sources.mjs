import { asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';
import {
  waitFor,
  waitNumber,
  waitUnavailable,
  waitValue,
} from '../../../scripts/hil/wait.mjs';

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function closeEnough(actual, expected, tolerance = 0.11) {
  return actual !== null && Math.abs(actual - expected) <= tolerance;
}

function waitDewPointApiState(controller, expected, label, options = {}) {
  return waitFor(async () => {
    const values = await controller.values([
      { key: 'raw', domain: 'number', name: 'api_input_cooling_dew_point' },
      { key: 'available', domain: 'binary_sensor', name: 'Cooling Dew Point Available' },
      { key: 'guard', domain: 'text_sensor', name: 'Cooling Guard Mode' },
      { key: 'selected', domain: 'sensor', name: 'Cooling Dew Point (Selected)' },
    ]);
    const raw = asFiniteNumber(values.raw);
    const selected = asFiniteNumber(values.selected);
    const accepted =
      closeEnough(raw, expected) &&
      String(values.available) === 'true' &&
      values.guard === 'Dew point (API input)' &&
      selected !== null;
    return accepted ? values : false;
  }, label, options);
}

export async function prepareInputSourceScenario(controller, simulator, interrupted) {
  await controller.setSelect('CM Override', 'Force CM0');
  await controller.setSwitch('Manual Cooling Enable', false);
  await controller.setSwitch('api_input_heating_enable', false);
  await controller.setSwitch('api_input_cooling_enable', false);
  await simulator.setSwitch('ODU 1 force no flow', false);
  await simulator.setSwitch('ODU 2 force no flow', false);
  await simulator.setSwitch('ODU external system pump flow', true);
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0 start', {
    timeoutMs: 50000,
    interrupted,
  });
}

async function testNumericIngress(controller, interrupted) {
  console.log('TEST numeric API ingress, expiry, recovery, and source-bound hold');
  await controller.setSelect('External Heat Demand Source', 'API input');
  await controller.setNumber('api_input_external_heat_demand', 7300);
  await waitNumber(
    controller,
    'External Heat Demand (Selected)',
    (value) => closeEnough(value, 7300, 1),
    'external demand accepted',
    { interrupted },
  );
  await waitUnavailable(controller, 'External Heat Demand (Selected)', 'external demand expired', {
    timeoutMs: 75000,
    interrupted,
  });
  await controller.setNumber('api_input_external_heat_demand', 6400);
  await waitNumber(
    controller,
    'External Heat Demand (Selected)',
    (value) => closeEnough(value, 6400, 1),
    'external demand recovered',
    { interrupted },
  );

  await controller.setSelect('Outside Temperature Source', 'API input');
  await controller.setNumber('api_input_outside_temperature', 12.3);
  await waitNumber(
    controller,
    'Outside Temperature (Selected)',
    (value) => closeEnough(value, 12.3),
    'outside API accepted',
    { interrupted },
  );
  await waitUnavailable(controller, 'Outside Temperature (Selected)', 'outside API expired', {
    timeoutMs: 75000,
    interrupted,
  });
  await controller.setNumber('api_input_outside_temperature', 11.7);
  await waitNumber(
    controller,
    'Outside Temperature (Selected)',
    (value) => closeEnough(value, 11.7),
    'outside API recovered',
    { interrupted },
  );

  await controller.setSelect('Cooling Dew Point Source', 'API input');
  await controller.setNumber('api_input_cooling_dew_point', 16.2);
  await waitDewPointApiState(controller, 16.2, 'cooling dew point API accepted', { interrupted });
  await waitUnavailable(controller, 'Cooling Dew Point (Selected)', 'cooling dew point expired', {
    timeoutMs: 75000,
    interrupted,
  });
  await controller.setNumber('api_input_cooling_dew_point', 15.8);
  await waitDewPointApiState(controller, 15.8, 'cooling dew point API recovered', { interrupted });

  await controller.setSelect('Room Temperature Source', 'API input');
  await controller.setSelect('Room Setpoint Source', 'API input');
  await controller.setNumber('api_input_room_temperature', 33.7);
  await controller.setNumber('api_input_room_setpoint', 24.1);
  await waitNumber(
    controller,
    'Room Temperature (Selected)',
    (value) => closeEnough(value, 33.7),
    'room API accepted',
    { interrupted },
  );
  await waitNumber(
    controller,
    'Room Setpoint (Selected)',
    (value) => closeEnough(value, 24.1),
    'setpoint API accepted',
    { interrupted },
  );
  await controller.setSelect('Room Temperature Source', 'HA input');
  await sleep(11000);
  const switched = asFiniteNumber(
    await controller.value('sensor', 'Room Temperature (Selected)'),
  );
  assert(
    switched === null || !closeEnough(switched, 33.7),
    `cross-source hold replayed API room value: ${switched}`,
  );
  console.log('PASS numeric API ingress and source-bound hold');
}

async function testEnableExpiry(controller, interrupted) {
  console.log('TEST heating/cooling enable expiry is fail-closed');
  await controller.setSelect('Heating Enable Source', 'API input');
  await controller.setSelect('Cooling Enable Source', 'API input');
  await controller.setSwitch('api_input_heating_enable', true);
  await controller.setSwitch('api_input_cooling_enable', true);
  for (const [name, label] of [
    ['Heating Enable Valid', 'heating API valid'],
    ['Heating Enable (Selected)', 'heating API selected'],
    ['Cooling Enable Valid', 'cooling API valid'],
    ['Cooling Enable (Selected)', 'cooling API selected'],
  ]) {
    await waitValue(controller, 'binary_sensor', name, true, label, {
      timeoutMs: 70000,
      interrupted,
    });
  }
  for (const [name, label] of [
    ['Heating Enable Valid', 'heating API expiry'],
    ['Heating Enable (Selected)', 'heating fails closed'],
    ['Cooling Enable Valid', 'cooling API expiry'],
    ['Cooling Enable (Selected)', 'cooling fails closed'],
  ]) {
    await waitValue(controller, 'binary_sensor', name, false, label, {
      timeoutMs: 85000,
      interrupted,
    });
  }
  console.log('PASS heating/cooling enable expiry');
}

async function testActiveSourceSwitch(controller, interrupted) {
  console.log('TEST active heating request loses permission on source switch');
  await controller.setSelect('Heating Enable Source', 'MQTT');
  await waitValue(
    controller,
    'binary_sensor',
    'Heating Enable (Selected)',
    false,
    'MQTT route is non-granting',
    { timeoutMs: 70000, interrupted },
  );
  await controller.setSelect('Heating Enable Source', 'API input');
  await controller.setSwitch('api_input_heating_enable', true);
  await controller.setSelect('Heating Control Mode', 'Power House');
  await controller.setNumber('Power House temperature reaction', 0);
  await controller.setNumber('Power House demand rise time', 2);
  await controller.setSelect('External Heat Demand Source', 'API input');
  await controller.setSelect('Outside Temperature Source', 'API input');
  await controller.setSelect('Room Temperature Source', 'API input');
  await controller.setSelect('Room Setpoint Source', 'API input');
  await controller.setSelect('Flow Source', 'Outdoor unit');
  await controller.setSelect('Outdoor Unit Flow Mode', 'Flowmeter HP1');
  await controller.setNumber('Flow Setpoint', 800);
  await controller.setSelect('Flow Control Mode', 'Flow Setpoint');
  await controller.setSelect('CM Override', 'Auto');

  const deadline = Date.now() + 240000;
  let activeMode;
  while (Date.now() < deadline) {
    if (interrupted()) throw new Error('HIL run interrupted; starting recovery');
    await controller.setNumber('api_input_external_heat_demand', 8000);
    await controller.setNumber('api_input_outside_temperature', 8);
    await controller.setNumber('api_input_room_temperature', 19);
    await controller.setNumber('api_input_room_setpoint', 21);
    await controller.setSwitch('api_input_heating_enable', true);
    const status = await controller.values([
      { key: 'mode', domain: 'text_sensor', name: 'Control Mode' },
      { key: 'enabled', domain: 'binary_sensor', name: 'Heating Enable (Selected)' },
      { key: 'external', domain: 'sensor', name: 'External Heat Demand (Selected)' },
      { key: 'outside', domain: 'sensor', name: 'Outside Temperature (Selected)' },
      { key: 'room', domain: 'sensor', name: 'Room Temperature (Selected)' },
      { key: 'setpoint', domain: 'sensor', name: 'Room Setpoint (Selected)' },
      { key: 'flow', domain: 'sensor', name: 'Flow average (Selected)' },
    ]);
    console.log(`STATE active source switch ${JSON.stringify(status)}`);
    const mode = String(status.mode);
    if (['CM2', 'CM3', 'CM4'].includes(mode)) {
      activeMode = mode;
      break;
    }
  }
  assert(activeMode, 'heating request never became active');
  await controller.setSelect('Heating Enable Source', 'MQTT');
  await waitValue(
    controller,
    'binary_sensor',
    'Heating Enable (Selected)',
    false,
    'active source switch withdraws grant',
    { timeoutMs: 70000, interrupted },
  );
  await waitFor(async () => {
    const mode = String(await controller.value('text_sensor', 'Control Mode'));
    return mode === 'CM1' || mode === 'CM0' ? mode : false;
  }, 'active request leaves heating mode', {
    timeoutMs: 100000,
    intervalMs: 1500,
    interrupted,
  });
  console.log(`PASS active source switch from ${activeMode}`);
}

async function testRebootReset(controller, waitForProfile, interrupted) {
  console.log('TEST reboot does not replay API state');
  await controller.setSelect('CM Override', 'Force CM0');
  await waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'CM0 before reboot', {
    timeoutMs: 50000,
    interrupted,
  });
  await controller.setSelect('Heating Enable Source', 'API input');
  await controller.setSelect('Cooling Enable Source', 'API input');
  await controller.setSelect('External Heat Demand Source', 'API input');
  await controller.setSwitch('api_input_heating_enable', true);
  await controller.setSwitch('api_input_cooling_enable', true);
  await controller.setNumber('api_input_external_heat_demand', 5000);
  await waitValue(
    controller,
    'binary_sensor',
    'Heating Enable (Selected)',
    true,
    'heating set before reboot',
    { timeoutMs: 70000, interrupted },
  );
  await controller.press('Restart');
  await sleep(6000);
  await waitForProfile();
  for (const [domain, name, expected, label] of [
    ['switch', 'api_input_heating_enable', false, 'heating API state reset'],
    ['switch', 'api_input_cooling_enable', false, 'cooling API state reset'],
    ['binary_sensor', 'Heating Enable Valid', false, 'heating validity reset'],
    ['binary_sensor', 'Cooling Enable Valid', false, 'cooling validity reset'],
  ]) {
    await waitValue(controller, domain, name, expected, label, {
      timeoutMs: 70000,
      interrupted,
    });
  }
  await waitUnavailable(controller, 'External Heat Demand (Selected)', 'external demand reset', {
    timeoutMs: 70000,
    interrupted,
  });
  console.log('PASS reboot reset without stale replay');
}

export async function runInputSourceScenarios({
  stage,
  controller,
  interrupted,
  waitForProfile,
}) {
  if (stage === 'all' || stage === 'inputs') {
    await testNumericIngress(controller, interrupted);
  }
  if (stage === 'all' || stage === 'enable-expiry') {
    await testEnableExpiry(controller, interrupted);
  }
  if (stage === 'all' || stage === 'active-switch') {
    await testActiveSourceSwitch(controller, interrupted);
  }
  if (stage === 'all' || stage === 'reboot-reset') {
    await testRebootReset(controller, waitForProfile, interrupted);
  }
}
