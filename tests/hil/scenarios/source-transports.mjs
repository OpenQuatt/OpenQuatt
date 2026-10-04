import assert from 'node:assert/strict';
import { prepareInputSourceScenario } from './input-sources.mjs';
import { startLocalInputFixture, validateLocalFixtureHost } from '../../../scripts/hil/local-input-fixture.mjs';
import { waitFor } from '../../../scripts/hil/wait.mjs';
import { asBoolean, asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const close = (value, expected) => asFiniteNumber(value) !== null && Math.abs(asFiniteNumber(value) - expected) <= 0.025;
const sourceStateEntities = [
  { key: 'room', domain: 'sensor', name: 'Room Temperature (Selected)' },
  { key: 'setpoint', domain: 'sensor', name: 'Room Setpoint (Selected)' },
  { key: 'roomCurrent', domain: 'binary_sensor', name: 'HIL Room Control Current' },
  { key: 'setpointCurrent', domain: 'binary_sensor', name: 'HIL Setpoint Control Current' },
  { key: 'generation', domain: 'sensor', name: 'HIL Setpoint Source Generation' },
];
const inputs = ['room_temperature', 'room_setpoint'];
// Room temperature is a transient input; only the stateful goal has a retained-setting endpoint.
export const mqttRetainedWritable = (input) => input === 'room_setpoint';

export function validateTransportExtra(extra) {
  assert(extra?.schema === 1, 'invalid source transport recovery schema');
  const local = extra.localInputs;
  assert(local && typeof local.cicUrl === 'string' && local.cicUrl.length <= 255 && typeof local.cicPolling === 'boolean', 'invalid local CIC recovery settings');
  assert(local.mqtt && typeof local.mqtt === 'object', 'missing local MQTT recovery settings');
  assert(local.mqtt.input_enabled && local.mqtt.input_accept_retained, 'missing local MQTT input recovery settings');
  assert(typeof local.mqtt.broker === 'string' && local.mqtt.broker.length <= 64 && typeof local.mqtt.username === 'string' && local.mqtt.username.length <= 64, 'invalid local MQTT recovery settings');
  assert(Number.isInteger(local.mqtt.port) && local.mqtt.port >= 1 && local.mqtt.port <= 65535, 'invalid local MQTT recovery port');
  assert(typeof local.mqtt.enabled === 'boolean' && typeof local.mqtt.password_set === 'boolean', 'invalid local MQTT recovery permission');
  assert(!local.mqtt.password_set, 'source fixture requires a password-free baseline; stored credentials cannot be restored after clearing');
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

export const sourceTransportScenario = {
  name: 'source-transports',
  async prepare(controller, simulator, interrupted) {
    await prepareInputSourceScenario(controller, simulator, interrupted);
  },
  preflight({ stage }) {
    assert(stage === 'transports', 'local input runner only supports transports');
    validateLocalFixtureHost(process.env.OQ_HIL_FIXTURE_HOST, { requireOwned: true });
  },
  async captureExtra(context) {
    const extra = { schema: 1 };
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
    await context.controller.setSelect('CM Override', 'Force CM0');
    await waitFor(async () => (await context.controller.value('text_sensor', 'Control Mode')) === 'CM0', 'safe source-fixture recovery CM0', { timeoutMs: 50000 });
    await restoreLocal(context.controller, context.snapshot.extra);
  },
  async execute({ controller, simulator, interrupted }) {
    const fixture = await startLocalInputFixture(process.env.OQ_HIL_FIXTURE_HOST);
    const check = () => { fixture.assertHealthy(); if (interrupted()) throw new Error('HIL interrupted'); };
    const wait = async (expected, label) => waitFor(async () => {
      check();
      const value = await controller.values(sourceStateEntities);
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
          const actual = await controller.values(sourceStateEntities);
          return close(actual.room, room) && close(actual.setpoint, goal);
        }, 'MQTT subscription and actual selected sample', { timeoutMs: 45000, intervalMs: 2000 });
      }
      await wait({ room, setpoint: goal }, `${source} actual selected input readback`);
    };
    const start = async (source) => {
      await controller.setSelect('Room Temperature Source', source);
      await controller.setSelect('Room Setpoint Source', source);
      await sample(source, 19, 21);
      return wait({ roomCurrent: true, setpointCurrent: true }, `${source} current selected control inputs`);
    };
    try {
      await fixture.send({ feed: feed() });
      await textWrite(controller, 'CIC - Feed URL', fixture.http_url);
      await controller.setSwitch('CIC - Enable polling', true);
      await controller.setSelect('Water Supply Source', 'CIC');
      await start('CIC');
      await fixture.send({ feed: feed(19.0005, 21) });
      await sleep(20000);
      await wait({ roomCurrent: true, setpointCurrent: true }, 'sub-publication-deadband CIC receipt remains current');
      await fixture.send({ feed: feed(19.025, 21) });
      await waitFor(async () => {
        check();
        const value = await controller.values(sourceStateEntities);
        assert(asBoolean(value.roomCurrent), 'new valid receipt lost currentness before selected-cache refresh');
        return Number(value.room) > 19.015 && Math.abs(Number(value.room) - 19.025) < 0.01 ? value : false;
      }, 'actual changed CIC selected room after producer/cache lag', { timeoutMs: 45000, intervalMs: 1000 });
      await sleep(20000);
      await wait({ roomCurrent: true }, 'continued CIC receipts remain current');
      for (const missing of ['otFtRoomTemperature', 'otFtRoomSetpoint']) {
        const partial = feed(19, 21); delete partial.thermostat[missing];
        await fixture.send({ feed: partial });
        await wait({ [missing === 'otFtRoomTemperature' ? 'roomCurrent' : 'setpointCurrent']: false }, `HTTP200 missing ${missing} invalidates retained producer currentness`);
        await sample('CIC', 19, 21);
        await wait({ roomCurrent: true, setpointCurrent: true }, 'fresh CIC same-value recovery restores currentness');
        await start('CIC');
      }
      await controller.setSwitch('CIC - Enable polling', false);
      await wait({ roomCurrent: false, setpointCurrent: false }, 'CIC polling withdrawn invalidates control inputs');
      console.log('PASS real local CIC ingress, repeated receipts, missing fields and recovery');

      await mqttWrite(controller, '/mqtt/save', { broker: process.env.OQ_HIL_FIXTURE_HOST, port: String(fixture.mqtt_port), username: '', password: '', clear_password: 'false', enabled: 'true' });
      for (const input of inputs) {
        await mqttWrite(controller, '/mqtt/input/save', { input, enabled: 'true' });
        if (mqttRetainedWritable(input)) await mqttWrite(controller, '/mqtt/input/retained/save', { input, accept_retained: 'false' });
      }
      const status = await waitFor(async () => { check(); const value = await controller.request('/mqtt/status'); return value.connected && !value.runtime_pending ? value : false; }, 'local MQTT broker connected', { timeoutMs: 45000 });
      topics = status.input_topics;
      await start('MQTT');
      await sample('MQTT', 18.5, 21);
      await wait({ room: 18.5, roomCurrent: true }, 'MQTT changed room value resolves as current');
      const before = await controller.values(sourceStateEntities);
      await simulator.setNumber('Thermostat room setpoint', 21);
      await controller.setSelect('Room Setpoint Source', 'OT thermostat');
      await wait({ setpoint: 21 }, 'equal-goal MQTT-to-OT selected source');
      const middle = await controller.values(sourceStateEntities);
      assert(Number(middle.generation) > Number(before.generation), 'MQTT-to-OT generation did not advance');
      await controller.setSelect('Room Setpoint Source', 'MQTT');
      await sample('MQTT', 19, 21);
      await wait({ setpointCurrent: true }, 'MQTT return resolves current input');
      const after = await controller.values(sourceStateEntities);
      assert(Number(after.generation) > Number(middle.generation), 'A-to-B-to-A generation did not advance');
      console.log('PASS real local MQTT CONNECT/SUBSCRIBE/QoS0 room and goal ingress, selected values and distinct source generations');
      return { cic: true, mqtt: true, brokerProtocol: 'MQTT 3.1.1 QoS0 fixture', productionCic: false };
    } finally { await fixture.stop(); }
  },
};
