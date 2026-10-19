# Release notes

Use this shared format for GitHub, Tweakers and Discord. Compare the previous
stable tag with the frozen release SHA, not just dev's latest commit or PR titles.
Use a different baseline only when explicitly requested. Read the actual PRs,
important discussions and relevant diffs before making user-facing claims.

## Editorial checklist

- Write simple, concise Dutch. Group related firmware and UI work by user
  outcome; improvements to existing features are not new introductions.
- Follow the preceding release's thematic layout, not a mandatory New/Fixed
  taxonomy. Aim for 250-400 words on GitHub/Tweakers and 80-140 on Discord;
  essential upgrade warnings may need more space. Patch notes are shorter.
- Put breaking changes, changed defaults, backup/recovery actions and data loss
  early. Mention longer updates or extra restarts when the candidate requires
  them; do not copy a one-time migration warning into every subsequent release.
- Explain practical effects, such as fewer stop/starts, without promising
  universal crash freedom, energy savings or hardware validation from a build.
- Keep review findings and test evidence in the release report. Do not hide a
  known user-relevant limitation behind an unqualified feature claim.
- Use **Beslislog**, **Systeemrecorder** and **Kp/Ki** consistently. Power House
  learning is for insight unless the actual release adds validated automatic
  application. Do not conflate learning with optional usage statistics.
- **OTB** means OpenTherm Boiler, the OT-master boiler control, not a bridge.
  Use **in ontwikkeling** where applicable; a narrow fix is not a general
  qualification of the implementation.
- Describe telemetry consent and identifiers accurately, not as anonymous if
  the same physical controller remains identifiable after a factory reset.
- No GitHub logo by default. Tweakers may use the established logo; verify its
  path exists in the release tree. A logo announcement is not recurring news.
- Derive hardware support from the release's target matrix. Q Single/Duo are
  currently active; Waveshare/Heatpump Listener remain on v0.50.0. That cutoff
  is not a new breaking change in each release.
- Use a real compare link. Draft release links are prospective until publication.

Save working files under `.tmp/releases/vX.Y.Z-{notes.md,tweakers.txt,discord.txt}`.
Keep drafts visibly marked until finalization and recheck all three against the
final release SHA. These ignored files are convenient working copies, not the
only durable record of a release decision.

## GitHub Markdown

Use a title, one-sentence introduction, any essential warning, then 3-5 short
thematic sections and the changelog. Retain the familiar thematic emoji headings.

```markdown
# OpenQuatt vX.Y.Z

[Korte samenvatting van deze release.]

> [!WARNING]
> [Alleen relevante updatewaarschuwingen en benodigde acties.]

## 🏠 [Belangrijkste thema]

[Korte toelichting op het praktische resultaat.]

## 🔧 Kleinere verbeteringen

- [Gebruikersgerichte wijziging.]

---

**Volledige changelog:** [vBASE → vX.Y.Z](https://github.com/OpenQuatt/OpenQuatt/compare/vBASE...vX.Y.Z)
```

Apply final GitHub notes only after Release Build has published successfully;
do not create or prematurely publish its draft to attach the text.

## Tweakers UBB

Deliver raw UBB in `.txt`, not Markdown. Use closed `[h1]`, `[h2]`, `[h3]`,
`[b]` and `[url=...]` tags and `•` for concise items; do not invent list tags.

```text
[h1]OpenQuatt vX.Y.Z[/h1]

[img=600,132]https://raw.githubusercontent.com/OpenQuatt/OpenQuatt/vX.Y.Z/docs/assets/brand/openquatt-logo-horizontal-adaptive.svg[/img]

[Korte introductie.]

[b]Let op bij deze update:[/b] [Alleen relevante acties.]

[h2]🏠 [Belangrijkste thema][/h2]

[Korte toelichting.]

[h2]🔧 Kleinere verbeteringen[/h2]

• [Wijziging.]

[h2]🧱 Beschikbaarheid[/h2]

[Ondersteunde hardware en relevante beperkingen.]

[url=https://github.com/OpenQuatt/OpenQuatt/releases/tag/vX.Y.Z][b]Release notes en download[/b][/url]
[url=https://github.com/OpenQuatt/OpenQuatt/compare/vBASE...vX.Y.Z]Volledige changelog: vBASE → vX.Y.Z[/url]
```

## Discord Markdown

Use a bold title, 3-5 short bullets, essential upgrade warnings and a release
link. No full duplicate of GitHub, logo or large headings.

```text
**OpenQuatt vX.Y.Z**

- **[Thema]:** [Praktische verbetering.]
- [Andere hoofdwijziging.]

**Let op:** [Essentiële actie, indien van toepassing.]

**Release en volledige notes:** https://github.com/OpenQuatt/OpenQuatt/releases/tag/vX.Y.Z
```
