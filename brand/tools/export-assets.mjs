import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as fontkit from "fontkit";
import { Resvg } from "@resvg/resvg-js";

const toolsDir = path.dirname(fileURLToPath(import.meta.url));
const brandDir = path.resolve(toolsDir, "..");

const palette = Object.freeze({
  brandOrange: "#EA580C",
  brightOrange: "#F97316",
  deepOrange: "#C2410C",
  charcoalNavy: "#0F1724",
  surfaceNavy: "#162131",
  offWhite: "#F8FAFC",
  ink: "#111827",
  blueGray: "#475569",
  warmLight: "#F6F2EA",
});

const font600 = fontkit.openSync(
  path.join(toolsDir, "node_modules/@fontsource/inter/files/inter-latin-600-normal.woff"),
);
const font700 = fontkit.openSync(
  path.join(toolsDir, "node_modules/@fontsource/inter/files/inter-latin-700-normal.woff"),
);

const assets = [];

function ensureParent(relativePath) {
  fs.mkdirSync(path.dirname(path.join(brandDir, relativePath)), { recursive: true });
}

function write(relativePath, content) {
  ensureParent(relativePath);
  fs.writeFileSync(path.join(brandDir, relativePath), content);
}

function record(asset) {
  assets.push({ bundle: true, ...asset });
}

function svgDocument(viewBox, body, { title, width, height } = {}) {
  const size = width && height ? ` width="${width}" height="${height}"` : "";
  const titleNode = title ? `\n  <title>${title}</title>` : "";
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}"${size}>${titleNode}\n${body}\n</svg>\n`;
}

function round(value) {
  return Number(value.toFixed(3));
}

const crcTable = Array.from({ length: 256 }, (_, value) => {
  let current = value;
  for (let bit = 0; bit < 8; bit += 1) {
    current = current & 1 ? 0xedb88320 ^ (current >>> 1) : current >>> 1;
  }
  return current >>> 0;
});

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBuffer = Buffer.from(type, "ascii");
  const payload = Buffer.from(data);
  const chunk = Buffer.alloc(12 + payload.length);
  chunk.writeUInt32BE(payload.length, 0);
  typeBuffer.copy(chunk, 4);
  payload.copy(chunk, 8);
  chunk.writeUInt32BE(crc32(Buffer.concat([typeBuffer, payload])), 8 + payload.length);
  return chunk;
}

function addSrgbChunk(pngBytes) {
  const png = Buffer.from(pngBytes);
  const ihdrEnd = 8 + 12 + png.readUInt32BE(8);
  return Buffer.concat([png.subarray(0, ihdrEnd), pngChunk("sRGB", [0]), png.subarray(ihdrEnd)]);
}

function ringPath({ cx, cy, rx, ry, gapStart, gapEnd }) {
  const point = (angle) => {
    const radians = (angle * Math.PI) / 180;
    return [round(cx + rx * Math.cos(radians)), round(cy + ry * Math.sin(radians))];
  };
  const start = point(gapStart);
  const end = point(gapEnd);
  return `M${start[0]} ${start[1]} A${rx} ${ry} 0 1 1 ${end[0]} ${end[1]}`;
}

const standardGeometry = Object.freeze({
  cx: 44.5,
  cy: 43.5,
  rx: 30.5,
  ry: 30.5,
  gapStart: 73,
  gapEnd: 17,
  stroke: 14.5,
  tail: [53, 52.5, 83.5, 83],
});

const microGeometry = Object.freeze({
  cx: 44,
  cy: 43.5,
  rx: 29.5,
  ry: 29.5,
  gapStart: 78,
  gapEnd: 12,
  stroke: 16,
  tail: [54, 53.5, 80, 79.5],
});

