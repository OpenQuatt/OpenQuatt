import assert from 'node:assert/strict';
import test from 'node:test';
import { cycleCounters, defrostObservation, defrostPreflight, runDefrostScenarios } from './scenarios/defrost.mjs';
import { parseDefrostArgs } from '../../scripts/hil/run-defrost.mjs';

function fixture(fault) {
  let time = 0, heating = false, peer = false, enabled = false, flow = 800;
  let loaded = false, state = 'IDLE', ticks = 0, active = false;
  let started = 0, completed = 0, aborted = 0, triggers = 0, completedReads = 0;
  const writes = [];
  const token = 'abcdef0123456789';
  function snapshot(hp) {
    if (hp === 1 && active && ++ticks > 3) { active = false; completed++; state = 'COMPLETE'; }
    if (hp === 1 && completed && ++completedReads === 4 && fault === 'duplicate') started++;
    const busy = hp === 1 ? active : peer;
    const guard = hp === 1 && peer ? 'PEER_DEFROST_ACTIVE' : flow < 250 && enabled ? 'INCIDENT_BLOCK' : 'READY';
    return {hp,online:true,fresh:true,identity_ready:true,loaded,auto_defrost_control_ok:true,
      busy,active:busy,manual:hp === 1 && active,can_trigger:loaded && !busy && guard === 'READY',
      operation_mode:busy ? 4 : heating ? 2 : 0,state:hp === 1 ? state : peer ? 'ACTIVE' : 'COMPLETE',guard,
      csrf_token:token};
  }
  const value = (name) => {
    if (name === 'HIL Defrost Flow Fixture Enable') return enabled;
    if (name === 'Boiler active') return false;
    if (name === 'Flow average (Selected)') return enabled ? flow : heating ? 800 : 0;
    if (name === 'HP1 - Working Mode') return active ? 4 : heating ? 2 : 0;
    if (['HP1 - Defrost','HP1 - 4-Way valve','HP1 - Bottom plate heater'].includes(name)) {
      return fault === 'missing-bit' && name === 'HP1 - Defrost' ? false : active;
    }
    const inputs = {'Outside Temperature (Selected)':7,'Room Temperature (Selected)':20,
      'Room Setpoint (Selected)':24,'External Heat Demand (Selected)':4000,
      'Heating Enable (Selected)':true,'Heating Enable Valid':true};
    return inputs[name];
  };
  const controller = {
    baseUrl:'http://controller',
    async value(_domain,name) { return value(name); },
    async values(settings) { return Object.fromEntries(settings.map(item=>[item.key,value(item.name)])); },
    async setSelect(name, option) { if (name === 'CM Override') heating = option === 'Auto'; },
    async setNumber(name, number) { if (name === 'HIL Defrost Flow Fixture') flow = number; },
    async setSwitch(name, on) { writes.push([name,on]); if(name === 'HIL Defrost Flow Fixture Enable') enabled = on; },
    async request(endpoint, options = {}) {
      if (endpoint === '/openquatt/incidents') return {schema_version:1,catalog_version:1,
        system:{control_mode:heating ? 2 : 0,boiler_command_active:false},
        heat_pumps:[1,2].map(index=>({index,link_state:'healthy',available_for_start:true,
          running_confirmed:heating,stop_confirmed:!heating,stop_unconfirmed:false,
          stop_unconfirmed_due_to_link_loss:false,must_stop:false}))};
      const match = /hp([12])\/(status|load|trigger)$/.exec(endpoint);
      assert.ok(match);
      const hp = Number(match[1]);
      if (match[2] === 'status') return snapshot(hp);
      assert.equal(options.headers.Origin,controller.baseUrl);
      assert.equal(new URLSearchParams(options.body).get('csrf_token'),token);
      if (match[2] === 'load') { loaded = true; state = 'LOADED'; }
      else {
        triggers++;
        if (peer) state = 'PEER_DEFROST_ACTIVE';
        else if (enabled && flow < 250) state = 'INCIDENT_BLOCK';
        else { active = true; ticks = 0; started++; state = 'ACCEPTED'; }
        if (fault === 'ack') throw new Error('lost trigger ACK');
        if (fault === 'early' && enabled && flow < 250) { active = true; started++; }
      }
      return snapshot(hp);
    },
  };
  const simulator = {
    async value(_domain,name) {
      if (name === 'ODU Defrost Contract') return fault === 'capability' ? null : 'manual-defrost-v1';
      if (name === 'ODU 1 defrost diagnostics') return `started=${started} completed=${completed} aborted=${aborted}`;
      if (name === 'ODU 1 defrost') return false;
      if (name === 'ODU 2 defrost') return peer;
      throw new Error(name);
    },
    async setSwitch(name,on) {
      writes.push([name,on]); peer=on;
      if (fault === 'cleanup' && !on) throw new Error('peer cleanup failed');
    },
  };
  return {controller,simulator,writes,now:()=>time,delay:async ms=>{time+=ms;},
    timeoutMs:20,holdMs:3,intervalMs:1,state:()=>({enabled,peer,triggers,started,completed,aborted})};
}

test('defrost stages prove exact boundaries, peer exclusion and one completed cycle without retaining CSRF', async () => {
  for (const stage of ['flow','overlap','cycle','all']) {
    const f=fixture();const result=await runDefrostScenarios({...f,stage});
    assert.equal(result.cases.length, stage === 'all' ? 4 : stage === 'flow' ? 3 : 1);
    assert.equal(f.state().enabled,false);assert.equal(f.state().peer,false);
    assert.ok(!JSON.stringify(result).includes('abcdef0123456789'));
    if (stage !== 'overlap') {
      assert.equal(f.state().started,1);assert.equal(f.state().completed,1);
      assert.ok(result.controlSamples.some(s=>s.flow===250));
    }
    if (['flow','all'].includes(stage)) assert.ok(result.controlSamples.some(s=>s.flow===249));
  }
});

test('missing installed defrost capability blocks before any mutation', async () => {
  const f=fixture('capability');await assert.rejects(defrostPreflight(f.controller,f.simulator),/BLOCKED/);
  await assert.rejects(runDefrostScenarios({...f,stage:'all'}),/BLOCKED/);assert.deepEqual(f.writes,[]);
});

test('defrost fails on missing physical readback or early low-flow start and releases fixtures', async () => {
  for (const fault of ['missing-bit','early','ack','cleanup','duplicate']) {
    const f=fixture(fault);
    await assert.rejects(runDefrostScenarios({...f,stage:fault === 'early' ? 'flow' : 'cycle'}),
      fault === 'missing-bit' ? /accepted.*timed out/ : fault === 'early' ? /below-minimum/ :
        fault === 'ack' ? /lost trigger ACK/ : fault === 'duplicate' ? /duplicated or aborted after completion/ : /cleanup failed/);
    assert.equal(f.state().enabled,false);assert.equal(f.state().peer,false);
    if (fault === 'ack') assert.equal(f.state().triggers,1);
  }
});

test('defrost parsers reject incomplete status and counters, and CLI preserves mutation gates', () => {
  assert.throws(()=>cycleCounters('started=1 completed=1'),/aborted/);
  assert.throws(()=>defrostObservation({hp:1},1),/missing/);
  const args=['--controller','http://controller','--simulator','http://simulator'];
  assert.throws(()=>parseDefrostArgs([...args,'--stage','all']),/--apply/);
  const parsed=parseDefrostArgs([...args,'--stage','all','--apply','--device','test.local','--restore-config','restore.yaml']);
  assert.equal(parsed.expectedProfile,'defrost-regression-v1');
});
