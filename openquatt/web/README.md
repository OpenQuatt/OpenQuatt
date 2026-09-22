# Web-appontwikkeling

Dit is de centrale ontwikkelrichtlijn voor de embedded OpenQuatt-web-app, voor
ontwikkelaars en coding agents. De afspraken gelden bij uitbreidingen en wijzigingen;
ze zijn geen opdracht om ongerelateerde bestaande code te herschrijven.

## Hergebruik en eigenaarschap

- Gebruik bestaande componenten vóór je een nieuwe variant maakt. Breid een helper
  uit als gedrag werkelijk overeenkomt; maak geen universele component met allerlei
  uitzonderingen voor slechts één scherm.
- Houd waarden, opties, validatie en busy-status in één veldmodel voor initiële
  rendering én live updates. Laat firmwareopties en lokale drafts niet afzonderlijk
  door dropdowns en keuzekaarten reconstrueren.
- Deel presentatie, niet automatisch domeinregels. Entity-controls en servicevelden
  hebben verschillende eigenaars en schrijfvoorwaarden. Geef servicevelden niet
  zomaar `data-oq-field` of gedeelde select-markers; behoud hun eigen autorisatie,
  beschikbaarheids-, identiteits- en schrijfcontroles.
- Houd domeinspecifieke copy, filters en capability-keuzes bij het domein. Voeg een
  abstractie pas toe wanneer meerdere gebruikers hetzelfde contract hebben.

Startpunten in de bestaande code:

| Onderdeel | Eigenaar |
|---|---|
| Selectwaarden, opties en busy-status | [settings/field-models.js](js/src/settings/field-models.js) |
| Settings-rendering en live veldpatches | [settings/controls.js](js/src/settings/controls.js) |
| Numerieke entity-invoer | [core/number-controls.js](js/src/core/number-controls.js) |
| Modals en behoud van bediening | [core/modal-shell.js](js/src/core/modal-shell.js), [core/modal-continuity.js](js/src/core/modal-continuity.js) |
| Gedeelde statistiekkaarten | [views/stat-card.js](js/src/views/stat-card.js) |
| Drafts en rendervergelijking | [core/control-drafts.js](js/src/core/control-drafts.js), [core/render-signatures.js](js/src/core/render-signatures.js) |

De [JS-bronindeling](js/src/README.md) beschrijft de modules en de precieze
opt-invoorwaarden voor gedeelde select-controls.

## Vormgeving en live bediening

- Gebruik bestaande tokens, spacing, knoppen, velden, kaarten en modalvarianten.
  Nieuwe CSS hoort bij het kleinste relevante onderdeel; geen globale overrides om
  één scherm te repareren. Zie de [CSS-bronindeling](css/src/README.md).
- Behoud de betekenisvolle kleuren van de overview. Uniformering is geen reden om
  elektrische/thermische waarden, COP, flow of status hun bestaande kleur te ontnemen.
- Live updates behouden drafts, focus, cursor/selectie, geopende native dropdowns
  en scrollpositie. Gebruik gerichte patches wanneer dat veilig kan; behoud een
  volledige render als de structuur of gespecialiseerde bediening die nodig heeft.
- Vermijd renders bij ongewijzigde data en bouw zware scherminhoud pas wanneer de
  betreffende view of modal die nodig heeft.
- Controleer labels, eenheden, toetsenbordbediening, lange teksten en zichtbare
  sluitknoppen op mobiel én desktop, in licht en donker.

## Nederlands en Engels

- Alle nieuwe of gewijzigde zichtbare appteksten gebruiken `t()` uit
  [i18n/index.js](js/src/i18n/index.js), inclusief foutmeldingen, bevestigingen,
  lege toestanden, tooltips en toegankelijkheidslabels. Voeg semantische sleutels
  met dezelfde placeholders toe aan zowel [nl.js](js/src/i18n/nl.js) als
  [en.js](js/src/i18n/en.js). Nederlands blijft de standaard- en fallbacktaal.
- Vertaal alleen de presentatie. Firmware-/API-waarden, entity-ID's, opgeslagen
  opties en interne logica-ID's blijven stabiel. Gebruik `optionLabel()` of
  `translateFirmwareText()` voor bekende firmwareteksten; vergelijk logica met de
  oorspronkelijke waarde, niet met een vertaald label.
- Bewaar vertaalsleutels in statische configuratie en haal de tekst tijdens renderen
  of het opstellen van een melding op. Cache geen vertalingen bij module-import.
  Een taalwissel moet ook bestaande views, modals en live veldpatches bijwerken,
  zonder drafts te verliezen of device-writes te starten.
- Gebruik `formatNumber()`, `formatDate()`, `formatTime()`, `formatDateTime()` of
  `getIntlLocale()` voor presentatie. Houd invoer-, opslag- en wireformaten
  taal-onafhankelijk. Escape vertaalde tekst en geïnterpoleerde onbetrouwbare
  waarden passend bij de HTML-context; `t()` doet geen HTML-escaping.
- Gebruik volledige statische sleutels waar mogelijk. De
  [i18n-build](i18n-bundle.mjs) zet deze om naar numerieke IDs en compacte catalogi.
  Dynamisch samengestelde sleutels vereisen expliciete ondersteuning in de
  runtime-index en een test tegen de gebouwde compacte bundle. Gebruik nooit
  gegenereerde IDs als bron-, opslag- of testcontract.