function symbolMarkup({
  geometry = standardGeometry,
  ring = palette.charcoalNavy,
  tail = palette.brandOrange,
  transform = "",
  className = "",
} = {}) {
  const [x1, y1, x2, y2] = geometry.tail;
  const attrs = [transform && `transform="${transform}"`, className && `class="${className}"`]
    .filter(Boolean)
    .join(" ");
  return `  <g${attrs ? ` ${attrs}` : ""}>
    <path d="${ringPath(geometry)}" fill="none" stroke="${ring}" stroke-width="${geometry.stroke}" stroke-linecap="round"/>
    <path d="M${x1} ${y1} L${x2} ${y2}" fill="none" stroke="${tail}" stroke-width="${geometry.stroke}" stroke-linecap="round"/>
  </g>`;
}

function textPaths({ text, font, size, x = 0, baseline = 0, fill, tracking = 0 }) {
  const run = font.layout(text);
  const scale = size / font.unitsPerEm;
  let cursor = 0;
  const paths = [];

  run.glyphs.forEach((glyph, index) => {
    const position = run.positions[index];
    const glyphX = x + cursor + position.xOffset * scale;
    const glyphY = baseline - position.yOffset * scale;
    paths.push(
      `    <path d="${glyph.path.toSVG()}" fill="${fill}" transform="matrix(${round(scale)} 0 0 ${round(-scale)} ${round(glyphX)} ${round(glyphY)})"/>`,
    );
    cursor += position.xAdvance * scale;
    if (index < run.glyphs.length - 1) cursor += tracking;
  });

  return { markup: paths.join("\n"), width: cursor };
}

function wordmarkMarkup({
  x = 0,
  baseline,
  size,
  neutral,
  accent,
  mono = false,
  openTracking = -1.8,
  quattTracking = -2.1,
  nQAdjustment = -3.4,
}) {
  const open = textPaths({
    text: "Open",
    font: font600,
    size,
    x,
    baseline,
    fill: neutral,
    tracking: openTracking * (size / 50),
  });
  const quattX = x + open.width + nQAdjustment * (size / 50);
  const quatt = textPaths({
    text: "Quatt",
    font: font700,
    size,
    x: quattX,
    baseline,
    fill: mono ? neutral : accent,
    tracking: quattTracking * (size / 50),
  });
  return { markup: `${open.markup}\n${quatt.markup}`, width: quattX + quatt.width - x };
}

function lineMarkup({ text, size, x, baseline, fill, tracking = 0, font = font600 }) {
  return textPaths({ text, font, size, x, baseline, fill, tracking });
}

function writeSvg(relativePath, viewBox, body, meta) {
  write(relativePath, svgDocument(viewBox, body, meta));
  record({
    logicalName: path.basename(relativePath, ".svg"),
    path: relativePath,
    type: "svg",
    variant: meta.variant,
    intendedBackground: meta.intendedBackground,
    viewBox,
    source: meta.source,
  });
}

function writePng(relativePath, sourceSvg, width, height, meta) {
  const renderer = new Resvg(sourceSvg, {
    fitTo: { mode: "width", value: width },
  });
  const png = addSrgbChunk(renderer.render().asPng());
  write(relativePath, png);
  record({
    logicalName: path.basename(relativePath, ".png"),
    path: relativePath,
    type: "png",
    variant: meta.variant,
    intendedBackground: meta.intendedBackground,
    dimensions: { width, height },
    source: meta.source,
  });
}

function makeSymbolSvg({ geometry = standardGeometry, ring, tail, title }) {
  return svgDocument("0 0 100 100", symbolMarkup({ geometry, ring, tail }), { title });
}

