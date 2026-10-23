import http from 'node:http';
import {WebSocketServer,WebSocket} from 'ws';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {randomBytes,timingSafeEqual} from 'node:crypto';
import {mkdirSync,readFileSync,writeFileSync,renameSync,existsSync,statfsSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const data=process.env.DATA_DIR||path.join(root,'service/data'), assets=process.env.TEEWORLDS_ASSETS||path.join(root,'release');
const secret=process.env.RUNTIME_TOKEN;if(!secret||secret.length<32)throw Error('RUNTIME_TOKEN is required');
for(const file of ['teeworlds_bridge','myelin-teeworlds','dm1.map','game.cfg'])if(!existsSync(path.join(assets,file)))throw Error('Missing runtime artifact: '+file);
const duration=Math.min(3000,Math.max(50,Number(process.env.MATCH_TICKS)||3000));
const rooms=new Map(), accounts=new Map();let sealing=Promise.resolve();
for(const dir of ['rooms','accounts'])mkdirSync(path.join(data,dir),{recursive:true,mode:0o700});
function save(file,value){const tmp=file+'.tmp';writeFileSync(tmp,JSON.stringify(value),{mode:0o600});renameSync(tmp,file);}
function roomPath(id){return path.join(data,'rooms',id);}
function persist(r){save(path.join(roomPath(r.id),'room.json'),{id:r.id,mode:r.mode,members:r.members,status:r.status,created:r.created,maxTicks:r.maxTicks,tick:r.tick,state:r.state,proof:r.proof,error:r.error});broadcast(r);}
function bind(account,id){accounts.set(account,id);save(path.join(data,'accounts',account+'.json'),{room:id});}
function current(account){let id=accounts.get(account);if(!id){try{id=JSON.parse(readFileSync(path.join(data,'accounts',account+'.json'))).room;accounts.set(account,id);}catch{return null;}}return load(id);}
function load(id){if(!/^[a-f0-9]{24}$/.test(id||''))return null;if(rooms.has(id))return rooms.get(id);try{const r=JSON.parse(readFileSync(path.join(roomPath(id),'room.json')));rooms.set(id,r);if(['running','waiting','sealing'].includes(r.status)){r.status='interrupted';r.error='The server restarted before this match was sealed. Start a new match.';persist(r);}rooms.set(id,r);return r;}catch{return null;}}
function view(r,account){if(!r)return{room:null};return{room:{id:r.id,mode:r.mode,status:r.status,slot:r.members.indexOf(account),players:r.members.map((a,i)=>({slot:i,name:a==='practice-bot'?'Practice bot':'Player '+(i+1)})),tick:r.tick||0,maxTicks:r.maxTicks||duration,state:r.state||null,appliedSeq:r.appliedSeq?.[r.members.indexOf(account)]??-1,proof:r.proof||null,error:r.error||null}};}
function fail(r,msg){clearInterval(r.timer);r.timer=null;if(r.child&&!r.child.killed)r.child.kill();r.status='failed';r.error=msg;persist(r);}
function safeInput(v){if(!v||typeof v!=='object')throw Error('Missing controls');const fields=['direction','x','y','jump','fire','hook','weapon'];const ranges=[[-1,1],[-2000,2000],[-2000,2000],[0,1],[0,63],[0,1],[0,5]];for(let i=0;i<fields.length;i++){const n=v[fields[i]];if(!Number.isInteger(n)||n<ranges[i][0]||n>ranges[i][1])throw Error('Invalid controls');}if(!Number.isSafeInteger(v.seq)||v.seq<0)throw Error('Invalid input sequence');return {...v,time:Date.now()};}
const neutral=()=>({direction:0,x:200,y:0,jump:0,fire:0,hook:0,weapon:0,seq:-1,time:0});
async function seal(r,result){
 r.status='sealing';r.state=result.state;persist(r);
 const request={type:'match',room:r.id,players:r.members,tape:readFileSync(path.join(roomPath(r.id),'tape.bin')).toString('hex'),result};
 save(path.join(roomPath(r.id),'match.json'),request);
 // A single replay worker bounds CPU and memory independently of live rooms.
 const job=async()=>{try{
  const out=await command(path.join(assets,'myelin-teeworlds'),['record',path.join(roomPath(r.id),'myelin'),path.join(roomPath(r.id),'match.json')]);
  const result=JSON.parse(out);save(path.join(roomPath(r.id),'export.json'),result.export);
  r.proof={...result.session,verified:result.replay.verified,frames:result.replay.frames,tapeHash:result.game.tape_hash};r.status='finished';persist(r);
 }catch(e){console.error('seal failed',r.id,e.message);fail(r,'The match ended, but verification failed. No verified result was issued.');}};
 sealing=sealing.then(job,job);await sealing;
}
function command(bin,args){return new Promise((resolve,reject)=>{const p=spawn(bin,args,{env:{...process.env,TEEWORLDS_ASSETS:assets},stdio:['ignore','pipe','pipe']});let out='',err='';const timeout=setTimeout(()=>p.kill('SIGKILL'),45000);p.stdout.on('data',b=>{out+=b;if(out.length>8_000_000)p.kill('SIGKILL');});p.stderr.on('data',b=>{if(err.length<2000)err+=b;});p.on('error',e=>{clearTimeout(timeout);reject(e);});p.on('exit',code=>{clearTimeout(timeout);code===0?resolve(out):reject(Error(err||'Verification worker failed'));});});}
function start(r){
 r.status='running';r.tick=0;r.inputs=[neutral(),neutral()];r.sent=[null,null];r.appliedTicks=new Map();r.lastSeen=Date.now();r.created=Date.now();persist(r);
 const child=spawn(path.join(assets,'teeworlds_bridge'),['live',path.join(roomPath(r.id),'tape.bin'),path.join(assets,'dm1.map'),path.join(assets,'game.cfg')],{stdio:['pipe','pipe','pipe']});r.child=child;let resultSeen=false;
 child.on('error',()=>fail(r,'The game engine could not start.'));
 child.stdin.on('error',()=>{});child.stderr.on('data',b=>console.error('engine',r.id,String(b).slice(0,300)));
 const lines=createInterface({input:child.stdout});lines.on('line',line=>{try{const m=JSON.parse(line);if(m.type==='frame'){r.state=m.state;r.appliedSeq=r.appliedTicks.get(m.state.tick)||[-1,-1];for(const tick of r.appliedTicks.keys())if(tick<=m.state.tick)r.appliedTicks.delete(tick);broadcast(r);}if(m.type==='result'){resultSeen=true;clearInterval(r.timer);r.timer=null;seal(r,m).catch(()=>fail(r,'Could not save match.'));}}catch{fail(r,'The game engine returned an invalid snapshot.');}});
 child.on('exit',()=>{if(!resultSeen&&r.status==='running')fail(r,'The game engine stopped unexpectedly.');});
 child.stdin.write([9,randomBytes(4).readUInt32LE()&0x7fffffff,10,0,0,11,0,10,1,0,11,1].join(' ')+' ');
 r.timer=setInterval(()=>{
  if(r.status!=='running')return;
  const events=[];
  for(let slot=0;slot<2;slot++){
   let input=r.inputs[slot];
   if(r.members[slot]==='practice-bot'){
    const chars=r.state?.items.filter(i=>i[0]===10)||[], me=chars.find(i=>i[1]===slot)?.[2],other=chars.find(i=>i[1]!==slot)?.[2];
    const x=me&&other?other[1]-me[1]:-200,y=me&&other?other[2]-me[2]:0;
    input={...neutral(),direction:Math.abs(x)>100?Math.sign(x):0,x:Math.max(-2000,Math.min(2000,x||1)),y:Math.max(-2000,Math.min(2000,y)),jump:r.tick%70<8?1:0,hook:r.tick%150<30?1:0,fire:(Math.floor(r.tick/12)%32)*2+1,weapon:2};
   }else if(Date.now()-input.time>600){input={...input,direction:0,jump:0,hook:0,fire:input.fire&~1};}
   const values=[input.direction,input.x,input.y,input.jump,input.fire,input.hook,0,input.weapon,0,0];
   let mask=0;for(let j=0;j<10;j++)if(!r.sent[slot]||values[j]!==r.sent[slot][j])mask|=1<<j;
   events.push(2,slot,mask);for(let j=0;j<10;j++)if(mask&(1<<j))events.push(values[j]);events.push(8,slot,0);r.sent[slot]=values;
  }
  events.push(1);r.tick++;r.appliedTicks.set(r.tick,r.inputs.map(i=>i.seq));
  if(!child.stdin.write(events.join(' ')+' ')){fail(r,'The game engine is overloaded. Please start a new match.');return;}
  if(r.tick>=(r.maxTicks||duration)){clearInterval(r.timer);r.timer=null;child.stdin.end('4 ');}
 },20);
}
function make(account,mode){const disk=statfsSync(data);if(disk.bavail*disk.bsize<1024**3)throw Error('Match storage is temporarily full. Please try later.');const old=current(account);if(old&&['waiting','running','sealing'].includes(old.status))throw Error('Finish or leave your current room first.');
 if([...rooms.values()].filter(r=>['running','waiting','sealing'].includes(r.status)).length>=8)throw Error('All arenas are busy. Try again shortly.');
 const r={id:randomBytes(12).toString('hex'),mode,members:mode==='practice'?[account,'practice-bot']:[account],status:'waiting',created:Date.now(),maxTicks:duration,tick:0};mkdirSync(roomPath(r.id),{mode:0o700});rooms.set(r.id,r);bind(account,r.id);persist(r);if(mode==='practice')start(r);return r;}
setInterval(()=>{for(const r of rooms.values()){if(r.status==='waiting'&&Date.now()-r.created>300000){r.status='expired';persist(r);}if(!['waiting','running','sealing'].includes(r.status)&&Date.now()-r.created>600000)rooms.delete(r.id);}},60000).unref();
function authenticated(req){const token=Buffer.from(req.headers['x-runtime-token']||''),expected=Buffer.from(secret),account=req.headers['x-utxoplay-account'];return token.length===expected.length&&timingSafeEqual(token,expected)&&/^[a-f0-9]{64}$/.test(account||'')?account:null;}
function acceptInput(r,account,b){
 if(!r||r.status!=='running'||b.room!==r.id||!r.members.includes(account))throw Error('No active match');
 const slot=r.members.indexOf(account),next=safeInput(b.input);if(next.seq>r.inputs[slot].seq)r.inputs[slot]=next;
}
const peers=new Map();
function sendSocket(ws,value){if(ws.readyState!==WebSocket.OPEN)return;if(ws.bufferedAmount>65536){ws.terminate();return;}ws.send(JSON.stringify(value));}
function broadcastAccount(account){for(const ws of peers.get(account)||[])sendSocket(ws,{type:'state',...view(current(account),account)});}
function broadcast(r){for(const account of r.members)broadcastAccount(account);}
const server=http.createServer(async(req,res)=>{
 const account=authenticated(req);
 const send=(code,body)=>{res.writeHead(code,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(body));};
 if(!account)return send(403,{error:'Forbidden'});
 try{
  if(req.method==='GET'&&req.url==='/api/state')return send(200,view(current(account),account));
  if(req.method==='GET'&&req.url==='/api/export'){const r=current(account);if(!r||!r.members.includes(account)||r.status!=='finished')return send(409,{error:'Finish a verified match to export it.'});return send(200,JSON.parse(readFileSync(path.join(roomPath(r.id),'export.json'))));}
  if(req.method!=='POST'||req.url!=='/api/action')return send(404,{error:'Unknown endpoint'});
  let raw='';for await(const chunk of req){raw+=chunk;if(raw.length>2048)return send(413,{error:'Input too large'});}
  const b=JSON.parse(raw);let r=current(account);
  if(b.type==='create'){if(!['practice','duel'].includes(b.mode))throw Error('Choose practice or duel');r=make(account,b.mode);}
  else if(b.type==='join'){
   if(r&&['waiting','running','sealing'].includes(r.status))throw Error('Leave your current room first.');
   r=load(b.room);if(!r||r.status!=='waiting'||r.mode!=='duel'||r.members.length!==1||Date.now()-r.created>300000)throw Error('This room is unavailable.');
   if(r.members[0]===account)throw Error('You already own this room.');r.members.push(account);bind(account,r.id);persist(r);start(r);
  }else if(b.type==='input'){
   if(!r||r.status!=='running'||b.room!==r.id||!r.members.includes(account))throw Error('No active match');acceptInput(r,account,b);
  }else if(b.type==='leave'){
   if(r?.status==='waiting'){r.status='cancelled';persist(r);}if(r?.status==='running'){const slot=r.members.indexOf(account);r.inputs[slot]=neutral();}
   accounts.delete(account);save(path.join(data,'accounts',account+'.json'),{room:null});r=null;
  }else throw Error('Unknown action');
  broadcastAccount(account);return send(200,view(r,account));
 }catch(e){return send(400,{error:e.message||'Request failed'});}
});
const sockets=new WebSocketServer({noServer:true,maxPayload:2048,perMessageDeflate:false});
server.on('upgrade',(req,socket,head)=>{
 socket.on('error',()=>socket.destroy());
 const account=authenticated(req);
 if(sockets.clients.size>=128||req.url!=='/api/socket'||!account||(peers.get(account)?.size||0)>=3){socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');return;}
 sockets.handleUpgrade(req,socket,head,ws=>{
  const group=peers.get(account)||new Set();group.add(ws);peers.set(account,group);
  let windowStart=Date.now(),messages=0;ws.alive=true;
  ws.on('pong',()=>{ws.alive=true;});ws.on('error',()=>ws.terminate());
  ws.on('close',()=>{group.delete(ws);if(!group.size)peers.delete(account);});
  ws.on('message',(raw,binary)=>{
   if(Date.now()-windowStart>=1000){windowStart=Date.now();messages=0;}
   if(binary||++messages>120){ws.close(1008,'Input limit');return;}
   try{const b=JSON.parse(raw);
    if(b.type==='ping'&&Number.isFinite(b.time))sendSocket(ws,{type:'pong',time:b.time});
    else if(b.type==='input')acceptInput(current(account),account,b);
    else throw Error('Unknown realtime message');
   }catch(e){sendSocket(ws,{type:'error',error:e.message});}
  });
  sendSocket(ws,{type:'ready'});broadcastAccount(account);
 });
});
const heartbeat=setInterval(()=>{for(const ws of sockets.clients){if(!ws.alive){ws.terminate();continue;}ws.alive=false;ws.ping();}},15000);heartbeat.unref();
server.requestTimeout=5000;server.headersTimeout=5000;
server.listen(Number(process.env.PORT||8783),process.env.BIND||'127.0.0.1',()=>console.log('Teeworlds room service ready'));
for(const signal of ['SIGTERM','SIGINT'])process.on(signal,()=>{for(const r of rooms.values())if(r.child){clearInterval(r.timer);r.child.kill();}clearInterval(heartbeat);for(const ws of sockets.clients)ws.terminate();server.close(()=>process.exit(0));});
