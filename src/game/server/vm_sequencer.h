#ifndef GAME_SERVER_VM_SEQUENCER_H
#define GAME_SERVER_VM_SEQUENCER_H

#include <stddef.h>
#include <stdint.h>
#include <base/system.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>
#include <generated/protocol.h>  // Updated path for latest master/CMake builds
#include <game/server/gamecontext.h>
#include <game/server/player.h>

// ============================================================================
// CONSTANTS & BITMASKS
// ============================================================================

enum {
  VM_MAX_CLIENTS = 4,
  VM_MAX_ARENA_SIZE = 500 * 1024,      // 500 KB Hard Limit for Blockchain Tx
  VM_ARENA_WARNING_LIMIT = 450 * 1024  // 450 KB Sudden Death Trigger
};

// Tape Events (Optimized for physics-only replay)
enum {
  TAPE_TICK = 1,
  TAPE_INPUT_DELTA = 2,  // Direct Input (Physics)
  TAPE_CLIENT_DROP = 3,
  TAPE_END_MATCH = 4,
  TAPE_GAME_MSG_STARTINFO = 5,
  TAPE_GAME_MSG_KILL = 6,
  TAPE_GAME_MSG_SETTEAM = 7,
  TAPE_PREDICTED_INPUT = 8,  // Predicted Input (Lag Compensation)
  TAPE_RNG_SEED = 9,
  TAPE_CLIENT_CONNECTED = 10,
  TAPE_CLIENT_ENTER = 11,
};

enum {
  F_DIR = 1 << 0,
  F_TARGET_X = 1 << 1,
  F_TARGET_Y = 1 << 2,
  F_JUMP = 1 << 3,
  F_FIRE = 1 << 4,
  F_HOOK = 1 << 5,
  F_FLAGS = 1 << 6,
  F_W_WEAPON = 1 << 7,
  F_N_WEAPON = 1 << 8,
  F_P_WEAPON = 1 << 9
};

// ============================================================================
// SHARED UTILITIES (Used by both Relay Server and RISC-V VM)
// ============================================================================

inline unsigned int CalculateVMStateCRC(CGameContext* pContext) {
  if (!pContext) return 0;

  unsigned int StateCRC = 0;

  for (int i = 0; i < VM_MAX_CLIENTS; i++) {
    if (pContext->m_apPlayers[i]) {
      // Incorporate Score
      int Score = pContext->m_apPlayers[i]->m_Score;
      StateCRC ^= (Score << (i % 8));

      // Incorporate Camera/View Position
      vec2 Pos = pContext->m_apPlayers[i]->m_ViewPos;
      StateCRC ^= (unsigned int)(Pos.x) << 4;
      StateCRC ^= (unsigned int)(Pos.y) >> 2;
    }
  }
  return StateCRC;
}

// ============================================================================
// THE TAPE STREAM INTERFACE
// ============================================================================

class ITapeStream {
 public:
  virtual ~ITapeStream() {}
  virtual bool HasData() const = 0;
  virtual int GetInt() = 0;
};

// ============================================================================
// 1. NATIVE MEMORY STREAM (For Local PC Debugging)
// ============================================================================

class CMemoryTapeStream : public ITapeStream {
 private:
  CUnpacker m_Unpacker;

 public:
  inline CMemoryTapeStream(const void* pData, int Size) {
    m_Unpacker.Reset(pData, Size);
  }

  virtual bool HasData() const override {
    return m_Unpacker.RemainingSize() > 0;
  }

  virtual int GetInt() override {
    int value = m_Unpacker.GetInt();
    dbg_assert(!m_Unpacker.Error(), "eof");
    return value;
  }
};

// ============================================================================
// 2. CKB MOLECULE V2 STREAM (Strictly for RISC-V Blockchain Execution)
// ============================================================================

#ifdef __riscv

#include <ckb_syscalls.h>
#include <molecule/molecule2_reader.h>

// Static callback to prevent multiple definition errors in single-header design
static uint32_t ckb_witness_mol2_read(uintptr_t args[4], uint32_t offset,
                                      uint32_t length, uint8_t* buffer) {
  uint64_t len = length;
  size_t witness_index = (size_t)args[0];
  size_t source_type = (size_t)args[1];
  size_t tape_start_offset = (size_t)args[2];
  int err = ckb_load_witness(buffer, &len, tape_start_offset + offset,
                             witness_index, source_type);
  if (err != 0) return 0;
  return (uint32_t)len;
}