function makeHorizontal({ neutral, accent, mono = false, compact = false, title }) {
  if (compact) {
    const wordmark = wordmarkMarkup({
      x: 61,
      baseline: 46,
      size: 39,
      neutral,
      accent,
      mono,
      nQAdjustment: -3.7,
    });
    const width = Math.ceil(69 + wordmark.width + 3);
    return {
      viewBox: `0 0 ${width} 60`,
      svg: svgDocument(
        `0 0 ${width} 60`,
        `${symbolMarkup({ ring: neutral, tail: mono ? neutral : accent, transform: "translate(1 1) scale(.58)" })}\n${wordmark.markup}`,
        { title },
      ),
    };
  }

  const wordmark = wordmarkMarkup({
    x: 99,
    baseline: 77,
    size: 64,
    neutral,
    accent,
    mono,
    nQAdjustment: -3.8,
  });
  const width = Math.ceil(116 + wordmark.width + 5);
  return {
    viewBox: `0 0 ${width} 100`,
    svg: svgDocument(
      `0 0 ${width} 100`,
      `${symbolMarkup({ ring: neutral, tail: mono ? neutral : accent, transform: "translate(2 5) scale(.9)" })}\n${wordmark.markup}`,
      { title },
    ),
  };
}

function makeWordmark({ neutral, accent, title }) {
  const wordmark = wordmarkMarkup({
    x: 4,
    baseline: 54,
    size: 64,
    neutral,
    accent,
    nQAdjustment: -3.7,
  });
  const width = Math.ceil(wordmark.width + 8);
  return {
    viewBox: `0 0 ${width} 72`,
    svg: svgDocument(`0 0 ${width} 72`, wordmark.markup, { title }),
  };
}

function makeStacked({ neutral, accent, title }) {
  const wordmark = wordmarkMarkup({
    x: 0,
    baseline: 151,
    size: 46,
    neutral,
    accent,
    nQAdjustment: -3.7,
  });
  const width = Math.ceil(wordmark.width + 16);
  const wordmarkX = (width - wordmark.width) / 2;
  const centered = wordmarkMarkup({
    x: wordmarkX,
    baseline: 151,
    size: 46,
    neutral,
    accent,
    nQAdjustment: -3.7,
  });
  const symbolX = (width - 92) / 2;
  return {
    viewBox: `0 0 ${width} 165`,
    svg: svgDocument(
      `0 0 ${width} 165`,
      `${symbolMarkup({ ring: neutral, tail: accent, transform: `translate(${round(symbolX)} 0) scale(.92)` })}\n${centered.markup}`,
      { title },
    ),
  };
}

function registerSvg(relativePath, generated, meta) {
  write(relativePath, generated.svg);
  record({
    logicalName: path.basename(relativePath, ".svg"),
    path: relativePath,
    type: "svg",
    variant: meta.variant,
    intendedBackground: meta.intendedBackground,
    viewBox: generated.viewBox,
    source: meta.source,
  });
}

function tileSvg({ size, geometry = standardGeometry, padding = 0.19, radius = 0, title }) {
  const symbolSize = size * (1 - 2 * padding);
  const offset = (size - symbolSize) / 2;
  return svgDocument(
    `0 0 ${size} ${size}`,
    `  <rect width="${size}" height="${size}" rx="${radius}" fill="${palette.charcoalNavy}"/>\n${symbolMarkup({
      geometry,
      ring: palette.offWhite,
      tail: palette.brightOrange,
      transform: `translate(${round(offset)} ${round(offset)}) scale(${round(symbolSize / 100)})`,
    })}`,
    { title, width: size, height: size },
  );
}

function makeSocialCard() {
  const wordmark = wordmarkMarkup({
    x: 370,
    baseline: 298,
    size: 92,
    neutral: palette.offWhite,
    accent: palette.brightOrange,
    nQAdjustment: -2.7,
  });
  const descriptor = lineMarkup({
    text: "OPEN SOURCE HEAT-PUMP CONTROL",
    size: 25,
    x: 374,
    baseline: 365,
    fill: "#CBD5E1",
    tracking: 4,
  });
  return svgDocument(
    "0 0 1280 640",
    `  <rect width="1280" height="640" fill="${palette.charcoalNavy}"/>
  <path d="M0 96H1280M0 544H1280M96 0V640M1184 0V640" fill="none" stroke="#233247" stroke-width="1" opacity=".7"/>
  <path d="M96 96H238M1042 544H1184" fill="none" stroke="${palette.brightOrange}" stroke-width="6" stroke-linecap="round"/>
${symbolMarkup({ ring: palette.offWhite, tail: palette.brightOrange, transform: "translate(96 176) scale(2.18)" })}
${wordmark.markup}
${descriptor.markup}`,
    { title: "OpenQuatt social card", width: 1280, height: 640 },
  );
}

