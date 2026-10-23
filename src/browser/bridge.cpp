// Modified Teeworlds distribution: UTXOPlay transport and presentation adapter.
// Original game rules are compiled from xxuejie's fixed-point CKB fork.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>
#include <replayer/replayer.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/compression.h>
#include <game/layers.h>
#include <game/server/entities/character.h>
#include <game/server/entities/projectile.h>

class Stream : public ITapeStream {
  mutable bool ready=false, ended=false;
  mutable int next=0;
  FILE *record;
  unsigned bytes=0;
public:
  explicit Stream(const char *path):record(fopen(path,"wb")) { if(!record) std::exit(2); }
  ~Stream() { fclose(record); }
  bool HasData() const override {
    if(!ready && !ended) { if(scanf("%d", &next)==1) ready=true; else ended=true; }
    return ready;
  }
  int GetInt() override {
    if(!HasData()) std::exit(3);
    ready=false; unsigned char packed[5];
    auto end=CVariableInt::Pack(packed,next,5); bytes+=end-packed;
    if(bytes>500*1024) std::exit(4);
    fwrite(packed,1,end-packed,record);return next;
  }
  void Flush() { fflush(record); }
};
class BrowserServer: public CMockServer {
public:
  CGameContext *game=nullptr;
  CSnapshotBuilder builder;
  int nextId=0;
  bool live=false;
  std::string last;
  int SnapNewID() override { return nextId++; }
  void SnapFreeID(int) override {}
  void *SnapNewItem(int type,int id,int size) override {return id<0?nullptr:builder.NewItem(type,id,size);}
  void AfterTick() override {
    // Same snapshot cadence during both live play and replay.
    game->OnPreSnap();builder.Init();game->OnSnap(-1);
    alignas(16) char raw[CSnapshot::MAX_SIZE];builder.Finish(raw);
    auto snap=reinterpret_cast<CSnapshot*>(raw);
    std::ostringstream out;out<<"{\"tick\":"<<Tick()<<",\"items\":[";
    bool firstItem=true;
    for(int i=0;i<snap->NumItems();i++) {
      const auto item=snap->GetItem(i);
      if(item->Type()!=NETOBJTYPE_CHARACTER && item->Type()!=NETOBJTYPE_PICKUP && item->Type()!=NETOBJTYPE_PROJECTILE && item->Type()!=NETOBJTYPE_LASER && item->Type()!=NETOBJTYPE_GAMEDATA)continue;
      int size=snap->GetItemSize(i)/sizeof(int);
      std::vector<int> data(item->Data(),item->Data()+size);
      if(item->Type()==NETOBJTYPE_CHARACTER && item->ID()<VM_MAX_CLIENTS) {
        auto player=game->m_apPlayers[item->ID()];
        if(player && player->GetCharacter()) {
          auto core=reinterpret_cast<CNetObj_CharacterCore*>(data.data());
          player->GetCharacter()->BrowserCore(core);core->m_Tick=Tick();
        }
      }
      if(!firstItem)out<<',';firstItem=false;
      out<<'['<<item->Type()<<','<<item->ID()<<",[";
      for(int j=0;j<size;j++){if(j)out<<',';out<<data[j];}out<<"]]";
    }
    out<<"],\"players\":[";bool first=true;
    for(int i=0;i<VM_MAX_CLIENTS;i++)if(game->m_apPlayers[i]) {
      if(!first)out<<',';first=false;auto p=game->m_apPlayers[i];
      out<<"{\"id\":"<<i<<",\"score\":"<<p->m_Score<<",\"alive\":"<<(p->GetCharacter()?"true":"false")<<'}';
    }
    out<<"],\"projectiles\":[";first=true;
    for(auto e=game->m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE);e;e=e->TypeNext()) {
      auto p=static_cast<CProjectile*>(e);CNetObj_Projectile info;p->FillInfo(&info);
      auto pos=p->GetPos(real_t(Tick()-info.m_StartTick)/int(SERVER_TICK_SPEED));
      if(!first)out<<',';first=false;
      out<<'['<<int(pos.x)<<','<<int(pos.y)<<','<<info.m_Type<<']';
    }
    out<<"]}";last=out.str();
    game->OnPostSnap();
    if(live && Tick()%2==0) std::cout<<"{\"type\":\"frame\",\"state\":"<<last<<"}"<<std::endl;
  }
};
int main(int argc,const char **argv) {
  if(argc!=5 || (strcmp(argv[1],"live") && strcmp(argv[1],"replay"))) {
    fprintf(stderr,"Usage: teeworlds_bridge live|replay tape.bin map.map config.cfg\n");return 1;
  }
  BrowserServer server;server.live=!strcmp(argv[1],"live");
  CGameContext game;server.game=&game;
  IKernel *kernel=IKernel::Create();CMockStorage storage;
  auto config=CreateConfigManager();auto map=CreateEngineMap();auto console=CreateConsole(CFGFLAG_SERVER);
  kernel->RegisterInterface(&server);kernel->RegisterInterface(&game);
  kernel->RegisterInterface(config);kernel->RegisterInterface(static_cast<IEngineMap*>(map));
  kernel->RegisterInterface(static_cast<IMap*>(map));kernel->RegisterInterface(console);kernel->RegisterInterface(&storage);
  config->Init(CFGFLAG_SERVER);console->Init();console->ExecuteFile(argv[4]);
  if(!map->Load(argv[3]))return 2;
  if(server.live) {
    CLayers layers;layers.Init(kernel,map);auto layer=layers.GameLayer();
    auto tiles=static_cast<CTile*>(map->GetData(layer->m_Data));
    std::cout<<"{\"type\":\"map\",\"width\":"<<layer->m_Width<<",\"height\":"<<layer->m_Height<<",\"tiles\":[";
    for(int i=0;i<layer->m_Width*layer->m_Height;i++){if(i)std::cout<<',';std::cout<<int(tiles[i].m_Index);}
    std::cout<<"],\"types\":{\"character\":"<<NETOBJTYPE_CHARACTER<<",\"pickup\":"<<NETOBJTYPE_PICKUP<<",\"laser\":"<<NETOBJTYPE_LASER<<"}}"<<std::endl;
  }
  unsigned crc=0;
  if(server.live) {Stream tape(argv[2]);ReplayGame(&server,&game,&tape,&crc);tape.Flush();}
  else {
    FILE *file=fopen(argv[2],"rb");if(!file)return 2;
    fseek(file,0,SEEK_END);long n=ftell(file);rewind(file);if(n<=0 || n>500*1024)return 4;
    std::vector<unsigned char> bytes(n);if(fread(bytes.data(),1,n,file)!=(size_t)n)return 2;fclose(file);
    CMemoryTapeStream tape(bytes.data(),n);ReplayGame(&server,&game,&tape,&crc);
  }
  if(server.last.empty())return 5;
  std::cout<<"{\"type\":\"result\",\"crc\":"<<crc<<",\"state\":"<<server.last<<"}"<<std::endl;
  // These objects use process-wide pools; process exit owns their teardown.
  std::_Exit(0);
}
