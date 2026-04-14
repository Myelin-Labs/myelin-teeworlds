use ckb_testtool::{
    ckb_types::{bytes::Bytes, core::TransactionBuilder, packed::*, prelude::*},
    context::Context,
};
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

    let mut context = Context::default();
    let always_success_bin: Bytes = ckb_always_success_script::ALWAYS_SUCCESS.to_vec().into();
    let replayer_bin: Bytes = std::fs::read(&cli.replayer).expect("read replayer").into();
    let always_success_out_point = context.deploy_cell(always_success_bin);
    let replayer_out_point = context.deploy_cell(replayer_bin);

    let lock_script = context
        .build_script(&always_success_out_point, Default::default())
        .expect("build lock");
    let type_script = context
        .build_script(&replayer_out_point, Default::default())
        .expect("build type");

    let input_out_point = context.create_cell(
        CellOutput::new_builder()
            .capacity(1000u64)
            .lock(lock_script.clone())
            .type_(type_script.clone())
            .build(),
        Bytes::new(),
    );
    let input = CellInput::new_builder()
        .previous_output(input_out_point)
        .build();

    let outputs = vec![
        CellOutput::new_builder()
            .capacity(990u64)
            .lock(lock_script.clone())
            .build(),
    ];
    let outputs_data = vec![Bytes::new(); 1];

    let tape: Bytes = std::fs::read(&cli.tape).expect("read tape").into();
    let map: Bytes = std::fs::read(&cli.map).expect("read map").into();
    let config: Bytes = std::fs::read(&cli.config).expect("read config").into();

    let tx = TransactionBuilder::default()
        .input(input)
        .outputs(outputs)
        .outputs_data(outputs_data.pack())
        .witness(Bytes::new())
        .witness(tape)
        .witness(map)
        .witness(config)
        .build();
    let tx = context.complete_tx(tx);

    let repr_tx = context.dump_tx(&tx).expect("dump tx");

    std::fs::write(
        &cli.output,
        serde_json::to_string_pretty(&repr_tx).expect("to json"),
    )
    .expect("write tx")
}
