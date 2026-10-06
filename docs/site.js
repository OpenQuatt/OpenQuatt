// Resolve old web-app bookmarks without allowing a malformed hash to stop navigation.
function followLegacyAnchor() {
  let anchor;
  try { anchor = decodeURIComponent(location.hash.slice(1)); } catch { return; }
  const link = document.getElementById(anchor);
  if (link?.dataset.legacyTarget) location.replace(link.dataset.legacyTarget);
}
followLegacyAnchor();
window.addEventListener("hashchange", followLegacyAnchor);

(() => {
  const body = document.body;
  const sidebarToggle = document.querySelector("[data-sidebar-toggle]");
  const sidebar = document.querySelector("[data-sidebar]");
  const sidebarBackdrop = document.querySelector("[data-sidebar-backdrop]");
  const sidebarLinks = [...document.querySelectorAll("[data-sidebar-link]"), ...document.querySelectorAll("[data-toc-link]")];
  const navToggles = [...document.querySelectorAll("[data-nav-toggle]")];
  const tocLinks = [...document.querySelectorAll("[data-toc-link]")];
  const searchOpenButton = document.querySelector("[data-search-open]");
  const searchModal = document.querySelector("[data-search-modal]");
  const searchInput = document.querySelector("[data-search-input]");
  const searchResults = document.querySelector("[data-search-results]");
  const searchCloseButtons = [...document.querySelectorAll("[data-search-close]")];
  const docsVersion = document.querySelector("[data-docs-version]");
  const focusableSelector = 'a[href], button:not([disabled]), input:not([disabled]), [tabindex]:not([tabindex="-1"])';
  let sidebarPreviousFocus = null;
  let searchPreviousFocus = null;
  let searchIndexPromise = null;

  function setPageInert(isInert, targets) {
    targets.filter(Boolean).forEach((target) => {
      target.inert = isInert;
    });
  }

  function getFocusableElements(container) {
    if (!container) {
      return [];
    }
    return [...container.querySelectorAll(focusableSelector)].filter((element) => !element.hidden && element.offsetParent !== null);
  }

  function setSidebarOpen(isOpen, { restoreFocus = true } = {}) {
    const mobile = window.innerWidth <= 960;
    const shouldOpen = Boolean(isOpen && mobile && sidebar);
    if (shouldOpen && !body.classList.contains("sidebar-open")) {
      sidebarPreviousFocus = document.activeElement;
    }
    body.classList.toggle("sidebar-open", shouldOpen);
    if (sidebarToggle) {
      sidebarToggle.setAttribute("aria-expanded", String(shouldOpen));
      const label = sidebarToggle.querySelector(".sr-only");
      if (label) {
        label.textContent = shouldOpen ? "Sluit navigatie" : "Open navigatie";
      }
    }
    setPageInert(shouldOpen, [
      document.querySelector(".site-brand"),
      document.querySelector(".site-header-actions"),
      document.querySelector(".docs-main"),
      document.querySelector(".page-rail"),
    ]);
    if (shouldOpen) {
      window.requestAnimationFrame(() => getFocusableElements(sidebar)[0]?.focus());
    } else if (restoreFocus && sidebarPreviousFocus instanceof HTMLElement) {
      sidebarPreviousFocus.focus();
    }
  }

  function normalizeSearchText(value) {
    return String(value || "")
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .toLowerCase();
  }

  function loadSearchIndex() {
    if (!searchIndexPromise) {
      const indexUrl = body.dataset.searchIndexUrl;
      searchIndexPromise = indexUrl
        ? fetch(indexUrl).then((response) => {
          if (!response.ok) {
            throw new Error(`Zoekindex gaf HTTP ${response.status}`);
          }
          return response.json();
        })
        : Promise.resolve([]);
    }
    return searchIndexPromise;
  }

  function createSearchResult(item) {
    const link = document.createElement("a");
    const indexUrl = new URL(body.dataset.searchIndexUrl, document.baseURI);
    link.className = "search-result";
    link.href = new URL(item.url, indexUrl).href;

    const title = document.createElement("span");
    title.className = "search-result-title";
    title.textContent = item.title;
    const summary = document.createElement("span");
    summary.className = "search-result-summary";
    summary.textContent = item.summary;
    const meta = document.createElement("span");
    meta.className = "search-result-meta";
    meta.textContent = item.kind;
    link.append(title, summary, meta);
    return link;
  }

  async function renderSearchResults(query) {
    if (!searchResults) {
      return;
    }
    const normalizedQuery = normalizeSearchText(query).trim();
    if (!normalizedQuery) {
      searchResults.innerHTML = '<p class="search-empty">Zoek op een onderwerp, schermnaam of foutmelding.</p>';
      return;
    }

    searchResults.innerHTML = '<p class="search-empty">Zoeken…</p>';
    try {
      const tokens = normalizedQuery.split(/\s+/).filter(Boolean);
      const index = await loadSearchIndex();
      const matches = index
        .map((item) => {
          const title = normalizeSearchText(item.title);
          const headings = normalizeSearchText((item.headings || []).join(" "));
          const summary = normalizeSearchText(item.summary);
          const allText = `${title} ${headings} ${summary} ${normalizeSearchText(item.text)}`;
          if (!tokens.every((token) => allText.includes(token))) {
            return null;
          }
          const score = tokens.reduce((total, token) => total
            + (title.includes(token) ? 8 : 0)
            + (headings.includes(token) ? 4 : 0)
            + (summary.includes(token) ? 2 : 0), 0);
          return { item, score };
        })
        .filter(Boolean)
        .sort((left, right) => right.score - left.score || left.item.title.localeCompare(right.item.title, "nl"))
        .slice(0, 8);

      searchResults.replaceChildren();
      if (!matches.length) {
        searchResults.innerHTML = '<p class="search-empty">Geen resultaten. Probeer een kortere of andere zoekterm.</p>';
        return;
      }
      matches.forEach(({ item }) => searchResults.appendChild(createSearchResult(item)));
    } catch (error) {
      console.warn("Zoekindex kon niet worden geladen", error);
      searchResults.innerHTML = '<p class="search-empty">Zoeken is nu niet beschikbaar. Gebruik de navigatie links.</p>';
    }
  }

  function setSearchOpen(isOpen) {
    if (!searchModal) {
      return;
    }
    if (isOpen && searchModal.hidden) {
      searchPreviousFocus = document.activeElement;
      setSidebarOpen(false, { restoreFocus: false });
    }
    searchModal.hidden = !isOpen;
    body.classList.toggle("search-open", isOpen);
    searchOpenButton?.setAttribute("aria-expanded", String(isOpen));
    setPageInert(isOpen, [document.querySelector(".site-header"), document.querySelector(".docs-shell")]);
    if (isOpen) {
      renderSearchResults(searchInput?.value || "");
      window.requestAnimationFrame(() => searchInput?.focus());
    } else if (searchPreviousFocus instanceof HTMLElement) {
      searchPreviousFocus.focus();
    }
  }

  function trapFocus(event, container, extraElements = []) {
    const elements = [...extraElements, ...getFocusableElements(container)].filter(Boolean);
    if (!elements.length) {
      return;
    }
    const first = elements[0];
    const last = elements[elements.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  }

  async function enhanceVersionContext() {
    if (!docsVersion || !body.dataset.versionUrl) {
      return;
    }
    try {
      const response = await fetch(body.dataset.versionUrl);
      if (!response.ok) {
        return;
      }
      const metadata = await response.json();
      if (metadata.version) {
        const label = metadata.channel === "local" ? "Lokale preview" : "Firmware stable";
        docsVersion.textContent = `${label}: ${metadata.version} · docs vanaf main`;
      }
    } catch (_error) {
      // De fallbacktekst blijft bruikbaar in losse of offline previews.
    }
  }

  function activateToc(hash) {
    tocLinks.forEach((link) => {
      link.classList.toggle("is-active", link.getAttribute("href") === `#${hash}`);
    });
  }

  function toggleNavSection(button) {
    const panelId = button.getAttribute("aria-controls");
    const panel = panelId ? document.getElementById(panelId) : null;
    const expanded = button.getAttribute("aria-expanded") === "true";
    button.setAttribute("aria-expanded", String(!expanded));
    if (panel) {
      panel.hidden = expanded;
    }
  }

  async function copyCode(button, pre) {
    const code = pre.querySelector("code");
    const text = code ? code.innerText : pre.innerText;
    if (!text || !navigator.clipboard) {
      return;
    }
    try {
      await navigator.clipboard.writeText(text);
      const originalLabel = button.textContent;
      button.textContent = "Gekopieerd";
      window.setTimeout(() => {
        button.textContent = originalLabel;
      }, 1400);
    } catch (error) {
      console.warn("Kon code niet kopieren", error);
    }
  }

  function enhanceCodeBlocks() {
    document.querySelectorAll("pre").forEach((pre) => {
      if (pre.dataset.enhanced === "true") {
        return;
      }
      pre.dataset.enhanced = "true";
      const button = document.createElement("button");
      button.type = "button";
      button.className = "code-copy";
      button.textContent = "Kopiëren";
      button.setAttribute("aria-live", "polite");
      button.addEventListener("click", () => {
        copyCode(button, pre);
      });
      pre.appendChild(button);
    });
  }

  function enhanceCableStepper() {
    const fallbackImage = document.querySelector('.page-q-edition img[src$="q-edition-kabels-stappen.svg"]');
    const fallbackContainer = fallbackImage?.closest("p");
    if (!fallbackImage || !fallbackContainer || fallbackImage.dataset.stepperEnhanced === "true") {
      return;
    }

    const steps = [
      {
        number: "01",
        label: "Voorbereiden",
        file: "q-edition-kabelstap-1-voorbereiden.svg",
        mobileFile: "q-edition-kabelstap-1-voorbereiden-mobile.svg",
        height: 332,
        mobileHeight: 650,
        alt: "Stap 1 van 7: installatie spanningsloos maken, fotograferen en kabels labelen.",
      },
      {
        number: "02",
        label: "Modbus",
        file: "q-edition-kabelstap-2-modbus.svg",
        mobileFile: "q-edition-kabelstap-2-modbus-mobile.svg",
        height: 405,
        mobileHeight: 720,
        alt: "Stap 2 van 7: Modbus A, G en B van de CiC verplaatsen naar M1 van de HCQ.",
      },
      {
        number: "03",
        label: "Thermostaat",
        file: "q-edition-kabelstap-3-thermostaat.svg",
        mobileFile: "q-edition-kabelstap-3-thermostaat-mobile.svg",
        height: 345,
        mobileHeight: 630,
        alt: "Stap 3 van 7: de twee OpenTherm-aders van de kamerthermostaat verplaatsen naar OTT.",
      },
      {
        number: "04",
        label: "CV-ketel",
        statusLabel: "Kies je ketelroute",
        routes: [
          {
            id: "opentherm",
            label: "OpenTherm",
            target: "OTB",
            file: "q-edition-kabelstap-4a-cv-ketel-opentherm.svg",
            mobileFile: "q-edition-kabelstap-4a-cv-ketel-opentherm-mobile.svg",
            height: 390,
            mobileHeight: 720,
            alt: "Stap 4 van 7, OpenTherm-route: de twee OpenTherm-aders van de CV-ketel verplaatsen van de CiC naar OTB van de HCQ.",
          },
          {
            id: "on-off",
            label: "Aan/uit",
            target: "R1 · COM + NO",
            file: "q-edition-kabelstap-4b-cv-ketel-aan-uit.svg",
            mobileFile: "q-edition-kabelstap-4b-cv-ketel-aan-uit-mobile.svg",
            height: 390,
            mobileHeight: 720,
            alt: "Stap 4 van 7, aan-uitroute: de twee aders van het CV-aan-uit-contact verplaatsen van de CiC naar COM en NO van R1.",
          },
        ],
      },
      {
        number: "05",
        label: "Sensor",
        file: "q-edition-kabelstap-5-sensor.svg",
        mobileFile: "q-edition-kabelstap-5-sensor-mobile.svg",
        height: 345,
        mobileHeight: 630,
        alt: "Stap 5 van 7: de bestaande Quatt flow- of temperatuursensorkabel verplaatsen naar Q.",
      },
      {
        number: "06",
        label: "Quatt-app via CiC",
        file: "q-edition-kabelstap-6-cic-compatibiliteit.svg",
        mobileFile: "q-edition-kabelstap-6-cic-compatibiliteit-mobile.svg",
        height: 405,
        mobileHeight: 800,
        alt: "Stap 6 van 7, optioneel: M2 met een aparte RS485-kabel verbinden met de vrijgekomen Modbuspoort van de CiC.",
      },
      {
        number: "07",
        label: "Eindcontrole",
        file: "q-edition-kabelstap-7-eindcontrole.svg",
        mobileFile: "q-edition-kabelstap-7-eindcontrole-mobile.svg",
        height: 329,
        mobileHeight: 650,
        alt: "Stap 7 van 7: alle aansluitingen controleren voordat de installatie wordt ingeschakeld.",
      },
    ];

    const fallbackSource = fallbackImage.getAttribute("src") || "";
    const assetPrefix = fallbackSource.slice(0, fallbackSource.lastIndexOf("/") + 1);
    const mobileViewportQuery = window.matchMedia("(max-width: 720px)");
    steps.flatMap((step) => step.routes || [step]).forEach((step) => {
      step.src = `${assetPrefix}${step.file}`;
      step.mobileSrc = `${assetPrefix}${step.mobileFile}`;
      const preload = new Image();
      preload.src = step.src;
      const mobilePreload = new Image();
      mobilePreload.src = step.mobileSrc;
    });

    const stepper = document.createElement("section");
    stepper.className = "cable-stepper";
    stepper.dataset.cableStepper = "";
    stepper.setAttribute("aria-label", "Heatpump Controller Q-edition aansluiten in zeven stappen");
    stepper.innerHTML = `
      <header class="cable-stepper-header">
        <div>
          <p class="cable-stepper-kicker">Aansluiten</p>
          <h4>Doorloop de zeven stappen</h4>
        </div>
        <div class="cable-stepper-header-tools">
          <p class="cable-stepper-status" data-cable-step-status aria-live="polite"></p>
          <button class="cable-stepper-zoom" type="button" data-cable-step-zoom aria-haspopup="dialog">
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5" />
            </svg>
            Vergroot
          </button>
        </div>
      </header>
      <div class="cable-stepper-progress" data-cable-step-progress role="progressbar" aria-label="Voortgang aansluitstappen" aria-valuemin="1" aria-valuemax="7">
        <span></span>
      </div>
      <div class="cable-stepper-tabs" data-cable-step-tabs role="tablist" aria-label="Kies een aansluitstap" aria-orientation="horizontal"></div>
      <div class="cable-stepper-canvas" data-cable-step-canvas>
        <div class="cable-stepper-viewport" id="q-edition-cable-step" role="tabpanel"></div>
      </div>
      <footer class="cable-stepper-actions">
        <button class="cable-stepper-button cable-stepper-button-secondary" type="button" data-cable-step-previous>Vorige</button>
        <button class="cable-stepper-button cable-stepper-button-primary" type="button" data-cable-step-next>Volgende</button>
      </footer>
    `;

    const tabsContainer = stepper.querySelector("[data-cable-step-tabs]");
    const viewport = stepper.querySelector(".cable-stepper-viewport");
    const canvas = stepper.querySelector("[data-cable-step-canvas]");
    const status = stepper.querySelector("[data-cable-step-status]");
    const progress = stepper.querySelector("[data-cable-step-progress]");
    const progressBar = progress?.querySelector("span");
    const previousButton = stepper.querySelector("[data-cable-step-previous]");
    const nextButton = stepper.querySelector("[data-cable-step-next]");
    const zoomButton = stepper.querySelector("[data-cable-step-zoom]");
    if (!tabsContainer || !viewport || !canvas || !status || !progress || !progressBar || !previousButton || !nextButton || !zoomButton) {
      return;
    }

    const dialog = document.createElement("dialog");
    dialog.className = "cable-stepper-dialog";
    dialog.setAttribute("aria-label", "Aansluitplan Heatpump Controller Q-edition vergroot");
    dialog.innerHTML = `
      <div class="cable-stepper-dialog-shell">
        <button class="cable-stepper-dialog-close" type="button" aria-label="Vergrote weergave sluiten">×</button>
        <div class="cable-stepper-dialog-content"></div>
      </div>
    `;
    document.body.appendChild(dialog);

    const dialogContent = dialog.querySelector(".cable-stepper-dialog-content");
    const dialogCloseButton = dialog.querySelector(".cable-stepper-dialog-close");
    const stepperPlaceholder = document.createComment("cable-stepper-position");
    if (!dialogContent || !dialogCloseButton) {
      dialog.remove();
      return;
    }

    const stepGraphic = document.createElement("object");
    stepGraphic.dataset.stepperEnhanced = "true";
    stepGraphic.className = "cable-stepper-image";
    stepGraphic.type = "image/svg+xml";
    stepGraphic.data = steps[0].src;
    stepGraphic.setAttribute("role", "img");
    stepGraphic.setAttribute("aria-label", steps[0].alt);
    stepGraphic.textContent = steps[0].alt;

    const routeChoice = document.createElement("div");
    routeChoice.className = "cable-stepper-route-choice";
    routeChoice.hidden = true;
    routeChoice.setAttribute("role", "group");
    routeChoice.setAttribute("aria-labelledby", "q-edition-ketelroute-title");
    routeChoice.innerHTML = `
      <p class="cable-stepper-route-kicker">04 · CV-ketel</p>
      <h5 id="q-edition-ketelroute-title">Kijk hoe de kabel naar de CV-ketel is aangesloten.</h5>
      <p class="cable-stepper-route-copy">Laat de kabel nog op de CiC zitten en vergelijk de aansluiting met de twee voorbeelden.</p>
      <div class="cable-stepper-route-options">
        <button type="button" class="cable-stepper-route-option" data-cable-route="opentherm">
          <img src="./assets/q-edition-ketelroute-opentherm.svg" alt="" aria-hidden="true" />
          <span class="cable-stepper-route-option-copy">
            <span>Route A</span>
            <strong>OpenTherm</strong>
            <small>Twee aders op de onderste CiC-aansluiting met het vlam- en OT-symbool.</small>
            <em>Dit is mijn aansluiting →</em>
          </span>
        </button>
        <button type="button" class="cable-stepper-route-option" data-cable-route="on-off">
          <img src="./assets/q-edition-ketelroute-aan-uit.svg" alt="" aria-hidden="true" />
          <span class="cable-stepper-route-option-copy">
            <span>Route B</span>
            <strong>Aan/uit</strong>
            <small>Twee aders aan de rechterkant van de CiC, onder het aparte afdekkapje.</small>
            <em>Dit is mijn aansluiting →</em>
          </span>
        </button>
      </div>
      <p class="cable-stepper-route-warning">Kies één van de twee. Sluit de CV-ketel straks niet op beide manieren aan.</p>
    `;
    viewport.append(routeChoice, stepGraphic);

    const tabs = steps.map((step, index) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "cable-stepper-tab";
      button.id = `q-edition-cable-step-tab-${index + 1}`;
      button.dataset.cableStepIndex = String(index);
      button.setAttribute("role", "tab");
      button.setAttribute("aria-controls", "q-edition-cable-step");
      button.innerHTML = `<span>${step.number}</span><strong>${step.label}</strong>`;
      tabsContainer.appendChild(button);
      return button;
    });

    let activeStep = 0;
    let selectedBoilerRoute = "";
    let animationTimer = 0;

    function showStep(index, { focusTab = false, animate = true } = {}) {
      const boundedIndex = Math.max(0, Math.min(steps.length - 1, index));
      const step = steps[boundedIndex];
      const selectedRoute = step.routes?.find((route) => route.id === selectedBoilerRoute) || null;
      const displayedStep = selectedRoute || step;
      const choosingRoute = Boolean(step.routes && !selectedRoute);
      const mobileView = mobileViewportQuery.matches;
      const source = mobileView ? displayedStep.mobileSrc : displayedStep.src;
      const width = mobileView ? 430 : 936;
      const height = mobileView ? displayedStep.mobileHeight : displayedStep.height;
      const logicalStepNumber = step.number.replace(/^0/, "").toLowerCase();
      const logicalProgress = Number.parseInt(step.number, 10);
      activeStep = boundedIndex;
      stepper.dataset.activeStep = String(boundedIndex + 1);
      stepper.dataset.mobileView = String(mobileView);
      stepper.dataset.boilerRoute = selectedRoute?.id || "";

      routeChoice.hidden = !choosingRoute;
      stepGraphic.hidden = choosingRoute;
      if (choosingRoute) {
        viewport.style.aspectRatio = "auto";
      } else {
        viewport.style.aspectRatio = `${width} / ${height}`;
        stepGraphic.data = source;
        stepGraphic.setAttribute("aria-label", displayedStep.alt);
        stepGraphic.textContent = displayedStep.alt;
      }
      const statusLabel = selectedRoute ? `${selectedRoute.label} → ${selectedRoute.target}` : step.statusLabel || step.label;
      status.textContent = `Stap ${logicalStepNumber} van 7 · ${statusLabel}`;
      progress.setAttribute("aria-valuenow", String(logicalProgress));
      progress.setAttribute("aria-valuetext", `Stap ${logicalStepNumber} van 7: ${statusLabel}`);
      progressBar.style.width = `${(logicalProgress / 7) * 100}%`;
      tabs.forEach((tab, tabIndex) => {
        const selected = tabIndex === boundedIndex;
        tab.setAttribute("aria-selected", String(selected));
        tab.tabIndex = selected ? 0 : -1;
      });
      viewport.setAttribute("aria-labelledby", tabs[boundedIndex].id);

      previousButton.disabled = boundedIndex === 0;
      previousButton.textContent = selectedRoute ? "Andere route" : "Vorige";
      nextButton.disabled = choosingRoute || boundedIndex === steps.length - 1;
      nextButton.textContent = choosingRoute ? "Kies een route" : boundedIndex === steps.length - 1 ? "Klaar" : "Volgende";

      window.clearTimeout(animationTimer);
      viewport.classList.remove("is-entering");
      if (animate) {
        window.requestAnimationFrame(() => {
          viewport.classList.add("is-entering");
          animationTimer = window.setTimeout(() => viewport.classList.remove("is-entering"), 240);
        });
      }

      if (focusTab) {
        tabs[boundedIndex].focus();
      }
    }

    tabs.forEach((tab, index) => {
      tab.addEventListener("click", () => showStep(index));
    });

    routeChoice.querySelectorAll("[data-cable-route]").forEach((button) => {
      button.addEventListener("click", () => {
        selectedBoilerRoute = button.dataset.cableRoute || "";
        showStep(activeStep);
      });
    });

    tabsContainer.addEventListener("keydown", (event) => {
      let targetIndex = activeStep;
      if (event.key === "ArrowRight") {
        targetIndex = Math.min(steps.length - 1, activeStep + 1);
      } else if (event.key === "ArrowLeft") {
        targetIndex = Math.max(0, activeStep - 1);
      } else if (event.key === "Home") {
        targetIndex = 0;
      } else if (event.key === "End") {
        targetIndex = steps.length - 1;
      } else {
        return;
      }
      event.preventDefault();
      showStep(targetIndex, { focusTab: true });
    });

    previousButton.addEventListener("click", () => {
      if (steps[activeStep].routes && selectedBoilerRoute) {
        selectedBoilerRoute = "";
        showStep(activeStep);
        return;
      }
      showStep(activeStep - 1);
    });
    nextButton.addEventListener("click", () => showStep(activeStep + 1));
    mobileViewportQuery.addEventListener("change", () => showStep(activeStep, { animate: false }));

    function restoreStepper() {
      if (stepperPlaceholder.isConnected) {
        stepperPlaceholder.replaceWith(stepper);
      }
      zoomButton.focus();
    }

    function closeDialog() {
      dialog.close();
      restoreStepper();
    }

    zoomButton.addEventListener("click", () => {
      stepper.before(stepperPlaceholder);
      dialogContent.appendChild(stepper);
      dialog.showModal();
      dialogCloseButton.focus();
    });

    dialogCloseButton.addEventListener("click", closeDialog);
    dialog.addEventListener("click", (event) => {
      if (event.target === dialog) {
        closeDialog();
      }
    });
    dialog.addEventListener("cancel", (event) => {
      event.preventDefault();
      closeDialog();
    });
    dialog.addEventListener("close", restoreStepper);

    fallbackContainer.replaceWith(stepper);
    showStep(0, { animate: false });
  }

  function enhanceWifiRoutes() {
    const routeAHeading = document.getElementById("route-a-via-usb");
    const routeBHeading = document.getElementById("route-b-via-het-openquatt-access-point");
    if (!routeAHeading || !routeBHeading || routeAHeading.dataset.routeEnhanced === "true") {
      return;
    }

    const routeANodes = [];
    let cursor = routeAHeading;
    while (cursor && cursor !== routeBHeading) {
      routeANodes.push(cursor);
      cursor = cursor.nextSibling;
    }

    const routeBNodes = [];
    cursor = routeBHeading;
    while (cursor && !(cursor.nodeType === Node.ELEMENT_NODE && cursor.tagName === "H2")) {
      routeBNodes.push(cursor);
      cursor = cursor.nextSibling;
    }

    const routeOptions = document.createElement("div");
    routeOptions.className = "wifi-route-options";
    routeOptions.setAttribute("aria-label", "Kies een route om Wi-Fi in te stellen");
    routeAHeading.before(routeOptions);

    function createRoute(kind, nodes) {
      const section = document.createElement("section");
      section.className = "wifi-route";
      section.dataset.wifiRoute = kind;
      const recommendation = document.createElement("p");
      recommendation.className = "wifi-route-recommendation";
      section.appendChild(recommendation);
      nodes.forEach((node) => section.appendChild(node));
      return { section, recommendation };
    }

    const usbRoute = createRoute("usb", routeANodes);
    const accessPointRoute = createRoute("access-point", routeBNodes);
    const mobileRouteQuery = window.matchMedia("(max-width: 720px)");

    function arrangeRoutes() {
      const mobile = mobileRouteQuery.matches;
      const preferred = mobile ? accessPointRoute : usbRoute;
      const secondary = mobile ? usbRoute : accessPointRoute;
      preferred.section.classList.add("is-recommended");
      secondary.section.classList.remove("is-recommended");
      preferred.recommendation.textContent = mobile ? "Aanbevolen op telefoon of tablet" : "Aanbevolen op computer";
      secondary.recommendation.textContent = "Alternatieve route";
      routeOptions.replaceChildren(preferred.section, secondary.section);
    }

    routeAHeading.dataset.routeEnhanced = "true";
    arrangeRoutes();
    mobileRouteQuery.addEventListener("change", arrangeRoutes);
  }

  if (sidebarToggle) {
    sidebarToggle.addEventListener("click", () => {
      setSidebarOpen(!body.classList.contains("sidebar-open"));
    });
  }

  sidebarBackdrop?.addEventListener("click", () => {
    setSidebarOpen(false);
  });

  sidebarLinks.forEach((link) => {
    link.addEventListener("click", () => {
      setSidebarOpen(false, { restoreFocus: false });
    });
  });

  searchOpenButton?.addEventListener("click", () => {
    setSearchOpen(true);
  });

  searchCloseButtons.forEach((button) => {
    button.addEventListener("click", () => {
      setSearchOpen(false);
    });
  });

  searchInput?.addEventListener("input", () => {
    renderSearchResults(searchInput.value);
  });

  document.addEventListener("keydown", (event) => {
    const target = event.target;
    const editing = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target?.isContentEditable;
    if (event.key === "/" && !editing && !event.metaKey && !event.ctrlKey && !event.altKey) {
      event.preventDefault();
      setSearchOpen(true);
      return;
    }
    if (event.key === "Escape") {
      if (!searchModal?.hidden) {
        event.preventDefault();
        setSearchOpen(false);
      } else if (body.classList.contains("sidebar-open")) {
        event.preventDefault();
        setSidebarOpen(false);
      }
      return;
    }
    if (event.key === "Tab" && !searchModal?.hidden) {
      trapFocus(event, searchModal);
    } else if (event.key === "Tab" && body.classList.contains("sidebar-open")) {
      trapFocus(event, sidebar, [sidebarToggle]);
    }
  });

  navToggles.forEach((button) => {
    button.addEventListener("click", () => {
      toggleNavSection(button);
    });
  });

  window.addEventListener("resize", () => {
    if (window.innerWidth > 960) {
      setSidebarOpen(false, { restoreFocus: false });
    }
  });

  if (tocLinks.length > 0 && "IntersectionObserver" in window) {
    const sections = tocLinks
      .map((link) => document.getElementById((link.getAttribute("href") || "").replace(/^#/, "")))
      .filter(Boolean);

    if (sections.length > 0) {
      activateToc(window.location.hash.replace(/^#/, "") || sections[0].id);

      const observer = new IntersectionObserver(
        (entries) => {
          const visible = entries
            .filter((entry) => entry.isIntersecting)
            .sort((left, right) => left.boundingClientRect.top - right.boundingClientRect.top);

          if (visible[0]) {
            activateToc(visible[0].target.id);
          }
        },
        {
          rootMargin: "-120px 0px -70% 0px",
          threshold: [0, 1],
        },
      );

      sections.forEach((section) => observer.observe(section));
    }
  }

  window.addEventListener("hashchange", () => {
    activateToc(window.location.hash.replace(/^#/, ""));
  });

  enhanceCodeBlocks();
  enhanceCableStepper();
  enhanceWifiRoutes();
  enhanceVersionContext();
})();
