#ifndef REPLAYER_REPLAYER_H
#define REPLAYER_REPLAYER_H

#include <string.h>

#include <engine/storage.h>
#include <engine/shared/config.h>

#include <game/server/vm_sequencer.h>

class CMockServer : public IServer {
 public:
  CMockServer() {
    m_TickSpeed = SERVER_TICK_SPEED;
    m_CurrentGameTick = 0;
  }

  void OnTick() { m_CurrentGameTick++; }
  virtual void AfterTick() {}

  virtual const char* ClientName(int ClientID) const {
    (void)ClientID;
    return "unknown";
  }

  virtual const char* ClientClan(int ClientID) const {
    (void)ClientID;
    return "unknown";
  }

  virtual int ClientCountry(int ClientID) const {
    (void)ClientID;
    return 0;
  }

  virtual bool ClientIngame(int ClientID) const {
    (void)ClientID;
    return true;
  }

  virtual int GetClientInfo(int ClientID, CClientInfo* pInfo) const {
    (void)ClientID;
    (void)pInfo;
    return 0;
  }

  virtual void GetClientAddr(int ClientID, char* pAddrStr, int Size) const {
    (void)ClientID;
    memset(pAddrStr, 0, Size);
  }

  virtual int GetClientVersion(int ClientID) const {
    (void)ClientID;
    return 0;
  }

  virtual int SendMsg(CMsgPacker* pMsg, int Flags, int ClientID) {
    (void)pMsg;
    (void)Flags;
    (void)ClientID;
    return 0;
  }

  virtual void SetClientName(int ClientID, char const* pName) {
    (void)ClientID;
    (void)pName;
  }

  virtual void SetClientClan(int ClientID, char const* pClan) {
    (void)ClientID;
    (void)pClan;
  }

  virtual void SetClientCountry(int ClientID, int Country) {
    (void)ClientID;
    (void)Country;
  }

  virtual void SetClientScore(int ClientID, int Score) {
    (void)ClientID;
    (void)Score;
  }

  virtual int SnapNewID() { return 0; }

  virtual void SnapFreeID(int ID) { (void)ID; }

  virtual void* SnapNewItem(int Type, int ID, int Size) {
    (void)Type;
    (void)ID;
    (void)Size;
    return nullptr;
  }

  virtual void SnapSetStaticsize(int ItemType, int Size) {
    (void)ItemType;
    (void)Size;
  }

  virtual void SetRconCID(int ClientID) { (void)ClientID; }

  virtual bool IsAuthed(int ClientID) const {
    (void)ClientID;
    return false;
  }

  virtual bool IsBanned(int ClientID) {
    (void)ClientID;
    return false;
  }

  virtual void Kick(int ClientID, const char* pReason) {
    (void)ClientID;
    (void)pReason;
  }
  virtual void ChangeMap(const char* pMap) { (void)pMap; }

  virtual void DemoRecorder_HandleAutoStart() {}
  virtual bool DemoRecorder_IsRecording() { return false; }

  virtual void OnStartMatch() {}
  virtual void OnEndMatch() {}
  virtual void Shutdown() {}
};

class CMockStorage : public IStorage {
 public:
  virtual IOHANDLE OpenFile(const char* pFilename, int Flags, int Type,
                            char* pBuffer = 0, int BufferSize = 0,
                            FCheckCallback pfnCheckCB = 0,
                            const void* pCheckCBData = 0) override {
    (void)Type;
    (void)BufferSize;

    IOHANDLE Handle = io_open(pFilename, Flags);
    if (Handle) {
      // do an additional check on the file
      if (pfnCheckCB && !pfnCheckCB(Handle, pCheckCBData)) {
        io_close(Handle);
        Handle = 0;
      } else
        return Handle;
    }

    if (pBuffer) {
      pBuffer[0] = 0;
    }
    return 0;
  }

  virtual void ListDirectory(int Type, const char* pPath,
                             FS_LISTDIR_CALLBACK pfnCallback,
                             void* pUser) override {
    dbg_assert(0, "unimplemented");
  }

  virtual void ListDirectoryFileInfo(int Type, const char* pPath,
                                     FS_LISTDIR_CALLBACK_FILEINFO pfnCallback,
                                     void* pUser) override {
    dbg_assert(0, "unimplemented");
  }

  virtual bool ReadFile(const char* pFilename, int Type, void** ppResult,
                        unsigned* pResultLen) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual char* ReadFileStr(const char* pFilename, int Type) override {
    dbg_assert(0, "unimplemented");
    return 0;
  }

  virtual bool FindFile(const char* pFilename, const char* pPath, int Type,
                        char* pBuffer, int BufferSize) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual bool FindFile(const char* pFilename, const char* pPath, int Type,
                        char* pBuffer, int BufferSize,
                        const SHA256_DIGEST* pWantedSha256, unsigned WantedCrc,
                        unsigned WantedSize) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual bool RemoveFile(const char* pFilename, int Type) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual bool RenameFile(const char* pOldFilename, const char* pNewFilename,
                          int Type) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual bool CreateFolder(const char* pFoldername, int Type) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual void GetCompletePath(int Type, const char* pDir, char* pBuffer,
                               unsigned BufferSize) override {
    dbg_assert(0, "unimplemented");
  }

