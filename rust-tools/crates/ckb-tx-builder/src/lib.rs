use ckb_testtool::{
    ckb_types::{bytes::Bytes, core::TransactionBuilder, packed::*, prelude::*},
    context::Context,
};

pub fn build_test_tx(
    replayer: &str,
    tape: &str,
    map: &str,
    config: &str,
    output: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let mut context = Context::default();
    let always_success_bin: Bytes = ckb_always_success_script::ALWAYS_SUCCESS.to_vec().into();
    let replayer_bin: Bytes = std::fs::read(replayer).expect("read replayer").into();
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

    let tape: Bytes = std::fs::read(tape).expect("read tape").into();
    let map: Bytes = std::fs::read(map).expect("read map").into();
    let config: Bytes = std::fs::read(config).expect("read config").into();

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

    let repr_tx = context.dump_tx(&tx)?;

    std::fs::write(
        output,
        serde_json::to_string_pretty(&repr_tx).expect("to json"),
    )?;

    Ok(())
}
