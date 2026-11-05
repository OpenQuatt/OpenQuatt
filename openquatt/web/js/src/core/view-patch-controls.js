let viewPatchControls = {};

export function setViewPatchControls(controls = {}) {
  Object.entries(controls).forEach(([key, value]) => {
    if (typeof value === "function") {
      viewPatchControls[key] = value;
    }
  });
}

export function patchEnergyDom() {
  return viewPatchControls.patchEnergyDom ? viewPatchControls.patchEnergyDom() : false;
}

export function patchResultsDom() {
  return viewPatchControls.patchResultsDom ? viewPatchControls.patchResultsDom() : false;
}

export function patchOverviewDom() {
  return viewPatchControls.patchOverviewDom ? viewPatchControls.patchOverviewDom() : false;
}

export function patchControlReplayDom() {
  return viewPatchControls.patchControlReplayDom ? viewPatchControls.patchControlReplayDom() : false;
}

export function patchDiagnosisDom() {
  return viewPatchControls.patchDiagnosisDom ? viewPatchControls.patchDiagnosisDom() : false;
}

export function patchLearningDom() {
  return viewPatchControls.patchLearningDom ? viewPatchControls.patchLearningDom() : false;
}
