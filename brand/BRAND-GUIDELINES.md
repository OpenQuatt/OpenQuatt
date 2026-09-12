# OpenQuatt brand guidelines — candidate v1

## Identity

The project name is always **OpenQuatt**: capital O, capital Q, no space and no `+`. The standalone symbol is the **Open Q**, an open ring with an orange lower-right control/heat-flow tail. Product or model descriptors are not part of this brand system.

This Stage 1 identity is a candidate. It must be approved before it replaces any production web-app or documentation branding.

## Construction and distinctiveness

The master uses a 100 × 100 coordinate system. The final ring is a clean optical circle with a controlled lower-right opening, even visual weight and an exact 45° tail that begins inside the counter. Compared with a generic closed Q, the open ring, separated colour plane and internal-to-external tail remain the identifying OpenQuatt features. The horizontal wordmark is deliberately large, close-set and tightly tracked so its cap height aligns more decisively with the standalone symbol.

The standard geometry uses a 14.5-unit optical stroke and a 56° opening. The micro geometry uses a 16-unit stroke, a wider 66° opening and a shorter tail. Do not substitute a downscaled standard mark at 16 px.

The distinctiveness review is a visual gate, not a legal clearance or trademark opinion.

## Logo families

- **Primary horizontal:** default for web headers, documentation, READMEs and presentations.
- **Compact horizontal:** tuned for 28–36 px header height and narrow navigation.
- **Stacked:** square and portrait layouts; never use at favicon scale.
- **Standalone symbol:** avatars, app icons, favicons, loading states and hardware identifiers.
- **Wordmark:** use only when the symbol is already present or the format is unusually shallow.

Files ending in `-dark` are intended for dark backgrounds; files ending in `-light` are intended for light backgrounds. Release logo SVGs contain outlined Inter glyphs and have no installed-font dependency.

## Colour

| Name | Hex | Primary role |
|---|---:|---|
| OpenQuatt Orange | `#EA580C` | Core accent on light surfaces |
| Bright Orange | `#F97316` | Accent on dark surfaces |
| Deep Orange | `#C2410C` | Normal-size orange text on light surfaces |
| Charcoal Navy | `#0F1724` | Primary dark background |
| Surface Navy | `#162131` | Raised dark surface |
| Off White | `#F8FAFC` | Reversed mark and primary dark-theme text |
| Ink | `#111827` | Primary light-theme text |
| Blue Gray | `#475569` | Secondary light-theme text |
| Warm Light | `#F6F2EA` | Optional warm page background |

On dark surfaces use Off White for the ring/“Open” and Bright Orange for the tail/“Quatt”. On light surfaces use Charcoal Navy and OpenQuatt Orange. Prefer Deep Orange when orange is normal-size UI text on white. Brand colours do not replace established green/red/blue semantic status colours.

## Typography

Inter is the primary family. The wordmark starts from Inter SemiBold 600 for “Open” and Bold 700 for “Quatt”, with optically tightened spacing and a dedicated `nQ` adjustment. UI fallback:

```css
font-family: "Inter", "Segoe UI", system-ui, -apple-system, sans-serif;
```

Do not redistribute font files in the brand kit. Technical logs may retain a system monospace stack.

## Clear space and minimum size

Let **X** equal the ring stroke width. Keep at least 1X clear space around the standalone symbol and around every side of a lockup.

- Standard symbol: minimum 20 px digital or 6 mm print.
- Micro symbol: 16–20 px digital only; use the supplied dark- or light-background variant.
- Compact horizontal lockup: minimum 110 px wide.
- Primary horizontal lockup: minimum 140 px wide or 30 mm print.

At smaller sizes, use the micro symbol instead of compressing the wordmark.

## App icons, favicons and avatars

Use the supplied exports rather than placing the transparent master on an arbitrary tile. The default tile is Charcoal Navy with an Off White ring and Bright Orange tail. The maskable master keeps the complete mark inside the central safe area and uses a full-bleed navy background.

- Favicons use the micro geometry.
- GitHub and social avatars use the standard symbol on a square navy field and must remain recognizable after a circular crop.
- Apple/PWA icons use a rounded-square source composition.

## Hardware and engraving

Use `openquatt-hardware-lockup-dark.svg` where width permits. Use the `mono-dark` variant for light ink on dark material, the `mono-light` variant for dark ink on light material, and `openquatt-symbol-engraving.svg` for a standalone single-colour process. No controller model or product descriptor is included in the brand master.

The vector master defines proportions, not the capabilities of a specific production process. Confirm minimum line width, knockout behavior and substrate contrast with the actual printer or engraver before manufacture.

## Do not

- rotate, stretch or compress the mark;
- move or recolour the tail independently;
- add glow, bevel, shadow or gradient to a master logo;
- place the full-colour mark on orange;
- use the Open Q as a system-state icon;
- append `+` to the official wordmark;
- rebuild the wordmark with live text for release artwork;
- infer permission to use or modify the brand assets while licensing is pending.

## Stage 2 integration

After human approval, production integration should be a separate change covering web-app assets, docs, README/social metadata and PWA manifests. Keep semantic UI colours and runtime behavior unchanged.
