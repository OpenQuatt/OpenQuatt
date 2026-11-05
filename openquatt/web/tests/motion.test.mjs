import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
  matchMedia: () => ({ matches: false }),
};

const { syncMotionAnimations } = await import("../js/src/core/motion.js");
const { state } = await import("../js/src/core/state.js");

function createAnimatedNode(animations) {
  return { getAnimations: () => animations };
}

function createMotionRoot(nodes) {
  return {
    querySelectorAll(selector) {
      return selector === ".oq-hp-tech-pipe-flow, .oq-hp-tech-fan-blades" ? nodes : [];
    },
  };
}

test("recreated pipe and fan animations share the document timeline origin", (t) => {
  t.after(() => {
    state.root = null;
  });
  const flow = { animationName: "oq-hp-pipe-flow", startTime: 1840.5 };
  const fan = { animationName: "oq-hp-fan-spin", startTime: 1840.5 };
  const transition = { transitionProperty: "stroke", startTime: 1840.5 };
  state.root = createMotionRoot([createAnimatedNode([flow]), createAnimatedNode([fan, transition])]);

  assert.equal(syncMotionAnimations(), 2);
  assert.equal(flow.startTime, 0);
  assert.equal(fan.startTime, 0);
  assert.equal(transition.startTime, 1840.5, "transitions keep their own timing");
  assert.equal(syncMotionAnimations(), 0, "aligned animations are left alone");
});

test("a patched board aligns only its own animations and tolerates browsers without getAnimations", () => {
  const water = { animationName: "oq-hp-water-flow", startTime: 92 };
  const board = createMotionRoot([createAnimatedNode([water]), {}]);
  assert.equal(syncMotionAnimations(board), 1);
  assert.equal(water.startTime, 0);
  assert.equal(syncMotionAnimations(null), 0);
});
