import { execFileSync } from "node:child_process";
import { gzipSync } from "node:zlib";
import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { WEB_BUNDLE_BUDGETS, WEB_BUNDLE_GZIP_GROWTH_LIMIT } from "./web-budgets.mjs";

const webDir = path.dirname(fileURLToPath(import.meta.url));
const sourceRoots = [path.join(webDir, "js", "src"), path.join(webDir, "css", "src")];
const mojibakePattern = /â€|Ã.|Â(?:\s|\u00a0)/u;
const bundleBaseRef = process.env.OPENQUATT_WEB_BUNDLE_BASE_REF?.trim();
const bundleBaseDir = process.env.OPENQUATT_WEB_BUNDLE_BASE_DIR?.trim();

async function collectFiles(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const files = await Promise.all(entries.map((entry) => {
    const entryPath = path.join(directory, entry.name);
    return entry.isDirectory() ? collectFiles(entryPath) : [entryPath];
  }));
  return files.flat();
}

function readBundleAtRef(ref, file) {
  const gitPath = path.posix.join("openquatt/web", file);
  try {
    return execFileSync("git", ["-C", webDir, "show", `${ref}:${gitPath}`], {
      maxBuffer: 10 * 1024 * 1024,
      stdio: ["ignore", "pipe", "pipe"],
    });
  } catch (error) {
    const detail = error.stderr?.toString().trim();
    throw new Error(`Could not read ${gitPath} at ${ref}${detail ? `: ${detail}` : ""}`);
  }
}

function formatDelta(value) {
  return `${value >= 0 ? "+" : ""}${value}`;
}

const sourceFiles = (await Promise.all(sourceRoots.map(collectFiles))).flat();
const encodingFailures = [];
for (const sourceFile of sourceFiles) {
  const source = await readFile(sourceFile, "utf8");
  if (mojibakePattern.test(source)) {
    encodingFailures.push(path.relative(webDir, sourceFile));
  }
}

const budgetFailures = [];
const bundleReports = [];
for (const budget of WEB_BUNDLE_BUDGETS) {
  const contents = await readFile(path.join(webDir, budget.file));
  const rawBytes = contents.byteLength;
  const gzipBytes = gzipSync(contents).byteLength;
  if (rawBytes > budget.raw) {
    budgetFailures.push(`${budget.file} raw: ${rawBytes} > ${budget.raw} bytes`);
  }

  if (!bundleBaseDir && !bundleBaseRef) {
    bundleReports.push(`${budget.file}: raw ${rawBytes} B, gzip ${gzipBytes} B (no base comparison)`);
    continue;
  }

  const baseContents = bundleBaseDir
    ? await readFile(path.join(bundleBaseDir, budget.file))
    : readBundleAtRef(bundleBaseRef, budget.file);
  const baseGzipBytes = gzipSync(baseContents).byteLength;
  const gzipDelta = gzipBytes - baseGzipBytes;
  const ratioLimit = Math.floor(baseGzipBytes * WEB_BUNDLE_GZIP_GROWTH_LIMIT.ratio);
  const regularAllowedIncrease = Math.min(WEB_BUNDLE_GZIP_GROWTH_LIMIT.bytes, ratioLimit);
  const migrationAllowedIncrease = Math.max(
    0,
    (budget.gzipBaselineCeiling ?? 0) - baseGzipBytes,
  );
  const allowedIncrease = Math.max(regularAllowedIncrease, migrationAllowedIncrease);
  const deltaPercent = baseGzipBytes ? (gzipDelta / baseGzipBytes) * 100 : 0;
  const formattedDeltaPercent = `${deltaPercent >= 0 ? "+" : ""}${deltaPercent.toFixed(2)}`;
  const migrationLimit = migrationAllowedIncrease > regularAllowedIncrease
    ? `, migration ceiling ${budget.gzipBaselineCeiling} B`
    : "";
  bundleReports.push(
    `${budget.file}: raw ${rawBytes} B, gzip ${gzipBytes} B, `
    + `base ${baseGzipBytes} B, delta ${formatDelta(gzipDelta)} B (${formattedDeltaPercent}%), `
    + `limit +${allowedIncrease} B${migrationLimit}`,
  );
  if (gzipDelta > allowedIncrease) {
    budgetFailures.push(
      `${budget.file} gzip growth: ${formatDelta(gzipDelta)} > +${allowedIncrease} bytes `
      + `(base ${baseGzipBytes}, current ${gzipBytes})`,
    );
  }
}

const baseLabel = bundleBaseRef || bundleBaseDir;
console.log(`Web bundle sizes${baseLabel ? ` compared with ${baseLabel}` : ""}:`);
bundleReports.forEach((report) => console.log(`- ${report}`));

if (encodingFailures.length || budgetFailures.length) {
  const failures = [
    ...encodingFailures.map((file) => `${file}: vermoedelijke mojibake`),
    ...budgetFailures,
  ];
  throw new Error(`Web quality check failed:\n- ${failures.join("\n- ")}`);
}

console.log(`Web quality ok: ${sourceFiles.length} source files and ${WEB_BUNDLE_BUDGETS.length} bundle budgets`);
