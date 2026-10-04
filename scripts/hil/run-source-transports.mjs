#!/usr/bin/env node
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs, run } from './run-input-sources.mjs';
import { sourceTransportScenario } from '../../tests/hil/scenarios/source-transports.mjs';
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = parseArgs(process.argv.slice(2), { validStages: new Set(['transports']), defaultProfile: 'source-transports-v1' });
    if (options.help) console.log('Local CIC/MQTT source HIL: explicit OQ_HIL_FIXTURE_HOST=lab desktop IPv4; --stage transports --apply with --device --test-config and --restore-config. Snapshots preserve original broker credential flags and private CIC URL.');
    else await run(options, sourceTransportScenario);
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
