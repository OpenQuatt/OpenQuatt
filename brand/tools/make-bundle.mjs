import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import yazl from "yazl";

const toolsDir = path.dirname(fileURLToPath(import.meta.url));
const brandDir = path.resolve(toolsDir, "..");
const distDir = path.join(brandDir, "dist");
const outputPath = path.join(distDir, "openquatt-brand-kit-v1.zip");
const manifest = JSON.parse(fs.readFileSync(path.join(brandDir, "asset-manifest.json"), "utf8"));
const fixedTime = new Date("2000-01-01T00:00:00.000Z");
const prefix = "openquatt-brand-kit-v1";

function resolveBrandPath(relativePath) {
  if (typeof relativePath !== "string" || !relativePath || path.isAbsolute(relativePath)) {
    throw new Error(`Ongeldig bundelpad: ${String(relativePath)}`);
  }
  const absolutePath = path.resolve(brandDir, relativePath);
  const relativeToBrand = path.relative(brandDir, absolutePath);
  if (relativeToBrand === "" || relativeToBrand === ".." || relativeToBrand.startsWith(`..${path.sep}`)) {
    throw new Error(`Bundelpad valt buiten brand/: ${relativePath}`);
  }
  return absolutePath;
}

fs.mkdirSync(distDir, { recursive: true });

const entries = ["asset-manifest.json", ...manifest.assets.filter((asset) => asset.bundle).map((asset) => asset.path)]
  .filter((entry, index, all) => all.indexOf(entry) === index)
  .sort();

const zip = new yazl.ZipFile();
for (const relativePath of entries) {
  const absolutePath = resolveBrandPath(relativePath);
  if (!fs.existsSync(absolutePath)) throw new Error(`Bundelbestand ontbreekt: ${relativePath}`);
  zip.addFile(absolutePath, `${prefix}/${relativePath}`, {
    mtime: fixedTime,
    mode: 0o100644,
    compress: true,
  });
}

await new Promise((resolve, reject) => {
  const output = fs.createWriteStream(outputPath);
  output.on("close", resolve);
  output.on("error", reject);
  zip.outputStream.on("error", reject).pipe(output);
  zip.end({ forceZip64Format: false });
});

console.log(`Bundel gemaakt: ${path.relative(brandDir, outputPath)} (${entries.length} bestanden)`);