  virtual bool GetHashAndSize(const char* pFilename, int StorageType,
                              SHA256_DIGEST* pSha256, unsigned* pCrc,
                              unsigned* pSize) override {
    dbg_assert(0, "unimplemented");
    return false;
  }

  virtual bool GetFileTime(const char* pFilename, int StorageType,
                           time_t* pCreated, time_t* pModified) override {
    dbg_assert(0, "unimplemented");
    return false;
  }
};

// Executes the game, return crc of final state
int ReplayGame(CMockServer* pServer, CGameContext* pGameServer,
               ITapeStream* pTape, unsigned* crc) {
  pGameServer->OnInit();

  CNetObj_PlayerInput aLastInput[VM_MAX_CLIENTS];
  mem_zero(aLastInput, sizeof(aLastInput));

  while (pTape->HasData()) {
    int event = pTape->GetInt();

    switch (event) {
      case TAPE_RNG_SEED: {
        int Seed = pTape->GetInt();
        vm_srand(Seed);
      } break;
      case TAPE_TICK: {
        pServer->OnTick();
        pGameServer->OnTick();
        pServer->AfterTick();
      } break;
      case TAPE_END_MATCH: {
        // Match is ended
        dbg_assert(!pTape->HasData(), "game shall end now");
      } break;
      case TAPE_CLIENT_CONNECTED: {
        int ClientID = pTape->GetInt();
        int AsSpec = pTape->GetInt();
        pGameServer->OnClientConnected(ClientID, AsSpec != 0);
      } break;
      case TAPE_CLIENT_ENTER: {
        int ClientID = pTape->GetInt();
        pGameServer->OnClientEnter(ClientID);
      } break;
      case TAPE_CLIENT_DROP: {
        int ClientID = pTape->GetInt();
        pGameServer->OnClientDrop(ClientID, "vm_timeout");
        mem_zero(&aLastInput[ClientID], sizeof(CNetObj_PlayerInput));
      } break;
      case TAPE_GAME_MSG_STARTINFO: {
        int ClientID = pTape->GetInt();
        CUnpacker Unpacker;
        Unpacker.Reset(nullptr, 0);
        pGameServer->OnMessage(NETMSGTYPE_CL_STARTINFO, &Unpacker, ClientID);
      } break;
      case TAPE_GAME_MSG_KILL: {
        int ClientID = pTape->GetInt();
        CUnpacker Unpacker;
        Unpacker.Reset(nullptr, 0);
        pGameServer->OnMessage(NETMSGTYPE_CL_KILL, &Unpacker, ClientID);
      } break;
      case TAPE_GAME_MSG_SETTEAM: {
        int ClientID = pTape->GetInt();
        int Team = pTape->GetInt();

        // Re-pack the integer so Teeworlds' API can unpack it natively
        CPacker Packer;
        Packer.Reset();
        Packer.AddInt(Team);

        CUnpacker Unpacker;
        Unpacker.Reset(Packer.Data(), Packer.Size());
        pGameServer->OnMessage(NETMSGTYPE_CL_SETTEAM, &Unpacker, ClientID);
      } break;
      case TAPE_INPUT_DELTA:
      case TAPE_PREDICTED_INPUT: {
        int ClientID = pTape->GetInt();
        int Mask = pTape->GetInt();
        CNetObj_PlayerInput Input = aLastInput[ClientID];

        if (Mask != 0) {
          if (Mask & F_DIR) Input.m_Direction = pTape->GetInt();
          if (Mask & F_TARGET_X) Input.m_TargetX = pTape->GetInt();
          if (Mask & F_TARGET_Y) Input.m_TargetY = pTape->GetInt();
          if (Mask & F_JUMP) Input.m_Jump = pTape->GetInt();
          if (Mask & F_FIRE) Input.m_Fire = pTape->GetInt();
          if (Mask & F_HOOK) Input.m_Hook = pTape->GetInt();
          if (Mask & F_FLAGS) Input.m_PlayerFlags = pTape->GetInt();
          if (Mask & F_W_WEAPON) Input.m_WantedWeapon = pTape->GetInt();
          if (Mask & F_N_WEAPON) Input.m_NextWeapon = pTape->GetInt();
          if (Mask & F_P_WEAPON) Input.m_PrevWeapon = pTape->GetInt();
          aLastInput[ClientID] = Input;
        }

        if (event == TAPE_INPUT_DELTA) {
          pGameServer->OnClientDirectInput(ClientID, &Input);
        } else {
          pGameServer->OnClientPredictedInput(ClientID, &Input);
        }

      } break;
      default: {
        dbg_msg("replayer", "unknown event: %d", event);
        dbg_break();
      } break;
    }
  }

  if (crc != NULL) {
    *crc = CalculateVMStateCRC(pGameServer);
  }
  return 0;
}

#endif
