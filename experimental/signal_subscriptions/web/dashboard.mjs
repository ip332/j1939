import {Dashboard} from './model.mjs';
const table=document.querySelector('#signals'),log=document.querySelector('#events'),info=document.querySelector('#binding'),connection=document.querySelector('#connection');
let active=null;
function render(model) {
  table.replaceChildren();
  for(const row of model.rows.values()) {
    const tr=document.createElement('tr'),state=model.state(row);
    tr.className=state;
    for(const value of [row.name,state==='stale'?'—':row.engineering??'nonfinite',row.unit,`bus ${row.domain} / SA ${row.source}`,state,row.timestamp_ns,row.raw,`${row.generation} / ${row.catalog.slice(0,12)} / ${row.key}`]) {
      const td=document.createElement('td');td.textContent=String(value);tr.append(td);
    }
    table.append(tr);
  }
  info.textContent=model.active?`Active generation / catalog: ${model.active} · clock ${model.clock?.now_ns??'unavailable'} ns`:'Waiting for catalog binding';
  log.replaceChildren();
  for(const e of model.statuses){const li=document.createElement('li');li.textContent=`${e.name}: ${e.status}`;log.append(li);}
}
function stop(session) {
  // Mark retired BEFORE abort/close: even synchronous/queued old callbacks ignore it.
  session.closed=true;
  session.request.abort();
  if(session.stream)session.stream.close();
}
function clearView(message) {
  table.replaceChildren();log.replaceChildren();info.textContent=message;
}
async function start() {
  if(active)stop(active);
  const session={request:new AbortController(),stream:null,closed:false};
  active=session;
  const current=()=>active===session&&!session.closed;
  delete document.body.dataset.error;delete document.body.dataset.done;
  clearView('Waiting for catalog binding');connection.textContent='Connecting…';
  const fail=(message,detail)=>{
    if(!current())return;
    stop(session);clearView('Data unavailable');
    connection.textContent=message;document.body.dataset.error=detail;
  };
  try {
    const response=await fetch('/catalogs',{signal:session.request.signal});
    if(!current())return;
    if(!response.ok)throw Error('catalog request failed');
    const catalogs=await response.json();
    if(!current())return;
    const model=new Dashboard(catalogs);
    render(model);
    const stream=new EventSource('/events');session.stream=stream;
    stream.onmessage=event=>{
      if(!current())return;
      try {
        const e=JSON.parse(event.data);model.apply(e);render(model);connection.textContent='Replaying';
        if(model.done) {
          stop(session);connection.textContent='Replay complete';document.body.dataset.done='true';
        }
      } catch(error) {fail(`Rejected: ${error.message}`,error.message);}
    };
    stream.onerror=()=>fail('Disconnected — Replay to restart','Disconnected');
  } catch(error) {fail(`Failed: ${error.message}`,error.message);}
}
document.querySelector('#replay').onclick=start;
start();
