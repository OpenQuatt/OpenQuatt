import assert from 'node:assert/strict';
import { controlledWarmupScenario, validateWarmupExtra, warmupStateEntities, warmupClose } from './controlled-warmup.mjs';
import { startLocalInputFixture, validateLocalFixtureHost } from '../../../scripts/hil/local-input-fixture.mjs';
import { waitFor } from '../../../scripts/hil/wait.mjs';
import { asBoolean } from '../../../scripts/hil/rest-client.mjs';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const close = warmupClose;
const inputs = ['room_temperature', 'room_setpoint'];
// Room temperature is a transient input; only the stateful goal has a retained-setting endpoint.
export const mqttRetainedWritable = (input) => input === 'room_setpoint';

export function validateTransportExtra(extra) {
  validateWarmupExtra(extra);
  const local = extra.localInputs;
  assert(local && typeof local.cicUrl === 'string' && local.cicUrl.length <= 255 && typeof local.cicPolling === 'boolean', 'invalid local CIC recovery settings');
  assert(local.mqtt && typeof local.mqtt === 'object', 'missing local MQTT recovery settings');
  assert(local.mqtt.input_enabled && local.mqtt.input_accept_retained, 'missing local MQTT input recovery settings');
  assert(typeof local.mqtt.broker === 'string' && local.mqtt.broker.length <= 64 && typeof local.mqtt.username === 'string' && local.mqtt.username.length <= 64, 'invalid local MQTT recovery settings');
  assert(Number.isInteger(local.mqtt.port) && local.mqtt.port >= 1 && local.mqtt.port <= 65535, 'invalid local MQTT recovery port');
  assert(typeof local.mqtt.enabled === 'boolean' && typeof local.mqtt.password_set === 'boolean', 'invalid local MQTT recovery permission');
  assert(!(local.mqtt.password_set && !local.mqtt.broker), 'empty original broker with stored credentials cannot be restored without clearing its password');
  for (const key of inputs) {
    assert(typeof local.mqtt.input_enabled[key] === 'boolean' && typeof local.mqtt.input_accept_retained[key] === 'boolean', 'invalid local MQTT input recovery permission');
  }
  assert(local.mqtt.input_accept_retained.room_temperature === false, 'transient room temperature retained flag cannot be restored');
  return extra;
}

