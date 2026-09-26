import {
  assertNoFallback, controlContext, healthy, prepareCommunicationsRegression,
} from '../../../scripts/hil/control-regression.mjs';

export async function runCommunicationsScenarios(options) {
  if (!['fallback', 'all'].includes(options.stage)) throw new Error('unsupported communications stage');
  const hpCount = options.hpCount ?? 2;
  const indices = Array.from({ length: hpCount }, (_, i) => i + 1);
  const ctx = controlContext({ ...options, hpCount });
  await ctx.idle();
  await ctx.demand();
  await ctx.until('heat-pump heating baseline', (s) => healthy(s) && s.mode === 2 &&
    s.hp.some((hp) => hp.running_confirmed), assertNoFallback);
  await ctx.withLoss(indices, async () => {
    await ctx.until('causal link-loss fallback', (s) => s.mode === 4 && s.boiler && s.boilerActive &&
      s.hp.every((hp) => hp.link_state === 'lost' && hp.stop_unconfirmed &&
        hp.stop_unconfirmed_due_to_link_loss && !hp.stop_confirmed), (s) => {
      if ((s.mode === 4 || s.boiler || s.boilerActive) && !s.hp.every((hp) =>
        hp.link_state === 'lost' && hp.stop_unconfirmed &&
        hp.stop_unconfirmed_due_to_link_loss && !hp.stop_confirmed)) {
        throw new Error('fallback activated before causal stop timeout');
      }
    });
    await ctx.hold('fallback stable', (s) => {
      if (s.mode !== 4 || !s.boiler || !s.boilerActive || !s.hp.every((hp) => hp.stop_unconfirmed_due_to_link_loss)) {
        throw new Error('link-loss fallback is not stable');
      }
    });
  });
  // CM1 can be demand confirmation while peers are still recovering. A return
  // to CM4 before recovered heating is allowed; direct CM4 -> CM2 is also valid.
  const recoveredHeating = (s) => healthy(s) && s.mode === 2 &&
    !s.boiler && !s.boilerActive && s.flow !== null && s.flow >= 250 &&
    s.hp.every((hp) => !hp.stop_unconfirmed && !hp.stop_unconfirmed_due_to_link_loss && !hp.must_stop) &&
    s.hp.some((hp) => hp.running_confirmed);
  await ctx.until('clean link recovery and handback', recoveredHeating, (s) => {
    if (s.mode === 0) throw new Error('handback flapped to CM0');
  });
  await ctx.hold('handback stable', (s) => {
    assertNoFallback(s);
    if (!recoveredHeating(s)) {
      throw new Error('link recovery or handback not stable');
    }
  });
  await ctx.idle();
  return {
    cases: ['all_peers_lost_enters_safe_fallback', 'link_recovery_hands_control_back_cleanly'],
    samples: ctx.samples,
  };
}

export const communicationsScenario = {
  name: 'communications', prepare: prepareCommunicationsRegression, execute: runCommunicationsScenarios,
};

export const monoCommunicationsScenario = {
  name: 'communications-mono',
  prepare: (controller, simulator, interrupted, snapshot) =>
    prepareCommunicationsRegression(controller, simulator, interrupted, snapshot, 1),
  execute: (options) => runCommunicationsScenarios({ ...options, hpCount: 1 }),
};
