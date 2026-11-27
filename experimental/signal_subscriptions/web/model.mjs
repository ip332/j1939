// Experimental session-bound read model. No DOM, CAN decoding or BRF dependency.
export class Dashboard {
  constructor(catalogs) {
    this.catalogs=new Map(catalogs.map(c=>[c.catalog_sha256,c]));
    this.bindings=new Map(); this.rows=new Map(); this.history=[]; this.statuses=[];
    this.active=null; this.clock=null; this.done=false;
  }
  association(e) {return `${e.generation}:${e.catalog}`;}
  apply(e) {
    if(e.type==='activate') {
      if(!this.catalogs.has(e.catalog)||JSON.stringify(e.layout)!=='[8,8,16]'||e.version!==1||e.source_mode!==0||e.namespace!=='0'||e.mapping!=='cbf29ce484222325')throw Error('binding mismatch');
      const id=this.association(e), prior=this.bindings.get(id);
      if(prior&&JSON.stringify(prior)!==JSON.stringify(e))throw Error('binding redefinition');
      this.bindings.set(id,e);this.active=id;
    } else if(e.type==='observation') {
      const binding=this.association(e);
      if(!this.bindings.has(binding))throw Error('unknown catalog/generation');
      if(!Number.isInteger(e.key)||e.key<0||e.key>0xffffffff)throw Error('invalid key');
      const key=BigInt(e.key);
      if(Number(key>>24n)!==e.domain||Number((key>>16n)&255n)!==e.source||Number(key&65535n)!==e.signal_id)throw Error('key mismatch');
      const meta=this.catalogs.get(e.catalog).signals[e.signal_id];
      if(!meta||meta.id!==e.signal_id||meta.signal.unit!==e.unit||meta.message_context.pgn!==e.pgn)throw Error('metadata mismatch');
      if(typeof e.raw!=='string'||!/^\d+$/.test(e.raw)||BigInt(e.raw)>0xffffffffffffffffn)throw Error('raw must remain an exact string');
      if(typeof e.timestamp_ns!=='string'||!/^\d+$/.test(e.timestamp_ns)||BigInt(e.timestamp_ns)>0xffffffffffffffffn)throw Error('timestamp must remain an exact string');
      const r=BigInt(e.raw),width=BigInt(meta.signal.width);
      if(r>=(1n<<width))throw Error('raw width mismatch');
      const signed=meta.signal.is_signed ? (r&(1n<<(width-1n)) ? r-(1n<<width) : r).toString() : null;
      if(e.signed_raw!==signed||e.quality!=='unknown')throw Error('signed/quality mismatch');
      const row={...e,name:`${meta.message}.${meta.signal.name}`,binding};
      this.rows.set(`${binding}:${e.key}`,row);this.history.push(row);
    } else if(e.type==='clock') {
      for(const value of [e.now_ns,e.max_age_ns])if(typeof value!=='string'||!/^\d+$/.test(value)||BigInt(value)>0xffffffffffffffffn)throw Error('invalid clock');
      if(!Number.isInteger(e.clock_id)||e.clock_id<0||e.clock_id>0xffffffff)throw Error('invalid clock');
      this.clock=e;
    } else if(e.type==='status')this.statuses.push(e);
    else if(e.type==='done')this.done=true;
    else if(e.type!=='footprint')throw Error('unknown event');
  }
  state(row) {
    if(row.binding!==this.active)return 'archived';
    if(!this.clock)return 'unchecked';
    if(row.clock_id!==this.clock.clock_id)return 'clock mismatch';
    const now=BigInt(this.clock.now_ns),stamp=BigInt(row.timestamp_ns);
    if(now<stamp)return 'clock reversed';
    return now-stamp>BigInt(this.clock.max_age_ns)?'stale':'fresh';
  }
}
