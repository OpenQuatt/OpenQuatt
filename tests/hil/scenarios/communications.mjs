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
  let sawCm1 = false;
  await ctx.until('clean link recovery and handback', (s) => {
    sawCm1 ||= s.mode === 1;
    return sawCm1 && healthy(s) && s.mode === 2 && !s.boiler && !s.boilerActive && s.hp.some((hp) => hp.running_confirmed);
  }, (s) => {
    if (s.mode === 0) throw new Error('handback flapped to CM0');
    if (sawCm1 && ![1, 2].includes(s.mode)) throw new Error('handback reverted after CM1');
  });
  await ctx.hold('handback stable', (s) => {
    assertNoFallback(s);
    if (!healthy(s) || s.mode !== 2 || !s.hp.some((hp) => hp.running_confirmed)) {
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
