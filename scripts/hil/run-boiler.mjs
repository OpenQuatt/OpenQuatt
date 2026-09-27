#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { boilerScenario } from '../../tests/hil/scenarios/boiler.mjs';

export function parseBoilerArgs(argv) {
  return parseArgs(argv, {
    validStages: new Set(['smoke', 'assist', 'permissions', 'all']),
    defaultProfile: 'boiler-regression-v1',
  });
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseBoilerArgs(process.argv.slice(2));
    if (options.help) console.log(`Usage:
  node scripts/hil/run-boiler.mjs --controller URL --simulator URL --stage smoke

Mutating run:
  node scripts/hil/run-boiler.mjs --controller URL --simulator URL
    --device HOST --test-config configs/hil/boiler_regression_duo_wifi.yaml
    --restore-config configs/heatpump_controller_q/duo_hil.yaml --stage all --apply

Stages: smoke, assist, permissions, all. Generic safety/recovery options are documented by
scripts/hil/run-input-sources.mjs --help. Production control timers stay unchanged.
`);
    else await run(options, boilerScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
