#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { parseArgs, run } from './run-input-sources.mjs';
import { v2PerformanceScenario } from '../../tests/hil/scenarios/v2-performance.mjs';

const VALID_STAGES = new Set(['smoke', 'power', 'performance', 'all']);
const HIL_PROFILE = 'issue-667-v2-performance-v1';
const SIMULATOR_CONTRACT = 'openquatt-modbus-opentherm-v2';

function usage() {
  return `Usage:
  node scripts/hil/run-v2-performance.mjs --controller URL --simulator URL [--stage smoke]

Mutating run:
  node scripts/hil/run-v2-performance.mjs --controller URL --simulator URL \\
    --device HOST --test-config configs/hil/issue_667_v2_performance_duo_wifi.yaml \\
    --restore-config configs/heatpump_controller_q/duo_wifi_hil.yaml --stage all --apply

Stages: smoke, power, performance, all. All generic HIL runner safety and recovery
options are accepted; use --help on scripts/hil/run-input-sources.mjs for details.
`;
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseArgs(process.argv.slice(2), {
      validStages: VALID_STAGES,
      defaultProfile: HIL_PROFILE,
      defaultSimulatorContract: SIMULATOR_CONTRACT,
    });
    if (options.help) console.log(usage());
    else await run(options, v2PerformanceScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
