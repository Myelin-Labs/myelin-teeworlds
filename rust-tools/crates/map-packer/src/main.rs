use clap::Parser;
use map_packer::pack_map;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long)]
    input: String,

    #[arg(short, long, default_value = "stripped.map")]
    output: String,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    println!("Reading original map: {:?}", args.input);

    pack_map(&args.input, &args.output)?;

    println!("Success: Stripped map written to {:?}", args.output);
    Ok(())
}
