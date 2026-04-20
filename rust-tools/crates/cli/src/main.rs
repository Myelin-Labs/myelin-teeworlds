use ckb_chain_spec::consensus::ConsensusBuilder;
use ckb_hash::blake2b_256;
use ckb_jsonrpc_types::{
    CellDep, CellOutput, DepType, Either, JsonBytes, OutPoint, OutputsValidator, TransactionView,
};
use ckb_mock_tx_types::{
    MockTransaction, ReprMockCellDep, ReprMockInfo, ReprMockInput, ReprMockTransaction, Resource,
};
use ckb_script::{ScriptGroupType, TransactionScriptsVerifier, TxVerifyEnv};
use ckb_sdk::{
    CkbRpcClient, SECP256K1, ScriptId,
    constants::SIGHASH_TYPE_HASH,
    rpc::ckb_indexer::{Order, ScriptType, SearchKey, SearchMode},
    traits::{
        CellDepResolver, DefaultCellCollector, DefaultCellDepResolver, DefaultHeaderDepResolver,
        DefaultTransactionDependencyProvider, SecpCkbRawKeySigner,
    },
    transaction::builder::FeeCalculator,
    tx_builder::{CapacityBalancer, TxBuilder, transfer::CapacityTransferBuilder, unlock_tx},
    unlock::{ScriptUnlocker, SecpSighashUnlocker},
};
use ckb_tx_builder::build_test_tx;
use ckb_types::{
    H256,
    bytes::Bytes,
    core::{self, Capacity, cell::resolve_transaction},
    packed,
    prelude::*,
};
use clap::{Args, Parser, Subcommand};
use map_packer::{pack_map, pack_map_content};
use std::collections::{HashMap, HashSet};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Arc;

#[derive(Debug, Parser)]
#[command(version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    /// Deploy a new teeworlds cell
    Deploy(DeployArgs),

    /// Start a new teeworlds game server
    Start(StartArgs),

    /// Join a teeworlds game
    Join(JoinArgs),

    /// Verify an on-chain teeworlds game
    Verify(VerifyArgs),

    /// Replay an on-chain teeworlds game
    Replay(ReplayArgs),

    /// Debug utils
    #[command(subcommand)]
    Utils(Utils),
}

#[derive(Debug, Args)]
struct DeployArgs {
    /// CKB RPC URL
    #[arg(long, default_value = "http://127.0.0.1:8114")]
    ckb_rpc: String,

    /// Secp256k1 sighash single sign type hash
    #[arg(long, default_value_t = SIGHASH_TYPE_HASH)]
    sighash: H256,

    /// Account providing CKBytes
    #[arg(long)]
    private_key: H256,

    /// Account controlling replayer cell
    #[arg(long)]
    target_private_key: H256,

    /// Replayer on-chain binary
    #[arg(long, default_value = "../ckb/build/replayer_stripped")]
    replayer: String,

    /// Saved deployment file
    #[arg(long, default_value = "deployment.json")]
    deployment: String,
}

#[derive(Debug, Args)]
struct StartArgs {
    /// CKB RPC URL
    #[arg(long, default_value = "http://127.0.0.1:8114")]
    ckb_rpc: String,

    /// Config file
    #[arg(long, default_value = "../build/test_game1.cfg")]
    config: String,

    /// Teeworlds server
    #[arg(long, default_value = "../build/teeworlds_srv")]
    teeworlds_srv: String,

    /// Deployment file
    #[arg(long, default_value = "deployment.json")]
    deployment: String,
}

#[derive(Debug, Args)]
struct JoinArgs {
    /// Teeworlds
    #[arg(long, default_value = "../build/teeworlds")]
    teeworlds: String,

    /// Server url
    #[arg(long)]
    server_url: String,
}

#[derive(Debug, Args)]
struct VerifyArgs {
    /// CKB RPC URL
    #[arg(long, default_value = "http://127.0.0.1:8114")]
    ckb_rpc: String,

    /// Tx hash to load from CKB
    #[arg(long)]
    tx_hash: H256,
}

