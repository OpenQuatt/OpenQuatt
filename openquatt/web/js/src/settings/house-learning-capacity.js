export const CAPACITY_SUPPLY_CHOICES = [35, 45];

export function normalizeHouseCapacity(value) {
  const { flow_lph: flowLph, limit_w: limitW, cap_hz: capHz, points } = value ?? {};
  if (!Number.isInteger(flowLph) || flowLph < 300 || flowLph > 3000
      || !Number.isInteger(limitW) || limitW < 0 || limitW > 10000
      || !Number.isInteger(capHz) || capHz < 0 || capHz > 90
      || !Array.isArray(points) || points.length < 2 || points.length > 43) return null;
  let previous = -1501;
  for (const point of points) {
    if (!Array.isArray(point) || point.length !== 3) return null;
    const [centiC, ...watts] = point;
    if (!Number.isInteger(centiC) || centiC <= previous || centiC < -1500 || centiC > 2000
        || !watts.every(value => value === null || Number.isInteger(value) && value >= 0 && value <= 20000)) return null;
    previous = centiC;
  }
  return { flowLph, limitW, capHz, points };
}

export function getCapacitySegments(profile, supplyC) {
  const column = CAPACITY_SUPPLY_CHOICES.indexOf(supplyC) + 1;
  if (!profile || !column) return [];
  const segments = [];
  let segment = [];
  let previous = null;
  for (const point of profile.points) {
    const centiC = point[0];
    const watts = point[column];
    // V2 has distinct model cells immediately either side of certain outside
    // temperatures. A one-hundredth-degree pair marks those two sides.
    const boundary = previous !== null && (centiC - previous === 1 || centiC - previous > 100);
    if (watts === null || boundary) {
      if (segment.length) segments.push(segment);
      segment = [];
    }
    if (watts !== null) segment.push({ outsideC: centiC / 100, watts });
    previous = centiC;
  }
  if (segment.length) segments.push(segment);
  return segments;
}

const interpolate = (a, b, outsideC) => a.watts + (b.watts - a.watts) * (outsideC - a.outsideC) / (b.outsideC - a.outsideC);

function limitSegment(segment, [minC, maxC]) {
  const points = segment.filter(({ outsideC }) => outsideC >= minC && outsideC <= maxC);
  for (const [edge, insert] of [[minC, "unshift"], [maxC, "push"]]) {
    const next = segment.findIndex(point => point.outsideC >= edge);
    if (next > 0 && segment[next].outsideC > edge) {
      points[insert]({ outsideC: edge, watts: interpolate(segment[next - 1], segment[next], edge) });
    }
  }
  return points;
}

function intersection(a, b) {
  const fraction = a.margin / (a.margin - b.margin);
  return { outsideC: a.outsideC + fraction * (b.outsideC - a.outsideC), watts: a.watts + fraction * (b.watts - a.watts) };
}

export function compareCapacityToDemand(segments, demandAt, bounds = [-Infinity, Infinity], demandCutoffC = null) {
  const crossings = [];
  const reserves = [];
  for (const segment of segments) {
    const covered = limitSegment(segment, bounds);
    if (demandCutoffC !== null) {
      const next = covered.findIndex(point => point.outsideC >= demandCutoffC);
      if (next > 0 && covered[next].outsideC > demandCutoffC) {
        covered.splice(next, 0, { outsideC: demandCutoffC, watts: interpolate(covered[next - 1], covered[next], demandCutoffC) });
      }
    }
    const points = covered.map(point => ({ ...point, margin: point.watts - demandAt(point.outsideC) }));
    let previous = null;
    let missingCrossing = false;
    let equalPoints = 0;
    let equalPoint;
    let reserve = [];
    for (const point of points) {
      if (point.margin === 0) {
        equalPoints += 1;
        equalPoint = point;
        continue;
      }
      if (previous && (previous.margin < 0) !== (point.margin < 0)) {
        if (equalPoints > 1) missingCrossing = true;
        else crossings.push(equalPoints ? equalPoint : intersection(previous, point));
      }
      equalPoints = 0;
      previous = point;
    }
    if (missingCrossing) crossings.push(null);

    for (let index = 1; index < points.length; index += 1) {
      const a = points[index - 1], b = points[index];
      if (a.margin <= 0 && b.margin <= 0) continue;
      const from = a.margin < 0 ? intersection(a, b) : a;
      const to = b.margin < 0 ? intersection(a, b) : b;
      if (!reserve.length) reserve.push(from);
      reserve.push(to);
      if (b.margin <= 0) {
        reserves.push(reserve);
        reserve = [];
      }
    }
    if (reserve.length) reserves.push(reserve);
  }
  return { crossing: crossings.length === 1 ? crossings[0] : null, reserves, demandAt };
}
