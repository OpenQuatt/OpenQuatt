export async function fetchWithTimeout(input, options = {}, timeoutMs = 0, timeoutMessage = "", consumeResponse = null, runtime = {}) {
  const fetchImplementation = typeof runtime.fetch === "function" ? runtime.fetch : fetch;
  const timerHost = runtime.timerHost || window;
  if (typeof AbortController !== "function" || !Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    const response = await fetchImplementation(input, options);
    return typeof consumeResponse === "function" ? consumeResponse(response) : response;
  }

  const controller = new AbortController();
  const timeoutId = timerHost.setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetchImplementation(input, { ...options, signal: controller.signal });
    return typeof consumeResponse === "function" ? await consumeResponse(response) : response;
  } catch (error) {
    if (controller.signal.aborted) {
      throw new Error(timeoutMessage || `request timed out after ${timeoutMs}ms`);
    }
    throw error;
  } finally {
    timerHost.clearTimeout(timeoutId);
  }
}

export async function copyTextToClipboard(text) {
  if (!text) {
    return false;
  }
  // Prefer the async Clipboard API whenever exposed; fall through to the
  // legacy path when it rejects (e.g. permissions) instead of failing hard.
  const clipboard = window.navigator?.clipboard;
  if (clipboard && typeof clipboard.writeText === "function") {
    try {
      await clipboard.writeText(text);
      return true;
    } catch (_error) {
      // Continue with the legacy fallback below.
    }
  }

  try {
    const focusOrigin = document.activeElement;
    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.top = "-1000px";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    textarea.focus({ preventScroll: true });
    textarea.select();

    let success = false;
    try {
      success = document.execCommand("copy") === true;
    } finally {
      document.body.removeChild(textarea);
      if (focusOrigin?.isConnected && typeof focusOrigin.focus === "function") {
        focusOrigin.focus({ preventScroll: true });
      }
    }
    return success;
  } catch (_error) {
    return false;
  }
}

export function downloadBlobFile(blob, filename) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.rel = "noreferrer";
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

export function downloadTextFile(filename, text, type = "text/plain") {
  downloadBlobFile(new Blob([text], { type }), filename);
}

export function downloadJsonFile(filename, payload) {
  downloadTextFile(filename, JSON.stringify(payload, null, 2), "application/json");
}
