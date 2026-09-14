import { readFile } from "node:fs/promises";
import { parse } from "acorn";
import { Features, transform as transformCss } from "lightningcss";
import { minify as minifyJavaScript } from "terser";
import {
  applyBundleSymbolPlan,
  createBundleSymbolManifest,
  loadCanonicalBundleSymbolPlan,
  validateUnusedCssSymbolReferences,
} from "./bundle-symbols.mjs";

const HTML_TEMPLATE_START = /^\s*</;
const HTML_TEMPLATE_END = />\s*$/;
const HTML_PRESERVE_WHITESPACE = /<(?:pre|script|style|textarea)(?:\s|>)/i;
const HTML_TAG_START = /[A-Za-z/!?]/;
const HTML_WHITESPACE = /[\t\n\f\r ]/;
const BUNDLE_SYMBOL_PLAN = loadCanonicalBundleSymbolPlan();

export const BUNDLE_SYMBOL_MANIFEST = Object.freeze(createBundleSymbolManifest(BUNDLE_SYMBOL_PLAN));

function startsHtmlTag(source, index) {
  return source[index] === "<" && HTML_TAG_START.test(source[index + 1] || "");
}

function compactHtmlTemplateQuasi(raw, state) {
  let compact = "";
  let index = 0;

  while (index < raw.length) {
    const character = raw[index];
    if (state.quote) {
      compact += character;
      if (character === state.quote) {
        state.quote = "";
      }
      index += 1;
      continue;
    }

    if (state.inTag) {
      if (character === '"' || character === "'") {
        state.quote = character;
        compact += character;
        index += 1;
        continue;
      }
      if (HTML_WHITESPACE.test(character)) {
        let whitespaceEnd = index + 1;
        while (whitespaceEnd < raw.length && HTML_WHITESPACE.test(raw[whitespaceEnd])) {
          whitespaceEnd += 1;
        }
        const nextCharacter = raw[whitespaceEnd];
        const beforeTagEnd = nextCharacter === ">";
        // Keep a delimiter before `/>`; otherwise `/` joins an unquoted attribute value.
        if (!beforeTagEnd && compact.at(-1) !== " ") {
          compact += " ";
        }
        index = whitespaceEnd;
        continue;
      }

      compact += character;
      if (character === ">") {
        state.inTag = false;
      }
      index += 1;
      continue;
    }

    if (startsHtmlTag(raw, index)) {
      state.inTag = true;
      compact += character;
      index += 1;
      continue;
    }

    if (HTML_WHITESPACE.test(character)) {
      let whitespaceEnd = index + 1;
      while (whitespaceEnd < raw.length && HTML_WHITESPACE.test(raw[whitespaceEnd])) {
        whitespaceEnd += 1;
      }
      const whitespace = raw.slice(index, whitespaceEnd);
      const betweenTags = /[\r\n]/.test(whitespace)
        && compact.at(-1) === ">"
        && startsHtmlTag(raw, whitespaceEnd);
      compact += betweenTags ? " " : whitespace;
      index = whitespaceEnd;
      continue;
    }

    compact += character;
    index += 1;
  }

  return compact;
}

function visitSyntaxTree(node, visitor) {
  if (!node || typeof node !== "object") {
    return;
  }
  visitor(node);
  for (const [key, value] of Object.entries(node)) {
    if (key === "start" || key === "end" || key === "loc") {
      continue;
    }
    if (Array.isArray(value)) {
      value.forEach((child) => visitSyntaxTree(child, visitor));
    } else if (value && typeof value === "object" && typeof value.type === "string") {
      visitSyntaxTree(value, visitor);
    }
  }
}

export function compactHtmlTemplateWhitespace(source) {
  const syntaxTree = parse(source, { ecmaVersion: "latest", sourceType: "module" });
  const edits = [];

  visitSyntaxTree(syntaxTree, (node) => {
    if (node.type !== "TemplateLiteral") {
      return;
    }
    const content = node.quasis.map((quasi) => quasi.value.cooked || "").join("${}");
    if (!HTML_TEMPLATE_START.test(content)
      || !HTML_TEMPLATE_END.test(content)
      || HTML_PRESERVE_WHITESPACE.test(content)) {
      return;
    }

    const state = { inTag: false, quote: "" };
    node.quasis.forEach((quasi) => {
      const raw = source.slice(quasi.start, quasi.end);
      const compact = compactHtmlTemplateQuasi(raw, state);
      if (compact !== raw) {
        edits.push({ start: quasi.start, end: quasi.end, compact });
      }
    });
  });

  return edits
    .sort((left, right) => right.start - left.start)
    .reduce((output, edit) => (
      `${output.slice(0, edit.start)}${edit.compact}${output.slice(edit.end)}`
    ), source);
}

export function compactHtmlTemplateWhitespacePlugin() {
  return {
    name: "openquatt-compact-html-templates",
    setup(pluginBuild) {
      pluginBuild.onLoad({ filter: /\.js$/, namespace: "file" }, async (args) => ({
        contents: compactHtmlTemplateWhitespace(await readFile(args.path, "utf8")),
        loader: "js",
      }));
    },
  };
}

export function minifyCssBundle(source, filename) {
  return transformCss({
    filename,
    code: Buffer.from(applyBundleSymbolPlan(source, BUNDLE_SYMBOL_PLAN)),
    minify: true,
    include: Features.MediaQueries,
    unusedSymbols: BUNDLE_SYMBOL_PLAN.unusedCssSymbols,
  }).code.toString().trim();
}

export async function minifyJavaScriptBundle(source, label = "JavaScript bundle") {
  validateUnusedCssSymbolReferences(source, BUNDLE_SYMBOL_PLAN.unusedCssSymbols, label);
  const minified = await minifyJavaScript(applyBundleSymbolPlan(source, BUNDLE_SYMBOL_PLAN), {
    ecma: 2020,
    module: true,
    // Retaining repeated token patterns produces a smaller embedded gzip asset.
    compress: {
      passes: 1,
      booleans: false,
      evaluate: false,
      sequences: false,
    },
    mangle: true,
    format: { ascii_only: true, comments: false },
  });
  if (!minified.code) {
    throw new Error(`Terser produced no output for ${label}`);
  }
  return minified.code.trim();
}
