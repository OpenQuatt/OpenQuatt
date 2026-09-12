import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const toolsDir = path.dirname(fileURLToPath(import.meta.url));
const brandDir = path.resolve(toolsDir, "..");
const repoDir = path.resolve(brandDir, "..");
const manifestPath = path.join(brandDir, "asset-manifest.json");
const failures = [];

function fail(message) {
  failures.push(message);
}

function resolveBrandPath(relativePath) {
  if (typeof relativePath !== "string" || !relativePath || path.isAbsolute(relativePath)) return null;
  const absolutePath = path.resolve(brandDir, relativePath);
  const relativeToBrand = path.relative(brandDir, absolutePath);
  if (relativeToBrand === "" || relativeToBrand === ".." || relativeToBrand.startsWith(`..${path.sep}`)) {
    return null;
  }
  return absolutePath;
}

function walk(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (entry.name === "node_modules" || entry.name === "dist") return [];
    const absolute = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(absolute) : [absolute];
  });
}

function pngDimensions(buffer) {
  const signature = buffer.subarray(0, 8).toString("hex");
  if (signature !== "89504e470d0a1a0a") return null;
  return { width: buffer.readUInt32BE(16), height: buffer.readUInt32BE(20) };
}

function pngChunkTypes(buffer) {
  const chunks = [];
  let offset = 8;
  while (offset + 12 <= buffer.length) {
    const length = buffer.readUInt32BE(offset);
    chunks.push(buffer.toString("ascii", offset + 4, offset + 8));
    offset += 12 + length;
  }
  return chunks;
}

