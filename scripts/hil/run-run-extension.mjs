#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { parseArgs, run } from './run-input-sources.mjs';
import { runExtensionScenario } from '../../tests/hil/scenarios/run-extension.mjs';

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseArgs(process.argv.slice(2), {
      validStages: new Set(['smoke', 'extension', 'all']),
      defaultProfile: 'run-extension-v2-fast',
      defaultSimulatorContract: 'openquatt-modbus-opentherm-v2',
    });
    if (options.help) {
      console.log('node scripts/hil/run-run-extension.mjs --controller URL --simulator URL --device HOST ' +
        '--test-config scripts/hil/configs/run_extension_duo.yaml ' +
        '--restore-config configs/heatpump_controller_q/duo_hil.yaml --stage extension --apply');
    } else {
      await run(options, runExtensionScenario);
    }
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
