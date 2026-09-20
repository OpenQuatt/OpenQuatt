export const BOILER_OPENTHERM_CAPABILITY = Object.freeze({
  UNKNOWN: "unknown",
  SUPPORTED: "supported",
  UNSUPPORTED: "unsupported",
});

export function getBoilerOpenThermCapability({
  linkEntityPresent = false,
  linkEntityConfirmedMissing = false,
} = {}) {
  if (linkEntityPresent) {
    return BOILER_OPENTHERM_CAPABILITY.SUPPORTED;
  }
  if (linkEntityConfirmedMissing) {
    return BOILER_OPENTHERM_CAPABILITY.UNSUPPORTED;
  }
  return BOILER_OPENTHERM_CAPABILITY.UNKNOWN;
}

export function getSupportedBoilerConnectionOptions(options = [], capability = BOILER_OPENTHERM_CAPABILITY.UNKNOWN) {
  if (capability === BOILER_OPENTHERM_CAPABILITY.SUPPORTED) {
    return [...options];
  }
  return options.filter((option) => option !== "OpenTherm");
}

