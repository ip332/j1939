import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {Dashboard} from './model.mjs';
const {events,catalogs}=JSON.parse(readFileSync(0,'utf8'));
const model=new Dashboard(catalogs);
for(const e of events)model.apply(e);
assert.equal(model.done,true);assert.equal(model.rows.size,18);
assert.equal(model.history.length,38);
const wide=model.history.find(r=>r.name==='WideUnsigned.Wide');
assert.equal(wide.raw,'18446744073709551615');assert.equal(BigInt(wide.raw),2n**64n-1n);
const same=model.history.filter(r=>r.name==='EngineA.Speed');
assert.equal(same[0].engineering,1000);assert.equal(same[0].unit,'rpm');
assert.equal(same.at(-1).engineering,2000);assert.equal(same.at(-1).unit,'demo rpm');
assert.equal(same[0].key,same.at(-1).key);assert.notEqual(same[0].catalog,same.at(-1).catalog);
assert.equal(model.state(same[0]),'archived');assert.equal(model.state(same.at(-1)),'stale');
for(const changes of [{catalog:'bad'},{generation:'99'},{key:0},{raw:Number(2n**64n-1n)},{unit:'bad'},{signed_raw:'0'}]) {
  const m=new Dashboard(catalogs);for(const e of events.filter(e=>e.type==='activate'))m.apply(e);
  assert.throws(()=>m.apply({...same[0],...changes}));assert.equal(m.rows.size,0);
}
console.log('PASS browser model:18 separate catalog rows,38 events, exact BigInt, stale/archived state and mismatch rejection');

assert.throws(()=>model.apply({type:'clock',now_ns:'-1',max_age_ns:'1',clock_id:7}));

const current=same.at(-1),originalClock=model.clock;
for(const [now,maxAge,clock,state] of [
  ['2000000000','1000000000',7,'fresh'],
  ['2000000001','1000000000',7,'stale'],
  ['1000000000','0',7,'fresh'],['1000000001','0',7,'stale'],
  ['999999999','0',7,'clock reversed'],['1000000000','0',8,'clock mismatch'],
  ['18446744073709551615','18446744073709551615',7,'fresh']]) {
  model.apply({type:'clock',now_ns:now,max_age_ns:maxAge,clock_id:clock});
  assert.equal(model.state(current),state);
}
model.apply(originalClock);
console.log('PASS browser freshness: inclusive age/zero/boundary, reversed/mismatched clocks and uint64 limits');