async function mqttWrite(controller, endpoint, values) {
  const status = await controller.request('/mqtt/status');
  await controller.request(endpoint, { method: 'POST', body: new URLSearchParams({ ...values, csrf_token: status.csrf_token }).toString(), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  await waitFor(async () => {
    const current = await controller.request('/mqtt/status');
    return current.runtime_pending === false ? current : false;
  }, 'MQTT worker mutation settles', { timeoutMs: 30000, intervalMs: 1500 });
}

async function textWrite(controller, name, value) {
  await controller.request(`/text/${encodeURIComponent(name)}/set`, { method: 'POST', body: new URLSearchParams({ value }).toString(), headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  await waitFor(async () => (await controller.value('text', name)) === value, 'private text setting readback', { timeoutMs: 30000 });
}

async function restoreLocal(controller, extra) {
  const { cicUrl, cicPolling, mqtt } = validateTransportExtra(extra).localInputs;
  const errors = [];
  const attempt = async (operation) => { try { await operation(); } catch (error) { errors.push(error); } };
  await attempt(() => controller.setSwitch('CIC - Enable polling', false));
  await attempt(() => textWrite(controller, 'CIC - Feed URL', cicUrl));
  for (const input of inputs) {
    await attempt(() => mqttWrite(controller, '/mqtt/input/save', { input, enabled: String(mqtt.input_enabled[input]) }));
    if (mqttRetainedWritable(input)) await attempt(() => mqttWrite(controller, '/mqtt/input/retained/save', { input, accept_retained: String(mqtt.input_accept_retained[input]) }));
  }
  await attempt(() => mqttWrite(controller, '/mqtt/save', { broker: mqtt.broker, port: String(mqtt.port), username: mqtt.username, password: '', clear_password: 'false', enabled: String(mqtt.enabled) }));
  await attempt(async () => {
    assert((await controller.value('text', 'CIC - Feed URL')) === cicUrl, 'private CIC URL recovery readback failed');
    await controller.setSwitch('CIC - Enable polling', cicPolling);
    assert.equal(asBoolean(await controller.value('switch', 'CIC - Enable polling')), cicPolling);
  });
  await attempt(async () => {
    const actual = await controller.request('/mqtt/status');
    for (const key of ['broker', 'port', 'username', 'enabled', 'password_set']) assert(actual[key] === mqtt[key], `MQTT recovery ${key}`);
    for (const input of inputs) {
      assert.equal(actual.input_enabled[input], mqtt.input_enabled[input]);
      assert.equal(actual.input_accept_retained[input], mqtt.input_accept_retained[input]);
    }
  });
  if (errors.length) throw new AggregateError(errors, 'local transport recovery failed; OTA blocked');
}

export const warmupTransportScenario = {
  ...controlledWarmupScenario,
  name: 'controlled-warmup-transports',
  preflight({ stage }) {
    assert(stage === 'transports', 'local input runner only supports transports');
    validateLocalFixtureHost(process.env.OQ_HIL_FIXTURE_HOST, { requireOwned: true });
  },
  async captureExtra(context) {
    const extra = await controlledWarmupScenario.captureExtra(context);
    const status = await context.controller.request('/mqtt/status');
    extra.localInputs = {
      cicUrl: await context.controller.value('text', 'CIC - Feed URL'),
      cicPolling: asBoolean(await context.controller.value('switch', 'CIC - Enable polling')),
      mqtt: Object.fromEntries(['broker', 'port', 'username', 'enabled', 'password_set', 'input_enabled', 'input_accept_retained'].map((key) => [key, status[key]])),
    };
    return validateTransportExtra(extra);
  },
  validateSnapshot(snapshot) { validateTransportExtra(snapshot.extra); },
  async beforeRestore(context) {
    await controlledWarmupScenario.beforeRestore(context);
    await restoreLocal(context.controller, context.snapshot.extra);
  },
  async execute({ controller, simulator, interrupted }) {
    const fixture = await startLocalInputFixture(process.env.OQ_HIL_FIXTURE_HOST);
    const check = () => { fixture.assertHealthy(); if (interrupted()) throw new Error('HIL interrupted'); };
    const wait = async (expected, label) => waitFor(async () => {
      check();
      const value = await controller.values(warmupStateEntities);
      return Object.entries(expected).every(([key, wanted]) => typeof wanted === 'number' ? close(value[key], wanted) : typeof wanted === 'boolean' ? asBoolean(value[key]) === wanted : value[key] === wanted) ? value : false;
    }, label, { timeoutMs: 45000, intervalMs: 1500, interrupted });
    const feed = (room = 19, goal = 18) => ({ thermostat: { otFtRoomTemperature: room, otFtRoomSetpoint: goal, otFtChEnabled: false, otFtCoolingEnabled: false }, flowMeter: { waterSupplyTemperature: 25 } });
    let topics;
    const sample = async (source, room, goal) => {
      check();
      if (source === 'CIC') await fixture.send({ feed: feed(room, goal) });
      else {
        await waitFor(async () => {
          check();
          await fixture.send({ publish: { [topics.room_temperature]: room, [topics.room_setpoint]: goal } });
          const actual = await controller.values(warmupStateEntities);
          return close(actual.room, room) && close(actual.setpoint, goal);
        }, 'MQTT subscription and actual selected sample', { timeoutMs: 45000, intervalMs: 2000 });
      }
      await wait({ room, setpoint: goal }, `${source} actual selected input readback`);
    };
    const start = async (source) => {
      await controller.setNumber('api_input_outside_temperature', 8);
      await controller.setSwitch('Controlled warmup enabled', false);
      await controller.setSelect('CM Override', 'Auto');
      await controller.setSelect('Heating Control Mode', 'Power House');
      await controller.setSelect('External Heat Demand Source', 'Disabled');
      await controller.setSelect('Room Temperature Source', source);
      await controller.setSelect('Room Setpoint Source', source);
      await controller.setNumber('Controlled warmup trigger', 1.5);
      await controller.setNumber('Controlled warmup step', 0.1);
      await controller.setNumber('Controlled warmup step time', 5);
      await controller.setNumber('Controlled warmup maximum offset', 0.3);
      await controller.setNumber('Controlled warmup maximum duration', 1);
      await sample(source, 19, 18);
      await controller.setSwitch('Controlled warmup enabled', true);
      await wait({ active: false, target: 18 }, `${source} initial baseline`);
      await sample(source, 19, 21);
      return wait({ active: true, target: 19.1, heatEnable: false, applied1: 0, applied2: 0, boiler: false, boilerHeat: false }, `${source} real transport activation without heat permission`);
    };
    try {
      await fixture.send({ feed: feed() });
      await textWrite(controller, 'CIC - Feed URL', fixture.http_url);
      await controller.setSwitch('CIC - Enable polling', true);
      await controller.setSelect('Water Supply Source', 'CIC');
      await start('CIC');
      await fixture.send({ feed: feed(19.0005, 21) });
      await sleep(20000);
      await wait({ active: true, target: 19.1 }, 'sub-publication-deadband CIC receipt retains fixed session');
      await fixture.send({ feed: feed(19.025, 21) });
      await waitFor(async () => {
        check();
        const value = await controller.values(warmupStateEntities);
        assert(asBoolean(value.active) && close(value.target, 19.1), 'new producer value cancelled session before selected-cache refresh');
        return Number(value.room) > 19.015 && Math.abs(Number(value.room) - 19.025) < 0.01 ? value : false;
      }, 'actual changed CIC selected room after producer/cache lag', { timeoutMs: 45000, intervalMs: 1000 });
      await sleep(20000);
      await wait({ active: true, target: 19.1 }, 'continued changed 5s CIC receipts retain fixed session');
      for (const missing of ['otFtRoomTemperature', 'otFtRoomSetpoint']) {
        const partial = feed(19, 21); delete partial.thermostat[missing];
        await fixture.send({ feed: partial });
        await wait({ active: false, status: 'Input unavailable' }, `HTTP200 missing ${missing} invalidates retained producer entity`);
        await sample('CIC', 19, 21);
        await wait({ active: false, target: 21 }, 'fresh CIC same-value recovery does not replay old edge');
        await start('CIC');
      }
      await controller.setSwitch('CIC - Enable polling', false);
      await wait({ active: false, status: 'Input unavailable' }, 'CIC polling withdrawn cancels');
      console.log('PASS real local CIC ingress, repeated receipts, missing fields and no old-edge replay');

      await mqttWrite(controller, '/mqtt/save', { broker: process.env.OQ_HIL_FIXTURE_HOST, port: String(fixture.mqtt_port), username: '', password: '', clear_password: 'false', enabled: 'true' });
      for (const input of inputs) {
        await mqttWrite(controller, '/mqtt/input/save', { input, enabled: 'true' });
        if (mqttRetainedWritable(input)) await mqttWrite(controller, '/mqtt/input/retained/save', { input, accept_retained: 'false' });
      }
      const status = await waitFor(async () => { check(); const value = await controller.request('/mqtt/status'); return value.connected && !value.runtime_pending ? value : false; }, 'local MQTT broker connected', { timeoutMs: 45000 });
      topics = status.input_topics;
      await start('MQTT');
      await sample('MQTT', 18.5, 21);
      await wait({ active: true, target: 19.1 }, 'MQTT room cooling does not move fixed target backwards');
      await simulator.setNumber('Thermostat room setpoint', 21);
      await controller.setSelect('Room Setpoint Source', 'OT thermostat');
      await wait({ active: false, setpoint: 21, target: 21 }, 'valid equal-goal MQTT-to-OT selected-source withdrawal cancels');
      await controller.setSelect('Room Setpoint Source', 'MQTT');
      await sample('MQTT', 19, 21);
      await wait({ active: false, target: 21 }, 'MQTT A-to-B-to-A cannot replay old selected edge');
      console.log('PASS real local MQTT CONNECT/SUBSCRIBE/QoS0 room and goal ingress, fixed target and source no-replay');
      return { cic: true, mqtt: true, brokerProtocol: 'MQTT 3.1.1 QoS0 fixture', productionCic: false };
    } finally { await fixture.stop(); }
  },
};
