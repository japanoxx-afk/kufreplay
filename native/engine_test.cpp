// Exercises the real adapter callbacks against a synthetic ABI-compatible fixture.
// This is not a KUF game and is never packaged as a user replay.
#include "engine.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
uint32_t resource=100,queueCount=0,applied=0;
uint32_t timerNow=100,selectionCalls=0;
bool deferredNetworkFixture=false;
uint32_t __stdcall fakeTimer() {return timerNow;}
uint32_t __fastcall fakeSelection(void*,void*,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g,uint32_t h) {
    ++selectionCalls;return a+b+c+d+e+f+g+h;
}
uint32_t __fastcall fakeChecksum(void*,void*) {return resource+at<uint32_t>(0xbed1e8)+at<uint32_t>(0xb827dc);}
uint32_t __fastcall fakeApply(void*,void*,uint32_t n,const void* bytes) {
    if(deferredNetworkFixture && n==96 && *(const uint16_t*)bytes==1007) {
        ++applied;return initializeMapHook(at<void*>(0xb8282c),nullptr,0x12,0x34);
    }
    if(n!=4) return 0;
    uint32_t value;memcpy(&value,bytes,4);
    resource+=value;
    at<uint32_t>(0xbed1e8)=at<uint32_t>(0xbed1e8)*0x8088405+1;
    ++at<uint32_t>(0xb827dc);++applied;return 1;
}
uint32_t __fastcall fakeAdvance(void* self,void*,uint32_t,uint32_t) {
    if(playback) {
        void* queue=at<void*>(0xb870c8);void** table=*reinterpret_cast<void***>(queue);
        using Count=uint32_t(__thiscall*)(void*);using Pop=uint32_t(__thiscall*)(void*,void*,uint32_t*);
        if(savedInitial.networkMode==1) {
            if(at<unsigned char>(0xb83ad0)!=1) exit(2);
            using Peer=uint32_t(__thiscall*)(void*,uint32_t);
            if(reinterpret_cast<Peer>(table[12])(queue,0)!=0) exit(2);
        }
        while(reinterpret_cast<Count>(table[4])(queue)) {
            unsigned char bytes[MaxCommand]{};uint32_t metadata=0;
            uint32_t n=reinterpret_cast<Pop>(table[2])(queue,bytes,&metadata);
            commandHook(self,nullptr,n,bytes);
        }
        return 1;
    }
    if(deferredNetworkFixture) {
        if(at<uint32_t>(0xb8a280)==20) {
            unsigned char packet[96]{};*(uint16_t*)packet=1007;
            commandHook(self,nullptr,sizeof(packet),packet);
            uint32_t command=5;commandHook(self,nullptr,4,&command);
        }
        return 1;
    }
    const uint32_t commands[]={5,9};
    for(auto command:commands) commandHook(self,nullptr,4,&command);
    return 1;
}
uint32_t __fastcall fakeCount(void*,void*) {return queueCount;}
bool mapThrows=false;
uint32_t __fastcall fakeMap(void*,void*,uint32_t a,uint32_t b) {
    if(a!=0x12 || b!=0x34 || at<unsigned char>(0xb83ad0)!=1 || at<uint32_t>(0xb3bae8)!=30) exit(2);
    if(mapThrows) RaiseException(0xe0000123,0,0,nullptr);
    at<uint32_t>(0xbed1e8)=51;
    return 567;
}
bool mapExceptionRestored(void* self) {
    mapThrows=true;
    __try {initializeMapHook(self,nullptr,0x12,0x34);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    mapThrows=false;
    return failed.load() && notice.load()==13 && at<unsigned char>(0xb83ad0)==0;
}
uint32_t __fastcall fakeResultRender(void*,void*) {return 123;}
bool processThrows=false;
uint32_t __fastcall fakeNetworkProcess(void*,void*) {
    auto queue=at<void*>(0xb870c8);auto table=*(void***)queue;
    using Tick=uint32_t(__thiscall*)(void*,uint32_t);
    if(at<uint16_t>(0xb3baf8)!=1 || ((Tick)table[6])(queue,0)!=at<uint32_t>(0xb8a280)) exit(2);
    if(processThrows) RaiseException(0xe0000123,0,0,nullptr);
    return 456;
}
bool processExceptionRestored(void* self) {
    bool caught=false;processThrows=true;
    __try {processHook(self,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=true;}
    processThrows=false;return caught;
}
uint32_t __fastcall fakeObserverDraw(void* self,void*) {
    if(at<unsigned char>(0xb3ba61)!=1 || ((unsigned char*)self)[0x6b]!=0 || ((unsigned char*)self)[0x69]!=0) exit(2);
    ((unsigned char*)self)[0x6b]=7;return 321;
}
uint32_t __fastcall fakeObserverThrow(void*,void*) {RaiseException(0xe0000123,0,0,nullptr);return 0;}
uint32_t __fastcall fakeAudio(void* self,void*,uint32_t a,uint32_t b,int x,int y,uint32_t c) {
    if(at<int>(0xbf2588)!=1066 || observerAudioVisibility(self,nullptr,7,x,y)!=1) {fprintf(stderr,"audio fixture width/depth %d/%u\n",at<int>(0xbf2588),observerAudioDepth);exit(2);}
    return a+b+c;
}
uint32_t __fastcall fakeAudioThrow(void*,void*,uint32_t,uint32_t,int,int,uint32_t) {RaiseException(0xe0000123,0,0,nullptr);return 0;}
bool audioExceptionRestored(void* self) {
    bool caught=false;
    __try {observerAudio(self,(Audio)fakeAudioThrow,1,2,3,4,5);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    return caught && observerAudioDepth==0 && at<int>(0xbf2588)==866;
}
bool panelThrows=false;
uint32_t syncedSelection=99;
uint32_t __fastcall fakePanelSync(void*,void*,uint32_t write) {syncedSelection=write;return 1;}
uint32_t __fastcall fakePanel(void* self,void*) {
    auto game=(unsigned char*)at<void*>(0xb8282c);
    if(*(uint32_t*)(game+0x664)!=1 || *(int16_t*)((unsigned char*)self+0x1898)!=1 || !observerPanelDrawing) {fprintf(stderr,"panel fixture owner/count/drawing %u/%d/%d\n",*(uint32_t*)(game+0x664),*(int16_t*)((unsigned char*)self+0x1898),observerPanelDrawing);exit(2);}
    observerSelectionSync(self,nullptr,1);
    if(panelThrows) RaiseException(0xe0000123,0,0,nullptr);
    return 456;
}
bool panelExceptionRestored(void* self) {
    bool caught=false;panelThrows=true;
    __try {observerPanelHook(self,nullptr);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    panelThrows=false;
    auto game=(unsigned char*)at<void*>(0xb8282c);
    return caught && *(uint32_t*)(game+0x664)==0 && !observerPanelDrawing;
}
bool observerExceptionRestored(void* self) {
    bool caught=false;
    __try {observerDisplay(self,reinterpret_cast<Process>(fakeObserverThrow));}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    return caught && ((unsigned char*)self)[0x6b]==1 && at<unsigned char>(0xb3ba61)==0 && displayDepth==0;
}
uint32_t __fastcall fakePop(void*,void*,void*,uint32_t*) {--queueCount;return 4;}
void resetState() {
    closePending.store(false);closeReady.store(false);
    failed.store(false);terminal.store(false);notice.store(0);active=false;producer=0;cursor=8;
    resource=100;applied=0;at<uint32_t>(0xb8a280)=1;at<uint32_t>(0xbed1e8)=0;at<uint32_t>(0xb827dc)=0;
}
void simulate() {++at<uint32_t>(0xb8a280);resource+=3;}
void require(bool condition,const char* message) {
    if(!condition) {fprintf(stderr,"FAILED: %s\n",message);exit(1);}
}
}

int main() {
    unsigned char nameSelf[0x520]{},nameStorage[80]{};
    char* nameText=(char*)nameStorage+16;
    strcpy_s(nameText,64,"TestUser_2");*(int*)(nameText-12)=10;
    *(char**)(nameSelf+0x500)=nameText;
    PlayerNames namesFixture{};captureNames(nameSelf,namesFixture);
    require(!strcmp(namesFixture.names[0],"TestUser_2") && !namesFixture.names[1][0],"capture lobby CString names without pointers");
    savedNames=namesFixture;char nameFallback[16];
    require(!strcmp(observerName(0,nameFallback),"TestUser_2") && !strcmp(observerName(1,nameFallback),"P2"),"display captured names and preserve legacy fallback");
    savedNames={1,{}};
    State counterActual{20,63084912,18,64821323},counterAfter{20,63084912,24,64821329};
    CommandHeader counterExpected{20,63084912,24,12};
    require(bootstrapCounterOnly(counterActual,counterExpected,counterAfter),"allow only bootstrap diagnostic counter difference");
    State different=counterActual;different.seed++;
    require(!bootstrapCounterOnly(different,counterExpected,counterAfter),"never normalize a different RNG generator seed");
    different=counterActual;different.checksum++;
    require(!bootstrapCounterOnly(different,counterExpected,counterAfter),"never normalize a different gameplay checksum");
    different=counterActual;different.tick++;
    require(!bootstrapCounterOnly(different,counterExpected,counterAfter),"never normalize a different simulation tick");
    different=counterAfter;different.randomCount++;
    require(!bootstrapCounterOnly(counterActual,counterExpected,different),"require consistent command and batch counter checkpoint");
    RECT cursorClipBefore{},cursorClipAfter{};
    GetClipCursor(&cursorClipBefore);
    int cursorCount=ShowCursor(FALSE);
    { DialogCursor dialogCursor;
      int visibleCount=ShowCursor(TRUE);ShowCursor(FALSE);
      require(visibleCount>=1,"dialog cursor visible on calling thread"); }
    int restoredCount=ShowCursor(TRUE);
    require(restoredCount==cursorCount+1,"dialog restores cursor display count");
    GetClipCursor(&cursorClipAfter);
    require(EqualRect(&cursorClipBefore,&cursorClipAfter)!=FALSE,"dialog restores cursor clip");
    base=(uintptr_t)VirtualAlloc(nullptr,0x882000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    require(base!=0,"fixture memory");
    textBase=base;textSize=0x882000;expectedText.resize(textSize);
    ring=(unsigned char*)VirtualAlloc(nullptr,RingSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(ring!=nullptr,"ring memory");
    // The checksum call target is a five-byte tail jump to a fixture function with the same ECX ABI.
    auto code=(unsigned char*)address(0x4dae50);code[0]=0xe9;
    int32_t delta=(int32_t)((uintptr_t)fakeChecksum-(uintptr_t)code-5);memcpy(code+1,&delta,4);
    unsigned char self[0xcb0]{};
    at<void*>(0xb870c0)=&self;
    strcpy_s((char*)address(0xb83ad9),256,"res\\map\\Skirmish\\");
    strcpy_s((char*)address(0xb83bd9),256,"fixture.kfm");
    void* vtable[13]{};vtable[2]=(void*)fakePop;vtable[4]=(void*)fakeCount;
    void** queue=vtable;at<void*>(0xb870c8)=&queue;
    apply=reinterpret_cast<Apply>(fakeApply);advance=reinterpret_cast<Advance>(fakeAdvance);
    resetState();playback=false;
    advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);simulate();
    State fullEnd{};require(snapshot(&self,fullEnd),"fixture live ending snapshot");emit(End,&fullEnd,sizeof(fullEnd));active=false;
    require(!failed.load() && applied==4,"capture two real callback batches");
    input.assign(Magic,Magic+8);
    uint32_t size=head.load();size_t begin=input.size();input.resize(begin+size);readRing(0,input.data()+begin,size);
    resetState();playback=true;queueCount=3;
    advanceHook(&self,nullptr,0,0);require(!failed.load() && queueCount==0,"discard spectator/live commands");
    simulate();advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    require(terminal.load() && !failed.load() && notice.load()==10 && applied==4 && resource==134,"identical state and completed replay");
    require(queue==vtable,"native dispatch restores the original queue vtable");
    auto manager=(unsigned char*)address(0x600000);memset(manager,0,0x40000);
    *(uint32_t*)(manager+0xdc38)=1;*(uint32_t*)(manager+0xdc3c)=0;
    ((unsigned char**)(manager+0x2c38))[0]=self;at<void*>(0xb870c0)=manager;
    self[0x6b]=1;self[0x69]=1;at<unsigned char>(0xb3ba61)=0;active=true;
    require(observerDisplay(self,reinterpret_cast<Process>(fakeObserverDraw))==321,"spectator display uses complete terrain and unit fog cache");
    require(self[0x6b]==1 && at<unsigned char>(0xb3ba61)==0 && displayDepth==0,"spectator display restores original fog state after callback");
    require(observerExceptionRestored(self),"spectator display restores original fog state on an exception");
    require(self[0x69]==1,"invisibility restored after ordinary and exceptional rendering");
    auto context=(unsigned char*)address(0x650000);memset(context,0,0x980);
    *(void**)context=address(0xa2cea8);*(int*)(context+0x974)=866;
    at<void*>(0xb8282c)=self;*(void**)(self+0x9b4)=context;
    at<int>(0xbf2578)=1066;at<int>(0xbf2588)=866;at<int>(0xbf2580)=0;showObserverHud=false;
    enterObserverDisplay();require(at<int>(0xbf2588)==1066 && *(int*)(context+0x974)==1066,"HUD hidden extends sprite clipping to the complete viewport");leaveObserverDisplay();
    require(at<int>(0xbf2588)==866 && *(int*)(context+0x974)==866,"viewport and sprite clips restored after ordinary rendering");
    require(observerExceptionRestored(self) && at<int>(0xbf2588)==866 && *(int*)(context+0x974)==866,"viewport and sprite clips restored after rendering exception");
    require(observerAudio(self,(Audio)fakeAudio,1,2,1000,100,5)==8 && observerAudioDepth==0 && at<int>(0xbf2588)==866,"spatial audio uses full hidden-HUD viewport with scoped visibility");
    require(audioExceptionRestored(self),"audio exception restores width and visibility scope");
    showObserverHud=true;
    auto ui=(unsigned char*)address(0x620000);memset(ui,0,0x1908);
    auto uiMethods=(void**)address(0x630000);memset(uiMethods,0,512);uiMethods[4]=(void*)fakePanel;
    *(void**)ui=uiMethods;*(void**)(ui+0x23c)=address(0x610000);*(int16_t*)(ui+0x1898)=42;
    ((void**)(self+0x48))[0]=address(0x610000);((void**)(self+0x48))[1]=address(0x611000);*(uint32_t*)(self+0x664)=0;
    auto selectedUnit=(unsigned char*)address(0x660000);memset(selectedUnit,0,0x100);
    *(int16_t*)(selectedUnit+8)=1;*(uint16_t*)(selectedUnit+0x14)=100;
    ((unsigned char**)(manager+0x2c38))[0]=selectedUnit;observerSelected=0;observerPanelSource=nullptr;
    auto syncCode=(unsigned char*)address(0x512a00);syncCode[0]=0xe9;
    delta=(int32_t)((uintptr_t)fakePanelSync-(uintptr_t)syncCode-5);memcpy(syncCode+1,&delta,4);
    require(observerPanelHook(ui,nullptr)==456 && syncedSelection==0,"observer panel reads selected owner without writing player selection");
    require(*(uint32_t*)(self+0x664)==0 && *(int16_t*)(ui+0x1898)==42 && *(void**)ui==uiMethods,"observer panel preserves native player and original UI selection");
    require(panelExceptionRestored(ui),"observer panel restores local player on exception");
    originalSelection=(Selection)fakeSelection;
    require(selectionHook(self,nullptr,1,2,3,4,5,6,7,8)==0 && selectionCalls==0,"observer input cannot alter native selection");
    inDispatch=true;
    require(selectionHook(self,nullptr,1,2,3,4,5,6,7,8)==36 && selectionCalls==1,"recorded selection dispatch retains all eight arguments");
    inDispatch=false;
    at<void*>(0xa16cfc)=(void*)fakeTimer;clockWall=clockValue=100;replaySpeed=1;timerNow=110;
    require(playbackClock()==110,"one times simulation clock");
    replaySpeed=2;timerNow=120;require(playbackClock()==130,"two times simulation clock");
    replaySpeed=4;timerNow=130;require(playbackClock()==170,"four times simulation clock");
    replaySpeed=1;timerNow=140;require(playbackClock()==180,"speed changes preserve clock continuity");
    savedInitial.networkMode=1;automaticStarted=true;savedBootstrap.tickInterval=30;
    clockWall=clockValue=clockTarget=100;replaySpeed=4;timerNow=130;
    require(playbackClock()==130 && clockTarget==220,"network clock limits each simulation slice to one tick");
    require(playbackClock()==160 && playbackClock()==190 && playbackClock()==220,"network clock preserves scaled time debt across simulation slices");
    exitConfirmationOpen=true;timerNow=5000;
    require(playbackClock()==220 && clockWall==5000,"exit confirmation freezes simulation time without accumulating modal wall time");
    require(applicationHook(self,nullptr)==1 && processHook(self,nullptr)==1,"exit confirmation suppresses reentrant game simulation");
    exitConfirmationOpen=false;timerNow=5010;
    require(playbackClock()==250 && clockTarget==260,"continuing after exit confirmation retains scaled clock without dialog time jump");
    savedInitial.networkMode=0;automaticStarted=false;replaySpeed=1;
    active=false;at<void*>(0xb870c0)=&self;
    resetState();at<uint32_t>(0xbed1e8)=9;advanceHook(&self,nullptr,0,0);
    require(failed.load() && notice.load()==7 && applied==0,"reject mismatched initial RNG before injection");
    resetState();advanceHook(&self,nullptr,0,0);simulate();++resource;advanceHook(&self,nullptr,0,0);
    require(failed.load() && notice.load()==8 && applied==2,"detect later simulation divergence");
    resetState();playback=false;producer=GetCurrentThreadId();tail.store(0);head.store(RingSize-1);
    State s{};emit(Before,&s,sizeof(s));require(failed.load() && notice.load()==2,"bounded ring overflow fails without blocking");
    resetState();head.store(0);tail.store(0);at<void*>(0xb8282c)=&self;
    advanceHook(&self,nullptr,0,0);
    simulate();advanceHook(&self,nullptr,0,0);
    uint32_t beforeClose=head.load();
    at<void*>(0xb870c0)=nullptr; // Real quit frees this before the game-object destructor.
    require(recordingWindowProc(nullptr,WM_CLOSE,0,0)==0 && closePending.load() && !active,"normal close queues match ending without blocking");
    require(!failed.load(),"ending never reads freed game state");
    Record closeRecord{};readRing(beforeClose,&closeRecord,sizeof(closeRecord));
    require(closeRecord.kind==TrimEnd,"ending uses verified prior dispatch boundary");
    at<void*>(0xb870c0)=&self;
    require(head.load()==beforeClose+sizeof(Record)+sizeof(State),"exactly one close end record");
    uint32_t afterClose=head.load();
    require(recordingWindowProc(nullptr,WM_CLOSE,0,0)==0 && head.load()==afterClose,"repeated close waits for writer acknowledgement");
    resetState();head.store(0);tail.store(0);advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    lastAdvanceAt.store(0);
    recordingWindowProc(nullptr,RecordPulse,0,0);
    require(active,"a paused match with unchanged map path must not finalize");
    strcpy_s((char*)address(0xb83ad9),256,"res\\map\\");
    recordingWindowProc(nullptr,RecordPulse,0,0);
    require(!active && !failed.load(),"results/menu directory transition finalizes retained game object");
    resetState();head.store(0);tail.store(0);advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    resultRender=reinterpret_cast<Process>(fakeResultRender);
    require(resultRenderHook(&self,nullptr)==123 && !active && !failed.load(),"result rendering ends capture with unchanged map path and preserves original callback");
    resetState();head.store(0);tail.store(0);playback=false;
    at<unsigned char>(0xb83ad0)=1;
    advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    require(active && !failed.load() && applied==4 && resource==131,"network capture preserves native command effects");
    require(initial.networkMode==1 && at<unsigned char>(0xb83ad0)==1 && queue==vtable,"network capture preserves network mode and queue");
    Record networkBegin{};readRing(0,&networkBegin,sizeof(networkBegin));
    require(networkBegin.kind==Begin && networkBegin.size==sizeof(Initial),"network capture emits initial state");
    Record networkNext{};readRing(sizeof(Record)+sizeof(Initial),&networkNext,sizeof(networkNext));
    require(networkNext.kind==Names && networkNext.size==sizeof(PlayerNames),"network capture includes portable player names");
    readRing(sizeof(Record)+sizeof(Initial)+sizeof(Record)+sizeof(PlayerNames),&networkNext,sizeof(networkNext));
    require(networkNext.kind==Before,"network capture does not emit single-player menu setup");
    uint32_t networkEnd=head.load();
    recordingWindowProc(nullptr,WM_CLOSE,0,0);
    require(!active && !failed.load() && head.load()==networkEnd+sizeof(Record)+sizeof(State),"network Alt-F4 requests durable trimmed completion");
    resetState();head.store(0);tail.store(0);playback=false;
    advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    at<unsigned char>(0xb83ad0)=2;advanceHook(&self,nullptr,0,0);
    require(!active && !failed.load(),"leaving network mode finalizes capture before unsupported mode");
    resetState();playback=true;at<unsigned char>(0xb83ad0)=1;
    advanceHook(&self,nullptr,0,0);
    require(!active && cursor==8 && applied==0 && !inDispatch,"unsupported network playback leaves input untouched");
    at<unsigned char>(0xb83ad0)=0;playback=false;
    resetState();head.store(0);tail.store(0);playback=false;
    at<unsigned char>(0xb83ad0)=1;at<uint16_t>(0xb83ad2)=0;at<unsigned char>(0xb83ad4)=1;
    at<uint16_t>(0xb83ad6)=3;at<uint32_t>(0xb3bae8)=30;
    *(uint32_t*)(self+0x664)=0;memset(self+0xc78,0,16);
    for(unsigned i=0;i<11;++i) {
        auto slot=(uint32_t*)address(0xb83cdc+i*0x40c);
        slot[0]=i==0?1:2;slot[1]=1000;slot[2]=1;at<unsigned char>(0xb83ce8+i*0x40c)=1;
    }
    at<uint32_t>(0xb86554)=1952796276;
    memset(self+0xc81,0xcd,3); // Native option structure padding, not enum fields.
    initializeMap=(Advance)fakeMap;
    require(initializeMapHook(self,nullptr,0x12,0x34)==567 && hasCapturedBootstrap,"capture map bootstrap before RNG initialization");
    advanceHook(self,nullptr,0,0);simulate();advanceHook(self,nullptr,0,0);simulate();
    State networkFinal{};snapshot(self,networkFinal);emit(End,&networkFinal,sizeof(networkFinal));active=false;
    input.assign(Magic,Magic+8);size=head.load();begin=input.size();input.resize(begin+size);readRing(0,input.data()+begin,size);
    savedInitial=initial;savedBootstrap=capturedBootstrap;hasBootstrap=true;
    resetState();playback=true;at<unsigned char>(0xb83ad0)=0;
    void* queueBefore=at<void*>(0xb870c8);
    require(initializeMapHook(self,nullptr,0x12,0x34)==567,"offline map bootstrap uses recorded map RNG and descriptors");
    require(at<void*>(0xb870c8)==queueBefore && at<unsigned char>(0xb83ad0)==0,"map bootstrap restores offline queue and network mode");
    advanceHook(self,nullptr,0,0);simulate();advanceHook(self,nullptr,0,0);simulate();advanceHook(self,nullptr,0,0);
    require(terminal.load() && !failed.load() && notice.load()==10 && applied==4,"network bootstrap and command replay reproduce final state");
    require(at<unsigned char>(0xb83ad0)==0 && queue==vtable,"network dispatch restores offline mode and original queue");
    require(mapExceptionRestored(self) && at<void*>(0xb870c8)==queueBefore,"exception restores map queue and network mode");
    hasBootstrap=false;savedInitial={};hasCapturedBootstrap=false;playback=false;
    resetState();head.store(0);tail.store(0);deferredNetworkFixture=true;resource=0;
    at<void*>(0xb8282c)=self;at<unsigned char>(0xb83ad0)=1;at<uint32_t>(0xb8a280)=10;
    for(unsigned i=0;i<11;++i) at<uint32_t>(0xb83cdc+i*0x40c)=3;
    advanceHook(self,nullptr,0,0);
    at<uint32_t>(0xb8a280)=20;advanceHook(self,nullptr,0,0);
    at<uint32_t>(0xb8a280)=30;snapshot(self,networkFinal);emit(End,&networkFinal,sizeof(networkFinal));active=false;
    input.assign(Magic,Magic+8);size=head.load();begin=input.size();input.resize(begin+size);readRing(0,input.data()+begin,size);
    HANDLE fixtureFile=CreateFileW(L"build\\native\\deferred-fixture.trace",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD fixtureWritten=0;
    require(fixtureFile!=INVALID_HANDLE_VALUE && WriteFile(fixtureFile,input.data(),(DWORD)input.size(),&fixtureWritten,nullptr) && fixtureWritten==input.size(),"write deferred native fixture");
    CloseHandle(fixtureFile);hasBootstrap=false;
    require(readInput(L"build\\native\\deferred-fixture.trace") && hasBootstrap,"native reader finds late bootstrap after startup command");
    DeleteFileW(L"build\\native\\deferred-fixture.trace");
    resetState();playback=true;resource=0;at<unsigned char>(0xb83ad0)=0;at<uint32_t>(0xb8a280)=1;
    require(initializeMapHook(self,nullptr,0x12,0x34)==1 && at<uint32_t>(0xbed1e8)==0,"defer native map initialization until recorded command");
    advanceHook(self,nullptr,0,0);
    require(!active && cursor==8 && applied==0,"wait for recorded initial tick without consuming commands");
    at<uint32_t>(0xb8a280)=10;advanceHook(self,nullptr,0,0);
    size_t waitingCursor=cursor;at<uint32_t>(0xb8a280)=11;advanceHook(self,nullptr,0,0);
    require(cursor==waitingCursor && applied==0,"preserve intervening simulation ticks before next network batch");
    at<uint32_t>(0xb8a280)=20;advanceHook(self,nullptr,0,0);
    at<uint32_t>(0xb8a280)=21;waitingCursor=cursor;advanceHook(self,nullptr,0,0);
    require(!terminal.load() && cursor==waitingCursor,"wait for recorded final boundary without ending replay early");
    at<uint32_t>(0xb8a280)=30;advanceHook(self,nullptr,0,0);
    require(terminal.load() && !failed.load() && notice.load()==10 && applied==2,"deferred bootstrap reproduces native final state");
    resetState();playback=true;savedInitial.networkMode=1;savedInitial.state.seed=1;
    process=(Process)fakeNetworkProcess;at<uint16_t>(0xb3baf4)=0;at<uint16_t>(0xb3baf8)=3;
    require(processHook(self,nullptr)==456 && queue==vtable && at<uint16_t>(0xb3baf8)==3,"network tick polling restores queue and batch interval");
    require(processExceptionRestored(self) && queue==vtable && at<uint16_t>(0xb3baf8)==3,"network tick polling restores state after exception");
    deferredNetworkFixture=false;hasBootstrap=false;savedInitial={};hasCapturedBootstrap=false;playback=false;
    resetState();patches.clear();
    auto call=(unsigned char*)address(0x4cfc55);call[0]=0xe8;
    delta=(int32_t)((uintptr_t)address(0x4daf50)-(uintptr_t)call-5);memcpy(call+1,&delta,4);
    unsigned char original[5];memcpy(original,call,5);
    require(patchCall(0x4cfc55,0x4daf50,(void*)advanceHook),"verify and patch relative call");
    require(!memcmp(call,expectedText.data()+((uintptr_t)call-textBase),5),"own patches included in runtime verification");
    restore();require(!memcmp(call,original,5),"restore exact owned bytes");
    call[0]=0x90;require(!patchCall(0x4cfc55,0x4daf50,(void*)advanceHook),"reject unexpected instruction bytes");
    printf("NATIVE_ENGINE_TESTS_PASS: capture/replay, live-input separation, RNG/state mismatch, overflow, patch integrity/restoration\n");
    return 0;
}
