#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { defrostScenario } from '../../tests/hil/scenarios/defrost.mjs';

export function parseDefrostArgs(argv) {
  return parseArgs(argv, {
    validStages: new Set(['smoke', 'flow', 'overlap', 'cycle', 'all']),
    defaultProfile: 'defrost-regression-v1',
  });
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseDefrostArgs(process.argv.slice(2));
    if (options.help) console.log(`Usage:
  node scripts/hil/run-defrost.mjs --controller URL --simulator URL --stage smoke

Mutating run:
  node scripts/hil/run-defrost.mjs --controller URL --simulator URL
    --device HOST --test-config configs/hil/defrost_regression_duo_wifi.yaml
    --restore-config configs/heatpump_controller_q/duo_hil.yaml --stage all --apply

Stages: smoke, flow, overlap, cycle, all. Generic safety/recovery options are documented by
scripts/hil/run-input-sources.mjs --help. Production control timers stay unchanged.
`);
    else await run(options, defrostScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
