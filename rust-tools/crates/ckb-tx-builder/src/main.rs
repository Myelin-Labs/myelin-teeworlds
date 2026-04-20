use ckb_tx_builder::build_test_tx;
use clap::Parser;

#[derive(Debug, Parser)]
#[command(version, about, long_about = None)]
struct Cli {
    /// Replayer script file
    #[arg(long)]
    replayer: String,

    /// Input tape
    #[arg(long)]
    tape: String,

    /// Map file
    #[arg(long)]
    map: String,

    /// Config file
    #[arg(long)]
    config: String,

    /// Output json tx file
    #[arg(long)]
    output: String,
}

fn main() {
    let cli = Cli::parse();

    build_test_tx(&cli.replayer, &cli.tape, &cli.map, &cli.config, &cli.output)
        .expect("build test tx");
}