function makeHardwareLockup({ mono = false, black = false }) {
  const neutral = black ? palette.ink : palette.offWhite;
  const accent = mono ? neutral : palette.brightOrange;
  const wordmark = wordmarkMarkup({
    x: 108,
    baseline: 78,
    size: 62,
    neutral,
    accent,
    mono,
    nQAdjustment: -3.8,
  });
  const width = Math.ceil(108 + wordmark.width + 12);
  return {
    viewBox: `0 0 ${width} 108`,
    svg: svgDocument(
      `0 0 ${width} 108`,
      `${symbolMarkup({ ring: neutral, tail: accent, transform: "translate(4 8) scale(.9)" })}\n${wordmark.markup}`,
      { title: `OpenQuatt hardware lockup${mono ? " monochrome" : ""}` },
    ),
  };
}

function makeHeaderExample({ dark }) {
  const background = dark ? palette.charcoalNavy : palette.warmLight;
  const surface = dark ? palette.surfaceNavy : "#FFFFFF";
  const border = dark ? "#2A3B52" : "#DCE2EA";
  const neutral = dark ? palette.offWhite : palette.charcoalNavy;
  const accent = dark ? palette.brightOrange : palette.brandOrange;
  const lockup = makeHorizontal({ neutral, accent, compact: true, title: "" });
  const nav = dark ? "#94A3B8" : palette.blueGray;
  return svgDocument(
    "0 0 1280 240",
    `  <rect width="1280" height="240" fill="${background}"/>
  <rect x="32" y="40" width="1216" height="160" rx="16" fill="${surface}" stroke="${border}"/>
  <g transform="translate(70 90)">${lockup.svg.replace(/^<svg[^>]*>|<\/svg>\s*$/g, "").replace(/<title>.*?<\/title>/, "")}</g>
  <g fill="${nav}">
    <rect x="680" y="110" width="96" height="12" rx="6"/>
    <rect x="820" y="110" width="78" height="12" rx="6"/>
    <rect x="942" y="110" width="112" height="12" rx="6"/>
  </g>
  <rect x="1110" y="94" width="92" height="44" rx="12" fill="none" stroke="${border}"/>`,
    { title: `OpenQuatt ${dark ? "web app dark" : "docs light"} header example`, width: 1280, height: 240 },
  );
}

function makeHardwareExample() {
  const lockup = makeHardwareLockup({});
  return svgDocument(
    "0 0 1200 600",
    `  <rect width="1200" height="600" fill="${palette.warmLight}"/>
  <rect x="78" y="90" width="1044" height="420" rx="32" fill="#101722" stroke="#344155" stroke-width="4"/>
  <g transform="translate(146 170) scale(1.3)">${lockup.svg.replace(/^<svg[^>]*>|<\/svg>\s*$/g, "").replace(/<title>.*?<\/title>/, "")}</g>
${symbolMarkup({ ring: palette.offWhite, tail: palette.brightOrange, transform: "translate(840 190) scale(1.8)" })}
  <circle cx="924" cy="430" r="9" fill="#32D583"/>
  <path d="M960 430H1038" stroke="#CBD5E1" stroke-width="6" stroke-linecap="round"/>`,
    { title: "OpenQuatt dark hardware example", width: 1200, height: 600 },
  );
}

