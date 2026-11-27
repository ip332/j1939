// Deterministic test of the actual controller, without a browser or live networking.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const {events,catalogs}=JSON.parse(readFileSync(0,'utf8'));
class Element {
  constructor(){this.textContent='';this.children=[];}
  replaceChildren(){this.children=[];}
  append(child){this.children.push(child);}
}
const nodes=new Map();
globalThis.document={body:{dataset:{}},querySelector(selector){if(!nodes.has(selector))nodes.set(selector,new Element());return nodes.get(selector);},createElement(){return new Element();}};
const pending=[],streams=[];let failSource=false;
function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
// Deliberately IGNORE abort in the transport mock. Tokens must work even if a
// response/body/error is already queued or cancellation arrives too late.
globalThis.fetch=(url,options)=>{
  const request=deferred();request.signal=options?.signal;pending.push(request);return request.promise;
};
globalThis.EventSource=class {
  constructor(url){if(failSource){failSource=false;throw Error('source creation failed');}this.url=url;this.closed=false;streams.push(this);}
  close(){this.closed=true;}
};
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const replay=()=>{nodes.get('#replay').onclick();return pending.at(-1);};
const response=(request,data=catalogs)=>request.resolve({ok:true,json:async()=>data});
const emit=(stream,event)=>stream.onmessage({data:JSON.stringify(event)});
const snapshot=()=>JSON.stringify({
  rows:nodes.get('#signals').children,log:nodes.get('#events').children,
  binding:nodes.get('#binding').textContent,connection:nodes.get('#connection').textContent,
  dataset:document.body.dataset
});
const activeCount=()=>streams.filter(s=>!s.closed).length;
const prefixStale=events.slice(0,events.findIndex(e=>e.type==='status'&&e.name==='latest-stale')+1);
const prefixFresh=events.slice(0,events.findIndex(e=>e.type==='status'&&e.name==='latest-fresh')+1);
await import('./dashboard.mjs');
assert.equal(pending.length,1);
const initial=pending[0],newest=replay();
assert.equal(initial.signal?.aborted,true);
response(newest);await tick();assert.equal(streams.length,1);
const current=streams[0];for(const e of prefixStale)emit(current,e);
assert.equal(nodes.get('#signals').children.filter(r=>r.className==='stale').length,9);
const staleSnapshot=snapshot();
response(initial);await tick(); // Old response cannot even create a stream.
assert.equal(streams.length,1);assert.equal(snapshot(),staleSnapshot);
console.log('PASS controller: reported out-of-order fetch/stale-to-fresh reproduction is prevented');

// A prior real stream must also be unable to inject a valid fresh-phase prefix.
const oldMessage=current.onmessage,oldError=current.onerror;
const next=replay();assert.equal(current.closed,true);assert.equal(activeCount(),0);
assert.equal(nodes.get('#signals').children.length,0);
response(next);await tick();const nextStream=streams.at(-1);
for(const e of prefixStale)emit(nextStream,e);
const nextSnapshot=snapshot();
for(const e of prefixFresh)oldMessage({data:JSON.stringify(e)});
oldMessage({data:JSON.stringify({type:'done'})});oldMessage({data:'{'});oldError();
assert.equal(snapshot(),nextSnapshot);assert.equal(nextStream.closed,false);

// Repeated Replay invalidates all previous pending requests, late success and failure.
const r1=replay(),r2=replay(),r3=replay();
assert.equal(r1.signal.aborted,true);assert.equal(r2.signal.aborted,true);
const before=streams.length;response(r3);await tick();const third=streams.at(-1);
response(r1);r2.reject(Error('late failure'));await tick();
assert.equal(streams.length,before+1);assert.equal(activeCount(),1);
assert.equal(nodes.get('#connection').textContent,'Connecting…');

// Supersede while response.json is pending, including late body failure.
const body=deferred();const waiting=replay();
waiting.resolve({ok:true,json:()=>body.promise});await tick();
const latest=replay();response(latest);await tick();const final=streams.at(-1);
const unaffected=snapshot();body.reject(Error('late body failure'));await tick();
assert.equal(snapshot(),unaffected);assert.equal(final.closed,false);
const body2=deferred(),waiting2=replay();waiting2.resolve({ok:true,json:()=>body2.promise});await tick();
const latest2=replay();response(latest2);await tick();const final2=streams.at(-1);
const unaffected2=snapshot();body2.resolve(catalogs);await tick();
assert.equal(snapshot(),unaffected2);assert.equal(activeCount(),1);

// Normal replay completes once and closes its OWN stream. Saved callbacks ignore EOF/error.
for(const e of events)emit(final2,e);
assert.equal(final2.closed,true);assert.equal(activeCount(),0);
assert.equal(document.body.dataset.done,'true');
assert.equal(nodes.get('#signals').children.length,18);
const complete=snapshot();final2.onerror();emit(final2,{type:'activate',bad:true});
assert.equal(snapshot(),complete);

// Current disconnect clears values and cannot overwrite a subsequent session.
const connected=replay();response(connected);await tick();const disconnected=streams.at(-1);
for(const e of prefixFresh)emit(disconnected,e);
assert.equal(nodes.get('#signals').children.length,18);disconnected.onerror();
assert.equal(disconnected.closed,true);assert.equal(nodes.get('#signals').children.length,0);
assert.equal(nodes.get('#connection').textContent,'Disconnected — Replay to restart');
assert.equal(document.body.dataset.done,undefined);
const disconnectedView=snapshot();emit(disconnected,events[0]);assert.equal(snapshot(),disconnectedView);

// Current HTTP/fetch/body/model/protocol failures terminate cleanly; Replay recovers.
for(const type of ['http','fetch','body','model','constructor','protocol','json']) {
  const request=replay();assert.equal(document.body.dataset.error,undefined);
  if(type==='http')request.resolve({ok:false});
  if(type==='fetch')request.reject(Error('network failed'));
  if(type==='body')request.resolve({ok:true,json:async()=>{throw Error('bad JSON');}});
  if(type==='model')response(request,null);
  if(type==='constructor'){failSource=true;response(request);}
  if(type==='protocol'||type==='json')response(request);
  await tick();
  if(type==='json')streams.at(-1).onmessage({data:'{'});
  if(type==='protocol')emit(streams.at(-1),{type:'observation',catalog:'wrong',generation:'0'});
  assert.equal(activeCount(),0);assert.equal(nodes.get('#signals').children.length,0);
  assert.equal(nodes.get('#binding').textContent,'Data unavailable');
  assert.ok(document.body.dataset.error);assert.equal(document.body.dataset.done,undefined);
}
const recovered=replay();response(recovered);await tick();
for(const e of events)emit(streams.at(-1),e);
assert.equal(document.body.dataset.done,'true');assert.equal(document.body.dataset.error,undefined);
assert.equal(nodes.get('#connection').textContent,'Replay complete');assert.equal(activeCount(),0);
console.log('PASS controller: superseded fetch/body success+failure, old events/done/error, repeated Replay, own-stream cleanup, disconnect, failure recovery and normal completion');
