import { fetchWithTimeout } from "./browser-utils.js";

export const WEB_APP_CACHE_REFRESH_TIMEOUT_MS = 10000;
export const WEB_APP_STATIC_ASSET_URLS = Object.freeze(["/0.css", "/0.js"]);

export function getWebAppCacheRefreshUrls() {
  const currentDocumentUrl = typeof window.location?.href === "string" && window.location.href
    ? window.location.href
    : "/";
  return [...new Set([currentDocumentUrl, ...WEB_APP_STATIC_ASSET_URLS])];
}

async function refreshWebAppResource(url) {
  // `reload` replaces a stale HTTP-cache entry; consuming the full body completes that refresh before navigation.
  return fetchWithTimeout(
    url,
    { cache: "reload", credentials: "same-origin" },
    WEB_APP_CACHE_REFRESH_TIMEOUT_MS,
    `${url} cache refresh timed out after ${WEB_APP_CACHE_REFRESH_TIMEOUT_MS}ms`,
    async (response) => {
      if (!response.ok) {
        throw new Error(`${url} cache refresh HTTP ${response.status}`);
      }
      await response.arrayBuffer();
    },
  );
}

export async function refreshWebAppCache() {
  const results = await Promise.allSettled(getWebAppCacheRefreshUrls().map(refreshWebAppResource));
  return results.every((result) => result.status === "fulfilled");
}
