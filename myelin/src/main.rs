mod game;
mod session;
use serde_json::{json,Value};
use std::{fs,path::Path};
fn read(path:&str)->session::AnyResult<Value>{if fs::metadata(path)?.len()>8_000_000{return Err("Input file too large".into())}Ok(serde_json::from_slice(&fs::read(path)?)?)}
fn main()->session::AnyResult<()> {
 let args:Vec<_>=std::env::args().collect();
 let result=match args.get(1).map(String::as_str){
  Some("record") if args.len()==4=>{let action=read(&args[3])?;let mut app=session::App::open(Path::new(&args[2]))?;let before=app.view()?;
   if before["session"]["actions"]!=0{return Err("This match is already sealed".into())}
   let view=app.action(json!({"session_id":before["session"]["id"],"expected_block":before["session"]["block_hash"],"action":action}))?;
   json!({"session":view["session"],"game":view["game"],"replay":app.replay()?,"export":app.export()?})},
  Some("verify") if args.len()==3=>session::verify_export(&read(&args[2])?)?,
  _=>return Err("Usage: myelin-teeworlds record <private-data-dir> <match.json> | verify <export.json>".into())
 };println!("{}",serde_json::to_string(&result)?);Ok(())
}
