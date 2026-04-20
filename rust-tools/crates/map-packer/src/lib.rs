use std::fs;
use twmap::{Layer, TwMap};

pub fn pack_map(input: &str, output: &str) -> Result<(), Box<dyn std::error::Error>> {
    let buffer = fs::read(input)?;
    let packed = pack_map_content(&buffer)?;
    fs::write(output, packed)?;

    Ok(())
}

pub fn pack_map_content(input: &[u8]) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let mut map = TwMap::parse(input)?;

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
    let mut out = vec![];
    map.save(&mut out)?;

    Ok(out)
}
