import { escapeHtml } from "../core/html.js";

// Text and status metrics share one structure; data attributes keep live values patchable.
export function renderStatCard({ label, value, note = "", status = false, tone = "", accent = false, valueData = {} }) {
  const toneClass = tone === "blue" ? " oq-stat--blue" : tone === "orange" ? " oq-stat--orange" : tone === "green" ? " oq-stat--green" : tone === "sky" ? " oq-stat--sky" : "";
  const attributes = Object.entries(valueData).map(([name, content]) => {
    if (!/^[a-z][a-z0-9-]*$/.test(name)) throw new Error(`Invalid stat data attribute: ${name}`);
    return ` data-${name}="${escapeHtml(content)}"`;
  }).join("");
  return `
    <article class="oq-stat${status ? " oq-stat--status" : ""}${toneClass}${accent ? " oq-stat--accent" : ""}">
      <p class="oq-stat-label">${escapeHtml(label)}</p>
      <strong class="oq-stat-value"${attributes}>${escapeHtml(value)}</strong>
      ${note ? `<p class="oq-stat-note">${escapeHtml(note)}</p>` : ""}
    </article>
  `;
}
