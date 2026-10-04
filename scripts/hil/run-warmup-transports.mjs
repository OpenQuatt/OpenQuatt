#!/usr/bin/env node
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { warmupTransportScenario } from '../../tests/hil/scenarios/warmup-transports.mjs';
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = parseArgs(process.argv.slice(2), { validStages: new Set(['transports']), defaultProfile: 'controlled-warmup-production-v1' });
    if (options.help) console.log('Local CIC/MQTT warmup HIL: explicit OQ_HIL_FIXTURE_HOST=lab desktop IPv4; --stage transports --apply with --device --test-config and --restore-config. Snapshots preserve original broker credential flags and private CIC URL.');
    else await run(options, warmupTransportScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
