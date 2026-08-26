import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { state } = await import("../js/src/core/state.js");
const { handleViewAction } = await import("../js/src/features/view-actions.js");
const { renderSettingsHistoryStorageModal } = await import("../js/src/settings/storage.js");

test("advanced storage details remain open across export renders", () => {
  const previous = {
    energyHistoryExportBusy: state.energyHistoryExportBusy,
    entities: state.entities,
    settingsStorageAdvancedOpen: state.settingsStorageAdvancedOpen,
    settingsStoragePage: state.settingsStoragePage,
  };

  try {
    state.entities = {
      lifetimeEnergyHistoryEnabled: { value: true },
    };
    state.settingsStoragePage = "energy";
    state.settingsStorageAdvancedOpen = false;

    let prevented = false;
    const button = {
      closest(selector) {
        assert.equal(selector, ".oq-settings-storage-advanced");
        return { hasAttribute: () => false };
      },
    };
    const event = {
      preventDefault() {
        prevented = true;
      },
    };

    assert.equal(handleViewAction("toggle-storage-advanced", button, event), true);
    assert.equal(prevented, true);
    assert.equal(state.settingsStorageAdvancedOpen, true);

    state.energyHistoryExportBusy = true;
    const html = renderSettingsHistoryStorageModal();
    assert.match(html, /<details class="oq-settings-storage-advanced" open><summary data-oq-action="toggle-storage-advanced">/);
  } finally {
    Object.assign(state, previous);
  }
});

test("diagnosis storage details expose full flush and index timing", () => {
  const previous = {
    entities: state.entities,
    settingsStoragePage: state.settingsStoragePage,
    trendHistoryMetadata: state.trendHistoryMetadata,
    trendHistoryMetadataSignature: state.trendHistoryMetadataSignature,
  };

  try {
    state.entities = {
      trendHistoryEnabled: { value: true },
      trendHistoryFlashEnabled: { value: true },
    };
    state.settingsStoragePage = "diagnosis";
    state.trendHistoryMetadataSignature = "test";
    state.trendHistoryMetadata = {
      available: "30 dagen",
      maxEraseDurationMs: 3,
      maxFlushDurationMs: 6,
      maxIndexUpdateDurationMs: 2,
      maxWriteDurationMs: 2,
    };

    const html = renderSettingsHistoryStorageModal();
    assert.match(html, /Langste volledige opslagactie<\/span>\s*<strong>6 ms<\/strong>/);
    assert.match(html, /Langste flashwrite<\/span>\s*<strong>2 ms<\/strong>/);
    assert.match(html, /Langste index-update<\/span>\s*<strong>2 ms<\/strong>/);
  } finally {
    Object.assign(state, previous);
  }
});
