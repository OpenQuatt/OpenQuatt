import "./views/shell.js";
import { handleChange, handleClick, handleFocusChange, handleInput, handleKeyDown, handlePointerDown, handlePointerMove, handlePointerUp, handleSettingsInteractionEnd, handleSettingsInteractionStart, handleWheel } from "./core/entity-actions.js";
import { FAVICON_DATA_URL } from "./core/embedded-assets.js";
import { setEventHandlers } from "./core/event-handlers.js";
import { boot } from "./core/runtime.js";
import { applyLocaleToDocument } from "./i18n/index.js";

function ensureFavicon() {
  if (!document.head) {
    return;
  }

  let favicon = document.head.querySelector('link[rel~="icon"]');
  if (!favicon) {
    favicon = document.createElement("link");
    favicon.rel = "icon";
    document.head.appendChild(favicon);
  }
  favicon.type = "image/svg+xml";
  favicon.href = FAVICON_DATA_URL;
}

setEventHandlers({
  handleChange,
  handleClick,
  handleFocusChange,
  handleInput,
  handleKeyDown,
  handlePointerDown,
  handlePointerMove,
  handlePointerUp,
  handleSettingsInteractionEnd,
  handleSettingsInteractionStart,
  handleWheel,
});

ensureFavicon();
applyLocaleToDocument();
boot();