// Editable source files.
const sourceConstruction = svgDocument(
  "0 0 100 100",
  `  <rect width="100" height="100" fill="${palette.offWhite}"/>
  <g opacity=".35" fill="none" stroke="${palette.blueGray}" stroke-width=".5">
    <path d="M0 50H100M50 0V100"/>
    <ellipse cx="${standardGeometry.cx}" cy="${standardGeometry.cy}" rx="${standardGeometry.rx}" ry="${standardGeometry.ry}"/>
    <rect x="6.75" y="5.75" width="75.5" height="75.5"/>
  </g>
${symbolMarkup({ ring: palette.charcoalNavy, tail: palette.brandOrange })}
  <circle cx="${standardGeometry.cx}" cy="${standardGeometry.cy}" r="1.5" fill="${palette.deepOrange}"/>`,
  { title: "Open Q editable construction" },
);
write("source/open-q-construction.svg", sourceConstruction);
record({ logicalName: "open-q-construction", path: "source/open-q-construction.svg", type: "svg", variant: "construction", intendedBackground: "light", viewBox: "0 0 100 100", source: "native geometry" });

const editableWordmark = svgDocument(
  "0 0 310 72",
  `  <text x="4" y="56" fill="${palette.charcoalNavy}" font-family="Inter, sans-serif" font-size="58" font-weight="600" letter-spacing="-1.2">Open</text>
  <text x="133" y="56" fill="${palette.brandOrange}" font-family="Inter, sans-serif" font-size="58" font-weight="700" letter-spacing="-2.4">Quatt</text>`,
  { title: "OpenQuatt editable live-text wordmark" },
);
write("source/wordmark-editable.svg", editableWordmark);
record({ logicalName: "wordmark-editable", path: "source/wordmark-editable.svg", type: "svg", variant: "editable-live-text", intendedBackground: "light", viewBox: "0 0 310 72", source: "Inter 600/700" });

// Standalone symbols.
const symbolSpecs = [
  ["logos/svg/openquatt-symbol.svg", standardGeometry, palette.charcoalNavy, palette.brandOrange, "default", "light"],
  ["logos/svg/openquatt-symbol-dark.svg", standardGeometry, palette.offWhite, palette.brightOrange, "dark-background", "dark"],
  ["logos/svg/openquatt-symbol-light.svg", standardGeometry, palette.charcoalNavy, palette.brandOrange, "light-background", "light"],
  ["logos/svg/openquatt-symbol-mono.svg", standardGeometry, palette.charcoalNavy, palette.charcoalNavy, "monochrome", "light"],
  ["logos/svg/openquatt-symbol-micro.svg", microGeometry, palette.charcoalNavy, palette.brandOrange, "micro-16-20px", "light"],
  ["logos/svg/openquatt-symbol-micro-dark.svg", microGeometry, palette.offWhite, palette.brightOrange, "micro-16-20px-dark-background", "dark"],
  ["logos/svg/openquatt-symbol-micro-light.svg", microGeometry, palette.charcoalNavy, palette.brandOrange, "micro-16-20px-light-background", "light"],
];

for (const [relativePath, geometry, ring, tail, variant, background] of symbolSpecs) {
  writeSvg(relativePath, "0 0 100 100", symbolMarkup({ geometry, ring, tail }), {
    title: `OpenQuatt Open Q ${variant}`,
    variant,
    intendedBackground: background,
    source: "source/open-q-construction.svg",
  });
}

// Logo families.
const logoSpecs = [
  ["openquatt-logo-horizontal-dark.svg", "horizontal", palette.offWhite, palette.brightOrange, false, false, "dark"],
  ["openquatt-logo-horizontal-light.svg", "horizontal", palette.charcoalNavy, palette.brandOrange, false, false, "light"],
  ["openquatt-logo-horizontal-mono-dark.svg", "horizontal", palette.offWhite, palette.offWhite, true, false, "dark"],
  ["openquatt-logo-horizontal-mono-light.svg", "horizontal", palette.charcoalNavy, palette.charcoalNavy, true, false, "light"],
  ["openquatt-logo-compact-dark.svg", "compact", palette.offWhite, palette.brightOrange, false, true, "dark"],
  ["openquatt-logo-compact-light.svg", "compact", palette.charcoalNavy, palette.brandOrange, false, true, "light"],
];

