import assert from 'node:assert/strict';
import test from 'node:test';
import { chmod, mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { startNativeHaFixture } from '../../scripts/hil/native-ha-fixture.mjs';

test('missing native HA runtime exits promptly so recovery remains reachable', async () => {
  const saved = { python: process.env.OQ_HIL_PYTHON, key: process.env.OQ_HIL_NATIVE_API_KEY };
  process.env.OQ_HIL_PYTHON = '/nonexistent-openquatt-hil-python';
  process.env.OQ_HIL_NATIVE_API_KEY = 'unit-test-placeholder';
  const started = Date.now();
  try {
    await assert.rejects(startNativeHaFixture('192.168.2.86'), /ENOENT/);
    assert(Date.now() - started < 8000);
  } finally {
    for (const [field, value] of [['OQ_HIL_PYTHON', saved.python], ['OQ_HIL_NATIVE_API_KEY', saved.key]]) {
      if (value === undefined) delete process.env[field]; else process.env[field] = value;
    }
  }
});

test('native HA unexpected exit after readiness remains latched and prevents success', { skip: process.platform === 'win32' && 'POSIX fake executable' }, async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'warmup-native-exit-'));
  const executable = path.join(directory, 'fake-python');
  const saved = { python: process.env.OQ_HIL_PYTHON, key: process.env.OQ_HIL_NATIVE_API_KEY };
  await writeFile(executable, '#!/bin/sh\necho \'{"type":"ready"}\'\nsleep 0.05\nexit 1\n');
  await chmod(executable, 0o700);
  process.env.OQ_HIL_PYTHON = executable;
  process.env.OQ_HIL_NATIVE_API_KEY = 'unit-test-placeholder';
  try {
    const fixture = await startNativeHaFixture('192.168.2.86');
    await new Promise((resolve) => setTimeout(resolve, 150));
    assert.throws(() => fixture.assertHealthy(), /exited 1/);
    await assert.rejects(fixture.send({ room: 19 }), /exited 1/);
    await assert.rejects(fixture.stop(), /exited 1/);
  } finally {
    for (const [field, value] of [['OQ_HIL_PYTHON', saved.python], ['OQ_HIL_NATIVE_API_KEY', saved.key]]) {
      if (value === undefined) delete process.env[field]; else process.env[field] = value;
    }
    await rm(directory, { recursive: true, force: true });
  }
});

