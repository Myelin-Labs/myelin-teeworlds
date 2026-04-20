#include <stdio.h>

#include "replayer.h"

#include <engine/shared/demo.h>
#include <engine/shared/snapshot.h>

class CMockSnapServer : public CMockServer {
  char m_Names[VM_MAX_CLIENTS][64];

 public:
  IGameServer* m_pGameServer;
  CSnapshotDelta m_SnapshotDelta;
  CSnapshotBuilder m_SnapshotBuilder;
  CDemoRecorder m_DemoRecorder;
  int m_NextID;

  CMockSnapServer() : m_DemoRecorder(&m_SnapshotDelta), m_NextID(0) {
    for (int i = 0; i < VM_MAX_CLIENTS; i++) {
      str_format(m_Names[i], 64, "tee %d", i + 1);
    }
  }

  IGameServer* GameServer() { return m_pGameServer; }

  void Init(IKernel* pKernel) {
    m_SnapshotBuilder.Init();

    IConsole* console = pKernel->RequestInterface<IConsole>();
    IStorage* storage = pKernel->RequestInterface<IStorage>();
    m_DemoRecorder.Init(console, storage);

    m_pGameServer = pKernel->RequestInterface<IGameServer>();
  }

  virtual const char* ClientName(int ClientID) const {
    if (ClientID < VM_MAX_CLIENTS) {
      return m_Names[ClientID];
    } else {
      return "unknown";
    }
  }

  virtual void AfterTick() override {
    GameServer()->OnPreSnap();

    char aData[CSnapshot::MAX_SIZE];

    m_SnapshotBuilder.Init();
    GameServer()->OnSnap(-1);
    int SnapshotSize = m_SnapshotBuilder.Finish(aData);

    m_DemoRecorder.RecordSnapshot(Tick(), aData, SnapshotSize);

    GameServer()->OnPostSnap();
  }

  virtual int SnapNewID() override {
    // For single replayer, it should not overflow.
    dbg_assert(m_NextID + 1 > m_NextID, "ID should not overflow");
    return m_NextID++;
  }

  virtual void SnapFreeID(int ID) override {
    dbg_assert(ID < m_NextID, "ID must be smaller than the next");
  }

  virtual void* SnapNewItem(int Type, int ID, int Size) override {
    dbg_assert(Type >= 0 && Type <= 0xffff, "incorrect type");
    return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
  }

  virtual void SnapSetStaticsize(int ItemType, int Size) override {
    m_SnapshotDelta.SetStaticsize(ItemType, Size);
  }
};

int main(int argc, const char** argv) {
  if (argc != 5) {
    fprintf(stderr,
            "Usage: %s <dump file> <map file> <config file> <dump file>\n",
            argv[0]);
    return -1;
  }

  dbg_logger_stdout();
  dbg_logger_debugger();

  uint8_t* dumpData = NULL;
  unsigned dumpDataLength = 0;
  {
    IOHANDLE fp = io_open(argv[1], IOFLAG_READ);
    dbg_assert(fp != NULL, "open dump file");
    io_read_all(fp, (void**)&dumpData, &dumpDataLength);
    io_close(fp);
  }

  CMemoryTapeStream tape(dumpData, dumpDataLength);
  CGameContext* pGameServer = new CGameContext;
  IKernel* pKernel = IKernel::Create();

  CMockSnapServer mockServer;
  // CKB has its own main method, here we are switching to CStorage
  // so we can record snapshots.
  // CMockStorage mockStorage;
  IStorage* pStorage =
      CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv);

  IConfigManager* pConfigManager = CreateConfigManager();
  IEngineMap* pEngineMap = CreateEngineMap();
  IConsole* pConsole = CreateConsole(CFGFLAG_SERVER);

  {
    bool fail = false;

    fail = fail || !pKernel->RegisterInterface(&mockServer);
    fail = fail || !pKernel->RegisterInterface(pGameServer);
    fail = fail || !pKernel->RegisterInterface(pConfigManager);
    fail = fail ||
           !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap));
    fail = fail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));
    fail = fail || !pKernel->RegisterInterface(pConsole);
    fail = fail || !pKernel->RegisterInterface(pStorage);

    dbg_assert(!fail, "registering interface");
  }

  pConfigManager->Init(CFGFLAG_SERVER);
  pConsole->Init();
  pConsole->ExecuteFile(argv[3]);
  mockServer.Init(pKernel);

  dbg_assert(pEngineMap->Load(argv[2]), "load map");

  mockServer.m_DemoRecorder.Start(
      argv[4], pGameServer->NetVersion(), pConfigManager->Values()->m_SvMap,
      pEngineMap->Sha256(), pEngineMap->Crc(), "server");

  // Do the job
  unsigned crc = 0xFFFFFFFF;
  dbg_assert(ReplayGame(&mockServer, pGameServer, &tape, &crc) == 0,
             "replay game");
  dbg_msg("replayer", "CRC: %x", crc);

  mockServer.m_DemoRecorder.Stop();

  delete pConsole;
  delete pEngineMap;
  delete pConfigManager;
  delete pKernel;
  delete pGameServer;
  free(dumpData);

  return 0;
}