for (const [fileName, variant, neutral, accent, mono, compact, background] of logoSpecs) {
  registerSvg(
    `logos/svg/${fileName}`,
    makeHorizontal({ neutral, accent, mono, compact, title: `OpenQuatt ${variant} logo for ${background} backgrounds` }),
    { variant: `${variant}${mono ? "-monochrome" : ""}`, intendedBackground: background, source: "source/open-q-construction.svg + outlined Inter 600/700" },
  );
}

for (const [fileName, neutral, accent, background] of [
  ["openquatt-logo-stacked-dark.svg", palette.offWhite, palette.brightOrange, "dark"],
  ["openquatt-logo-stacked-light.svg", palette.charcoalNavy, palette.brandOrange, "light"],
]) {
  registerSvg(`logos/svg/${fileName}`, makeStacked({ neutral, accent, title: `OpenQuatt stacked logo for ${background} backgrounds` }), {
    variant: "stacked",
    intendedBackground: background,
    source: "source/open-q-construction.svg + outlined Inter 600/700",
  });
}

for (const [fileName, neutral, accent, background] of [
  ["openquatt-wordmark-dark.svg", palette.offWhite, palette.brightOrange, "dark"],
  ["openquatt-wordmark-light.svg", palette.charcoalNavy, palette.brandOrange, "light"],
]) {
  registerSvg(`logos/svg/${fileName}`, makeWordmark({ neutral, accent, title: `OpenQuatt wordmark for ${background} backgrounds` }), {
    variant: "wordmark",
    intendedBackground: background,
    source: "outlined Inter 600/700",
  });
}

// Hardware masters. These carry only the OpenQuatt identity, without a product descriptor.
registerSvg("hardware/openquatt-hardware-lockup-dark.svg", makeHardwareLockup({}), {
  variant: "hardware-full-colour",
  intendedBackground: "dark hardware",
  source: "source/open-q-construction.svg + outlined Inter 600/700",
});
registerSvg("hardware/openquatt-hardware-lockup-mono-dark.svg", makeHardwareLockup({ mono: true }), {
  variant: "hardware-monochrome-white",
  intendedBackground: "dark hardware",
  source: "source/open-q-construction.svg + outlined Inter 600/700",
});
registerSvg("hardware/openquatt-hardware-lockup-mono-light.svg", makeHardwareLockup({ mono: true, black: true }), {
  variant: "hardware-monochrome-black",
  intendedBackground: "light hardware",
  source: "source/open-q-construction.svg + outlined Inter 600/700",
});
writeSvg(
  "hardware/openquatt-symbol-engraving.svg",
  "0 0 100 100",
  symbolMarkup({ ring: "#000000", tail: "#000000" }),
  { title: "Open Q single-colour engraving mark", variant: "engraving", intendedBackground: "material-dependent", source: "source/open-q-construction.svg" },
);

// Platform icon masters and exports.
const faviconSvg = tileSvg({ size: 64, geometry: microGeometry, padding: 0.14, radius: 12, title: "OpenQuatt favicon" });
write("icons/favicon/favicon.svg", faviconSvg);
record({ logicalName: "favicon", path: "icons/favicon/favicon.svg", type: "svg", variant: "favicon", intendedBackground: "baked charcoal tile", viewBox: "0 0 64 64", source: "logos/svg/openquatt-symbol-micro.svg" });

const appMaster = tileSvg({ size: 512, padding: 0.18, radius: 96, title: "OpenQuatt app icon master" });
write("icons/pwa/app-icon-master.svg", appMaster);
record({ logicalName: "app-icon-master", path: "icons/pwa/app-icon-master.svg", type: "svg", variant: "app-icon-master", intendedBackground: "baked charcoal tile", viewBox: "0 0 512 512", source: "logos/svg/openquatt-symbol-dark.svg" });

const maskableMaster = tileSvg({ size: 512, padding: 0.24, radius: 0, title: "OpenQuatt maskable icon master" });
write("icons/pwa/maskable-icon-master.svg", maskableMaster);
record({ logicalName: "maskable-icon-master", path: "icons/pwa/maskable-icon-master.svg", type: "svg", variant: "maskable-master", intendedBackground: "baked charcoal tile", viewBox: "0 0 512 512", source: "logos/svg/openquatt-symbol-dark.svg" });

