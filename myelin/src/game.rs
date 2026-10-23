//! Modified distribution: replay the actual upstream fixed-point engine before finality.
use serde::{Deserialize,Serialize};
use serde_json::{json,Value};
use std::{fs,process::{Command,Stdio},time::{Instant,Duration},io::Write};
pub const ID:&str="myelin-teeworlds";
pub const MAX_ACTIONS:u64=1;
#[derive(Clone,Default,Serialize,Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Game { result:Option<Value>, tape_hash:Option<String>, room:Option<String>, players:Vec<String> }
fn h(b:&[u8])->String {blake3::hash(b).to_hex().to_string()}
// Validate the untrusted tape before invoking any upstream C++ event dispatcher.
pub fn validate_tape(bytes:&[u8])->Result<u64,String>{
 if bytes.is_empty() || bytes.len()>500*1024{return Err("Invalid tape size".into())}
 let mut ints=Vec::new();let mut at=0;
 while at<bytes.len(){
  let first=bytes[at];at+=1;let mut value=(first&63) as u32;let negative=first&64!=0;let mut b=first;let mut shift=6;
  while b&128!=0 {if at>=bytes.len() || shift>27{return Err("Invalid packed integer".into())}b=bytes[at];at+=1;if shift==27 && b&127>15{return Err("Integer overflow".into())}value|=((b&127) as u32)<<shift;shift+=7;}
  let n=if negative{!(value as i32)}else{value as i32};ints.push(n);
 }
 let mut i=0;let mut ticks=0;let mut connected=[false;2];let mut entered=[false;2];let mut seeded=false;
 let get=|i:&mut usize|->Result<i32,String>{let n=*ints.get(*i).ok_or("Truncated tape")?;*i+=1;Ok(n)};
 while i<ints.len(){let event=get(&mut i)?;match event {
  9 if !seeded && i==1=>{get(&mut i)?;seeded=true;},
  10 if seeded=>{let id=get(&mut i)?;if !(0..2).contains(&id)||connected[id as usize]||get(&mut i)?!=0{return Err("Invalid player connect".into())}connected[id as usize]=true;},
  11=>{let id=get(&mut i)?;if !(0..2).contains(&id)||!connected[id as usize]||entered[id as usize]{return Err("Invalid player enter".into())}entered[id as usize]=true;},
  1 if seeded=>{ticks+=1;if ticks>3000{return Err("Match exceeds 60 seconds".into())}},
  2|8=>{let id=get(&mut i)?;if !(0..2).contains(&id)||!entered[id as usize]{return Err("Unknown player".into())}let mask=get(&mut i)?;if !(0..=1023).contains(&mask){return Err("Invalid input mask".into())}
    for field in 0..10 {if mask&(1<<field)!=0 {let n=get(&mut i)?;let valid=match field{0=>(-1..=1).contains(&n),1|2=>(-2000..=2000).contains(&n),3|5=>(0..=1).contains(&n),4|8|9=>(0..=63).contains(&n),6=>n==0,7=>(0..=5).contains(&n),_=>false};if !valid{return Err("Invalid input value".into())}}}
  },
  4 if i==ints.len() && ticks>0=>return Ok(ticks),
  _=>return Err("Invalid tape event or order".into())
 }}Err("Tape has no final marker".into())
}
impl Game {
 pub fn apply(&mut self, action:&Value)->Result<(),String>{
  if self.result.is_some() || action["type"]!="match"{return Err("Expected one complete match".into())}
  let tape=hex::decode(action["tape"].as_str().ok_or("Missing tape")?).map_err(|e|e.to_string())?;
  let ticks=validate_tape(&tape)?;
  let room=action["room"].as_str().ok_or("Missing room")?;
  if room.len()!=24 || !room.bytes().all(|b|b.is_ascii_hexdigit()){return Err("Invalid room id".into())}
  let players:Vec<String>=serde_json::from_value(action["players"].clone()).map_err(|e|e.to_string())?;
  if players.len()!=2 || players.iter().any(|p|p!="practice-bot" && (p.len()!=64 || !p.bytes().all(|b|b.is_ascii_hexdigit()))){return Err("Invalid players".into())}
  let assets=std::path::PathBuf::from(std::env::var("TEEWORLDS_ASSETS").unwrap_or_else(|_|"release".into()));
  for (name,expected) in [("teeworlds_bridge",include_bytes!("../../build/teeworlds_bridge").as_slice()),("dm1.map",include_bytes!("../../datasrc/maps/dm1.map").as_slice()),("game.cfg",include_bytes!("../game.cfg").as_slice())]{
   if h(&fs::read(assets.join(name)).map_err(|e|e.to_string())?)!=h(expected){return Err(format!("Artifact digest mismatch: {name}"))}
  }
  let mut tape_file=tempfile::NamedTempFile::new().map_err(|e|e.to_string())?;tape_file.write_all(&tape).map_err(|e|e.to_string())?;
  let output=tempfile::NamedTempFile::new().map_err(|e|e.to_string())?;
  let mut child=Command::new(assets.join("teeworlds_bridge")).args(["replay"]).arg(tape_file.path()).arg(assets.join("dm1.map")).arg(assets.join("game.cfg")).stdin(Stdio::null()).stdout(output.reopen().map_err(|e|e.to_string())?).stderr(Stdio::null()).spawn().map_err(|e|e.to_string())?;
  let start=Instant::now();loop{if let Some(status)=child.try_wait().map_err(|e|e.to_string())?{if !status.success(){return Err("Native replay failed".into())}break}if start.elapsed()>Duration::from_secs(10){let _=child.kill();let _=child.wait();return Err("Replay exceeded time limit".into())}std::thread::sleep(Duration::from_millis(10));}
  let replay:Value=serde_json::from_slice(&fs::read(output.path()).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
  if replay!=action["result"] || replay["state"]["tick"]!=ticks {return Err("Tape replay does not match live result".into())}
  self.result=Some(replay);self.tape_hash=Some(h(&tape));self.room=Some(room.into());self.players=players;Ok(())
 }
 pub fn view(&self)->Value{json!({"result":self.result,"tape_hash":self.tape_hash,"room":self.room,"players":self.players})}
 pub fn validate(&self)->Result<(),String>{if self.result.is_some() && (self.tape_hash.as_ref().map(String::len)!=Some(64)||self.players.len()!=2){return Err("Invalid match snapshot".into())}Ok(())}
}
#[cfg(test)]mod tests{use super::*;#[test]fn malformed_tapes_fail_closed(){for tape in [vec![],vec![9,1,10,63,0,11,63,1,4],vec![9,1,1],vec![9,1,1,4,1],vec![128;8]]{assert!(validate_tape(&tape).is_err());}assert_eq!(validate_tape(&[9,1,1,4]).unwrap(),1);}}
