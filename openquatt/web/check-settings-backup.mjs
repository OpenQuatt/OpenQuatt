import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const DEFAULT_CONFIG_PATH = path.join(__dirname, "js", "src", "core", "config.js");

function formatList(values) {
  return values.length ? values.join(", ") : "none";
}

function sorted(values) {
  return [...values].sort((a, b) => a.localeCompare(b));
}

function collectDuplicateBackupKeys(sections) {
  const seen = new Map();
  const duplicates = new Map();

  sections.forEach((section) => {
    section.keys.forEach((key) => {
      const locations = seen.get(key) || [];
      locations.push(section.id);
      seen.set(key, locations);
      if (locations.length > 1) {
        duplicates.set(key, locations);
      }
    });
  });

  return sorted([...duplicates.entries()].map(([key, locations]) => `${key} (${locations.join(", ")})`));
}

function validateSectionShape(section) {
  const hasLabel = (typeof section.label === "string" && section.label.trim())
    || (typeof section.labelKey === "string" && section.labelKey.trim());
  return section
    && typeof section.id === "string"
    && section.id.trim()
    && Boolean(hasLabel)
    && Array.isArray(section.keys);
}

async function loadConfig(configPath) {
  const source = await readFile(configPath, "utf8");
  const scriptSource = source
    .replace(/^import\s+[\s\S]*?;\n/gm, "")
    .replace(/\bexport\s+(?=(?:async\s+)?function|const\s)/g, "");
  const sandbox = {};
  vm.runInNewContext(
    `${scriptSource}

globalThis.__settingsBackupCheckConfig = {
  ENTITY_DEFS,
  SETTINGS_KEYS,
  SETTINGS_BACKUP_SECTIONS,
  SETTINGS_BACKUP_KEYS,
  SETTINGS_BACKUP_WRITABLE_DOMAINS: [...SETTINGS_BACKUP_WRITABLE_DOMAINS],
  SETTINGS_BACKUP_EXPECTED_EXTRA_KEYS: [...SETTINGS_BACKUP_EXPECTED_EXTRA_KEYS],
  SETTINGS_BACKUP_EXCLUDED_KEYS: [...SETTINGS_BACKUP_EXCLUDED_KEYS],
};`,
    sandbox,
    { filename: configPath },
  );
  return sandbox.__settingsBackupCheckConfig;
}

export async function checkSettingsBackupConfig(configPath = DEFAULT_CONFIG_PATH) {
  const config = await loadConfig(configPath);
  const {
    ENTITY_DEFS,
    SETTINGS_KEYS,
    SETTINGS_BACKUP_SECTIONS,
    SETTINGS_BACKUP_KEYS,
    SETTINGS_BACKUP_WRITABLE_DOMAINS,
    SETTINGS_BACKUP_EXPECTED_EXTRA_KEYS,
    SETTINGS_BACKUP_EXCLUDED_KEYS,
  } = config;

  const errors = [];
  const entityDefs = ENTITY_DEFS || {};
  const settingsKeys = SETTINGS_KEYS || [];
  const backupSections = SETTINGS_BACKUP_SECTIONS || [];
  const backupKeys = SETTINGS_BACKUP_KEYS || [];
  const writableDomains = new Set(SETTINGS_BACKUP_WRITABLE_DOMAINS || []);
  const expectedExtraKeys = new Set(SETTINGS_BACKUP_EXPECTED_EXTRA_KEYS || []);
  const excludedKeys = new Set(SETTINGS_BACKUP_EXCLUDED_KEYS || []);
  const backupKeySet = new Set(backupKeys);

  const invalidSections = backupSections
    .filter((section) => !validateSectionShape(section))
    .map((section) => String(section?.id || section?.label || section?.labelKey || "<unknown>"));
  if (invalidSections.length) {
    errors.push(`invalid backup sections: ${formatList(invalidSections)}`);
  }

  const sectionIds = backupSections.map((section) => section.id);
  const duplicateSectionIds = sorted(sectionIds.filter((id, index) => sectionIds.indexOf(id) !== index));
  if (duplicateSectionIds.length) {
    errors.push(`duplicate backup section ids: ${formatList(duplicateSectionIds)}`);
  }

  const duplicateBackupKeys = collectDuplicateBackupKeys(backupSections.filter(validateSectionShape));
  if (duplicateBackupKeys.length) {
    errors.push(`duplicate backup keys: ${formatList(duplicateBackupKeys)}`);
  }

  const flattenedBackupKeys = backupSections.flatMap((section) => section.keys || []);
  const derivedBackupKeys = [...new Set(flattenedBackupKeys)];
  if (derivedBackupKeys.length !== backupKeys.length || derivedBackupKeys.some((key) => !backupKeySet.has(key))) {
    errors.push("SETTINGS_BACKUP_KEYS does not match SETTINGS_BACKUP_SECTIONS");
  }

  const settingsKeysWithoutEntity = sorted(settingsKeys.filter((key) => !entityDefs[key]));
  if (settingsKeysWithoutEntity.length) {
    errors.push(`settings keys without entity definition: ${formatList(settingsKeysWithoutEntity)}`);
  }

  const backupKeysWithoutEntity = sorted(backupKeys.filter((key) => !entityDefs[key]));
  if (backupKeysWithoutEntity.length) {
    errors.push(`backup keys without entity definition: ${formatList(backupKeysWithoutEntity)}`);
  }

  const expectedBackupKeys = new Set();
  settingsKeys.forEach((key) => {
    const entity = entityDefs[key];
    if (!entity || excludedKeys.has(key)) {
      return;
    }
    if (writableDomains.has(entity.domain)) {
      expectedBackupKeys.add(key);
    }
  });
  expectedExtraKeys.forEach((key) => expectedBackupKeys.add(key));

  const excludedKeysInBackup = sorted(backupKeys.filter((key) => excludedKeys.has(key)));
  if (excludedKeysInBackup.length) {
    errors.push(`excluded keys are still in backup: ${formatList(excludedKeysInBackup)}`);
  }

  const missingExpectedKeys = sorted([...expectedBackupKeys].filter((key) => !backupKeySet.has(key)));
  if (missingExpectedKeys.length) {
    errors.push(`backup is missing expected settings: ${formatList(missingExpectedKeys)}`);
  }

  const unexpectedBackupKeys = sorted(backupKeys.filter((key) => !expectedBackupKeys.has(key)));
  if (unexpectedBackupKeys.length) {
    errors.push(`backup contains unexpected keys: ${formatList(unexpectedBackupKeys)}`);
  }

  const nonWritableBackupKeys = sorted(backupKeys.filter((key) => {
    const entity = entityDefs[key];
    return entity && !writableDomains.has(entity.domain) && !expectedExtraKeys.has(key);
  }));
  if (nonWritableBackupKeys.length) {
    errors.push(`backup contains non-writable keys: ${formatList(nonWritableBackupKeys)}`);
  }

  if (errors.length) {
    throw new Error(`Settings backup config is out of sync:\n- ${errors.join("\n- ")}`);
  }

  console.log(`Settings backup config ok: ${backupKeys.length} keys across ${backupSections.length} sections`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
  await checkSettingsBackupConfig();
}