#[derive(Debug, Args)]
struct ReplayArgs {
    /// CKB RPC URL
    #[arg(long, default_value = "http://127.0.0.1:8114")]
    ckb_rpc: String,

    /// Tx hash to load from CKB
    #[arg(long)]
    tx_hash: H256,

    /// Teeworlds
    #[arg(long, default_value = "../build/teeworlds")]
    teeworlds: String,

    /// Teeworlds replayer
    #[arg(long, default_value = "../build/teeworlds_replayer")]
    teeworlds_replayer: String,
}

#[derive(Debug, Subcommand)]
enum Utils {
    /// Pack map into compact format for on-chain validation. Sounds, images, etc.
    /// will be stripped from the map.
    PackMap(PackMapArgs),

    /// Build a test transaction so ckb-debugger can run it.
    BuildTestTx(BuildTestTxArgs),
}

#[derive(Debug, Args)]
struct PackMapArgs {
    #[arg(short, long)]
    input: String,

    #[arg(short, long, default_value = "stripped.map")]
    output: String,
}

#[derive(Debug, Args)]
struct BuildTestTxArgs {
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

    match cli.command {
        Commands::Utils(Utils::PackMap(args)) => {
            println!("Reading original map: {:?}", args.input);
            pack_map(&args.input, &args.output).expect("pack map");
            println!("Success: Stripped map written to {:?}", args.output);
        }
        Commands::Utils(Utils::BuildTestTx(args)) => {
            build_test_tx(
                &args.replayer,
                &args.tape,
                &args.map,
                &args.config,
                &args.output,
            )
            .expect("build test transaction");
        }
        Commands::Deploy(args) => {
            let ckb_client = CkbRpcClient::new(args.ckb_rpc.as_str());
            let mut cell_dep_resolver = {
                let genesis_block = ckb_client
                    .get_block_by_number(0.into())
                    .expect("get genesis block")
                    .unwrap();
                DefaultCellDepResolver::from_genesis(&core::BlockView::from(genesis_block))
                    .expect("create dep resolver")
            };
            let header_dep_resolver = DefaultHeaderDepResolver::new(args.ckb_rpc.as_str());
            let mut cell_collector = DefaultCellCollector::new(args.ckb_rpc.as_str());
            let tx_dep_provider =
                DefaultTransactionDependencyProvider::new(args.ckb_rpc.as_str(), 10);

            let sender_key = secp256k1::SecretKey::from_slice(args.private_key.as_bytes())
                .expect("parse private key");
            let sender = {
                let pubkey = secp256k1::PublicKey::from_secret_key(&SECP256K1, &sender_key);
                let hash160 = blake2b_256(&pubkey.serialize()[..])[0..20].to_vec();
                packed::Script::new_builder()
                    .code_hash(args.sighash.pack())
                    .hash_type(core::ScriptHashType::Type)
                    .args(Bytes::from(hash160).pack())
                    .build()
            };
            let signer = SecpCkbRawKeySigner::new_with_secret_keys(vec![sender_key]);
            let sighash_unlocker = SecpSighashUnlocker::from(Box::new(signer) as Box<_>);
            let sighash_script_id = ScriptId::new_type(args.sighash.clone());
            let mut unlockers = HashMap::default();
            unlockers.insert(
                sighash_script_id,
                Box::new(sighash_unlocker) as Box<dyn ScriptUnlocker>,
            );

            // Step 1: deploy replayer binary
            let deploy_tx = {
                let output = packed::CellOutput::new_builder().build();
                let data: Bytes = std::fs::read(&args.replayer)
                    .expect("read replayer binary")
                    .into();
                let occupied_capacity = output
                    .occupied_capacity(Capacity::bytes(data.len()).expect("capacity"))
                    .expect("capacity");
                let output = output
                    .as_builder()
                    .capacity(
                        occupied_capacity
                            .safe_add(Capacity::bytes(1000).expect("capacity"))
                            .expect("capacity"),
                    )
                    .build();

                let placeholder_witness = packed::WitnessArgs::new_builder()
                    .lock(Some(Bytes::from(vec![0u8; 65])).pack())
                    .build();
                let balancer =
                    CapacityBalancer::new_simple(sender.clone(), placeholder_witness, 1000);

                let builder = CapacityTransferBuilder::new(vec![(output, data)]);
                let (tx, still_locked_groups) = builder
                    .build_unlocked(
                        &mut cell_collector,
                        &cell_dep_resolver,
                        &header_dep_resolver,
                        &tx_dep_provider,
                        &balancer,
                        &unlockers,
                    )
                    .expect("build unlocked");
                assert!(still_locked_groups.is_empty());

                let tx_hash = ckb_client
                    .send_transaction(
                        TransactionView::from(tx.clone()).inner,
                        Some(OutputsValidator::Passthrough),
                    )
                    .expect("send transaction");
                wait_for_tx(&ckb_client, &tx_hash);

                tx
            };
            println!(
                "Replayer binary deployed via transaction: {:x}",
                deploy_tx.hash()
            );

            // Step 2: create replayer cell
            let initial_tx = {
                let replayer_code_hash = packed::CellOutput::calc_data_hash(
                    &deploy_tx.outputs_data().get(0).unwrap().raw_data(),
                );
                let replayer_script_id = ScriptId::new(
                    replayer_code_hash.clone().into(),
                    core::ScriptHashType::Data2,
                );
                let replayer_cell_dep = packed::CellDep::new_builder()
                    .out_point(
                        packed::OutPoint::new_builder()
                            .tx_hash(deploy_tx.hash())
                            .build(),
                    )
                    .dep_type(core::DepType::Code)
                    .build();
                cell_dep_resolver.insert(
                    replayer_script_id,
                    replayer_cell_dep,
                    "replayer".to_string(),
                );

                let replayer_id: Bytes = {
                    let mut data = [0u8; 32];
                    use rand::prelude::*;
                    rand::rng().fill_bytes(&mut data);
                    data.to_vec().into()
                };
                let replayer_type_script = packed::Script::new_builder()
                    .code_hash(replayer_code_hash)
                    .hash_type(core::ScriptHashType::Data2)
                    .args(replayer_id)
                    .build();

                let output = {
                    let target_key =
                        secp256k1::SecretKey::from_slice(args.target_private_key.as_bytes())
                            .expect("parse private key");
                    let target = {
                        let pubkey = secp256k1::PublicKey::from_secret_key(&SECP256K1, &target_key);
                        let hash160 = blake2b_256(&pubkey.serialize()[..])[0..20].to_vec();
                        packed::Script::new_builder()
                            .code_hash(args.sighash.pack())
                            .hash_type(core::ScriptHashType::Type)
                            .args(Bytes::from(hash160).pack())
                            .build()
                    };
                    packed::CellOutput::new_builder()
                        .lock(target.clone())
                        .type_(Some(replayer_type_script.clone()))
                        .build()
                };
                let occupied_capacity = output
                    .occupied_capacity(Capacity::zero())
                    .expect("capacity");
                let output = output
                    .as_builder()
                    .capacity(
                        occupied_capacity
                            .safe_add(Capacity::bytes(1000).expect("capacity"))
                            .expect("capacity"),
                    )
                    .build();

                let placeholder_witness = packed::WitnessArgs::new_builder()
                    .lock(Some(Bytes::from(vec![0u8; 65])).pack())
                    .build();
                let balancer = CapacityBalancer::new_simple(sender, placeholder_witness, 1000);

                let builder = CapacityTransferBuilder::new(vec![(output, Bytes::new())]);
                let (tx, still_locked_groups) = builder
                    .build_unlocked(
                        &mut cell_collector,
                        &cell_dep_resolver,
                        &header_dep_resolver,
                        &tx_dep_provider,
                        &balancer,
                        &unlockers,
                    )
                    .expect("build unlocked");
                assert!(still_locked_groups.is_empty());

                let tx_hash = ckb_client
                    .send_transaction(
                        TransactionView::from(tx.clone()).inner,
                        Some(OutputsValidator::Passthrough),
                    )
                    .expect("send transaction");
                wait_for_tx(&ckb_client, &tx_hash);

                let json_tx_hash: H256 = deploy_tx.hash().into();
                let json_replayer_type_script =
                    ckb_jsonrpc_types::Script::from(replayer_type_script);
                let data = (
                    json_tx_hash,
                    args.target_private_key,
                    json_replayer_type_script,
                );
                let data_json = serde_json::to_string_pretty(&data).expect("to json");
                std::fs::write(&args.deployment, &data_json).expect("write");

                tx
            };
            println!(
                "Replayer cell deployed, initial transction: {:x}",
                initial_tx.hash()
            );
        }
        Commands::Start(args) => {
            let mut teeworlds_cmd =
                build_spawn_command(&args.teeworlds_srv).expect("spawn teeworlds srv");
            let base_path = teeworlds_cmd.get_current_dir().unwrap().to_path_buf();

            let config = std::fs::read_to_string(&args.config).expect("read config");
            let map_name = config
                .lines()
                .find_map(|line| line.trim().strip_prefix("sv_map "))
                .map(|value| value.trim())
                .expect("extract map name");
            let map_path = {
                let mut cur = base_path.clone();
                cur.push(format!("data/maps/{map_name}.map"));
                cur.canonicalize().expect("normalize dir")
            };
            let map = std::fs::read(&map_path).expect("read map");
            let packed_map = pack_map_content(&map).expect("pack map");

            // Run teeworlds and gather tape first.
            {
                println!("⚙️  \x1b[36mInitializing Teeworlds Engine (50Hz)\x1b[0m");
                println!("🌐 Listening for connections on 0.0.0.0:38303...");
                teeworlds_cmd
                    .arg("-f")
                    .arg(&build_relative_path(&args.config, &base_path).expect("relative path"));

                // No server log for optimized terminal output
                assert!(teeworlds_cmd.output().expect("replay").status.success());

                // Print server log for debugging
                // let mut child = teeworlds_cmd.spawn().expect("spawn");
                // assert!(child.wait().expect("srv").success());

                println!("🛑 Session ended. Extracting deterministic game state...");
                println!("⛓️  \x1b[33mSubmitting state proof to CKB Devnet...\x1b[0m");
            }

            let tape_file_path = {
                let mut cur = base_path.clone();
                cur.push("dump.bin.2");
                cur.canonicalize().expect("normalize dir")
            };
            let tape = std::fs::read(&tape_file_path).expect("read tape");

            // Now we have everything, we can submit to CKB
            let (json_tx_hash, target_private_key, json_replayer_type_script): (
                H256,
                H256,
                ckb_jsonrpc_types::Script,
            ) = serde_json::from_slice(&std::fs::read(args.deployment).expect("read"))
                .expect("from json");
            let ckb_client = CkbRpcClient::new(args.ckb_rpc.as_str());
            let cells = ckb_client
                .get_cells(
                    SearchKey {
                        script: json_replayer_type_script,
                        script_type: ScriptType::Type,
                        script_search_mode: Some(SearchMode::Exact),
                        filter: None,
                        with_data: None,
                        group_by_transaction: None,
                    },
                    Order::Desc,
                    10.into(),
                    None,
                )
                .expect("get cells rpc")
                .objects;
            assert_eq!(1, cells.len());
            let replayer_cell = cells[0].clone();

            // Assemble transaction.
            let cell_dep_resolver = {
                let genesis_block = ckb_client
                    .get_block_by_number(0.into())
                    .expect("fetch genesis block")
                    .unwrap();
                DefaultCellDepResolver::from_genesis(&core::BlockView::from(genesis_block))
                    .expect("create dep resolver")
            };

            let tx = core::TransactionView::new_advanced_builder()
                .cell_dep(
                    packed::CellDep::new_builder()
                        .out_point(
                            packed::OutPoint::new_builder()
                                .tx_hash(json_tx_hash)
                                .build(),
                        )
                        .dep_type(core::DepType::Code)
                        .build(),
                )
                .cell_dep(
                    cell_dep_resolver
                        .resolve(&replayer_cell.output.lock.clone().into())
                        .unwrap(),
                )
                .input(
                    packed::CellInput::new_builder()
                        .previous_output(replayer_cell.out_point)
                        .build(),
                )
                .output(replayer_cell.output.clone())
                .output_data(Bytes::new())
                .witness(
                    packed::WitnessArgs::new_builder()
                        .lock(Some(Bytes::from(vec![0u8; 65])).pack())
                        .build()
                        .as_bytes(),
                )
                .witness(tape)
                .witness(packed_map)
                .witness(config)
                .build();

            // Update tx to reflect tx fee
            let fee_calculator = FeeCalculator::new(1000);
            let fee = fee_calculator.fee(tx.data().as_reader().serialized_size_in_block() as u64);
            let new_output = {
                let output = tx.outputs().get(0).unwrap();
                let capacity: Capacity = output.capacity().into();
                let new_capacity = capacity.safe_sub(fee).expect("capacity");
                output.as_builder().capacity(new_capacity).build()
            };
            let tx = tx
                .as_advanced_builder()
                .set_outputs(vec![new_output])
                .build();

            // Build signer
            let sender_key = secp256k1::SecretKey::from_slice(target_private_key.as_bytes())
                .expect("parse private key");
            let signer = SecpCkbRawKeySigner::new_with_secret_keys(vec![sender_key]);
            let sighash_unlocker = SecpSighashUnlocker::from(Box::new(signer) as Box<_>);
            let sighash_script_id = ScriptId::new_type(replayer_cell.output.lock.code_hash.clone());
            let mut unlockers = HashMap::default();
            unlockers.insert(
                sighash_script_id,
                Box::new(sighash_unlocker) as Box<dyn ScriptUnlocker>,
            );
            let tx_dep_provider =
                DefaultTransactionDependencyProvider::new(args.ckb_rpc.as_str(), 10);
            let (tx, still_locked_groups) =
                unlock_tx(tx, &tx_dep_provider, &unlockers).expect("unlock tx");
            assert!(still_locked_groups.is_empty());

            let tx_hash = ckb_client
                .send_transaction(tx.data().into(), Some(OutputsValidator::Passthrough))
                .expect("send tx");
            // println!("Submitted tx: {tx_hash:x} containing teeworlds game to CKB Network!");

            wait_for_tx(&ckb_client, &tx_hash);
            println!("✅ \x1b[1;32mTX LANDED:\x1b[0m \x1b[1m{:x}\x1b[0m", tx_hash);
        }
        Commands::Join(args) => {
            println!("🎮 \x1b[36mTeeworlds Client booting...\x1b[0m");
            println!("🔌 Connecting to server at {}", args.server_url);
            println!("🚀 \x1b[1;36mEntering Game: Teeworlds (50Hz)...\x1b[0m");

            let mut teeworlds_cmd =
                build_spawn_command(&args.teeworlds).expect("build teeworlds command");
            teeworlds_cmd
                .arg("--console")
                .arg(format!("connect {}", args.server_url));
            assert!(teeworlds_cmd.output().expect("replay").status.success());
        }
        Commands::Verify(args) => {
            println!("📥 Fetching game state for TX: {}", args.tx_hash);
            println!("⚙️  \x1b[36mBooting CKB-VM (RISC-V execution layer)...\x1b[0m");
            println!("⏳ Re-executing Teeworlds physics kernel...");

            let client = CkbRpcClient::new(&args.ckb_rpc);
            let (tx, _) = fetch_transaction(&client, &args.tx_hash);
            assert_eq!(4, tx.inner.witnesses.len());

            let mock_tx = fill_transaction(&tx, &client);
            let verifier_resource = Resource::from_mock_tx(&mock_tx).expect("create resource");
            let resolved_tx = resolve_transaction(
                mock_tx.core_transaction(),
                &mut HashSet::new(),
                &verifier_resource,
                &verifier_resource,
            )
            .expect("resolve");

            let verifier_hardforks = core::hardfork::HardForks {
                ckb2021: core::hardfork::CKB2021::new_mirana()
                    .as_builder()
                    .rfc_0032(20)
                    .build()
                    .unwrap(),
                ckb2023: core::hardfork::CKB2023::new_mirana()
                    .as_builder()
                    .rfc_0049(30)
                    .build()
                    .unwrap(),
            };
            let verifier_consensus = Arc::new(
                ConsensusBuilder::default()
                    .hardfork_switch(verifier_hardforks)
                    .build(),
            );
            let verifier_epoch = ckb_types::core::EpochNumberWithFraction::new(35, 0, 1);
            let verifier_header_view = core::HeaderView::new_advanced_builder()
                .epoch(verifier_epoch.pack())
                .build();
            let verifier_tx_env = Arc::new(TxVerifyEnv::new_commit(&verifier_header_view));

            let verifier = TransactionScriptsVerifier::new(
                resolved_tx.into(),
                verifier_resource,
                verifier_consensus,
                verifier_tx_env,
            );
            let replayer_script_hash = mock_tx.mock_info.inputs[0]
                .output
                .type_()
                .to_opt()
                .expect("replayer type")
                .calc_script_hash();
            let cycles = verifier
                .verify_single(
                    ScriptGroupType::Type,
                    &replayer_script_hash,
                    1_000_000_000u64,
                )
                .expect("verify");

            println!("✅ \x1b[1;32m[ STATE VERIFIED ]\x1b[0m");
            use num_format::{Locale, ToFormattedString};
            println!(
                "⚡ Performance: \x1b[1;32m{} CYCLES\x1b[0m",
                cycles.to_formatted_string(&Locale::en)
            );
        }
        Commands::Replay(args) => {
            println!("📥 Fetching verified game state for TX: {}", args.tx_hash);

            let client = CkbRpcClient::new(&args.ckb_rpc);
            let (tx, _) = fetch_transaction(&client, &args.tx_hash);
            assert_eq!(4, tx.inner.witnesses.len());

            println!("🎞️  Compiling deterministic snapshots...");

            let config =
                String::from_utf8(tx.inner.witnesses[3].as_bytes().to_vec()).expect("config");
            let map_name = config
                .lines()
                .find_map(|line| line.trim().strip_prefix("sv_map "))
                .map(|value| value.trim())
                .expect("extract map name");
            let map_path = format!("data/maps/{map_name}.map");

            // Write tape to a local path
            let mut tape_file = tempfile::NamedTempFile::new().expect("create temp file");
            tape_file
                .write_all(tx.inner.witnesses[1].as_bytes())
                .expect("write");

            // Write config to a local path
            let mut config_file = tempfile::NamedTempFile::new().expect("create temp file");
            config_file.write_all(config.as_bytes()).expect("write");

            // Run teeworlds replayer to build demo file
            let mut replayer_cmd =
                build_spawn_command(&args.teeworlds_replayer).expect("create replayer cmd");
            replayer_cmd
                .arg(tape_file.path())
                .arg(map_path)
                .arg(config_file.path())
                .arg("demos/cli_replay.demo");
            assert!(
                replayer_cmd
                    .output()
                    .expect("replayer cmd output")
                    .status
                    .success()
            );

            println!("🚀 \x1b[1;36mLaunching Game Replay...\x1b[0m");

            // Run teeworlds client to play the demo file
            let mut teeworlds_cmd =
                build_spawn_command(&args.teeworlds).expect("build teeworld command");
            teeworlds_cmd.arg("play demos/cli_replay.demo");
            assert!(teeworlds_cmd.output().expect("replay").status.success());
        }
    }
}

