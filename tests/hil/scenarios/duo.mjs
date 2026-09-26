import {
  assertNoFallback, controlContext, healthy, prepareControlRegression,
} from '../../../scripts/hil/control-regression.mjs';

export async function runDuoScenarios(options) {
  const ctx = controlContext(options);
  const cases = [];
  if (!['start', 'peer-loss', 'all'].includes(options.stage)) throw new Error('unsupported Duo stage');
  for (const lost of [1, 2]) {
    const available = 3 - lost;
    await ctx.idle();
    if (options.stage === 'start' || options.stage === 'all') {
      await ctx.withLoss([lost], async () => {
        await ctx.until('peer lost before demand', (s) => s.hp[lost - 1].link_state === 'lost');
        await ctx.demand();
        let sawCm1 = false;
        await ctx.until('available peer compressor started', (s) => {
          sawCm1 ||= s.mode === 1;
          return sawCm1 && s.mode === 2 && s.hp[available - 1].link_state === 'healthy' &&
            s.hp[available - 1].running_confirmed && !s.hp[lost - 1].available_for_start;
        }, assertNoFallback);
        await ctx.hold('single-peer heating stable', (s) => {
          assertNoFallback(s);
          if (s.mode !== 2 || s.hp[available - 1].link_state !== 'healthy' ||
              !s.hp[available - 1].running_confirmed || s.hp[lost - 1].link_state !== 'lost' ||
              s.hp[lost - 1].available_for_start) throw new Error('available peer lost heating');
        });
      });
      cases.push({ contract: 'available_peer_starts_from_idle', lost });
      await ctx.until('recovered Duo link and cleared cause', healthy, assertNoFallback);
      await ctx.idle();
    }
    if (options.stage === 'peer-loss' || options.stage === 'all') {
      await ctx.demand();
      await ctx.until('both peers ready, heating active', (s) => healthy(s) && s.mode === 2 &&
        s.hp.some((hp) => hp.running_confirmed), assertNoFallback);
      await ctx.withLoss([lost], async () => {
        await ctx.until('one peer lost, remaining peer heats', (s) => s.mode === 2 &&
          s.hp[lost - 1].link_state === 'lost' && s.hp[available - 1].link_state === 'healthy' &&
          s.hp[available - 1].running_confirmed, assertNoFallback);
        await ctx.hold('partial outage stable', (s) => {
          assertNoFallback(s);
          if (s.mode !== 2 || s.hp[available - 1].link_state !== 'healthy' ||
              !s.hp[available - 1].running_confirmed || s.hp[lost - 1].link_state !== 'lost' ||
              s.hp[lost - 1].available_for_start) {
            throw new Error('remaining peer lost communication or heating');
          }
        });
      });
      cases.push({ contract: 'remaining_peer_keeps_heating', lost });
      await ctx.until('recovered Duo link and cleared cause', healthy, assertNoFallback);
      await ctx.idle();
    }
  }
  return { cases, samples: ctx.samples };
}

export const duoScenario = { name: 'duo', prepare: prepareControlRegression, execute: runDuoScenarios };