const avatarMaster = tileSvg({ size: 1024, padding: 0.17, radius: 0, title: "OpenQuatt avatar master" });
write("icons/social/avatar-master.svg", avatarMaster);
record({ logicalName: "avatar-master", path: "icons/social/avatar-master.svg", type: "svg", variant: "avatar-master", intendedBackground: "baked charcoal tile", viewBox: "0 0 1024 1024", source: "logos/svg/openquatt-symbol-dark.svg" });

for (const [relativePath, source, width, height, variant] of [
  ["icons/favicon/favicon-16x16.png", faviconSvg, 16, 16, "favicon-micro"],
  ["icons/favicon/favicon-32x32.png", faviconSvg, 32, 32, "favicon-micro"],
  ["icons/pwa/apple-touch-icon.png", appMaster, 180, 180, "apple-touch"],
  ["icons/pwa/android-chrome-192x192.png", appMaster, 192, 192, "pwa"],
  ["icons/pwa/android-chrome-512x512.png", appMaster, 512, 512, "pwa"],
  ["icons/pwa/maskable-icon-512x512.png", maskableMaster, 512, 512, "pwa-maskable"],
  ["icons/social/github-avatar-512.png", avatarMaster, 512, 512, "github-avatar"],
  ["icons/social/social-avatar-1024.png", avatarMaster, 1024, 1024, "social-avatar"],
]) {
  writePng(relativePath, source, width, height, {
    variant,
    intendedBackground: "baked charcoal tile",
    source: variant.includes("favicon") ? "icons/favicon/favicon.svg" : variant === "pwa-maskable" ? "icons/pwa/maskable-icon-master.svg" : variant.includes("avatar") ? "icons/social/avatar-master.svg" : "icons/pwa/app-icon-master.svg",
  });
}

// Social card.
const socialCard = makeSocialCard();
write("social/openquatt-social-card-1280x640.svg", socialCard);
record({ logicalName: "openquatt-social-card-1280x640", path: "social/openquatt-social-card-1280x640.svg", type: "svg", variant: "social-card", intendedBackground: "baked charcoal", viewBox: "0 0 1280 640", source: "logos/svg/openquatt-logo-horizontal-dark.svg" });
writePng("social/openquatt-social-card-1280x640.png", socialCard, 1280, 640, { variant: "social-card", intendedBackground: "baked charcoal", source: "social/openquatt-social-card-1280x640.svg" });

// Useful transparent PNG logo exports.
for (const [relativePath, svgPath, width, height, variant, background] of [
  ["logos/png/openquatt-logo-horizontal-dark-1200.png", "logos/svg/openquatt-logo-horizontal-dark.svg", 1200, 300, "horizontal", "dark"],
  ["logos/png/openquatt-logo-horizontal-light-1200.png", "logos/svg/openquatt-logo-horizontal-light.svg", 1200, 300, "horizontal", "light"],
  ["logos/png/openquatt-logo-stacked-dark-800.png", "logos/svg/openquatt-logo-stacked-dark.svg", 800, 510, "stacked", "dark"],
  ["logos/png/openquatt-logo-stacked-light-800.png", "logos/svg/openquatt-logo-stacked-light.svg", 800, 510, "stacked", "light"],
]) {
  const source = fs.readFileSync(path.join(brandDir, svgPath), "utf8").replace("<svg ", `<svg width="${width}" height="${height}" `);
  writePng(relativePath, source, width, height, { variant, intendedBackground: background, source: svgPath });
}