if (!fs.existsSync(manifestPath)) {
  fail("asset-manifest.json ontbreekt");
} else {
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  const seenPaths = new Set();

  for (const asset of manifest.assets ?? []) {
    for (const field of ["logicalName", "path", "type", "variant", "intendedBackground", "source"]) {
      if (!asset[field]) fail(`Manifestveld ${field} ontbreekt voor ${asset.path ?? asset.logicalName ?? "onbekend asset"}`);
    }
    if (seenPaths.has(asset.path)) fail(`Dubbel manifestpad: ${asset.path}`);
    seenPaths.add(asset.path);
    const absolute = resolveBrandPath(asset.path);
    if (!absolute) {
      fail(`Ongeldig of extern manifestpad: ${String(asset.path)}`);
      continue;
    }
    if (!fs.existsSync(absolute)) {
      fail(`Vereist bestand ontbreekt: ${asset.path}`);
      continue;
    }

    if (asset.type === "png") {
      const png = fs.readFileSync(absolute);
      const dimensions = pngDimensions(png);
      if (!dimensions) {
        fail(`Ongeldige PNG: ${asset.path}`);
      } else if (
        dimensions.width !== asset.dimensions?.width ||
        dimensions.height !== asset.dimensions?.height
      ) {
        fail(
          `PNG-afmeting wijkt af voor ${asset.path}: ${dimensions.width}x${dimensions.height}`,
        );
      }
      if (!pngChunkTypes(png).includes("sRGB")) fail(`PNG mist expliciet sRGB-profiel: ${asset.path}`);
    } else if (asset.type === "svg") {
      const source = fs.readFileSync(absolute, "utf8");
      const actualViewBox = source.match(/\bviewBox\s*=\s*["']([^"']+)["']/i)?.[1];
      if (actualViewBox !== asset.viewBox) {
        fail(`SVG-viewBox wijkt af voor ${asset.path}: ${actualViewBox ?? "ontbreekt"}`);
      }
    }
  }

  const required = [
    "logos/svg/openquatt-logo-horizontal-dark.svg",
    "logos/svg/openquatt-logo-horizontal-light.svg",
    "logos/svg/openquatt-logo-horizontal-mono-dark.svg",
    "logos/svg/openquatt-logo-horizontal-mono-light.svg",
    "logos/svg/openquatt-logo-compact-dark.svg",
    "logos/svg/openquatt-logo-compact-light.svg",
    "logos/svg/openquatt-logo-stacked-dark.svg",
    "logos/svg/openquatt-logo-stacked-light.svg",
    "logos/svg/openquatt-wordmark-dark.svg",
    "logos/svg/openquatt-wordmark-light.svg",
    "logos/svg/openquatt-symbol.svg",
    "logos/svg/openquatt-symbol-dark.svg",
    "logos/svg/openquatt-symbol-light.svg",
    "logos/svg/openquatt-symbol-mono.svg",
    "logos/svg/openquatt-symbol-micro.svg",
    "logos/svg/openquatt-symbol-micro-dark.svg",
    "logos/svg/openquatt-symbol-micro-light.svg",
    "icons/favicon/favicon.svg",
    "icons/favicon/favicon-16x16.png",
    "icons/favicon/favicon-32x32.png",
    "icons/pwa/apple-touch-icon.png",
    "icons/pwa/android-chrome-192x192.png",
    "icons/pwa/android-chrome-512x512.png",
    "icons/pwa/maskable-icon-512x512.png",
    "icons/social/github-avatar-512.png",
    "icons/social/social-avatar-1024.png",
    "social/openquatt-social-card-1280x640.svg",
    "social/openquatt-social-card-1280x640.png",
    "hardware/openquatt-hardware-lockup-dark.svg",
    "hardware/openquatt-hardware-lockup-mono-dark.svg",
    "hardware/openquatt-hardware-lockup-mono-light.svg",
    "hardware/openquatt-symbol-engraving.svg",
    "tokens/brand-tokens.css",
    "tokens/brand-tokens.json",
    "source/open-q-construction.svg",
    "source/wordmark-editable.svg",
    "README.md",
    "BRAND-GUIDELINES.md",
    "BRAND-ASSET-LICENSE.md",
    "fonts/README.md",
  ];
  for (const requiredPath of required) {
    if (!seenPaths.has(requiredPath)) fail(`Manifest mist vereist bestand: ${requiredPath}`);
  }

  for (const [name, value] of Object.entries(manifest.palette ?? {})) {
    if (!/^#[0-9A-F]{6}$/.test(value)) fail(`Ongeldige palettewaarde ${name}: ${value}`);
  }
}

for (const absolute of walk(brandDir)) {
  const relative = path.relative(brandDir, absolute);
  if (/\.(ttf|otf|woff2?)$/i.test(relative)) {
    fail(`Fontbinary buiten development dependencies: ${relative}`);
  }
  if (!relative.endsWith(".svg")) continue;

  const source = fs.readFileSync(absolute, "utf8");
  if (!/<svg\b/i.test(source) || !/\bviewBox\s*=/.test(source)) fail(`SVG zonder viewBox: ${relative}`);
  if (/<image\b/i.test(source)) fail(`SVG bevat <image>: ${relative}`);
  if (/<foreignObject\b/i.test(source)) fail(`SVG bevat <foreignObject>: ${relative}`);
  if (/\b(?:href|src)\s*=\s*["'](?:https?:|data:|\/\/)/i.test(source)) fail(`SVG bevat externe of ingebedde resource: ${relative}`);

  if (relative.startsWith("logos/svg/")) {
    if (/<text\b/i.test(source)) fail(`Release-logo bevat live tekst: ${relative}`);
    if (/<filter\b/i.test(source)) fail(`Release-logo bevat filter: ${relative}`);
    if (/font-family/i.test(source)) fail(`Release-logo heeft fontafhankelijkheid: ${relative}`);
  }

  if (relative !== "source/wordmark-editable.svg" && /<text\b/i.test(source)) {
    fail(`Alleen de editable wordmarkbron mag live tekst bevatten: ${relative}`);
  }
}

const previewPath = path.join(brandDir, "preview/index.html");
if (!fs.existsSync(previewPath)) {
  fail("Previewpagina ontbreekt: preview/index.html");
} else {
  const previewHtml = fs.readFileSync(previewPath, "utf8");
  for (const match of previewHtml.matchAll(/\b(?:src|href)=["']([^"']+)["']/g)) {
    const reference = match[1];
    if (reference.startsWith("#") || /^[a-z]+:/i.test(reference)) continue;
    const resolved = path.resolve(path.dirname(previewPath), reference.split("#", 1)[0]);
    const relativeToRepo = path.relative(repoDir, resolved);
    if (relativeToRepo === ".." || relativeToRepo.startsWith(`..${path.sep}`)) {
      fail(`Previewverwijzing valt buiten de repository: ${reference}`);
      continue;
    }
    if (!fs.existsSync(resolved)) fail(`Previewverwijzing bestaat niet: ${reference}`);
  }
}

const tokenCss = fs.existsSync(path.join(brandDir, "tokens/brand-tokens.css"))
  ? fs.readFileSync(path.join(brandDir, "tokens/brand-tokens.css"), "utf8")
  : "";
for (const color of ["#EA580C", "#F97316", "#C2410C", "#0F1724", "#162131", "#F8FAFC", "#111827", "#475569", "#F6F2EA"]) {
  if (!tokenCss.includes(color)) fail(`CSS-tokens missen ${color}`);
}

const tokenJsonPath = path.join(brandDir, "tokens/brand-tokens.json");
const tokenJsonText = fs.existsSync(tokenJsonPath) ? fs.readFileSync(tokenJsonPath, "utf8") : "";
for (const color of ["#EA580C", "#F97316", "#C2410C", "#0F1724", "#162131", "#F8FAFC", "#111827", "#475569", "#F6F2EA"]) {
  if (!tokenJsonText.includes(color)) fail(`JSON-tokens missen ${color}`);
}

if (failures.length) {
  console.error(`Brandvalidatie mislukt (${failures.length}):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log("Brandvalidatie geslaagd: manifest, SVG-veiligheid, outlines, tokens en afmetingen kloppen.");
