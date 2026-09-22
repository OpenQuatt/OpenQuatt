#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { APP_VIEWS, QUICK_STEPS, SETTINGS_GROUPS } from "../openquatt/web/js/src/core/config.js";
import nl from "../openquatt/web/js/src/i18n/nl.js";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, "..");
const failures = [];

function read(relativePath) {
  return fs.readFileSync(path.join(REPO_ROOT, relativePath), "utf8");
}

function section(markdown, heading) {
  const lines = markdown.split(/\r?\n/);
  const marker = `## ${heading}`;
  const start = lines.findIndex((line) => line.trim() === marker);
  if (start < 0) {
    return [];
  }
  const endOffset = lines.slice(start + 1).findIndex((line) => /^##\s+/.test(line));
  const end = endOffset < 0 ? lines.length : start + 1 + endOffset;
  return lines.slice(start + 1, end);
}

function backtickTableLabels(lines) {
  return lines.flatMap((line) => {
    const match = line.match(/^\|\s*`([^`]+)`\s*\|/);
    return match ? [match[1]] : [];
  });
}

function thirdLevelHeadings(lines) {
  return lines.flatMap((line) => {
    const match = line.match(/^###\s+(.+?)\s*$/);
    return match ? [match[1]] : [];
  });
}

function numberedBoldLabels(lines) {
  return lines.flatMap((line) => {
    const match = line.match(/^\d+\.\s+\*\*([^*:]+):\*\*/);
    return match ? [match[1]] : [];
  });
}

function assertEqual(relativePath, contract, actual, expected) {
  if (JSON.stringify(actual) === JSON.stringify(expected)) {
    return;
  }
  failures.push({
    file: relativePath,
    message: `${contract} loopt niet gelijk met de web-appbron. Verwacht ${JSON.stringify(expected)}, gevonden ${JSON.stringify(actual)}.`,
  });
}

const webAppPath = "docs/web-app.md";
const qEditionPath = "docs/q-edition.md";
const webApp = read(webAppPath);
const qEdition = read(qEditionPath);

function resolveNl(key) {
  return String(key || "").split(".").reduce((node, part) => (node && typeof node === "object" ? node[part] : undefined), nl) || "";
}

const quickStepLabels = QUICK_STEPS.map((step) => resolveNl(step.titleKey));
const viewLabels = APP_VIEWS.map((view) => resolveNl(view.labelKey));
const settingsGroupLabels = SETTINGS_GROUPS.map((group) => resolveNl(group.labelKey));

assertEqual(
  webAppPath,
  "De Quick Start-tabel",
  backtickTableLabels(section(webApp, "Eerste keer: Quick Start")),
  quickStepLabels,
);
assertEqual(
  qEditionPath,
  "De Quick Start-stappen",
  numberedBoldLabels(section(qEdition, "4. Quick Start afronden")),
  quickStepLabels,
);
assertEqual(
  webAppPath,
  "De hoofdschermen-tabel",
  backtickTableLabels(section(webApp, "Hoofdschermen")),
  viewLabels,
);
assertEqual(
  webAppPath,
  "De instellingengroepen",
  thirdLevelHeadings(section(webApp, "Instellingen")),
  settingsGroupLabels,
);

const firefoxPolicyFiles = [
  "docs/install/index.html",
  "docs/install/install.js",
];
for (const relativePath of firefoxPolicyFiles) {
  if (/\bFirefox\b/i.test(read(relativePath))) {
    failures.push({
      file: relativePath,
      message: "Firefox wordt genoemd als Web Serial-installatiebrowser; documenteer alleen Chrome of Edge.",
    });
  }
}

if (failures.length > 0) {
  console.error(`Web/docs-contractcontrole vond ${failures.length} probleem/problemen:`);
  for (const failure of failures) {
    console.error(`::error file=${failure.file},line=1::${failure.message}`);
    console.error(`- ${failure.file}: ${failure.message}`);
  }
  process.exitCode = 1;
} else {
  console.log("Web/docs-contractcontrole geslaagd.");
}
