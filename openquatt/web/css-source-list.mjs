import path from "node:path";

export const CSS_SOURCE_FILES = [
  "css/src/00-tokens.css",
  "css/src/01-shell.css",
  "css/src/02-interface-panel.css",
  "css/src/02-devtools.css",
  "css/src/03-modals.css",
  "css/src/04-debug-recording.css",
  "css/src/05-layout-controls.css",
  "css/src/06-shared-stats.css",
  "css/src/10-settings-layout.css",
  "css/src/11-settings-climate.css",
  "css/src/12-settings-service.css",
  "css/src/13-settings-connectivity.css",
  "css/src/14-settings-system-tools.css",
  "css/src/15-settings-cards-fields.css",
  "css/src/16-settings-storage.css",
  "css/src/17-settings-integrations-controls.css",
  "css/src/20-overview.css",
  "css/src/21-control-replay.css",
  "css/src/30-energy.css",
  "css/src/40-heatpump.css",
  "css/src/90-responsive.css",
];

export function resolveCssSources(webDir, { preview = false } = {}) {
  return CSS_SOURCE_FILES
    .filter((sourceFile) => preview || sourceFile !== "css/src/02-devtools.css")
    .map((sourceFile) => path.join(webDir, ...sourceFile.split("/")));
}
