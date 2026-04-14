use clap::Parser;
use std::fs;
use std::path::PathBuf;
use twmap::{Layer, TwMap};

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long)]
    input: PathBuf,

    #[arg(short, long, default_value = "stripped.map")]
    output: PathBuf,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    println!("Reading original map: {:?}", args.input);

    let buffer = fs::read(&args.input)?;
    let mut map = TwMap::parse(buffer.as_slice())?;

    // 1. Destroy the heavy media buffers to massively shrink the file size
    map.images.clear();
    map.sounds.clear();
    map.envelopes.clear();

    // 2. Sever all dangling references so twmap's internal validator doesn't panic
    for group in &mut map.groups {
        for layer in &mut group.layers {
            match layer {
                Layer::Tiles(tiles) => {
                    tiles.image = None;
                }
                Layer::Quads(quads) => {
                    quads.image = None;
                    for quad in &mut quads.quads {
                        quad.position_env = None;
                        quad.color_env = None;
                    }
                }
                Layer::Sounds(sounds_layer) => {
                    sounds_layer.sound = None;
                    for source in &mut sounds_layer.sources {
                        source.position_env = None;
                        source.sound_env = None;
                    }
                }
                // Game, Collision, and Teleport layers don't use media, so we ignore them
                _ => {} 
            }
        }
    }

    // 3. Save as a strictly standard Teeworlds Datafile v4 (zlib compressed)
    let mut out_file = fs::File::create(&args.output)?;
    map.save(&mut out_file)?;

    println!("Success: Stripped map written to {:?}", args.output);
    Ok(())
}
