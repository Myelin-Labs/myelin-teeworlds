// Real engine + Myelin verification; two independent authenticated identities.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const {WebSocket}=createRequire(new URL('../service/package.json',import.meta.url))('ws');
import {spawn,spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {once} from 'node:events';
const dir=mkdtempSync(path.join(tmpdir(),'myelin-teeworlds-test-'));
const token='disposable-teeworlds-acceptance-token';
let child,socket;
async function start(){child=spawn(process.execPath,['service/server.mjs'],{env:{...process.env,RUNTIME_TOKEN:token,DATA_DIR:dir,PORT:'18983',MATCH_TICKS:'150'},stdio:['ignore','pipe','pipe']});child.stderr.on('data',b=>process.stderr.write(b));await once(child.stdout,'data');}
async function stop(){const p=once(child,'exit');child.kill();await p;child=null;}
const a='a'.repeat(64),b='b'.repeat(64),outsider='c'.repeat(64);
async function req(account,body,endpoint='state',extra={}){const r=await fetch('http://127.0.0.1:18983/api/'+(body?'action':endpoint),{method:body?'POST':'GET',headers:{'Content-Type':'application/json','X-Runtime-Token':token,'X-UTXOPlay-Account':account,...extra},body:body?JSON.stringify(body):undefined});return{status:r.status,data:await r.json()};}
try{
 await start();assert.equal((await req(a,undefined,'state',{'X-Runtime-Token':'wrong'})).status,403);
 const created=await req(a,{type:'create',mode:'duel'});assert.equal(created.status,200);const room=created.data.room.id;assert.equal(created.data.room.status,'waiting');
 assert.equal((await req(b)).data.room,null);
 const joined=await req(b,{type:'join',room});assert.equal(joined.data.room.slot,1);assert.equal(joined.data.room.status,'running');
 assert.equal((await req(outsider,{type:'join',room})).status,400);
 const controls={direction:1,x:300,y:-100,jump:1,fire:1,hook:1,weapon:2,seq:1};
 assert.equal((await req(outsider,{type:'input',room,input:controls})).status,400);
 assert.equal((await req(a,{type:'input',room,input:{...controls,x:999999}})).status,400);
 assert.equal((await req(a,{type:'input',room,input:controls})).status,200);
 assert.equal((await req(b,{type:'input',room,input:{...controls,direction:-1,x:-300}})).status,200);
 socket=new WebSocket('ws://127.0.0.1:18983/api/socket',{headers:{'X-Runtime-Token':token,'X-UTXOPlay-Account':a}});
 const messages=[];socket.on('message',raw=>messages.push(JSON.parse(raw)));await once(socket,'open');
 socket.send(JSON.stringify({type:'input',room,input:{...controls,seq:2}}));
 for(let n=0;n<100&&!messages.some(m=>m.room?.appliedSeq===2);n++)await new Promise(r=>setTimeout(r,10));
 assert.ok(messages.some(m=>m.type==='ready'));assert.ok(messages.some(m=>m.room?.appliedSeq===2),'WebSocket input must appear in an authoritative engine snapshot');
 socket.close();await once(socket,'close');socket=null;
 let finished;
 for(let n=0;n<100;n++){await new Promise(r=>setTimeout(r,100));const v=(await req(a)).data.room;if(v.status==='failed')throw Error(v.error);if(v.status==='finished'){finished=v;break;}}
 assert.ok(finished,'Match must finish and seal');assert.equal(finished.tick,150);assert.equal(finished.proof.verified,true);
 const ea=await req(a,undefined,'export'),eb=await req(b,undefined,'export');assert.equal(ea.status,200);assert.deepEqual(ea.data,eb.data);assert.equal((await req(outsider,undefined,'export')).status,409);
 const exportPath=path.join(dir,'export.json');writeFileSync(exportPath,JSON.stringify(ea.data));
 const verify=spawnSync('./release/myelin-teeworlds',['verify',exportPath],{encoding:'utf8'});assert.equal(verify.status,0,verify.stderr);assert.equal(JSON.parse(verify.stdout).verified,true);
 const altered=structuredClone(ea.data);const block=Buffer.from(altered.blocks[0],'hex');block[block.length-1]^=1;altered.blocks[0]=block.toString('hex');writeFileSync(exportPath,JSON.stringify(altered));assert.notEqual(spawnSync('./release/myelin-teeworlds',['verify',exportPath]).status,0);
 const request=JSON.parse(readFileSync(path.join(dir,'rooms',room,'match.json')));request.result.crc^=1;const bad=path.join(dir,'bad.json');writeFileSync(bad,JSON.stringify(request));assert.notEqual(spawnSync('./release/myelin-teeworlds',['record',path.join(dir,'bad-state'),bad]).status,0);
 await stop();await start();const resumed=(await req(a)).data.room;assert.equal(resumed.status,'finished');assert.equal(resumed.proof.state_root,finished.proof.state_root);assert.deepEqual((await req(a,undefined,'export')).data,ea.data);
 console.log('PASS: two-player room, account isolation, input bounds, native replay, Myelin finality, independent export verification, tamper rejection, restart persistence.');
}finally{socket?.terminate();if(child)await stop();rmSync(dir,{recursive:true,force:true});}
