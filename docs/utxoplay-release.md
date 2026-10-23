# UTXOPlay release verification

Release date: 23 October 2026. Game implementation: `4ee8772587b4311910fd6442df6f3a10c3d1783a` (subsequent documentation/fixture changes do not change the engine or Myelin application profile).

The public store is https://store.utxoplay.io/play/teeworlds. Production runs the native engine and Myelin verifier pair distributed with the release. No CKB transactions or asset transfers were made during verification.

## Verified behavior

- Two disposable public guest accounts joined one private duel and received distinct server-assigned player slots. A third account could neither join the running room nor export it.
- The public round processed 3,000 ticks at 50 Hz, accepted movement/jump/hook/fire inputs, and produced one finalized Myelin match frame.
- Both participants downloaded identical exports. The Teeworlds entry appeared in the account library.
- The public export was independently replayed on a separate local machine using the released engine/verifier pair. Its verified state root was `132824c83cb8e419dbe2e69c3a9cf718d78136e08b5de93f3e0f6afb739241f8`.
- Local acceptance covered malformed input, room isolation, native replay, Myelin signatures, export tampering, altered live-result rejection, and finished-record recovery after service restart.
- The store's wallet authentication tests and original Chess/Skyward public gameplay regression checks passed after deployment.
- Local browser checks covered store discovery, game launch, keyboard input, practice completion, the verified-result screen, and export availability, without browser console errors.

The fixture in `tests/fixtures/public-duel-session.json` uses only disposable test identities. Verify it with:

```sh
TEEWORLDS_ASSETS=./release ./release/myelin-teeworlds verify tests/fixtures/public-duel-session.json
```

## Verification limits

The embedded browser's public-domain screenshot operation timed out, while public HTTPS/API checks and local browser interactions passed. Its viewport override did not change the observed DOM width, so mobile layout is not claimed as device-tested. Real wallet extension/Passkey prompts retain the store's existing device-specific acceptance requirements. The production Teeworlds run was a real 60-second integration test, not a load test of the eight-room capacity.

This release proves native deterministic replay and Myelin record consistency. It does not claim on-chain CKB settlement, permissionless finality, or fair server scheduling.
