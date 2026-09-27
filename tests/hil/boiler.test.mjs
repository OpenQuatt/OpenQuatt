import assert from 'node:assert/strict';
import test from 'node:test';
import { runBoilerScenarios, boilerPreflight } from './scenarios/boiler.mjs';
import { parseBoilerArgs } from '../../scripts/hil/run-boiler.mjs';

function bench(fault) {
  let time = 0, heating = false, demand = 20000, assist = false, fallback = false;
  let requests = 0, rises = 0, previousCh = false;
  const responses = [true, true], writes = [];
  let sampledMode = 0, publishedDemand = 20000, publicationWait = 0, injectedPulse = false;
  function mode() { return !heating ? 0 : responses.every(Boolean) ? (assist && demand > 10000 ? 3 : 2) : fallback ? 4 : 1; }
  function ch() { return fault === 'unauthorized' ? heating : [3, 4].includes(mode()); }
  const controller = {
    async setNumber(name, value) { if (name === 'api_input_external_heat_demand') {
      demand = value;
      if (fault === 'delayed-demand' && value === 4000) publicationWait = 3;
      else publishedDemand = value;
    } },
    async setSelect(name, option) { if (name === 'CM Override') heating = option === 'Auto'; },
    async setSwitch(name, value) {
      writes.push([name, value]);
      if (name === 'Boiler assist enabled') assist = value;
      if (name === 'Boiler fallback on heat-pump fault') fallback = value;
      if (fault === 'ack' && name === 'Boiler assist enabled' && value) throw Error('lost permission ACK');
    },
    async value(_domain, name) {
      if (name === 'Boiler assist enabled') return assist;
      if (name === 'Boiler fallback on heat-pump fault') return fallback;
      if (name === 'Boiler active') return ch();
      if (name === 'Flow average (Selected)') return heating ? 800 : 0;
      throw Error(name);
    },
    async values(items) {
      if (items.some(i => i.key === 'demand')) {
        if (publicationWait > 0) publicationWait--;
        else publishedDemand = demand;
      }
      const v = {outside:7,room:20,setpoint:24,demand:publishedDemand,enable:true,valid:true,
        deficit:demand > 10000 ? 8000 : 0,on:1000,off:400,rated:6000,
        connection:'OpenTherm',link:true,mismatch:false};
      return Object.fromEntries(items.map(item => [item.key,v[item.key]]));
    },
    async request() {
      sampledMode = mode();
      return {schema_version:1,catalog_version:1,system:{control_mode:sampledMode,boiler_command_active:ch()},
        heat_pumps:responses.map((online,i)=>({index:i+1,link_state:online?'healthy':'lost',
          available_for_start:online,running_confirmed:heating && online && fault !== 'compressor',
          stop_confirmed:!heating && online,stop_unconfirmed:!online,
          stop_unconfirmed_due_to_link_loss:!online,must_stop:!online}))};
    },
  };
  const simulator = {
    async setSwitch(name,value) {
      const hp=Number(/^ODU ([12]) responses enabled$/.exec(name)[1]);
      writes.push([name,value]);responses[hp-1]=value;
      if (fault === 'gate-ack' && !value) throw Error('lost gate ACK');
    },
    async value(_domain,name) { return responses[Number(/^ODU ([12]) responses enabled$/.exec(name)[1])-1]; },
    async values(items) {
      const enabled=ch();
      if (fault !== 'stale-peer') requests++;
      if (enabled && !previousCh) rises++;
      previousCh=enabled;
      if (!injectedPulse && ((fault === 'permission-pulse' && sampledMode === 2 && !assist && requests >= 5) ||
          (fault === 'fallback-pulse' && sampledMode === 1) ||
          (fault === 'handback-pulse' && assist && demand === 4000))) {
        rises++; injectedPulse = true; // a complete pulse hidden between observations
      }
      if (fault === 'counter-reset' && requests >= 6) requests = 0;
      const v={responses:true,automatic:true,manual:false,dhw:false,fault:false,valid:true,ch:fault==='missing-echo'?false:enabled,active:enabled,requests,rises};
      if(fault==='capability')delete v.requests;
      return Object.fromEntries(items.map(item=>[item.key,v[item.key]]));
    },
  };
  return {controller,simulator,writes,responses,now:()=>time,delay:async ms=>{time+=ms;},
    timeoutMs:20,holdMs:3,permissionHoldMs:3,intervalMs:1,state:()=>({assist,fallback,sampledMode})};
}