class CCkbTapeStream : public ITapeStream {
 private:
  enum { CACHE_SIZE = 1024 };
  uint8_t m_DataSourceMem[MOL2_DATA_SOURCE_LEN(CACHE_SIZE)];
  mol2_data_source_t* m_pDataSource;
  mol2_cursor_t m_MolCursor;
  uint32_t m_CurrentOffset;
  uint32_t m_TotalSize;

 public:
  inline CCkbTapeStream(size_t WitnessIndex, size_t SourceType,
                        size_t TapeStartOffset, int64_t TapeEndOffset = -1) {
    m_CurrentOffset = 0;
    m_pDataSource = (mol2_data_source_t*)m_DataSourceMem;

    uint64_t SyscallLen = CACHE_SIZE;
    int err = ckb_load_witness(m_pDataSource->cache, &SyscallLen,
                               TapeStartOffset, WitnessIndex, SourceType);
    uint32_t FullWitnessSize = (uint32_t)SyscallLen;

    if (err != 0 || FullWitnessSize <= TapeStartOffset) {
      m_TotalSize = 0;
      m_pDataSource->cache_size = 0;
    } else {
      uint32_t RemainingWitness = FullWitnessSize - TapeStartOffset;
      if (TapeEndOffset != -1 &&
          (uint32_t)(TapeEndOffset - TapeStartOffset) < RemainingWitness) {
        m_TotalSize = (uint32_t)(TapeEndOffset - TapeStartOffset);
      } else {
        m_TotalSize = RemainingWitness;
      }
      m_pDataSource->start_point = 0;
      m_pDataSource->cache_size =
          (m_TotalSize < CACHE_SIZE) ? m_TotalSize : CACHE_SIZE;
    }

    m_pDataSource->read = ckb_witness_mol2_read;
    m_pDataSource->total_size = m_TotalSize;
    m_pDataSource->args[0] = WitnessIndex;
    m_pDataSource->args[1] = SourceType;
    m_pDataSource->args[2] = TapeStartOffset;
    m_pDataSource->args[3] = 0;
    m_pDataSource->max_cache_size = CACHE_SIZE;

    m_MolCursor.offset = 0;
    m_MolCursor.size = m_TotalSize;
    m_MolCursor.data_source = m_pDataSource;
  }

  virtual bool HasData() const override {
    return m_CurrentOffset < m_TotalSize;
  }

  // TODO: rewrite those
  virtual int ReadRawInt() override {
    if (m_CurrentOffset + sizeof(int) > m_TotalSize) return 0;
    int Value = 0;
    mol2_read_at(&m_MolCursor, m_CurrentOffset, (uint8_t*)&Value, sizeof(int));
    m_CurrentOffset += sizeof(int);
    return Value;
  }

  virtual void PrepareUnpackerChunk(int MaxExpectedBytes) override {
    uint32_t BytesToRead = MaxExpectedBytes;
    if (m_CurrentOffset + BytesToRead > m_TotalSize) {
      BytesToRead = m_TotalSize - m_CurrentOffset;
    }
    mol2_read_at(&m_MolCursor, m_CurrentOffset, m_pDataSource->cache,
                 BytesToRead);
  }

  virtual const uint8_t* CurrentChunkPointer() const override {
    return m_pDataSource->cache;
  }
  virtual void Advance(int BytesConsumed) override {
    m_CurrentOffset += BytesConsumed;
  }
};

#endif  // __riscv

// ============================================================================
// CVMSequencer: The Flat Arena Tape Recorder
// ============================================================================

class CVMSequencer {
 private:
  uint8_t m_aArena[VM_MAX_ARENA_SIZE];
  int m_ArenaOffset;
  CPacker m_Packer;

  CNetObj_PlayerInput m_aLastInput[VM_MAX_CLIENTS];
  bool m_aHasInput[VM_MAX_CLIENTS];

  bool m_TapeFinished;

  void PackInt(int value) {
    m_Packer.AddInt(value);
    if (m_Packer.Error()) {
      MoveToArena();
      m_Packer.AddInt(value);
      dbg_assert(!m_Packer.Error(), "pack on empty packer");
    }
  }

