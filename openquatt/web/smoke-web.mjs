import { access, readdir, readFile, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { build, transform } from "esbuild";
import { resolveCssSources } from "./css-source-list.mjs";

const __filename = fileURLToPath(import.meta.url);
const webDir = path.dirname(__filename);
const repoDir = path.resolve(webDir, "..", "..");
const jsSourceDir = path.join(webDir, "js", "src");
const cssSources = resolveCssSources(webDir);

const allowedBareImports = new Set(["virtual:embedded-assets"]);
const boundaryAllowedEdges = new Set([
  "core/entity-actions.js -> features/debug-recording.js",
  "core/entity-actions.js -> features/control-replay-actions.js",
  "core/entity-actions.js -> features/firmware-actions.js",
  "core/entity-actions.js -> features/firmware-update.js",
  "core/entity-actions.js -> features/mqtt-actions.js",
  "core/entity-actions.js -> features/odu-eeprom-dump.js",
  "core/entity-actions.js -> features/odu-runtime-frequency.js",
  "core/entity-actions.js -> features/quickstart-ui-actions.js",
  "core/entity-actions.js -> features/security-actions.js",
  "core/entity-actions.js -> features/shell-actions.js",
  "core/entity-actions.js -> features/storage-history.js",
  "core/entity-actions.js -> features/system-actions.js",
  "core/entity-actions.js -> features/view-actions.js",
  "core/entity-actions.js -> features/webserver-logs.js",
  "core/entity-actions.js -> settings/electrical-limit.js",
  "core/entity-actions.js -> views/energy.js",
  "core/entity-sync.js -> features/mqtt-actions.js",
  "core/entity-sync.js -> features/odu-eeprom-dump.js",
  "core/entity-sync.js -> features/odu-runtime-frequency.js",
  "core/entity-sync.js -> features/security-actions.js",
  "core/entity-write-actions.js -> features/firmware-update.js",
  "core/entity-write-actions.js -> features/security-actions.js",
  "core/entity-write-actions.js -> features/storage-history.js",
  "core/entity-write-actions.js -> features/webserver-logs.js",
  "core/render-signatures.js -> features/security-actions.js",
  "core/runtime.js -> features/debug-recording.js",
  "views/overview.js -> settings/cooling.js",
  "views/shell.js -> settings/core.js",
]);

function toBundlePath(value) {
  return value.split(path.sep).join("/");
}

async function fileExists(filePath) {
  try {
    await access(filePath);
    return true;
  } catch (_error) {
    return false;
  }
}

async function collectFiles(dir, predicate) {
  const entries = await readdir(dir, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...await collectFiles(fullPath, predicate));
      continue;
    }
    if (!predicate || predicate(fullPath)) {
      files.push(fullPath);
    }
  }
  return files.sort((a, b) => a.localeCompare(b));
}