fn build_relative_path(
    relative_file: &str,
    base_path: &Path,
) -> Result<PathBuf, Box<dyn std::error::Error>> {
    Ok(build_full_path(relative_file)?
        .strip_prefix(base_path)?
        .to_path_buf())
}

fn build_full_path(relative_file: &str) -> Result<PathBuf, Box<dyn std::error::Error>> {
    let mut cur = std::env::current_dir()?;
    cur.push(relative_file);
    Ok(cur.canonicalize()?)
}

fn build_spawn_command(relative_binary: &str) -> Result<Command, Box<dyn std::error::Error>> {
    let binary_path = build_full_path(relative_binary)?;
    let execute_path = {
        let mut cur = binary_path.clone();
        cur.pop();
        cur.canonicalize()?
    };

    let mut cmd = Command::new(binary_path);
    cmd.current_dir(execute_path);
    Ok(cmd)
}

// Sigh that at 2026, I still need this helper method
fn fetch_transaction(client: &CkbRpcClient, tx_hash: &H256) -> (TransactionView, Option<H256>) {
    let tx_with_status = client
        .get_transaction(tx_hash.clone())
        .expect("rpc")
        .expect(&format!("tx {:x} missing", tx_hash));
    let header_hash = tx_with_status.tx_status.block_hash.clone();

    let tx: TransactionView = {
        let tx = tx_with_status
            .transaction
            .expect(&format!("tx {:x} missing", tx_hash));
        match tx.inner {
            Either::Left(tx) => tx,
            Either::Right(molecule_tx) => {
                packed::TransactionView::from_slice(molecule_tx.as_bytes())
                    .expect("molecule parsing")
                    .unpack()
                    .into()
            }
        }
    };
    (tx, header_hash)
}

