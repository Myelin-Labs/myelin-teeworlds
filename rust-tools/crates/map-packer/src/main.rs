use clap::Parser;
use std::fs::{self, File};
use std::io::Write;
// FIX 1: Import the Load trait so we can decompress the lazy-loaded buffers
use twmap::{Layer, TwMap, Load}; 

#[derive(Parser, Debug)]
#[command(name = "map-packer", about = "Converts Teeworlds map to minimal CKB-VM binary")]
struct Args {
    /// Input .map file
    #[arg(short, long)]
    input: String,

    /// Output .bin file
    #[arg(short, long)]
    output: String,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    println!("Loading map: {}", args.input);

    // Read raw bytes and parse the Teeworlds map headers
    let map_data = fs::read(&args.input)?;
    let map = TwMap::parse(&map_data)?;

    // FIX 2: Use .into_iter() to take ownership of the map data
    let mut game_layer = None;
    for group in map.groups.into_iter() {
        for layer in group.layers.into_iter() {
            if let Layer::Game(gl) = layer {
                game_layer = Some(gl);
                break;
            }
        }
        if game_layer.is_some() {
            break;
        }
    }

    let mut gl = game_layer.ok_or("Could not find the 'Game' layer in the map.")?;

    // FIX 3: Decompress the specific tile buffer we need using the Load trait
    gl.tiles.load()?;

    // Now safely unwrap the decompressed `Array2<GameTile>`
    let tiles = gl.tiles.unwrap();

    // The shape of a 2D array is [height, width].
    let height = tiles.shape()[0] as u32;
    let width = tiles.shape()[1] as u32;
    println!("Found Game layer. Dimensions: {}x{}", width, height);

    let mut grid_data = vec![0u8; (width * height) as usize];
    
    // Explicitly annotate type for clean binary serialization
    let mut entities: Vec<(u16, u16, u16)> = Vec::new();

    for y in 0..height {
        for x in 0..width {
            // ndarray indexing [row, col]
            let tile_id = tiles[[y as usize, x as usize]].id;
            let idx = (y * width + x) as usize;

            if tile_id > 0 && tile_id <= 10 {
                // Structural/collision tile (1 = Solid, 2 = Death, 3 = Nohook, etc.)
                grid_data[idx] = tile_id;
            } else if tile_id >= 192 {
                // Entity (Spawn, Armor, Weapon, Flag)
                // Treat the grid space as air so players don't collide with the entity point
                grid_data[idx] = 0;
                entities.push((tile_id as u16, x as u16, y as u16));
            } else {
                // Air or other ignored tiles
                grid_data[idx] = 0;
            }
        }
    }

    println!("Extracted {} entities.", entities.len());

    // Write binary ensuring Little-Endian order (native RISC-V compatibility)
    let mut out_file = File::create(&args.output)?;

    // 1. Header (width, height, num_entities) -> 3 x u32
    out_file.write_all(&width.to_le_bytes())?;
    out_file.write_all(&height.to_le_bytes())?;
    out_file.write_all(&(entities.len() as u32).to_le_bytes())?;

    // 2. Collision Grid -> Vec<u8>
    out_file.write_all(&grid_data)?;

    // 3. Entities -> u16 type, u16 x, u16 y
    for (ent_type, ent_x, ent_y) in entities {
        out_file.write_all(&ent_type.to_le_bytes())?;
        out_file.write_all(&ent_x.to_le_bytes())?;
        out_file.write_all(&ent_y.to_le_bytes())?;
    }

    println!("Successfully packed binary to '{}'", args.output);
    Ok(())
}
