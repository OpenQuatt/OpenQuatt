#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { duoScenario } from '../../tests/hil/scenarios/duo.mjs';

export function parseDuoArgs(argv) {
  return parseArgs(argv, {
    validStages: new Set(['smoke', 'start', 'peer-loss', 'all']),
    defaultProfile: 'control-regression-v1',
  });
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseDuoArgs(process.argv.slice(2));
    if (options.help) console.log(`Usage:
  node scripts/hil/run-duo.mjs --controller URL --simulator URL --stage smoke

Mutating run:
  node scripts/hil/run-duo.mjs --controller URL --simulator URL
    --device HOST --test-config configs/hil/control_regression_duo_wifi.yaml
    --restore-config configs/heatpump_controller_q/duo_hil.yaml --stage start --apply

Stages: smoke, start, peer-loss, all. Generic safety/recovery options are documented by
scripts/hil/run-input-sources.mjs --help. Production control timers stay unchanged.
`);
    else await run(options, duoScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
