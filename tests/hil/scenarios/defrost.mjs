import { asBoolean, asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';
import { assertNoFallback, controlContext, healthy, prepareControlRegression } from '../../../scripts/hil/control-regression.mjs';

const check = (condition, message) => { if (!condition) throw new Error(message); };
const path = (hp) => `/openquatt/odu-defrost/hp${hp}/`;
const flags = ['online', 'fresh', 'identity_ready', 'loaded', 'auto_defrost_control_ok',
  'busy', 'active', 'manual', 'can_trigger'];

export function defrostObservation(status, hp) {
  check(status?.hp === hp, `invalid HP${hp} defrost snapshot`);
  for (const flag of flags) check(typeof status[flag] === 'boolean', `missing defrost ${flag}`);
  check(Number.isInteger(status.operation_mode) && typeof status.state === 'string' &&
    typeof status.guard === 'string', 'missing defrost state/mode/guard');
  return Object.fromEntries(['hp', ...flags, 'operation_mode', 'state', 'guard'].map((key) => [key, status[key]]));
}

export function cycleCounters(text) {
  const result = {};
  for (const key of ['started', 'completed', 'aborted']) {
    const match = new RegExp(`(?:^|\\s)${key}=(\\d+)(?:\\s|$)`).exec(String(text));
    check(match, `missing manual-defrost-v1 ${key} counter`);
    result[key] = Number(match[1]);
    check(Number.isSafeInteger(result[key]), `invalid ${key} counter`);
  }
  return result;
}

export async function defrostPreflight(controller, simulator) {
  check(await simulator.value('text_sensor', 'ODU Defrost Contract', { optional: true }) === 'manual-defrost-v1',
    'BLOCKED: simulator requires installed manual-defrost-v1 capability; no defrost coverage claimed');
  cycleCounters(await simulator.value('text_sensor', 'ODU 1 defrost diagnostics'));
  for (const hp of [1, 2]) {
    defrostObservation(await controller.request(`${path(hp)}status`), hp);
    check(!asBoolean(await simulator.value('switch', `ODU ${hp} defrost`)), 'injected defrost must be off');
  }
}

export async function runDefrostScenarios(options) {
  const { controller, simulator, interrupted = () => false, stage } = options;
  check(['flow', 'overlap', 'cycle', 'all'].includes(stage), 'unsupported defrost stage');
  await defrostPreflight(controller, simulator);
  const ctx = controlContext(options);
  const samples = [];
  const cases = [];
  async function status(hp = 1) {
    const state = defrostObservation(await controller.request(`${path(hp)}status`), hp);
    check(samples.length < 5000, 'defrost sample limit exceeded');
    samples.push({ at: new Date(now()).toISOString(), ...state });
    return state;
  }
  async function action(hp, operation) {
    check(!interrupted(), 'HIL run interrupted before defrost action');
    const raw = await controller.request(`${path(hp)}status`);
    defrostObservation(raw, hp);
    check(typeof raw.csrf_token === 'string' && /^[a-f0-9]+$/i.test(raw.csrf_token), 'missing defrost CSRF token');
    check(!interrupted(), 'HIL run interrupted before defrost action write');
    // One authenticated request, never retry a trigger after a lost ACK.
    await controller.request(`${path(hp)}${operation}`, { method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded', Origin: controller.baseUrl },
      body: new URLSearchParams({ csrf_token: raw.csrf_token }).toString() });
  }
  const counters = async () => cycleCounters(await simulator.value('text_sensor', 'ODU 1 defrost diagnostics'));
  const now = options.now ?? Date.now;
  const delay = options.delay ?? ((ms) => new Promise(resolve => setTimeout(resolve, ms)));
  async function poll(label, predicate, hp = 1) {
    const deadline = now() + (options.timeoutMs ?? 600000);
    while (now() < deadline) {
      check(!interrupted(), 'HIL run interrupted during defrost polling');
      const s = await status(hp);
      await ctx.until('defrost control observation', () => true, state => {
        check(!state.boiler && !state.boilerActive && state.mode !== 4, 'unexpected boiler fallback during defrost');
      }); // refresh demand and retain selected flow/control observations
      if (await predicate(s)) return s;
      await delay(options.intervalMs ?? 1500);
    }
    throw new Error(`${label} timed out`);
  }
  async function heating() {
    await ctx.until('defrost heating baseline', s => healthy(s) && s.mode === 2 &&
      s.hp[0].running_confirmed, assertNoFallback);
    await action(1, 'load');
    await poll('defrost parameters loaded', s => s.loaded && !s.busy && s.fresh && s.identity_ready);
  }
  async function selectedFlow(value) {
    await controller.setNumber('HIL Defrost Flow Fixture', value);
    await controller.setSwitch('HIL Defrost Flow Fixture Enable', true);
    await ctx.until('exact selected flow fixture', s => s.flow === value);
  }
  let completedCounters;
  async function verifyCompletedCycle() {
    const s = await status();
    check(JSON.stringify(await counters()) === JSON.stringify(completedCounters), 'defrost duplicated or aborted after completion');
    check(s.online && s.fresh && s.identity_ready && !s.active && !s.manual && !s.busy &&
      !asBoolean(await controller.value('binary_sensor', 'HP1 - Defrost')), 'defrost reactivated after completion');
  }
  let failure;
  try {
    check(!asBoolean(await controller.value('switch', 'HIL Defrost Flow Fixture Enable')),
      'flow fixture must initially be off');
    check(!interrupted(), 'HIL run interrupted before defrost preparation');
    await ctx.idle();
    await ctx.demand();
    await heating();
    if (['overlap', 'all'].includes(stage)) {
      const before = await counters();
      await simulator.setSwitch('ODU 2 defrost', true);
      await poll('peer defrost observed', s => s.active && s.fresh, 2);
      await poll('peer overlap guard', s => s.guard === 'PEER_DEFROST_ACTIVE' && !s.can_trigger);
      await action(1, 'trigger');
      await poll('peer overlap refused', s => !s.busy && s.state === 'PEER_DEFROST_ACTIVE');
      check(JSON.stringify(await counters()) === JSON.stringify(before), 'peer overlap started a defrost cycle');
      cases.push('blocks_when_peer_defrost_active');
      await simulator.setSwitch('ODU 2 defrost', false);
      await poll('peer defrost cleared', s => !s.active && !s.busy && s.fresh, 2);
      await heating();
    }
    if (['flow', 'all'].includes(stage)) {
      await selectedFlow(249);
      const before = await counters();
      await action(1, 'trigger');
      await poll('below-minimum flow refused', s => !s.busy && s.state === 'INCIDENT_BLOCK');
      check(JSON.stringify(await counters()) === JSON.stringify(before), 'below-minimum flow started defrost');
      cases.push('rejects_below_minimum_flow');
    }
    // The accepted-at-minimum and completion contracts share one normal cycle.
    if (['flow', 'cycle', 'all'].includes(stage)) {
      await selectedFlow(250);
      await heating();
      await poll('defrost ready at minimum flow', s => s.online && s.fresh && s.identity_ready &&
        s.loaded && s.auto_defrost_control_ok && !s.busy && !s.active && s.can_trigger && s.guard === 'READY');
      const before = await counters();
      await action(1, 'trigger');
      await poll('manual defrost accepted', async s => {
        const actual = await controller.values([
          {key:'mode',domain:'sensor',name:'HP1 - Working Mode'},
          {key:'bit',domain:'binary_sensor',name:'HP1 - Defrost'},
          {key:'valve',domain:'binary_sensor',name:'HP1 - 4-Way valve'},
          {key:'heater',domain:'binary_sensor',name:'HP1 - Bottom plate heater'},
        ]);
        const c = await counters();
        return s.online && s.identity_ready && s.active && s.manual && s.fresh && s.operation_mode === 4 && s.state === 'ACCEPTED' &&
          asFiniteNumber(actual.mode) === 4 && asBoolean(actual.bit) && asBoolean(actual.valve) &&
          asBoolean(actual.heater) && c.started === before.started + 1;
      });
      if (['flow', 'all'].includes(stage)) cases.push('accepts_at_minimum_flow');
      await poll('manual defrost completes once', async s => {
        const c = await counters();
        check(c.started === before.started + 1 && c.aborted === before.aborted, 'defrost duplicated or aborted');
        return s.online && s.identity_ready && !s.active && !s.busy && !s.manual && s.fresh && s.operation_mode === 2 &&
          s.state === 'COMPLETE' && c.completed === before.completed + 1 &&
          asFiniteNumber(await controller.value('sensor', 'HP1 - Working Mode')) === 2 &&
          !asBoolean(await controller.value('binary_sensor', 'HP1 - Defrost'));
      });
      cases.push('forced_defrost_completes_once');
      await controller.setSwitch('HIL Defrost Flow Fixture Enable', false);
      completedCounters = { started: before.started + 1, completed: before.completed + 1, aborted: before.aborted };
      await poll('post-defrost heating recovered', async () => {
        await verifyCompletedCycle();
        const s = await ctx.until('post-defrost control readback', () => true, assertNoFallback);
        return healthy(s) && s.mode === 2 && s.hp[0].running_confirmed;
      });
      const stableUntil = now() + (options.holdMs ?? 30000);
      do {
        await verifyCompletedCycle();
        await ctx.until('post-defrost stable', () => true, s => {
          assertNoFallback(s);
          check(healthy(s) && s.mode === 2 && s.hp[0].running_confirmed, 'post-defrost heating not stable');
        });
        await delay(options.intervalMs ?? 1500);
      } while (now() < stableUntil);

    }
    await ctx.idle();
    if (completedCounters) await verifyCompletedCycle();
  } catch (error) { failure = error; }
  // Always release both the peer bit and synthetic flow, including lost ACKs.
  for (const [client, name] of [[simulator, 'ODU 2 defrost'], [controller, 'HIL Defrost Flow Fixture Enable']]) {
    try {
      await client.setSwitch(name, false);
      check(!asBoolean(await client.value('switch', name)), `${name} cleanup readback failed`);
    } catch (error) { failure = failure ? new AggregateError([failure, error], 'defrost and cleanup failed') : error; }
  }
  if (failure) {
    // Recovery ignores the original interrupt but remains bounded. CM0 may be
    // deferred by defrost ownership and the ordinary minimum runtime.
    try { await controlContext({ ...options, interrupted: () => false }).idle(); }
    catch (error) { failure = new AggregateError([failure, error], 'defrost and safe-stop recovery failed'); }
  }
  const result = { cases, samples, controlSamples: ctx.samples };
  if (failure) { failure.scenarioResult = result; throw failure; }
  return result;
}

export const defrostScenario = {
  name: 'defrost', preflight: defrostPreflight,
  beforeFirmwareRestore: async ({ controller, simulator }) => {
    await controlContext({ controller, simulator, interrupted: () => false }).idle();
  },
  prepare: async (controller, simulator, interrupted, snapshot) => {
    await defrostPreflight(controller, simulator);
    await prepareControlRegression(controller, simulator, interrupted, snapshot);
  },
  execute: runDefrostScenarios,
};
