import { asBoolean, asFiniteNumber } from '../../../scripts/hil/rest-client.mjs';
import { controlContext, healthy, prepareCommunicationsRegression } from '../../../scripts/hil/control-regression.mjs';

const check = (condition, message) => { if (!condition) throw new Error(message); };
const settings = [
  { key: 'deficit', domain: 'sensor', name: 'HP deficit (W)' },
  { key: 'on', domain: 'number', name: 'CM3 deficit ON threshold' },
  { key: 'off', domain: 'number', name: 'CM3 deficit OFF threshold' },
  { key: 'rated', domain: 'number', name: 'Boiler rated heat power' },
  { key: 'connection', domain: 'select', name: 'Boiler connection' },
  { key: 'link', domain: 'binary_sensor', name: 'OTB - Boiler Link Available' },
  { key: 'mismatch', domain: 'binary_sensor', name: 'OTB - Boiler Connection Mismatch' },
];
const peerSettings = [
  { key: 'valid', domain: 'binary_sensor', name: 'Master status valid' },
  { key: 'ch', domain: 'binary_sensor', name: 'Master CH Enable' },
  { key: 'active', domain: 'binary_sensor', name: 'Simulated CH active' },
  { key: 'requests', domain: 'sensor', name: 'OpenTherm request count' },
  { key: 'rises', domain: 'sensor', name: 'OpenTherm CH Enable rising edge count' },
];

export async function boilerPreflight(controller, simulator) {
  // Capability/type checks are read-only and precede snapshot, settings and OTA.
  const gates = await simulator.values([
    {key:'responses',domain:'switch',name:'Responses enabled'},
    {key:'automatic',domain:'switch',name:'Automatic boiler model'},
    {key:'manual',domain:'switch',name:'Manual telemetry'},
    {key:'dhw',domain:'switch',name:'DHW demand'},
    {key:'fault',domain:'switch',name:'Fault indication'},
  ]);
  check(asBoolean(gates.responses) && asBoolean(gates.automatic) && !asBoolean(gates.manual) &&
    !asBoolean(gates.dhw) && !asBoolean(gates.fault), 'boiler simulator requires automatic fault-free CH operation');
  const peer = await simulator.values(peerSettings);
  for (const key of ['valid', 'ch', 'active']) asBoolean(peer[key]);
  for (const key of ['requests', 'rises']) check(Number.isSafeInteger(asFiniteNumber(peer[key])) && Number(peer[key]) >= 0, `invalid OpenTherm ${key}`);
  const v = await controller.values(settings);
  for (const key of ['on', 'off', 'rated']) check(asFiniteNumber(v[key]) !== null, `missing boiler ${key}`);
  check(Number(v.on) > Number(v.off) && Number(v.off) >= 0 && Number(v.rated) > 0,
    'invalid boiler thresholds/rated power');
}

