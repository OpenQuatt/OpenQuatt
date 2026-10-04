#!/usr/bin/env node
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describeHilFailure, parseArgs, run } from './run-input-sources.mjs';
import { controlledWarmupScenario } from '../../tests/hil/scenarios/controlled-warmup.mjs';

export const WARMUP_STAGES = new Set(['smoke', 'boundaries', 'curve', 'regulation', 'regulation-tail', 'timing', 'stale', 'restart', 'duration', 'timers', 'short-timers', 'all']);

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseArgs(process.argv.slice(2), {
      validStages: WARMUP_STAGES,
      defaultProfile: 'controlled-warmup-production-v1',
    });
    if (options.help) {
      console.log('Controlled warmup HIL: smoke, boundaries, curve, regulation and regulation-tail (native HA fixture required), timing (real 5 min), stale (real 10 min), restart, duration (real 1 h), timers (timing/stale/restart/duration), short-timers (timing/stale/restart), all.');
      console.log('Use --controller URL --simulator URL --device HOST --test-config configs/hil/controlled_warmup_duo_wifi.yaml --restore-config configs/heatpump_controller_q/duo_hil.yaml --stage all --apply. Generic input-source runner recovery options apply.');
    } else await run(options, controlledWarmupScenario);
  } catch (error) {
    console.error(`FAIL ${describeHilFailure(error)}`);
    process.exitCode = 1;
  }
}
