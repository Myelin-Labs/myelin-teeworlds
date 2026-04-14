#include <stdio.h>

#include "replayer.h"

int main(int argc, const char** argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <dump file> <map file> <config file>\n",
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

  CMockServer mockServer;
  CMockStorage mockStorage;

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
    fail = fail || !pKernel->RegisterInterface(&mockStorage);

    dbg_assert(!fail, "registering interface");
  }

  pConfigManager->Init(CFGFLAG_SERVER);
  pConsole->Init();
  pConsole->ExecuteFile(argv[3]);

  dbg_assert(pEngineMap->Load(argv[2]), "load map");

  // Do the job
  unsigned crc = 0xFFFFFFFF;
  dbg_assert(ReplayGame(&mockServer, pGameServer, &tape, &crc) == 0,
             "replay game");
  dbg_msg("replayer", "CRC: %x", crc);

  delete pConsole;
  delete pEngineMap;
  delete pConfigManager;
  delete pKernel;
  delete pGameServer;
  free(dumpData);

  return 0;
}
