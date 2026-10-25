// Correlate logged preparation/commands/feedback, never infer a compressor start.
// Keep retries, pumps and controller boots separate, including incomplete archives.
export function groupControlReplayStarts(events, timeOf) {
  const attempts = [];
  const pending = new Map();
  let preparation = [];
  let previous = null;
  const sorted = [...events].sort((a, b) => timeOf(a) - timeOf(b) || Number(a.seq) - Number(b.seq));
  const nearby = (a, b) => Number.isFinite(timeOf(a)) && Number.isFinite(timeOf(b))
    && timeOf(b) >= timeOf(a) && timeOf(b) - timeOf(a) <= 180000;
  for (const event of sorted) {
    const gap = previous && Number.isInteger(previous.seq) && Number.isInteger(event.seq)
      && event.seq !== previous.seq && event.seq !== ((previous.seq + 1) >>> 0);
    const reset = previous && Number.isFinite(previous.uptime_s) && Number.isFinite(event.uptime_s)
      && event.uptime_s < previous.uptime_s;
    if (gap || reset) {
      pending.clear();
      preparation = [];
    }
    previous = event;
    const type = event.event_type;
    const hp = String(event.subject || '').toUpperCase();
    if (type === 'boot_marker') {
      pending.clear();
      preparation = [];
      continue;
    }
    if (type === 'flow_hold_start' && event.reason === 'flow_preflow') {
      preparation = preparation.filter(item => item.event_type === 'control_mode_change'
        && Number(item.value_a) === 0 && Number(item.value_b) === 1
        && nearby(item, event) && timeOf(event) - timeOf(item) <= 1000);
    }
    const preparing = ((type === 'flow_hold_start' || type === 'flow_hold_clear') && event.reason === 'flow_preflow')
      || (type === 'control_mode_change' && ((Number(event.value_a) === 0 && Number(event.value_b) === 1)
        || (Number(event.value_a) === 1 && [2, 5].includes(Number(event.value_b)))))
      || (type === 'topology_change' && ['single', 'duo'].includes(event.to));
    if (preparing) {
      preparation = preparation.filter(item => nearby(item, event));
      preparation.push(event);
      continue;
    }
    if (!['HP1', 'HP2'].includes(hp)) continue;
    if (type === 'source_start') {
      const before = preparation.filter(item => nearby(item, event)
        && (item.event_type !== 'topology_change' || ['SYSTEM', 'BOTH', hp].includes(String(item.subject).toUpperCase())));
      const attempt = { subject: hp, command: event, events: [...before, event], confirmed: false, failed: false };
      preparation = [];
      attempts.push(attempt);
      pending.set(hp, attempt);
    } else if (type === 'hp_start_confirmed' || (type === 'incident_start' && event.reason === 'hp_start_failed')) {
      const attempt = pending.get(hp);
      if (attempt && nearby(attempt.command, event)) {
        attempt.events.push(event);
        attempt.confirmed = type === 'hp_start_confirmed';
        attempt.failed = !attempt.confirmed;
      }
      pending.delete(hp);
    } else if (type === 'source_stop' || type === 'hp_stop_confirmed') {
      pending.delete(hp);
      preparation = [];
    }
  }
  return attempts;
}