function extractImportSpecifiers(source) {
  const specifiers = [];
  const staticImportPattern = /\b(?:import|export)\s+(?:[^"']*?\s+from\s*)?["']([^"']+)["']/g;
  const dynamicImportPattern = /\bimport\s*\(\s*["']([^"']+)["']\s*\)/g;
  for (const pattern of [staticImportPattern, dynamicImportPattern]) {
    let match;
    while ((match = pattern.exec(source)) !== null) {
      specifiers.push(match[1]);
    }
  }
  return specifiers;
}

async function resolveRelativeImport(importer, specifier) {
  const basePath = path.resolve(path.dirname(importer), specifier);
  const candidates = path.extname(basePath) ? [basePath] : [`${basePath}.js`, path.join(basePath, "index.js")];
  for (const candidate of candidates) {
    if (await fileExists(candidate)) {
      return candidate;
    }
  }
  return null;
}

function toSourceRelative(filePath) {
  return toBundlePath(path.relative(jsSourceDir, filePath));
}

async function buildSourceImportGraph() {
  const sourceFiles = await collectFiles(jsSourceDir, (filePath) => filePath.endsWith(".js"));
  const graph = new Map(sourceFiles.map((filePath) => [toSourceRelative(filePath), []]));
  for (const filePath of sourceFiles) {
    const source = await readFile(filePath, "utf8");
    const relativePath = toSourceRelative(filePath);
    for (const specifier of extractImportSpecifiers(source)) {
      if (!specifier.startsWith(".")) {
        continue;
      }
      const resolved = await resolveRelativeImport(filePath, specifier);
      if (resolved) {
        graph.get(relativePath).push(toSourceRelative(resolved));
      }
    }
  }
  return graph;
}

async function checkSourceImports() {
  const sourceFiles = await collectFiles(jsSourceDir, (filePath) => filePath.endsWith(".js"));
  const errors = [];
  for (const filePath of sourceFiles) {
    const source = await readFile(filePath, "utf8");
    for (const specifier of extractImportSpecifiers(source)) {
      if (!specifier.startsWith(".")) {
        if (!allowedBareImports.has(specifier)) {
          errors.push(`${path.relative(repoDir, filePath)} imports unexpected bare module ${specifier}`);
        }
        continue;
      }
      const resolved = await resolveRelativeImport(filePath, specifier);
      if (!resolved) {
        errors.push(`${path.relative(repoDir, filePath)} has unresolved import ${specifier}`);
      }
    }
  }
  if (errors.length) {
    throw new Error(`Source import check failed:\n- ${errors.join("\n- ")}`);
  }
}

async function checkImportCycles() {
  const graph = await buildSourceImportGraph();
  const visiting = new Set();
  const visited = new Set();
  const stack = [];
  const cycleMessages = new Set();

  function visit(node) {
    if (visiting.has(node)) {
      const start = stack.indexOf(node);
      if (start >= 0) {
        cycleMessages.add([...stack.slice(start), node].join(" -> "));
      }
      return;
    }
    if (visited.has(node)) {
      return;
    }

    visiting.add(node);
    stack.push(node);
    for (const target of graph.get(node) || []) {
      visit(target);
    }
    stack.pop();
    visiting.delete(node);
    visited.add(node);
  }

  for (const node of graph.keys()) {
    visit(node);
  }

  if (cycleMessages.size) {
    throw new Error(`Import cycle check failed:\n- ${[...cycleMessages].join("\n- ")}`);
  }
}

async function checkImportBoundaries() {
  const graph = await buildSourceImportGraph();
  const errors = [];
  const seenEdges = new Set();
  for (const [source, targets] of graph.entries()) {
    for (const target of targets) {
      const edge = `${source} -> ${target}`;
      seenEdges.add(edge);
      if (source.startsWith("core/") && (target.startsWith("features/") || target.startsWith("settings/") || target.startsWith("views/")) && !boundaryAllowedEdges.has(edge)) {
        errors.push(`${edge}; core-to-UI imports need an explicit smoke allowlist entry`);
      }
      if (source.startsWith("views/") && target.startsWith("settings/") && !boundaryAllowedEdges.has(edge)) {
        errors.push(`${edge}; view-to-settings imports need an explicit smoke allowlist entry`);
      }
      if (source.startsWith("settings/") && target.startsWith("views/") && !boundaryAllowedEdges.has(edge)) {
        errors.push(`${edge}; settings-to-view imports need an explicit smoke allowlist entry`);
      }
    }
  }
  for (const edge of boundaryAllowedEdges) {
    if (!seenEdges.has(edge)) {
      errors.push(`${edge}; remove stale smoke allowlist entry`);
    }
  }
  if (errors.length) {
    throw new Error(`Import boundary check failed:\n- ${errors.join("\n- ")}`);
  }
}

async function buildEmbeddedAssetModule() {
  const assets = [
    ["HP_GENERATION_IMAGE_V1", path.join(webDir, "assets", "quatt-hybrid-v1.webp")],
    ["HP_GENERATION_IMAGE_V2", path.join(webDir, "assets", "quatt-hybrid-v2.webp")],
  ];
  const lines = [];
  for (const [name, assetPath] of assets) {
    const bytes = await readFile(assetPath);
    lines.push(`export const ${name} = "data:image/webp;base64,${bytes.toString("base64")}";`);
  }
  const logoMarkup = await readFile(path.join(webDir, "assets", "openquatt-logo.svg"), "utf8");
  lines.push(`export const LOGO_MARKUP = ${JSON.stringify(logoMarkup.trim())};`);
  return lines.join("\n");
}

function embeddedAssetsPlugin() {
  return {
    name: "openquatt-smoke-embedded-assets",
    setup(pluginBuild) {
      pluginBuild.onResolve({ filter: /^virtual:embedded-assets$/ }, (args) => ({
        path: args.path,
        namespace: "openquatt-smoke-embedded-assets",
      }));
      pluginBuild.onLoad({ filter: /.*/, namespace: "openquatt-smoke-embedded-assets" }, async () => ({
        contents: await buildEmbeddedAssetModule(),
        loader: "js",
      }));
    },
  };
}

async function checkJavaScriptBundleFresh() {
  const outputPath = path.join(webDir, "js", "openquatt-app.js");
  const result = await build({
    entryPoints: [path.join(jsSourceDir, "app.js")],
    bundle: true,
    format: "iife",
    legalComments: "none",
    minify: true,
    target: "es2020",
    define: { __OQ_PREVIEW__: "false" },
    write: false,
    plugins: [embeddedAssetsPlugin()],
  });
  const header = [
    `/* Generated minified bundle: ${toBundlePath(path.relative(webDir, outputPath))}. */`,
    "/* Source files are in ./js/src and ./css/src. Rebuild with: node openquatt/web/build-assets.mjs */",
  ].join("\n");
  const expected = `${header}\n${(result.outputFiles[0]?.text || "").trim()}\n`;
  const actual = await readFile(outputPath, "utf8");
  if (actual !== expected) {
    throw new Error("JS bundle is stale. Run: rtk npm run build:web");
  }
}

async function checkCssBundleFresh() {
  const outputPath = path.join(webDir, "css", "openquatt-app.css");
  const sourceParts = await Promise.all(cssSources.map((source) => readFile(source, "utf8")));
  const header = [
    `/* Generated minified bundle: ${toBundlePath(path.relative(webDir, outputPath))}. */`,
    "/* Source files are in ./js/src and ./css/src. Rebuild with: node openquatt/web/build-assets.mjs */",
  ].join("\n");
  const minified = (await transform(sourceParts.map((source) => source.trimEnd()).join("\n"), { loader: "css", minify: true })).code.trim();
  const expected = `${header}\n${minified}\n`;
  const actual = await readFile(outputPath, "utf8");
  if (actual !== expected) {
    throw new Error("CSS bundle is stale. Run: rtk npm run build:web");
  }
}

function assertContains(source, needle, label) {
  if (!source.includes(needle)) {
    throw new Error(`${label} is missing expected source contract: ${needle}`);
  }
}

async function checkWriteActionContracts() {
  const sources = new Map();
  async function source(relativePath) {
    if (!sources.has(relativePath)) {
      sources.set(relativePath, await readFile(path.join(webDir, relativePath), "utf8"));
    }
    return sources.get(relativePath);
  }

  const entityActions = await source("js/src/core/entity-actions.js");
  const entityWriteActions = await source("js/src/core/entity-write-actions.js");
  const securityActions = await source("js/src/features/security-actions.js");
  const securityAccess = await source("js/src/features/security-access.js");
  const mockDevice = await source("js/mock-device.js");
  const mqttActions = await source("js/src/features/mqtt-actions.js");
  const firmwareActions = await source("js/src/features/firmware-actions.js");
  const debugRecording = await source("js/src/features/debug-recording.js");
  const oduRuntimeFrequency = await source("js/src/features/odu-runtime-frequency.js");
  const systemActions = await source("js/src/features/system-actions.js");
  const webServerLogs = await source("js/src/features/webserver-logs.js");

  assertContains(securityActions, 'fetch("/api-security/status"', "API security status");
  assertContains(securityAccess, "provisioning_pending", "API provisioning status UI");
  assertContains(mockDevice, "provisioning_closed", "API provisioning mock status");
  for (const endpoint of ["/api-security/enable", "/api-security/rotate", "/api-security/disable"]) {
    if (securityActions.includes(endpoint) || securityAccess.includes(endpoint) || mockDevice.includes(endpoint)) {
      throw new Error(`Removed API security mutation endpoint is still present: ${endpoint}`);
    }
  }
  const statusPayloadStart = mockDevice.indexOf("function getApiSecurityStatusPayload()");
  const statusPayloadEnd = mockDevice.indexOf("function handleApiSecurityStatus()", statusPayloadStart);
  const statusPayload = mockDevice.slice(statusPayloadStart, statusPayloadEnd);
  if (statusPayload.includes("key:") || statusPayload.includes("csrf_token")) {
    throw new Error("API security status mock exposes a secret or CSRF token");
  }
  if (mockDevice.includes("refreshApiSecurityToken")) {
    throw new Error("Mock still references the removed API security token lifecycle");
  }
  assertContains(mqttActions, 'fetch("/mqtt/save"', "MQTT config save");
  assertContains(mqttActions, 'fetch("/mqtt/input/save"', "MQTT input save");
  assertContains(firmwareActions, 'buildEntityPath(installButtonEntity.domain, installButtonEntity.name, "press")', "Firmware install button endpoint");
  assertContains(debugRecording, 'body.set("csrf_token", csrfToken)', "Debug recording CSRF protection");
  assertContains(debugRecording, 'const path = rolling ? "start?rolling=1" : `start?duration_s=${encodeURIComponent(minutes * 60)}`', "Debug recording start path");
  assertContains(debugRecording, "await postDebugRecordingDevice(path)", "Debug recording start");
  assertContains(debugRecording, 'postDebugRecordingDevice("stop")', "Debug recording stop");
  assertContains(debugRecording, 'getDebugRecordingEndpoint("download")', "Debug recording download");
  assertContains(systemActions, 'triggerNamedButton("restartAction"', "Restart confirm");
  assertContains(entityWriteActions, "export async function commitOpenQuattRegulationPause", "OpenQuatt pause write helper");
  assertContains(entityWriteActions, "export async function commitOpenQuattRegulationResumeNow", "OpenQuatt resume write helper");
  assertContains(oduRuntimeFrequency, "getOduRuntimeFrequencyEndpoint", "ODU runtime native endpoint");
  assertContains(oduRuntimeFrequency, 'body.set("csrf_token", status.csrfToken)', "ODU runtime CSRF write guard");
  assertContains(webServerLogs, "kan DEBUG zoveel logging produceren dat de web-app en Home Assistant traag of onbereikbaar worden.", "Debug logger safety warning");
}

async function checkStateSliceContracts() {
  const source = await readFile(path.join(jsSourceDir, "core/state-slices.js"), "utf8");
  const transformed = await transform(source, { format: "cjs", loader: "js", target: "es2020" });
  const context = { module: { exports: {} }, exports: {} };
  context.exports = context.module.exports;
  vm.runInNewContext(transformed.code, context, { filename: "state-slices.js" });
  const slices = context.module.exports;
  const groups = [
    slices.createHistoryState(24),
    slices.createDiagnosticsState("recording-id"),
    slices.createSettingsState(),
    slices.createSecurityState(),
    slices.createFirmwareState(),
    slices.createMotionState(true),
  ];
  const seenKeys = new Set();
  groups.forEach((group) => {
    Object.keys(group).forEach((key) => {
      if (seenKeys.has(key)) {
        throw new Error(`State slice key is duplicated: ${key}`);
      }
      seenKeys.add(key);
    });
  });
  if (groups[0].trendWindowHours !== 24 || groups[1].debugRecordingAcknowledgedId !== "recording-id" || groups[5].reducedMotion !== true) {
    throw new Error("State slice input values are not preserved");
  }

  const featureState = await readFile(path.join(jsSourceDir, "core/feature-state.js"), "utf8");
  assertContains(featureState, "foreignKey", "Feature-state ownership guard");
  for (const action of [
    "updateDebugRecordingState",
    "updateEnergyHistoryState",
    "updateFirmwareState",
    "updateMqttState",
    "updateWebServerLogState",
  ]) {
    assertContains(featureState, action, `${action} action`);
  }
}

async function checkMockFixtureContracts() {
  const scenarioSource = await readFile(path.join(webDir, "js/mock-scenarios.js"), "utf8");
  const incidentScenarioSource = await readFile(path.join(webDir, "js/mock-incident-scenarios.js"), "utf8");
  const fixtureSource = await readFile(path.join(webDir, "js/mock-fixtures.js"), "utf8");
  const mockSource = await readFile(path.join(webDir, "js/mock-device.js"), "utf8");
  const buildSource = await readFile(path.join(webDir, "build-assets.mjs"), "utf8");
  const configSource = await readFile(path.join(jsSourceDir, "core/config.js"), "utf8");
  const configModule = await import(`data:text/javascript;base64,${Buffer.from(configSource).toString("base64")}`);
  const devHtml = await readFile(path.join(webDir, "dev.html"), "utf8");
  const entityDefinitions = Object.values(configModule.ENTITY_DEFS).map(({ domain, name }) => [domain, name]);
  const context = { window: { __OQ_MOCK_ENTITY_DEFS__: Object.freeze(entityDefinitions) } };
  vm.runInNewContext(scenarioSource, context, { filename: "mock-scenarios.js" });
  vm.runInNewContext(incidentScenarioSource, context, { filename: "mock-incident-scenarios.js" });
  vm.runInNewContext(fixtureSource, context, { filename: "mock-fixtures.js" });
  const fixtures = context.window.__OQ_MOCK_FIXTURES__;
  const incidentScenarios = context.window.__OQ_MOCK_INCIDENT_SCENARIOS__;
  if (!entityDefinitions || entityDefinitions.length < 300) {
    throw new Error("Generated mock entity definitions are incomplete");
  }
  if (!fixtures || fixtures.hp2Entities.length < 30 || fixtures.devControlOptions.scenario.length < 10) {
    throw new Error("Mock fixtures are incomplete");
  }
  if (!incidentScenarios || incidentScenarios.scenarios.length < 10) {
    throw new Error("Heat-pump incident mock scenarios are incomplete");
  }
  assertContains(buildSource, "ENTITY_DEFS", "Canonical mock entity source");
  assertContains(mockSource, "__OQ_MOCK_ENTITY_DEFS__", "Generated mock entity definitions");
  assertContains(mockSource, 'setEntity("switch", "Usage statistics"', "Usage telemetry mock switch");
  assertContains(
    mockSource,
    'setEntity("binary_sensor", "Usage statistics choice configured", { value: false, state: false })',
    "Usage telemetry mock initial choice",
  );
  assertContains(mockSource, 'if (name === "Usage statistics")', "Usage telemetry mock switch handling");
  assertContains(
    mockSource,
    'setEntity("binary_sensor", "Usage statistics choice configured", { value: true, state: true })',
    "Usage telemetry mock persisted choice",
  );
  assertContains(mockSource, "mockFixtures.hp2Entities", "HP2 mock fixtures");
  assertContains(mockSource, 'url.pathname.endsWith("/openquatt/incidents")', "Incident snapshot mock endpoint");
  assertContains(mockSource, "collectEvents", "Incident decision-log synchronization");
  assertContains(mockSource, 'renderDevControlOptions("scenario")', "Scenario control fixtures");
  const scriptOrder = [
    "mock-scenarios.js",
    "mock-incident-scenarios.js",
    "mock-entity-defs.js",
    "mock-fixtures.js",
    "mock-device.js",
    "openquatt-preview.js",
  ].map((script) => devHtml.indexOf(script));
  if (scriptOrder.some((position) => position < 0) || scriptOrder.some((position, index) => index > 0 && position <= scriptOrder[index - 1])) {
    throw new Error("Preview scripts must load in dependency order");
  }
}

async function checkPreviewAssetsAvailable() {
  for (const relativePath of [
    "js/mock-entity-defs.js",
    "js/mock-incident-scenarios.js",
    "js/openquatt-preview.js",
    "css/openquatt-preview.css",
  ]) {
    try {
      await stat(path.join(webDir, ...relativePath.split("/")));
    } catch (error) {
      if (error.code === "ENOENT") {
        throw new Error(`Missing preview asset ${relativePath}. Run: rtk npm run build:web:preview`);
      }
      throw error;
    }
  }
}

async function checkPagesDemoContracts() {
  const devScript = await readFile(path.join(repoDir, "scripts", "dev.py"), "utf8");
  const pagesWorkflow = await readFile(path.join(repoDir, ".github", "workflows", "pages-deploy.yml"), "utf8");
  assertContains(devScript, '["npm", "run", "build:web:preview"]', "Pages preview build");
  for (const relativePath of [
    "css/openquatt-preview.css",
    "js/mock-scenarios.js",
    "js/mock-incident-scenarios.js",
    "js/mock-entity-defs.js",
    "js/mock-fixtures.js",
    "js/mock-device.js",
    "js/openquatt-preview.js",
  ]) {
    assertContains(devScript, `"${relativePath}"`, `Pages demo asset ${relativePath}`);
  }
  assertContains(pagesWorkflow, "run: npm ci", "Pages web build dependency install");
}

async function checkEmbeddedAssetContracts() {
  const logoMarkup = await readFile(path.join(webDir, "assets/openquatt-logo.svg"), "utf8");
  const configSource = await readFile(path.join(jsSourceDir, "core/config.js"), "utf8");
  const embeddedAssets = await readFile(path.join(jsSourceDir, "core/embedded-assets.js"), "utf8");
  if (!logoMarkup.includes("<svg") || !logoMarkup.includes("OpenQuatt logo")) {
    throw new Error("OpenQuatt logo asset is invalid");
  }
  if (configSource.includes("LOGO_MARKUP")) {
    throw new Error("OpenQuatt logo markup must not live in core/config.js");
  }
  assertContains(embeddedAssets, "LOGO_MARKUP", "Embedded OpenQuatt logo export");
}

async function checkBrowserSmokeMatrix() {
  const matrix = await readFile(path.join(webDir, "BROWSER_SMOKE_MATRIX.md"), "utf8");
  for (const requiredText of [
    "Overview",
    "Energy",
    "Settings",
    "Firmware modals",
    "History import/export",
    "Desktop light",
    "Desktop dark",
    "Mobile light",
    "Mobile dark",
  ]) {
    assertContains(matrix, requiredText, `Browser smoke matrix: ${requiredText}`);
  }
}

async function checkResponsiveCssOwnership() {
  const source = await readFile(path.join(webDir, "css/src/90-responsive.css"), "utf8");
  let depth = 0;
  const invalidTopLevel = [];
  source.split(/\r?\n/).forEach((line, index) => {
    const trimmed = line.trim();
    if (depth === 0 && trimmed && !trimmed.startsWith("/*") && !trimmed.startsWith("@media")) {
      invalidTopLevel.push(`${index + 1}: ${trimmed}`);
    }
    depth += (line.match(/\{/g) || []).length;
    depth -= (line.match(/\}/g) || []).length;
  });
  if (depth !== 0 || invalidTopLevel.length) {
    throw new Error(`90-responsive.css contains non-responsive top-level rules:\n${invalidTopLevel.join("\n")}`);
  }
}

async function checkProductionInterfaceCssContracts() {
  const relativeSources = cssSources.map((filePath) => toBundlePath(path.relative(webDir, filePath)));
  if (relativeSources.includes("css/src/02-devtools.css")) {
    throw new Error("Production CSS includes preview-only devtools styles");
  }
  if (!relativeSources.includes("css/src/02-interface-panel.css")) {
    throw new Error("Production CSS is missing interface panel styles");
  }

  const productionCss = (await Promise.all(cssSources.map((filePath) => readFile(filePath, "utf8")))).join("\n");
  for (const [needle, label] of [
    [".oq-helper-hub {", "interface panel"],
    [".oq-helper-hub-toggle {", "interface panel toggle"],
    [".oq-helper-status-grid {", "interface status grid"],
    ["esp-app.oq-native-app {", "ESPHome fallback surface"],
  ]) {
    assertContains(productionCss, needle, `Production CSS: ${label}`);
  }
}

async function checkSharedBrowserUtilityContracts() {
  const utilityRelativePath = "core/browser-utils.js";
  const utilitySource = await readFile(path.join(jsSourceDir, utilityRelativePath), "utf8");
  for (const [needle, label] of [
    ["export async function fetchWithTimeout", "fetch timeout helper"],
    ["export async function copyTextToClipboard", "clipboard helper"],
    ["export function downloadBlobFile", "blob download helper"],
    ["export function downloadJsonFile", "JSON download helper"],
    ['document.execCommand("copy")', "clipboard fallback"],
    ["URL.createObjectURL", "object URL download"],
  ]) {
    assertContains(utilitySource, needle, label);
  }

  const sourceFiles = await collectFiles(jsSourceDir, (filePath) => filePath.endsWith(".js"));
  const sharedOnlyPatterns = [
    { pattern: "new AbortController", label: "fetch timeout controllers" },
    { pattern: "controller.abort()", label: "fetch timeout aborts" },
    { pattern: "clipboard.writeText", label: "clipboard writes" },
    { pattern: 'document.createElement("textarea")', label: "clipboard textarea fallback" },
    { pattern: 'document.execCommand("copy")', label: "clipboard execCommand fallback" },
    { pattern: "URL.createObjectURL", label: "object URL downloads" },
    { pattern: "URL.revokeObjectURL", label: "object URL cleanup" },
  ];
  const errors = [];
  for (const filePath of sourceFiles) {
    const relativePath = toBundlePath(path.relative(jsSourceDir, filePath));
    if (relativePath === utilityRelativePath) {
      continue;
    }
    const source = await readFile(filePath, "utf8");
    for (const { pattern, label } of sharedOnlyPatterns) {
      if (source.includes(pattern)) {
        errors.push(`${relativePath} contains ${label}; use ${utilityRelativePath}`);
      }
    }
  }
  if (errors.length) {
    throw new Error(`Shared browser utility check failed:\n- ${errors.join("\n- ")}`);
  }
}

async function checkSharedCoreUtilityContracts() {
  const sourceFiles = await collectFiles(jsSourceDir, (filePath) => filePath.endsWith(".js"));
  const utilityContracts = [
    {
      relativePath: "core/html.js",
      expected: "export function escapeHtml",
      blockedExport: "export function escapeHtml",
      blockedImportPattern: /\bimport\s+\{[^}]*\bescapeHtml\b[^}]*\}\s+from\s+["'][^"']*shell\.js["']/,
      label: "HTML escaping",
    },
    {
      relativePath: "core/formatting.js",
      expected: "export function formatNumericState",
      blockedExport: "export function formatNumericState",
      blockedImportPattern: /\bimport\s+\{[^}]*\bformatNumericState\b[^}]*\}\s+from\s+["'][^"']*overview\.js["']/,
      label: "numeric state formatting",
    },
  ];
  const errors = [];
  for (const contract of utilityContracts) {
    const utilityPath = path.join(jsSourceDir, contract.relativePath);
    const utilitySource = await readFile(utilityPath, "utf8");
    assertContains(utilitySource, contract.expected, contract.label);
    for (const filePath of sourceFiles) {
      const relativePath = toBundlePath(path.relative(jsSourceDir, filePath));
      const source = await readFile(filePath, "utf8");
      if (relativePath !== contract.relativePath && source.includes(contract.blockedExport)) {
        errors.push(`${relativePath} exports ${contract.label}; use ${contract.relativePath}`);
      }
      if (contract.blockedImportPattern.test(source)) {
        errors.push(`${relativePath} imports ${contract.label} from a view module; use ${contract.relativePath}`);
      }
    }
  }
  if (errors.length) {
    throw new Error(`Shared core utility check failed:\n- ${errors.join("\n- ")}`);
  }
}

