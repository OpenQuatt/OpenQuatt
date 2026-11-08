import { state } from "./state.js";

export function clearLegacyMotionVariables() {
  if (!state.root) {
    return;
  }

  state.root.style.removeProperty("--oq-flow-offset");
  state.root.style.removeProperty("--oq-flow-offset-reverse");
  state.root.style.removeProperty("--oq-fan-rotation");
  if (!state.root.getAttribute("style")) {
    state.root.removeAttribute("style");
  }
}

// Pipe flow and fan motion are CSS animations. A full render recreates the schematic, which would restart
// them from the first frame; anchoring every one to the document timeline origin keeps the motion continuous.
export function syncMotionAnimations(root = state.root) {
  if (!root || typeof root.querySelectorAll !== "function") {
    return 0;
  }

  let aligned = 0;
  root.querySelectorAll(".oq-hp-tech-pipe-flow, .oq-hp-tech-fan-blades").forEach((node) => {
    if (typeof node.getAnimations !== "function") {
      return;
    }
    node.getAnimations().forEach((animation) => {
      if (animation.animationName && animation.startTime !== 0) {
        animation.startTime = 0;
        aligned += 1;
      }
    });
  });
  return aligned;
}