  void MoveToArena() {
    if (m_Packer.Size() > 0) {
      dbg_assert(m_ArenaOffset + m_Packer.Size() <= VM_MAX_ARENA_SIZE,
                 "not enough tape");

      mem_copy(&m_aArena[m_ArenaOffset], m_Packer.Data(), m_Packer.Size());
      m_ArenaOffset += m_Packer.Size();
      m_Packer.Reset();
    }
  }

 public:
  inline CVMSequencer() {
    m_ArenaOffset = 0;
    m_TapeFinished = false;
    m_Packer.Reset();
    mem_zero(m_aHasInput, sizeof(m_aHasInput));
  }

  inline bool CanContinue() const {
    return !m_TapeFinished && m_ArenaOffset < VM_ARENA_WARNING_LIMIT;
  }

  inline void FinalizeTape() {
    if (m_TapeFinished) return;

    PackInt(TAPE_END_MATCH);
    MoveToArena();

    m_TapeFinished = true;
  }

  inline void LogTick() { PackInt(TAPE_TICK); }

  inline void LogClientConnected(int ClientID, bool AsSpec) {
    PackInt(TAPE_CLIENT_CONNECTED);
    PackInt(ClientID);
    PackInt((int)AsSpec);
  }

  inline void LogClientEnter(int ClientID) {
    PackInt(TAPE_CLIENT_ENTER);
    PackInt(ClientID);
  }

  inline void LogClientDrop(int ClientID) {
    PackInt(TAPE_CLIENT_DROP);
    PackInt(ClientID);

    m_aHasInput[ClientID] = false;
  }

  inline void LogRngSeed(uint32_t Seed) {
    PackInt(TAPE_RNG_SEED);
    PackInt(Seed);
  }

  // --- GAME MESSAGES ---

  inline void LogGameMsg_StartInfo(int ClientID) {
    PackInt(TAPE_GAME_MSG_STARTINFO);
    PackInt(ClientID);
  }

  inline void LogGameMsg_Kill(int ClientID) {
    PackInt(TAPE_GAME_MSG_KILL);
    PackInt(ClientID);
  }

  inline void LogGameMsg_SetTeam(int ClientID, int Team) {
    PackInt(TAPE_GAME_MSG_SETTEAM);
    PackInt(ClientID);
    PackInt(Team);
  }

  // --- DELTA COMPRESSION WITH ZERO-MASK PING ---

  inline void LogInputDelta(int EventType, int ClientID,
                            const CNetObj_PlayerInput* pNewInput) {
    if (m_TapeFinished || ClientID >= VM_MAX_CLIENTS) return;

    int ChangedMask = 0;

    if (!m_aHasInput[ClientID]) {
      ChangedMask = 0xFFFF;  // First time seeing this player
    } else {
      CNetObj_PlayerInput* pOld = &m_aLastInput[ClientID];

      if (pOld->m_Direction != pNewInput->m_Direction) ChangedMask |= F_DIR;
      if (pOld->m_TargetX != pNewInput->m_TargetX) ChangedMask |= F_TARGET_X;
      if (pOld->m_TargetY != pNewInput->m_TargetY) ChangedMask |= F_TARGET_Y;
      if (pOld->m_Jump != pNewInput->m_Jump) ChangedMask |= F_JUMP;
      if (pOld->m_Fire != pNewInput->m_Fire) ChangedMask |= F_FIRE;
      if (pOld->m_Hook != pNewInput->m_Hook) ChangedMask |= F_HOOK;
      if (pOld->m_PlayerFlags != pNewInput->m_PlayerFlags)
        ChangedMask |= F_FLAGS;
      if (pOld->m_WantedWeapon != pNewInput->m_WantedWeapon)
        ChangedMask |= F_W_WEAPON;
      if (pOld->m_NextWeapon != pNewInput->m_NextWeapon)
        ChangedMask |= F_N_WEAPON;
      if (pOld->m_PrevWeapon != pNewInput->m_PrevWeapon)
        ChangedMask |= F_P_WEAPON;
    }

    PackInt(EventType);
    PackInt(ClientID);
    PackInt(ChangedMask);

    if (ChangedMask & F_DIR) PackInt(pNewInput->m_Direction);
    if (ChangedMask & F_TARGET_X) PackInt(pNewInput->m_TargetX);
    if (ChangedMask & F_TARGET_Y) PackInt(pNewInput->m_TargetY);
    if (ChangedMask & F_JUMP) PackInt(pNewInput->m_Jump);
    if (ChangedMask & F_FIRE) PackInt(pNewInput->m_Fire);
    if (ChangedMask & F_HOOK) PackInt(pNewInput->m_Hook);
    if (ChangedMask & F_FLAGS) PackInt(pNewInput->m_PlayerFlags);
    if (ChangedMask & F_W_WEAPON) PackInt(pNewInput->m_WantedWeapon);
    if (ChangedMask & F_N_WEAPON) PackInt(pNewInput->m_NextWeapon);
    if (ChangedMask & F_P_WEAPON) PackInt(pNewInput->m_PrevWeapon);

    m_aLastInput[ClientID] = *pNewInput;
    m_aHasInput[ClientID] = true;
  }