fn fill_transaction(tx: &TransactionView, client: &CkbRpcClient) -> MockTransaction {
    let inputs = tx
        .inner
        .inputs
        .iter()
        .map(|input| {
            let (output, data, header) = resolve_out_point(&input.previous_output, client);

            ReprMockInput {
                input: input.clone(),
                output,
                data,
                header,
            }
        })
        .collect();

    let header_deps = tx
        .inner
        .header_deps
        .iter()
        .map(|header_hash| {
            client
                .get_header(header_hash.clone())
                .expect("rpc")
                .expect(&format!("header {:x} missing", header_hash))
        })
        .collect();

    let mut cell_deps = Vec::new();
    for cell_dep in &tx.inner.cell_deps {
        cell_deps.extend(resolve_cell_dep(&cell_dep, client));
    }

    let mock_info = ReprMockInfo {
        inputs,
        cell_deps,
        header_deps,
        extensions: vec![],
    };

    let mock_tx = ReprMockTransaction {
        mock_info,
        tx: tx.inner.clone(),
    };

    mock_tx.into()
}

fn resolve_cell_dep(cell_dep: &CellDep, client: &CkbRpcClient) -> Vec<ReprMockCellDep> {
    let mut deps = Vec::new();
    let (output, data, header) = resolve_out_point(&cell_dep.out_point, client);
    deps.push(ReprMockCellDep {
        cell_dep: cell_dep.clone(),
        output,
        data: data.clone(),
        header,
    });
    if cell_dep.dep_type == DepType::DepGroup {
        let vec = packed::OutPointVec::from_slice(data.as_bytes()).expect("parsing OutPointVec");
        for o in vec {
            let (output, data, header) = resolve_out_point(&o.clone().into(), client);
            deps.push(ReprMockCellDep {
                cell_dep: CellDep {
                    out_point: o.into(),
                    dep_type: DepType::Code,
                },
                output,
                data,
                header,
            });
        }
    }
    deps
}

fn resolve_out_point(
    out_point: &OutPoint,
    client: &CkbRpcClient,
) -> (CellOutput, JsonBytes, Option<H256>) {
    let (tx, header_hash) = fetch_transaction(client, &out_point.tx_hash);
    let cell_output = tx.inner.outputs[out_point.index.value() as usize].clone();
    let data = tx.inner.outputs_data[out_point.index.value() as usize].clone();

    (cell_output, data, header_hash)
}

fn wait_for_tx(client: &CkbRpcClient, tx_hash: &H256) {
    println!("Submiting transaction: {tx_hash:x} to CKB Network...");
    loop {
        let status = client
            .get_transaction_status(tx_hash.clone())
            .expect("get tx status");
        if status.tx_status.status == ckb_jsonrpc_types::Status::Committed {
            break;
        }
        println!("Waiting for transaction to be committed...");
        std::thread::sleep(std::time::Duration::from_millis(1500));
    }
}