export async function runBoilerScenarios(options) {
  const { controller, simulator, interrupted = () => false, stage } = options;
  check(['assist', 'permissions', 'all'].includes(stage), 'unsupported boiler stage');
  const cases = [];
  const now = options.now ?? Date.now;
  let lastRequests, lastFreshAt = now();
  const ctx = controlContext({ ...options, demandW: 20000, observeExtra: async state => {
    const v = await controller.values(settings);
    state.deficit = asFiniteNumber(v.deficit);
    state.assistOn = asFiniteNumber(v.on);
    state.assistOff = asFiniteNumber(v.off);
    check(state.deficit !== null && state.assistOn !== null && state.assistOff !== null,
      'missing boiler deficit/threshold feedback');
    check(v.connection === 'OpenTherm' && asBoolean(v.link) && !asBoolean(v.mismatch),
      'boiler transport unavailable or mismatched');
    const peer = await simulator.values(peerSettings);
    check(asBoolean(peer.valid), 'missing fresh OpenTherm master status');
    state.peerCh = asBoolean(peer.ch);
    state.peerActive = asBoolean(peer.active);
    state.peerRequests = asFiniteNumber(peer.requests);
    state.peerRises = asFiniteNumber(peer.rises);
    check(Number.isSafeInteger(state.peerRequests) && state.peerRequests >= 0 &&
      Number.isSafeInteger(state.peerRises) && state.peerRises >= 0, 'invalid OpenTherm counters');
    check(lastRequests === undefined || state.peerRequests >= lastRequests, 'OpenTherm counter reset during run');
    if (lastRequests === undefined || state.peerRequests > lastRequests) lastFreshAt = now();
    lastRequests = state.peerRequests;
    check(now() - lastFreshAt < 20000, 'OpenTherm peer feedback became stale');
  }});
  async function permission(name, value) {
    check(!interrupted(), 'HIL run interrupted before boiler permission write');
    await controller.setSwitch(name, value);
    check(asBoolean(await controller.value('switch', name)) === value, `${name} readback differs`);
  }
  let forbiddenRises;
  const noBoiler = s => {
    check(!s.boiler && !s.boilerActive && !s.peerCh && !s.peerActive &&
      s.mode !== 3 && s.mode !== 4, 'boiler activated without permission');
    check(s.peerRises === forbiddenRises, 'forbidden boiler CH pulse detected');
  };
  const heating = s => healthy(s) && s.hp.every(h => !h.must_stop && !h.stop_unconfirmed) &&
    s.hp.some(h => h.running_confirmed) && s.flow !== null && s.flow >= 250;
  const heatInvariant = s => check(heating(s) && [2, 3].includes(s.mode), 'healthy heat-pump heating lost during assist');
  const cleared = s => heating(s) && s.mode === 2 && !s.boiler && !s.boilerActive && !s.peerCh && !s.peerActive;
  let failure;
  try {
    const initial = await ctx.idle();
    forbiddenRises = initial.peerRises;
    await permission('Boiler assist enabled', false);
    await permission('Boiler fallback on heat-pump fault', false);
    await ctx.demand();
    await ctx.until('high-demand CM2 baseline', s => heating(s) && s.mode === 2 && s.deficit > s.assistOn, noBoiler);
    if (['permissions', 'all'].includes(stage)) {
      // Real defaults: 120s CM2 dwell + 300s promote + margin. Do not shorten
      // firmware guards; fake-clock tests may inject a shorter observation span.
      await ctx.hold('disabled assist cannot promote', s => {
        heatInvariant(s); noBoiler(s);
        check(s.deficit > s.assistOn, 'assist permission test lost its sustained deficit');
      }, options.permissionHoldMs ?? 450000);
      cases.push('assist_disabled_blocks_sustained_deficit');
    }
    if (['assist', 'all'].includes(stage)) {
      const before = await ctx.until('boiler assist starts from withdrawn output', s => heating(s) && !s.boiler && !s.boilerActive && !s.peerCh && !s.peerActive, noBoiler);
      await permission('Boiler assist enabled', true);
      const assisted = await ctx.until('CM3 assist and received OpenTherm output', s => s.mode === 3 &&
        s.boiler && s.boilerActive && s.peerCh && s.peerActive && s.deficit > s.assistOn &&
        s.peerRequests > before.peerRequests && s.peerRises === before.peerRises + 1, heatInvariant);
      await ctx.hold('CM3 assist stable', s => {
        heatInvariant(s);
        check(s.mode === 3 && s.boiler && s.boilerActive && s.peerCh && s.peerActive && s.peerRises === assisted.peerRises,
          'CM3 lacks stable controller/received boiler output');
      });
      cases.push('sustained_deficit_enters_cm3_with_boiler');
      await ctx.setDemand(4000);
      const noNewStart = s => {
        heatInvariant(s);
        check(s.peerRises === assisted.peerRises, 'boiler CH pulse during handback');
      };
      await ctx.until('deficit cleared', s => s.deficit <= s.assistOff, noNewStart);
      // Boiler power can already withdraw while the CM3 minimum dwell expires.
      await ctx.until('CM3 demotes with boiler output withdrawn', s => cleared(s) && s.peerRequests > assisted.peerRequests, noNewStart);
      await ctx.hold('CM2 handback stable', s => {
        noNewStart(s); check(cleared(s), 'boiler handback not stable');
      });
      forbiddenRises = assisted.peerRises;
      cases.push('cleared_deficit_returns_to_cm2');
    }
    if (['permissions', 'all'].includes(stage)) {
      await permission('Boiler assist enabled', false);
      await ctx.withLoss([1, 2], async () => {
        await ctx.until('causal stop uncertainty with fallback disabled', s => s.hp.every(h =>
          h.link_state === 'lost' && h.stop_unconfirmed && h.stop_unconfirmed_due_to_link_loss && !h.stop_confirmed), noBoiler);
        await ctx.hold('disabled fallback remains off after stop timeout', s => {
          noBoiler(s);
          check(s.hp.every(h => h.link_state === 'lost' && h.stop_unconfirmed_due_to_link_loss),
            'fallback permission test lost its causal outage');
        });
      });
      await ctx.until('links and CM2 recover without fallback', cleared, noBoiler);
      cases.push('fallback_disabled_blocks_causal_outage');
    }
    await ctx.idle();
  } catch (error) { failure = error; }
  // Lost response ACKs are cleaned by withLoss; release both permissions even
  // if an earlier write succeeded but its ACK/readback was lost.
  for (const name of ['Boiler assist enabled', 'Boiler fallback on heat-pump fault']) {
    try {
      await controller.setSwitch(name, false);
      check(!asBoolean(await controller.value('switch', name)), `${name} cleanup failed`);
    } catch (error) { failure = failure ? new AggregateError([failure, error], 'boiler run and cleanup failed') : error; }
  }
  if (failure) {
    try { await controlContext({ ...options, interrupted: () => false }).idle(); }
    catch (error) { failure = new AggregateError([failure, error], 'boiler run and safe-stop recovery failed'); }
  }
  const result = { cases, samples: ctx.samples };
  if (failure) { failure.scenarioResult = result; throw failure; }
  return result;
}

export const boilerScenario = {
  name: 'boiler', preflight: boilerPreflight,
  prepare: async (controller, simulator, interrupted, snapshot) => {
    await boilerPreflight(controller, simulator);
    await prepareCommunicationsRegression(controller, simulator, interrupted, snapshot);
    check(!interrupted(), 'HIL run interrupted before boiler fallback write');
    await controller.setSwitch('Boiler fallback on heat-pump fault', false);
  },
  beforeFirmwareRestore: async ({ controller, simulator }) => {
    const ctx = controlContext({ controller, simulator, interrupted: () => false });
    await ctx.idle();
    await ctx.until('boiler command withdrawn before restore OTA', s => !s.boiler && !s.boilerActive);
    const startingRequests = asFiniteNumber((await simulator.values(peerSettings)).requests);
    check(Number.isSafeInteger(startingRequests), 'missing OpenTherm request counter before recovery');
    const deadline = Date.now() + 180000;
    while (Date.now() < deadline) {
      const peer = await simulator.values(peerSettings);
      if (asFiniteNumber(peer.requests) > startingRequests && asBoolean(peer.valid) && !asBoolean(peer.ch) && !asBoolean(peer.active)) return;
      await new Promise(resolve => setTimeout(resolve, 1500));
    }
    throw new Error('received boiler output did not withdraw before restore OTA');
  },
  execute: runBoilerScenarios,
};