- Beide talen blijven offline beschikbaar in dezelfde firmware. Pas de compacte
  build toe en bewaak het bestaande raw/gzip-budget; voeg geen aparte taalfirmware
  of externe afhankelijkheid voor het ophalen van vertalingen toe.
- Controleer gewijzigde schermen in NL én EN, inclusief taalwisselen, behoud na
  herladen en langere Engelse teksten. Voer de onderstaande webvalidatie uit:
  `npm run check:web` omvat cataloguspariteit, sleutel-/hardcoded-copychecks en
  compacte-bundletests. Voeg gerichte locale-regressietests toe waar gedrag,
  formattering of dynamische sleutels geraakt worden. Controleer bij merges ook
  nieuwe UI-copy uit de doelbranch; een conflictvrije merge garandeert geen
  volledige vertaling.

## Requests, bevestiging en compatibiliteit

- Gebruik de bestaande request- en statehelpers. Begrens wachttijden, voorkom
  dubbele polling en ruim timers/requests op bij het verlaten van een scherm.
  Een lopende schrijfopdracht mag daarbij niet als geannuleerd worden beschouwd
  alleen omdat de UI sluit: reconcileer het resultaat bij heropening.
- Laat late of verouderde antwoorden geen nieuwere keuze, draft of schermstatus
  overschrijven. Maak retry-, busy-, offline- en foutgedrag expliciet.
- Een geaccepteerde schrijfopdracht is geen bewezen eindresultaat. Meld succes pas
  na de bevestiging die het domein vereist; toon een onzekere uitkomst als onzeker.
  Herhaal niet-idempotente writes niet blind na een timeout.
- Behoud authenticatie/CSRF en firmware-side veiligheidscontroles. Een disabled
  knop is geen beveiligingsgrens. Escape onbetrouwbare tekst via de bestaande helpers.
- Behandel ontbrekende entities, oude firmware en legacy-backups expliciet. Valideer
  backupwaarden op type en bereik; verzin geen nul/default voor ontbrekende of
  ongeldige data. Wijzig schema's alleen met een bewuste migratie/restorestrategie.

## Productiebundel en omvang

- De normale bediening blijft lokaal en zelfstandig: geen GitHub Pages, CDN of
  andere externe hosting voor noodzakelijke UI-assets. Expliciete netwerkfuncties
  zoals updates staan los van het laden en bedienen van de app.
- Bewerk `js/src/`, `css/src/` en bronassets, niet de gegenereerde bundles.
  Productie- en previewbundles zijn gegenereerd en worden niet gecommit.
  Houd broncode leesbaar; [de build](build-assets.mjs) verzorgt de verkleining.
- Houd demo/mockgedrag in de preview. Gebruik `__OQ_PREVIEW__` voor code die uit
  productie moet verdwijnen; `dev.html` laadt standaard de previewbundles.
- Gebruik volledige, statisch herkenbare CSS-classnamen waar mogelijk. Dynamische
  namen en externe consumenten moeten passen bij het contract in
  [bundle-symbols.mjs](bundle-symbols.mjs); gebruik gegenereerde korte namen nooit
  als bron- of testcontract. Omzeil geen symbol-/dode-CSS-check om een build groen te maken.
- Hergebruik eerst voordat je nieuwe dependencies, assets of renderpaden toevoegt.
  Rapporteer raw/gzip-groei tegenover de PR-doelbranch en onderbouw een eventuele
  wijziging aan [web-budgets.mjs](web-budgets.mjs). Actuele grenswaarden horen daar,
  niet als tweede kopie in deze richtlijn.

## Validatie en onderhoud

Voer vanuit de repository-root uit na webcodewijzigingen (dependencies installeren
met `npm ci` indien nodig):

```sh
npm run check:web
npm run smoke:web
node openquatt/web/build-assets.mjs --check
```

`check:web` bouwt de bundles en controleert tests/kwaliteit/budgetten. `smoke:web`
bouwt de preview en controleert onder meer importgrenzen. Een lokale budgetcheck
zonder basisvergelijking bewijst niet dat de PR binnen het relatieve groeibudget valt.

- Voeg gerichte regressietests toe voor de gewijzigde contracten, inclusief relevante
  fouten, stale responses, dubbele acties, ontbrekende data en herstel na mislukken.
- Volg de [browsermatrix](BROWSER_SMOKE_MATRIX.md). Test ook de productie-JS en -CSS;
  een geslaagde preview bewijst niet dat minificatie en symbolcompactie correct zijn.
- Controleer Safari/iOS waar native invoer, focus, scrolling of mobiele layout wordt
  geraakt. Vermeld wat werkelijk is getest en wat niet; mocktests vervangen geen
  hardwaretest wanneer firmwaregedrag verandert. Doe geen echte device-writes/OTA
  zonder toestemming.
- Houd de complete PR-diff klein en review die tegen de actuele doelbranch, gevolgd
  door een afzonderlijke koude nacontrole. Vermeld validatie en resterende risico's.
- Bij uitsluitend documentatie: controleer links en commando's en voer
  `npm run check:docs` uit; webbuilds, browserchecks en firmwarecompiles zijn dan
  niet nodig.

Leg algemene afspraken hier vast, implementatiedetails in de JS/CSS-README's en
afdwingbare invarianten in tests/checkers. Houd [AGENTS.md](AGENTS.md) kort en
verwijzend. Gebruikersuitleg blijft in [docs/web-app.md](../../docs/web-app.md).