// Application examples are raster review material generated from the masters.
const webappExample = makeHeaderExample({ dark: true });
const docsExample = makeHeaderExample({ dark: false });
const hardwareExample = makeHardwareExample();
writePng("examples/webapp-dark.png", webappExample, 1280, 240, { variant: "example-webapp-header", intendedBackground: "dark", source: "logos/svg/openquatt-logo-compact-dark.svg" });
writePng("examples/docs-light.png", docsExample, 1280, 240, { variant: "example-docs-header", intendedBackground: "light", source: "logos/svg/openquatt-logo-compact-light.svg" });
writePng("examples/hardware-dark.png", hardwareExample, 1200, 600, { variant: "example-hardware", intendedBackground: "dark hardware", source: "hardware/openquatt-hardware-lockup-dark.svg" });

// Source-of-truth tokens.
const tokenJson = {
  version: 1,
  brand: "OpenQuatt",
  colors: {
    orange: { value: palette.brandOrange, purpose: "Core brand accent" },
    orangeBright: { value: palette.brightOrange, purpose: "Accent on dark surfaces" },
    orangeDeep: { value: palette.deepOrange, purpose: "Accessible orange text on light surfaces" },
    navy: { value: palette.charcoalNavy, purpose: "Primary dark background" },
    surface: { value: palette.surfaceNavy, purpose: "Dark raised surface" },
    offWhite: { value: palette.offWhite, purpose: "Reversed mark and text" },
    ink: { value: palette.ink, purpose: "Light-theme text" },
    blueGray: { value: palette.blueGray, purpose: "Secondary light-theme text" },
    warmLight: { value: palette.warmLight, purpose: "Optional warm page background" },
  },
  spacing: [4, 8, 12, 16, 24, 32, 40, 48, 64],
  radii: { small: 8, medium: 12, large: 16, pill: 999 },
  typography: { family: "Inter", fallback: '"Segoe UI", system-ui, -apple-system, sans-serif' },
};
write("tokens/brand-tokens.json", `${JSON.stringify(tokenJson, null, 2)}\n`);
record({ logicalName: "brand-tokens-json", path: "tokens/brand-tokens.json", type: "json", variant: "design-tokens", intendedBackground: "not-applicable", source: "OpenQuatt Brand Pack Specification v1.0" });

write(
  "tokens/brand-tokens.css",
  `:root {
  --oq-brand-orange: ${palette.brandOrange};
  --oq-brand-orange-bright: ${palette.brightOrange};
  --oq-brand-orange-deep: ${palette.deepOrange};
  --oq-brand-navy: ${palette.charcoalNavy};
  --oq-brand-surface: ${palette.surfaceNavy};
  --oq-brand-off-white: ${palette.offWhite};
  --oq-brand-ink: ${palette.ink};
  --oq-brand-blue-gray: ${palette.blueGray};
  --oq-brand-warm-light: ${palette.warmLight};
  --oq-brand-radius-sm: 8px;
  --oq-brand-radius-md: 12px;
  --oq-brand-radius-lg: 16px;
  --oq-brand-radius-pill: 999px;
}
`,
);
record({ logicalName: "brand-tokens-css", path: "tokens/brand-tokens.css", type: "css", variant: "design-tokens", intendedBackground: "not-applicable", source: "tokens/brand-tokens.json" });

for (const [logicalName, relativePath] of [
  ["brand-readme", "README.md"],
  ["brand-guidelines", "BRAND-GUIDELINES.md"],
  ["brand-asset-license", "BRAND-ASSET-LICENSE.md"],
  ["font-readme", "fonts/README.md"],
]) {
  record({ logicalName, path: relativePath, type: "document", variant: "documentation", intendedBackground: "not-applicable", source: "OpenQuatt Brand Pack Specification v1.0" });
}

const manifest = {
  version: 1,
  brand: "OpenQuatt",
  concept: "Concept A — Open Q",
  status: "production candidate pending human approval",
  generatedBy: "brand/tools/export-assets.mjs",
  palette,
  assets: assets.sort((a, b) => a.path.localeCompare(b.path)),
};
write("asset-manifest.json", `${JSON.stringify(manifest, null, 2)}\n`);

console.log(`Exported ${assets.length} deterministic brand assets.`);
