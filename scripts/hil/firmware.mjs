import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmod, copyFile, mkdir, readFile, stat } from 'node:fs/promises';
import path from 'node:path';

function esphomeBinary(value) {
  return value || process.env.OQ_ESPHOME_BIN || 'esphome';
}

export function firmwareCommand({
  esphome = esphomeBinary(),
  config,
  device,
}) {
  if (!config) throw new Error('firmware config is required');
  if (!device) throw new Error('OTA device address is required');
  return {
    executable: esphome,
    args: [
      'run',
      '--device',
      device,
      '--ota-platform',
      'esphome',
      '--no-logs',
      config,
    ],
  };
}

export function firmwareCompileCommand({ esphome = esphomeBinary(), config }) {
  if (!config) throw new Error('firmware config is required');
  return { executable: esphome, args: ['compile', config] };
}

export function firmwareArtifactUploadCommand({
  esphome = esphomeBinary(),
  config,
  device,
  artifact,
}) {
  if (!config) throw new Error('firmware config is required');
  if (!device) throw new Error('OTA device address is required');
  if (!artifact) throw new Error('prevalidated firmware artifact is required');
  return {
    executable: esphome,
    args: [
      'upload',
      '--device',
      device,
      '--ota-platform',
      'esphome',
      '--file',
      artifact,
      config,
    ],
  };
}

async function runCommand(command, {
  spawnImpl = spawn,
  cwd = process.cwd(),
  capture = false,
} = {}) {
  let output = '';
  await new Promise((resolve, reject) => {
    const child = spawnImpl(command.executable, command.args, {
      cwd,
      env: process.env,
      stdio: capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
      shell: false,
    });
    if (capture) {
      child.stdout.on('data', (chunk) => {
        output += chunk;
        process.stdout.write(chunk);
      });
      child.stderr.on('data', (chunk) => {
        output += chunk;
        process.stderr.write(chunk);
      });
    }
    child.once('error', reject);
    child.once('exit', (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(
        `firmware command failed${signal ? ` with signal ${signal}` : ` with exit code ${code}`}`,
      ));
    });
  });
  return output;
}

export async function flashFirmware(options, { spawnImpl = spawn, cwd = process.cwd() } = {}) {
  const command = firmwareCommand(options);
  await runCommand(command, { spawnImpl, cwd });
}

export async function prepareFirmwareRestore({ config, artifactDirectory, esphome }, dependencies = {}) {
  if (!artifactDirectory) throw new Error('restore artifact directory is required');
  const output = await runCommand(firmwareCompileCommand({ config, esphome }), {
    ...dependencies,
    capture: true,
  });
  const artifactMatch = output.match(/Created:\s+(.+\/firmware\.ota\.bin)\s*$/m);
  const hashMatch = output.match(/config_hash=(0x[0-9a-fA-F]{8})\b/);
  if (!artifactMatch || !hashMatch) {
    throw new Error('compiled restore firmware output lacks artifact path or config hash');
  }
  await mkdir(artifactDirectory, { recursive: true, mode: 0o700 });
  const artifact = path.join(artifactDirectory, 'restore-firmware.ota.bin');
  await copyFile(artifactMatch[1].trim(), artifact);
  await chmod(artifact, 0o600);
  const identity = await firmwareArtifactIdentity(artifact);
  return { artifact, configHash: hashMatch[1].toLowerCase(), ...identity };
}

export async function firmwareArtifactIdentity(artifact) {
  const [data, metadata] = await Promise.all([readFile(artifact), stat(artifact)]);
  return {
    sha256: createHash('sha256').update(data).digest('hex'),
    size: metadata.size,
  };
}

export async function verifyFirmwareArtifact(artifact, expected) {
  if (!expected || !/^[0-9a-f]{64}$/.test(expected.sha256) ||
      !Number.isSafeInteger(expected.size) || expected.size <= 0) {
    throw new Error('snapshot has no valid restore artifact identity');
  }
  const actual = await firmwareArtifactIdentity(artifact);
  if (actual.sha256 !== expected.sha256 || actual.size !== expected.size) {
    throw new Error(
      `restore artifact integrity differs: expected ${expected.sha256}/${expected.size}, ` +
        `received ${actual.sha256}/${actual.size}`,
    );
  }
  return actual;
}

export async function flashFirmwareArtifact(options, dependencies = {}) {
  await runCommand(firmwareArtifactUploadCommand(options), dependencies);
}
