#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { communicationsScenario } from '../../tests/hil/scenarios/communications.mjs';

export function parseCommunicationsArgs(argv) {
  return parseArgs(argv, {
    validStages: new Set(['smoke', 'fallback', 'all']),
    defaultProfile: 'control-regression-v1',
  });
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseCommunicationsArgs(process.argv.slice(2));
    if (options.help) console.log(`Usage:
  node scripts/hil/run-communications.mjs --controller URL --simulator URL --stage smoke

Mutating run:
  node scripts/hil/run-communications.mjs --controller URL --simulator URL
    --device HOST --test-config configs/hil/control_regression_duo_wifi.yaml
    --restore-config configs/heatpump_controller_q/duo_hil.yaml --stage fallback --apply

Stages: smoke, fallback, all. Generic safety/recovery options are documented by
scripts/hil/run-input-sources.mjs --help. Production control timers stay unchanged.
`);
    else await run(options, communicationsScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
