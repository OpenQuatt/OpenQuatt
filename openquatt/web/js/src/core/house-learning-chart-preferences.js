export const CAPACITY_SUPPLY_CHOICES = [35, 45];
export const HOUSE_LEARNING_SERIES = ["configured", "records", "learned", "interval", "capacity"];
const SUPPLY_KEY = "oq-house-learning-capacity-supply";
const HIDDEN_SERIES_KEY = "oq-house-learning-hidden-series";

function readPreference(key) {
  try {
    return JSON.parse(window.localStorage.getItem(key));
  } catch (_error) {
    return null;
  }
}

function writePreference(key, value) {
  try {
    window.localStorage.setItem(key, JSON.stringify(value));
  } catch (_error) {
    // The current page's choices remain usable when browser storage is unavailable.
  }
}

export function readHouseLearningChartPreferences() {
  const supply = readPreference(SUPPLY_KEY);
  const hidden = readPreference(HIDDEN_SERIES_KEY);
  return {
    houseLearningCapacitySupplyC: CAPACITY_SUPPLY_CHOICES.includes(supply) ? supply : CAPACITY_SUPPLY_CHOICES[0],
    houseLearningHiddenSeries: Array.isArray(hidden) ? HOUSE_LEARNING_SERIES.filter(series => hidden.includes(series)) : [],
  };
}

export function rememberHouseCapacitySupply(supplyC) {
  writePreference(SUPPLY_KEY, supplyC);
}

export function rememberHiddenHouseLearningSeries(hiddenSeries) {
  writePreference(HIDDEN_SERIES_KEY, hiddenSeries);
}