async function checkRuntimeBoundaryContracts() {
  const stateSource = await readFile(path.join(jsSourceDir, "core", "state.js"), "utf8");
  const shellSource = await readFile(path.join(jsSourceDir, "views", "shell.js"), "utf8");
  const runtimeSource = await readFile(path.join(jsSourceDir, "core", "runtime.js"), "utf8");
  assertContains(stateSource, "export const state", "shared state module");
  assertContains(shellSource, "setRenderCallback(render)", "render callback registration");
  assertContains(runtimeSource, 'from "./event-handlers.js"', "runtime event handler indirection");

  const sourceFiles = await collectFiles(jsSourceDir, (filePath) => filePath.endsWith(".js"));
  const errors = [];
  for (const filePath of sourceFiles) {
    const relativePath = toBundlePath(path.relative(jsSourceDir, filePath));
    const source = await readFile(filePath, "utf8");
    if (relativePath !== "views/shell.js" && /\bimport\s+\{[^}]*\brender\b[^}]*\}\s+from\s+["'][^"']*shell\.js["']/.test(source)) {
      errors.push(`${relativePath} imports render from views/shell.js; use core/render-scheduler.js`);
    }
    if (/\bimport\s+\{[^}]*\bstate\b[^}]*\}\s+from\s+["'][^"']*runtime\.js["']/.test(source)) {
      errors.push(`${relativePath} imports state from runtime.js; use core/state.js`);
    }
    if (relativePath === "core/runtime.js" && source.includes('from "./entity-actions.js"')) {
      errors.push("core/runtime.js imports entity-actions directly; use core/event-handlers.js");
    }
  }
  if (errors.length) {
    throw new Error(`Runtime boundary check failed:\n- ${errors.join("\n- ")}`);
  }
}

