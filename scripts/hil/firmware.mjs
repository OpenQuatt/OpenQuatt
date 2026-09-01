import { spawn } from 'node:child_process';

export function firmwareCommand({
  esphome = process.env.OQ_ESPHOME_BIN || 'esphome',
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

export async function flashFirmware(options, { spawnImpl = spawn, cwd = process.cwd() } = {}) {
  const command = firmwareCommand(options);
  await new Promise((resolve, reject) => {
    const child = spawnImpl(command.executable, command.args, {
      cwd,
      env: process.env,
      stdio: 'inherit',
      shell: false,
    });
    child.once('error', reject);
    child.once('exit', (code, signal) => {
      if (code === 0) {
        resolve();
        return;
      }
      reject(
        new Error(
          `firmware command failed${signal ? ` with signal ${signal}` : ` with exit code ${code}`}`,
        ),
      );
    });
  });
}