  inline const void* GetTape() {
    MoveToArena();
    return m_aArena;
  }
  inline int GetTapeSize() {
    MoveToArena();
    return m_ArenaOffset;
  }
};

// ============================================================================
// CVMContextWrapper: The API Interceptor
// ============================================================================

class CVMContextWrapper : public IGameServer {
 private:
  CGameContext* m_pRealGame;
  CVMSequencer* m_pSequencer;
  bool m_GameEnded;
  int m_GameCount;

  inline void BroadcastTerminationResults() {
    unsigned int StateCRC = CalculateVMStateCRC(m_pRealGame);

    int TopScore = -9999;
    int WinnerID = -1;

    for (int i = 0; i < VM_MAX_CLIENTS; i++) {
      if (m_pRealGame->m_apPlayers[i]) {
        int Score = m_pRealGame->m_apPlayers[i]->m_Score;
        if (Score > TopScore) {
          TopScore = Score;
          WinnerID = i;
        }
      }
    }

    char aBuf[256];
    if (WinnerID != -1) {
      str_format(aBuf, sizeof(aBuf),
                 "Blockchain Engine Limit! Winner: %s (Score: %d) | Hash: %08X",
                 m_pRealGame->Server()->ClientName(WinnerID), TopScore,
                 StateCRC);
    } else {
      str_format(aBuf, sizeof(aBuf),
                 "Blockchain Engine Limit! No active players. | Hash: %08X",
                 StateCRC);
    }

    m_pRealGame->SendChat(-1, CHAT_ALL, -1, "=== VM TAPE TERMINATED ===");
    m_pRealGame->SendChat(
        -1, CHAT_ALL, -1,
        "You can stop playing now, the game has officially ended.");
    m_pRealGame->SendChat(-1, CHAT_ALL, -1, aBuf);
  }

  void ResetGame() {
    // TODO: this would crash the server, debug what is the reason later
    // if (m_pSequencer != NULL) {
    //   delete m_pSequencer;
    // }
    m_pSequencer = new CVMSequencer;
    uint32_t seed = time_get();
    vm_srand(seed);
    m_pSequencer->LogRngSeed(seed);

    m_GameCount++;
  }

 protected:
  virtual void SetKernel(IKernel* pKernel) {
    IInterface::SetKernel(pKernel);
    m_pRealGame->SetKernel(pKernel);
  }

 public:
  inline CVMContextWrapper(CGameContext* pReal) {
    m_pRealGame = pReal;
    m_pSequencer = NULL;
    m_GameEnded = false;
    m_GameCount = 0;
  }

  // --- SYNCHRONOUS LOGIC INTERCEPTOR ---

  virtual void OnTick() override {
    if (!m_GameEnded && !m_pSequencer->CanContinue()) {
      m_GameEnded = true;
      m_pSequencer->FinalizeTape();
      BroadcastTerminationResults();
    }

    if (!m_GameEnded) {
      m_pSequencer->LogTick();
    }

    m_pRealGame->OnTick();
  }

  // --- INPUT INTERCEPTORS ---