async function checkBasePathNormalization() {
  const source = await readFile(path.join(jsSourceDir, "core", "url-path.js"), "utf8");
  const sandbox = {};
  vm.runInNewContext(
    `${source.replace(/\bexport\s+/g, "")}
globalThis.normalizeBasePath = normalizeBasePath;`,
    sandbox,
    { filename: "core/url-path.js" },
  );
  const { normalizeBasePath } = sandbox;
  const cases = [
    ["/", ""],
    ["", ""],
    ["/dev.html", ""],
    ["/index.html", ""],
    ["/nested/dev.html", "/nested"],
    ["/proxy/", "/proxy"],
    ["/proxy", "/proxy"],
  ];
  const failures = cases
    .map(([input, expected]) => ({ input, expected, actual: normalizeBasePath(input) }))
    .filter((result) => result.actual !== result.expected);
  if (failures.length) {
    throw new Error(`Base path normalization failed: ${JSON.stringify(failures)}`);
  }
}

async function checkScrollKeeperContracts() {
  const source = await readFile(path.join(jsSourceDir, "core", "scroll-keeper.js"), "utf8");
  const sandbox = {};
  vm.runInNewContext(
    `${source.replace(/\bexport\s+/g, "")}
globalThis.createScrollKeeper = createScrollKeeper;`,
    sandbox,
    { filename: "core/scroll-keeper.js" },
  );
  const { createScrollKeeper } = sandbox;
  const scroller = {
    clientHeight: 100,
    dataset: { step: "one" },
    scrollHeight: 300,
    scrollTop: 100,
  };
  let active = true;
  let token = 0;
  const keeper = createScrollKeeper({
    getScroller: () => scroller,
    getToken: () => token,
    setToken: (value) => { token = value; },
    isActive: () => active,
    getIdentity: (element) => element.dataset.step,
    preserveGrowth: true,
    stickToBottom: true,
  });

  const growthState = keeper.capture();
  scroller.scrollHeight = 350;
  scroller.scrollTop = 0;
  keeper.restore(growthState);
  if (scroller.scrollTop !== 150) {
    throw new Error(`Scroll keeper growth restore failed: ${scroller.scrollTop}`);
  }

  scroller.scrollHeight = 300;
  scroller.scrollTop = 155;
  const bottomState = keeper.capture();
  scroller.scrollHeight = 400;
  scroller.scrollTop = 0;
  keeper.restore(bottomState);
  if (scroller.scrollTop !== 400) {
    throw new Error(`Scroll keeper bottom restore failed: ${scroller.scrollTop}`);
  }

  scroller.scrollHeight = 300;
  scroller.scrollTop = 80;
  const identityState = keeper.capture();
  scroller.dataset.step = "two";
  scroller.scrollTop = 12;
  keeper.restore(identityState);
  if (scroller.scrollTop !== 12) {
    throw new Error("Scroll keeper restored a different modal identity");
  }

  scroller.dataset.step = "one";
  active = false;
  keeper.queue(identityState, false);
  if (scroller.scrollTop !== 12) {
    throw new Error("Scroll keeper restored while inactive");
  }
}

async function main() {
  await stat(path.join(webDir, "dev.html"));
  await checkPreviewAssetsAvailable();
  await checkPagesDemoContracts();
  await checkSourceImports();
  await checkImportCycles();
  await checkImportBoundaries();
  await checkBasePathNormalization();
  await checkScrollKeeperContracts();
  await checkWriteActionContracts();
  await checkStateSliceContracts();
  await checkMockFixtureContracts();
  await checkEmbeddedAssetContracts();
  await checkBrowserSmokeMatrix();
  await checkResponsiveCssOwnership();
  await checkProductionInterfaceCssContracts();
  await checkSharedBrowserUtilityContracts();
  await checkSharedCoreUtilityContracts();
  await checkRuntimeBoundaryContracts();
  await checkJavaScriptBundleFresh();
  await checkCssBundleFresh();
  console.log("Web smoke ok");
}

await main();
