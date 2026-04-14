// When stack is reordered, we can use full 4MB memory range for heap
#ifdef CKB_REORDER_STACK
long __ckb_hijack_brk_max = 0x00400000;
#endif

#include <replayer/replayer.h>

int main() {
  CCkbTapeStream tape(1, CKB_SOURCE_INPUT, 0);
  CGameContext* pGameServer = new CGameContext;
  IKernel* pKernel = IKernel::Create();

  CMockServer mockServer;
  CMockStorage mockStorage;

  IConfigManager* pConfigManager = CreateConfigManager();
  IEngineMap* pEngineMap = CreateEngineMap();
  IConsole* pConsole = CreateConsole(CFGFLAG_SERVER);

  {
    bool fail = false;

    fail = fail || !pKernel->RegisterInterface(&mockServer);
    fail = fail || !pKernel->RegisterInterface(&mockStorage);

    fail = fail || !pKernel->RegisterInterface(pGameServer);
    fail = fail || !pKernel->RegisterInterface(pConfigManager);
    fail = fail ||
           !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap));
    fail = fail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));
    fail = fail || !pKernel->RegisterInterface(pConsole);

    dbg_assert(!fail, "registering interface");
  }

  pConfigManager->Init(CFGFLAG_SERVER);
  pConsole->Init();
  pConsole->ExecuteFile("3");

  dbg_assert(pEngineMap->Load("2"), "load map");

  unsigned crc = 0xFFFFFFFF;
  dbg_assert(ReplayGame(&mockServer, pGameServer, &tape, &crc) == 0,
             "replay game");
  dbg_msg("replayer", "CRC: %x", crc);

  // No need to delete anything, CKB-VM programs are short-lived.

  return 0;
}
