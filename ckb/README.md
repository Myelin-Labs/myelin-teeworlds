# Run Teeworlds on CKB

This file documents the exact steps to run Teeworlds on Nervos CKB.

First refer to the top level [README](../README.md) file on how to install dependencies, then:

```
$ git clone https://github.com/xxuejie/teeworlds
$ cd teeworlds
$ git submodule update --init
$ mkdir build
$ cd build
$ cmake ..
$ make
```

Use the following game config file now:

```
$ cat << EOF > test_game1.cfg
sv_name "testserver1"
sv_max_clients 4
sv_auto_demo_record 1
sv_gametype dm
sv_map dm1
sv_timelimit 1
sv_register 0
sv_port 38303
EOF
```

Now you can build CKB contract (make sure you have latest LLVM and clang installed):

```
$ cd ../ckb
$ make
```

While Teeworlds does not specifically say it, I've tried running Teeworlds on Ubuntu 24.04 with x86_64 CPU, CachyOS with x86_64 CPU, and macOS on Apple Silicon CPU.

Finally, build the Rust based scaffolding code:

```
$ cd ../rust-tools
$ cargo build --release
```

Now we have built everything needed, all future commands, can be run from `rust-tools` folder.

First, use [offckb](https://github.com/ckb-devrel/offckb) to start a CKB node:

```
$ offckb node
```

Using `offckb accounts list`, find 2 test accounts with enough CKBytes we can use. One is used to deploy the contract, one is used to create cells used to run Teeworlds game. Once accounts are settled, we can start the deployment process:

```
$ ./target/release/teeworlds-cli deploy \
  --private-key <private key 1> \
  --target-private-key <private key 2>
Submiting transaction: ae10a373a5efdcc35dd8fb95678334facaebdd54cd7d1d8e42a3ae7878e79e62 to CKB Network...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Replayer binary deployed via transaction: ae10a373a5efdcc35dd8fb95678334facaebdd54cd7d1d8e42a3ae7878e79e62
Submiting transaction: 37efe9ae50f37d0d219246189485612d109f053b5210301bee2946afa26d5261 to CKB Network...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Replayer cell deployed, initial transction: 37efe9ae50f37d0d219246189485612d109f053b5210301bee2946afa26d5261
```

Notice the hash on your system might vary. When deployed, a new `deployment.json` will be created in current folder. Keep this file, it contains current deployment information.

Now you can start the game server:

```
$ ./target/release/teeworlds-cli start
⚙️  Initializing Teeworlds Engine (50Hz)
🌐 Listening for connections on 0.0.0.0:38303...
```

On 2 machines (if current machine has X or GUI, you can reuse current one), you can join the game:

```
$ ./target/release/teeworlds-cli join --server-url <server URL>:38303
```

You can now play the game, current config sets game to be one minute. When game finishes, the game will prompt with "Server shutdown" message, this is because we've patched Teeworlds server to automatically exit when a game has completed. From the game server terminal, you might find following content:

```
$ ./target/release/teeworlds-cli start
⚙️  Initializing Teeworlds Engine (50Hz)
🌐 Listening for connections on 0.0.0.0:38303...
🛑 Session ended. Extracting deterministic game state...
⛓️  Submitting state proof to CKB Devnet...
Submiting transaction: 7ad90014eaf58b12c0bd1c25ccbfc0a5dd8587f42d9c5ce8067af91ce631d901 to CKB Network...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
Waiting for transaction to be committed...
✅ TX LANDED: 7ad90014eaf58b12c0bd1c25ccbfc0a5dd8587f42d9c5ce8067af91ce631d901
```

Now you can try rerunning the TX with more information:

```
$ ./target/release/teeworlds-cli verify --tx-hash 7ad90014eaf58b12c0bd1c25ccbfc0a5dd8587f42d9c5ce8067af91ce631d901
📥 Fetching game state for TX: 7ad90014eaf58b12c0bd1c25ccbfc0a5dd8587f42d9c5ce8067af91ce631d901
⚙️   Booting CKB-VM (RISC-V execution layer)...
⏳ Re-executing Teeworlds physics kernel...
✅ [ STATE VERIFIED ]
⚡ Performance: 160,779,457 CYCLES
```

Or on a machine with X or GUI, you can replay the game as well:

```
$ ./target/release/teeworlds-cli replay --tx-hash 7ad90014eaf58b12c0bd1c25ccbfc0a5dd8587f42d9c5ce8067af91ce631d901
```

Teeworlds might show some initial time where neither player has join the game(I might get to this later), you can press `esc`, then use the scrollbar to scroll to later time to view the replay.

Both `verify` and `replay` command actually first pulls the game tape from CKB, then use the game tape to either verify or replay the game. CKB remains the final source of all Teeworlds games.