  virtual void OnClientPredictedInput(int ClientID, void* pInput) override {
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      m_pSequencer->LogInputDelta(TAPE_PREDICTED_INPUT, ClientID,
                                  (CNetObj_PlayerInput*)pInput);
    }
    m_pRealGame->OnClientPredictedInput(ClientID, pInput);
  }

  virtual void OnClientDirectInput(int ClientID, void* pInput) override {
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      m_pSequencer->LogInputDelta(TAPE_INPUT_DELTA, ClientID,
                                  (CNetObj_PlayerInput*)pInput);
    }
    m_pRealGame->OnClientDirectInput(ClientID, pInput);
  }

  // --- GAME EVENT INTERCEPTORS ---

  virtual void OnClientConnected(int ClientID, bool AsSpec) override {
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      m_pSequencer->LogClientConnected(ClientID, AsSpec);
    }
    m_pRealGame->OnClientConnected(ClientID, AsSpec);
  }

  virtual void OnClientEnter(int ClientID) override {
    dbg_msg("server", "client %d entered", ClientID);
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      m_pSequencer->LogClientEnter(ClientID);
    }
    m_pRealGame->OnClientEnter(ClientID);
  }

  virtual void OnClientDrop(int ClientID, const char* pReason) override {
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      m_pSequencer->LogClientDrop(ClientID);
    }
    m_pRealGame->OnClientDrop(ClientID, pReason);
  }

  virtual void OnMessage(int MsgID, CUnpacker* pUnpacker,
                         int ClientID) override {
    if (!m_GameEnded && ClientID < VM_MAX_CLIENTS) {
      if (MsgID == NETMSGTYPE_CL_STARTINFO) {
        m_pSequencer->LogGameMsg_StartInfo(ClientID);
      } else if (MsgID == NETMSGTYPE_CL_KILL) {
        m_pSequencer->LogGameMsg_Kill(ClientID);
      } else if (MsgID == NETMSGTYPE_CL_SETTEAM) {
        CUnpacker Copy = *pUnpacker;
        int Team = Copy.GetInt();
        m_pSequencer->LogGameMsg_SetTeam(ClientID, Team);
      }
    }

    m_pRealGame->OnMessage(MsgID, pUnpacker, ClientID);
  }

  virtual void OnInit() override {
    m_pRealGame->OnInit();
    ResetGame();
  }

  virtual void OnShutdown() override {
    m_pRealGame->OnShutdown();
    delete m_pSequencer;
  }

  virtual void OnStartMatch() override { m_pRealGame->OnStartMatch(); }

  virtual void OnEndMatch() override {
    m_pRealGame->OnEndMatch();

    dbg_msg("server", "game finished, crc: %x",
            CalculateVMStateCRC(m_pRealGame));

    char aBuf[1024];
    const char* dumpPrefix = Kernel()
                                 ->RequestInterface<IConfigManager>()
                                 ->Values()
                                 ->m_SvSequencerDump;
    str_format(aBuf, sizeof(aBuf), "%s.%d", dumpPrefix, m_GameCount);

    m_pSequencer->FinalizeTape();
    dbg_msg("server", "dump sequencer data to %s", aBuf);
    IOHANDLE file = io_open(aBuf, IOFLAG_WRITE);
    io_write(file, m_pSequencer->GetTape(), m_pSequencer->GetTapeSize());
    io_close(file);

    ResetGame();
  }

  // ============================================================================
  // THE COMPLETE IGameServer PASS-THROUGH BLOCK (Master Branch)
  // ============================================================================
  virtual void OnConsoleInit() override { m_pRealGame->OnConsoleInit(); }

  virtual void OnPreSnap() override { m_pRealGame->OnPreSnap(); }
  virtual void OnSnap(int ClientID) override { m_pRealGame->OnSnap(ClientID); }
  virtual void OnPostSnap() override { m_pRealGame->OnPostSnap(); }

  virtual bool IsClientBot(int ClientID) const override {
    return m_pRealGame->IsClientBot(ClientID);
  }
  virtual bool IsClientReady(int ClientID) const override {
    return m_pRealGame->IsClientReady(ClientID);
  }
  virtual bool IsClientPlayer(int ClientID) const override {
    return m_pRealGame->IsClientPlayer(ClientID);
  }
  virtual bool IsClientSpectator(int ClientID) const override {
    return m_pRealGame->IsClientSpectator(ClientID);
  }

  virtual const char* GameType() const override {
    return m_pRealGame->GameType();
  }
  virtual const char* Version() const override {
    return m_pRealGame->Version();
  }
  virtual const char* NetVersion() const override {
    return m_pRealGame->NetVersion();
  }
  virtual const char* NetVersionHashUsed() const override {
    return m_pRealGame->NetVersionHashUsed();
  }
  virtual const char* NetVersionHashReal() const override {
    return m_pRealGame->NetVersionHashReal();
  }
};

#endif