test('boiler stages prove CM3 assist, deficit handback and disabled permissions with received OT output', async () => {
  for (const stage of ['assist','permissions','all']) {
    const f=bench();const result=await runBoilerScenarios({...f,stage});
    assert.equal(result.cases.length,stage==='all'?4:2);
    assert.deepEqual(f.responses,[true,true]);assert.equal(f.state().assist,false);assert.equal(f.state().fallback,false);
    if(stage!=='permissions')assert.ok(result.samples.some(s=>s.mode===3&&s.peerCh&&s.peerRises===1));
    if(stage!=='assist')assert.ok(result.samples.some(s=>s.hp.every(h=>h.stop_unconfirmed_due_to_link_loss)&&!s.peerCh));
  }
});

test('boiler rejects cached/missing OT echo, compressor absence and unauthorized activation', async () => {
  for(const fault of ['missing-echo','stale-peer','compressor','unauthorized']) {
    const f=bench(fault);
    await assert.rejects(runBoilerScenarios({...f,stage:'all'}));
    assert.equal(f.state().assist,false);assert.equal(f.state().fallback,false);assert.deepEqual(f.responses,[true,true]);
  }
});

test('boiler cleans permissions and both response gates after ambiguous ACKs and interrupts', async () => {
  for(const fault of ['ack','gate-ack']) {
    const f=bench(fault);await assert.rejects(runBoilerScenarios({...f,stage:'all'}));
    assert.equal(f.state().assist,false);assert.equal(f.state().fallback,false);assert.deepEqual(f.responses,[true,true]);
  }
  const f=bench();await assert.rejects(runBoilerScenarios({...f,stage:'assist',interrupted:()=>true}),/interrupted/);
  assert.ok(!f.writes.some(([name,on])=>name==='Boiler assist enabled'&&on));
});

test('boiler capability and CLI fail closed without mutation authorization', async () => {
  const f=bench('capability');await assert.rejects(boilerPreflight(f.controller,f.simulator),/requests/);
  assert.deepEqual(f.writes,[]);
  const args=['--controller','http://controller','--simulator','http://simulator'];
  assert.throws(()=>parseBoilerArgs([...args,'--stage','all']),/--apply/);
  assert.equal(parseBoilerArgs([...args,'--stage','smoke']).expectedProfile,'boiler-regression-v1');
});


test('boiler tolerates delayed selected-demand publication but catches hidden CH pulses and counter resets', async () => {
  const delayed=bench('delayed-demand');
  assert.equal((await runBoilerScenarios({...delayed,stage:'assist'})).cases.length,2);
  for(const fault of ['permission-pulse','fallback-pulse','handback-pulse','counter-reset']) {
    const f=bench(fault);
    await assert.rejects(runBoilerScenarios({...f,stage:'all'}), fault==='counter-reset' ? /counter reset/ : /pulse/);
    assert.equal(f.state().assist,false);assert.equal(f.state().fallback,false);assert.deepEqual(f.responses,[true,true]);
  }
});

test('disabled-assist observation covers full production dwell and promote window', async () => {
  const f=bench(); delete f.permissionHoldMs;
  f.intervalMs=1000;f.timeoutMs=1000000;f.holdMs=30000;
  const result=await runBoilerScenarios({...f,stage:'permissions'});
  assert.equal(result.cases.length,2);assert.ok(f.now()>=450000);
});
