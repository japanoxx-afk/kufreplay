// Exercises the real adapter callbacks against a synthetic ABI-compatible fixture.
// This is not a KUF game and is never packaged as a user replay.
#include "engine.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
uint32_t resource=100,queueCount=0,applied=0;
unsigned forwardedHotkeys=0;
LRESULT CALLBACK fakeHotkeyWindow(HWND,UINT,WPARAM,LPARAM) {++forwardedHotkeys;return 987;}

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
unsigned productionApplyCalls=0;
uint32_t __fastcall fakeProductionApply(void*,void*,uint32_t n,const void* bytes) {
    ++productionApplyCalls;
    auto order=(const uint16_t*)bytes;
    resource+=order[0]+order[1];
    at<uint32_t>(0xbed1e8)=at<uint32_t>(0xbed1e8)*0x8088405+1;
    ++at<uint32_t>(0xb827dc);
    return n+order[0]+order[1];
}
bool legacyPopulationReadFaults(void* self,unsigned owner) {
    __try {
        auto player=((unsigned char**)((unsigned char*)self+48))[owner];
        volatile int used=*(int*)(player+0xe24);(void)used;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
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
uint32_t __fastcall fakeFullSurface(void*,void*) {return 456;}
unsigned alarmBlits=0;
void __cdecl fakeAlarmBlit(const void*,void*,int,int,int) {++alarmBlits;}
uint32_t voiceLoadCalls=0;
uint32_t __fastcall fakeVoiceArchive(void*,void*,uint32_t,uint32_t archive,uint32_t) {++voiceLoadCalls;return archive;}
void* nameDestination[8]{};char assignedNames[8][64]{};unsigned nameAssignments=0;
void __fastcall fakeAssignName(void* self,void*,const char* text,int length) {
    if(nameAssignments>=8 || length<1 || length>63) exit(2);
    nameDestination[nameAssignments]=self;
    memcpy(assignedNames[nameAssignments],text,length);++nameAssignments;
}
uint32_t deathSound=0,deathArchive=0,unitAudioCalls=0,originCalls=0,regionCalls=0;
uint32_t __fastcall fakeUnitAudio(void*,void*,void*,int,uint32_t) {++unitAudioCalls;return 99;}
uint32_t __fastcall fakeDeathAudio(void*,void*,uint32_t sound,uint32_t volume,int,int,uint32_t) {
    deathSound=sound;deathArchive=volume;return 123;
}
void __fastcall fakeCameraOrigin(void* self,void*,int x,int y) {
    ++originCalls;((int*)self)[0]=x;((int*)self)[1]=y;
}
void __fastcall fakeCameraRegion(void* self,void*,int x,int y,int width,int height) {
    if(width!=-1 || height!=-1) exit(2);
    ++regionCalls;((int*)self)[0]=x;((int*)self)[1]=y;
}
bool cameraThrows=false;
void __fastcall fakeCameraInput(void* self,void*) {
    if(cameraThrows) RaiseException(0xe0000124,0,0,nullptr);
    if(*(int16_t*)((unsigned char*)self+0x170c)>0) at<int>(0xb869c4)=9900;
    else at<int>(0xb869c4)+=7; // Manual scrolling still runs without a target.
}
bool cameraSelectionRestoredOnException(void* player) {
    cameraThrows=true;
    __try {observerCameraInputHook(player,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {cameraThrows=false;return *(int16_t*)((unsigned char*)player+0x170c)==1;}
    cameraThrows=false;return false;
}
bool minimapFlagsPreserveRegisters(unsigned char* object,bool expectedVisible) {
    unsigned char zero=0;uint32_t eaxValue=0,ecxValue=0,edxValue=0;
    __asm {
        push edi
        mov edi,object
        mov eax,1234
        mov ecx,5678
        mov edx,9012
        call observerMinimapBuildingFlags
        setz zero
        mov eaxValue,eax
        mov ecxValue,ecx
        mov edxValue,edx
        pop edi
    }
    return (zero==!expectedVisible) && eaxValue==1234 && ecxValue==5678 && edxValue==9012;
}
unsigned resultButtons=0;uint32_t resultButtonValue=0;
uint32_t __fastcall fakeResultButton(void*,void*,uint32_t button) {++resultButtons;resultButtonValue=button;return button+1000;}
int ringFrame=-1;
void __fastcall fakeBuildingRingFrame(void*,void*,int frame,int variant) {if(variant) exit(2);ringFrame=frame;}
uint32_t __fastcall fakeResearchStep(void* self,void*) {
    auto object=(unsigned char*)self;
    --*(int16_t*)(object+0x1484);
    if(*(int16_t*)(object+0x1484)<=0) {*(int16_t*)(object+0x1488)=0;return 1;}
    return 0;
}
unsigned resultTransitions=0;
void __fastcall fakeResultTransition(void* self,void*,uint32_t state) {
    if(state!=16) exit(2);
    *(uint32_t*)((unsigned char*)self+0x128)=state;++resultTransitions;
}
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
uint32_t __fastcall fakeObserverThrow(void*,void*);
void* expectedPanelPlayer=nullptr;
uint32_t __fastcall fakeResourcePanel(void* self,void*) {
    auto ui=(unsigned char*)self;auto game=(unsigned char*)at<void*>(0xb8282c);
    if(*(void**)(ui+0x23c)!=expectedPanelPlayer || *(uint32_t*)(game+0x664)!=2 || !observerPanelDrawing || *(int16_t*)(ui+0x1898)!=1 || *(int16_t*)*(void**)(ui+0x189c)!=0) exit(2);
    auto resource=observerUnit(0);
    if(!observerResource(resource) || *(int*)(resource+0x14f8)!=12500 || *(uint16_t*)(resource+0x14)!=0) exit(2);
    return 654;
}
bool expectSelectionRing=false;
uint32_t __fastcall fakeSelectionRingDraw(void* self,void*) {
    auto object=(unsigned char*)self;
    if(object[0x4e]!=(expectSelectionRing?1:0) || object[0x4f]!=(expectSelectionRing?1:0) || object[0x3d]!=5) exit(2);
    return 987;
}
bool selectionRingExceptionRestored(void* self) {
    bool caught=false;
    __try {observerObjectDisplay(self,(Process)fakeObserverThrow);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    auto object=(unsigned char*)self;
    return caught && object[0x4e]==7 && object[0x4f]==9 && object[0x3d]==5;
}
unsigned bodyPrepares=0;
uint32_t __fastcall fakeBuildingBody(void* self,void*) {
    auto object=(unsigned char*)self;
    if(!displayDepth || object[0x6b] || object[0x14d9]!=1 || at<unsigned char>(0xb3ba61)!=1) exit(2);
    object[0x14d8]=1;++bodyPrepares;return 0;
}
uint32_t __fastcall fakeObserverDraw(void* self,void*) {
    if(at<unsigned char>(0xb3ba61)!=1 || ((unsigned char*)self)[0x6b]!=0 || ((unsigned char*)self)[0x69]!=0) exit(2);
    if(*(int16_t*)((unsigned char*)self+6)==1 && ((unsigned char*)self)[0x14d9]!=1) exit(2);
    ((unsigned char*)self)[0x6b]=7;return 321;
}
bool textDrawThrows=false,textExpectTint=false;
uint32_t __fastcall fakeTextDraw(void* self,void*,const char* text,void* pixels,int x,int pitch,int height,int translate) {
    auto font=(unsigned char*)self;
    if(pixels!=(void*)0x1234 || x!=23 || pitch!=240 || height!=30 || translate!=0) exit(2);
    if(textExpectTint && (strcmp(text,"James") || *(uint32_t*)(font+0x40)!=1 || *(uint16_t*)(font+0x46)!=observerPlayerColor(0))) exit(2);
    if(textDrawThrows) RaiseException(0xe0000123,0,0,nullptr);
    return 456;
}
bool textExceptionRestored(void* font,const char* text) {
    bool caught=false;textDrawThrows=true;
    __try {observerTextDrawHook(font,nullptr,text,(void*)0x1234,23,240,30,0);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    textDrawThrows=false;return caught && *(uint32_t*)((unsigned char*)font+0x40)==0 && *(uint16_t*)((unsigned char*)font+0x46)==0x1234 && observerIdTintDepth==0;
}
uint32_t __fastcall fakeObserverThrow(void*,void*) {RaiseException(0xe0000123,0,0,nullptr);return 0;}
uint32_t __fastcall fakeMinimapProduce(void* self,void*) {
    if(!displayDepth || at<unsigned char>(0xb3ba61)!=1 || !observerMinimapBuildingVisible((unsigned char*)self,nullptr)) exit(2);
    return 777;
}
bool buildingExceptionRestored(void* self) {
    bool caught=false;
    __try {observerObjectDisplay(self,(Process)fakeObserverThrow);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    return caught && ((unsigned char*)self)[0x6b]==1 && ((unsigned char*)self)[0x69]==1 && at<unsigned char>(0xb3ba61)==0;
}
uint32_t __fastcall fakeAudio(void* self,void*,uint32_t a,uint32_t b,int x,int y,uint32_t c) {
    if(at<int>(0xbf2588)!=1066 || observerAudioVisibility(self,nullptr,7,x,y)!=1) {fprintf(stderr,"audio fixture width/depth %d/%u\n",at<int>(0xbf2588),observerAudioDepth);exit(2);}
    if(at<int>(0xb869c4)!=700 || at<int>(0xb869c8)!=800) {fprintf(stderr,"audio camera cache mismatch\n");exit(2);}
    return a+b+c;
}
uint32_t __fastcall fakeAudioThrow(void*,void*,uint32_t,uint32_t,int,int,uint32_t) {RaiseException(0xe0000123,0,0,nullptr);return 0;}
uint32_t __fastcall fakeMixerUpdate(void* self,void*) {
    if(at<int>(0xbf2588)!=1066 || at<int>(0xb869c4)!=700 || at<int>(0xb869c8)!=800 ||
       *(int*)((unsigned char*)self+0xfcd20)!=(int)0x80000000u) exit(2);
    return 876;
}
bool audioExceptionRestored(void* self) {
    bool caught=false;
    __try {observerAudio(self,(Audio)fakeAudioThrow,1,2,3,4,5);}
    __except(GetExceptionCode()==0xe0000123?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    return caught && observerAudioDepth==0 && at<int>(0xbf2588)==866;
}
bool panelThrows=false;
bool panelKnownFault=false;
uint32_t syncedSelection=99;
uint32_t __fastcall fakePanelSync(void*,void*,uint32_t write) {syncedSelection=write;return 1;}
uint32_t __fastcall fakePanel(void* self,void*) {
    auto game=(unsigned char*)at<void*>(0xb8282c);
    if(*(uint32_t*)(game+0x664)!=1 || *(int16_t*)((unsigned char*)self+0x1898)!=1 || !observerPanelDrawing) {fprintf(stderr,"panel fixture owner/count/drawing %u/%d/%d\n",*(uint32_t*)(game+0x664),*(int16_t*)((unsigned char*)self+0x1898),observerPanelDrawing);exit(2);}
    observerSelectionSync(self,nullptr,1);
    if(panelKnownFault) ((void(__cdecl*)())address(0x4943fd))();
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

int wmain(int argc,wchar_t** argv) {
    if(argc==2) {
        require(readInput(argv[1]),"read real replay trace and validate native metadata");
        for(unsigned i=0;i<8;++i) {
            char fallback[16];printf("P%u=%s\n",i+1,observerName(i,fallback));
        }
        return 0;
    }
    unsigned char nameSelf[0x520]{},nameStorage[80]{};
    char* nameText=(char*)nameStorage+16;
    strcpy_s(nameText,64,"TestUser_2");*(int*)(nameText-12)=10;
    *(char**)(nameSelf+0x500)=nameText;
    PlayerNames namesFixture{};captureNames(nameSelf,namesFixture);
    require(!strcmp(namesFixture.names[0],"TestUser_2") && !namesFixture.names[1][0],"capture lobby CString names without pointers");
    savedNames=namesFixture;char nameFallback[16];
    require(!strcmp(observerName(0,nameFallback),"TestUser_2") && !strcmp(observerName(1,nameFallback),"P2"),"display captured names and preserve legacy fallback");
    savedNames={1,{}};
    {
        PlayerNames recorded{1,{}};strcpy_s(recorded.names[0],"James");strcpy_s(recorded.names[1],"yunin");
        require(loadRecordedNames((const unsigned char*)&recorded,sizeof(recorded)),"reload match-start recorded names after previous display state was cleared");
        char fallback[16];require(!strcmp(observerName(0,fallback),"James") && !strcmp(observerName(1,fallback),"yunin"),"recorded names replace P1/P2 in resource rows");
        require(!loadRecordedNames((const unsigned char*)&recorded,sizeof(recorded)-1),"reject truncated names without replacing valid display names");
        savedNames={1,{}};
    }
    {
        unsigned char app[0x200]{};playback=true;terminal.store(true);failed.store(false);notice.store(10);playbackResultsStarted=false;
        require(beginReplayResults(app,(ChangeState)fakeResultTransition) && *(uint32_t*)(app+0x128)==16 && resultTransitions==1,"verified final replay enters native result state on next application frame");
        require(beginReplayResults(app,(ChangeState)fakeResultTransition) && resultTransitions==1,"result UI opens once without repeated reconstruction");
        failed.store(true);playbackResultsStarted=false;
        require(!beginReplayResults(app,(ChangeState)fakeResultTransition) && resultTransitions==1,"desync never presented as completed match results");
        failed.store(false);notice.store(14);
        require(!beginReplayResults(app,(ChangeState)fakeResultTransition),"unsupported initialization retains failure notice");
        notice.store(10);terminal.store(false);
        require(!beginReplayResults(app,(ChangeState)fakeResultTransition),"in-progress replay never opens results");
        playback=false;notice.store(0);playbackResultsStarted=false;
    }
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
    require(observerTrackingShortcut('C',(SHORT)0x8000,0),"Ctrl+C starts tracking");
    require(!observerTrackingShortcut('C',0,0),"plain C never starts tracking");
    require(!observerTrackingShortcut('C',(SHORT)0x8000,1L<<30),"Ctrl+C repeat does not toggle");
    require(!observerTrackingShortcut('O',(SHORT)0x8000,0),"other Ctrl shortcuts do not track");
    char shortDisplay[20];
    require(observerDisplayName("ab",shortDisplay)==2 && !strcmp(shortDisplay,"ab"),"short IDs keep compact display");
    require(observerDisplayName("abcdefghijklmnopq",shortDisplay)==18 && !strcmp(shortDisplay,"abcdefghijklmnop.."),"long IDs truncate consistently for column sizing");
    playback=true;
    require(replayVoice(nullptr,(VoiceAudio)fakeVoiceArchive,500,100,0)==0xffffffffu && voiceLoadCalls==0,"replay dialogue skips loading rather than passing a null resource archive");
    playback=false;
    require(replayVoice(nullptr,(VoiceAudio)fakeVoiceArchive,500,100,0)==100,"normal gameplay speech volume remains unchanged");
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
    {
        auto previousProc=oldWindowProc;oldWindowProc=fakeHotkeyWindow;
        for(bool running:{false,true}) for(bool ended:{false,true}) {
            playback=true;active=running;terminal.store(ended);
            for(UINT message:{WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SYSKEYUP}) {
                unsigned before=forwardedHotkeys;
                require(recordingWindowProc(nullptr,message,VK_F5,0)==0 &&
                        recordingWindowProc(nullptr,message,VK_F5,1L<<30)==0 && forwardedHotkeys==before,
                        "F5 reload blocked during playback: loading/active/terminal, repeats and release");
            }
        }
        playback=false;active=false;terminal.store(false);
        for(UINT message:{WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SYSKEYUP})
            require(recordingWindowProc(nullptr,message,VK_F5,0)==987,"F5 normal-play and recording input preserved");
        playback=true;
        require(recordingWindowProc(nullptr,WM_KEYDOWN,VK_F6,0)==987,"unrelated function keys preserve existing behavior");
        playback=false;oldWindowProc=previousProc;
    }
    {
        minimapAlarmBlit=fakeAlarmBlit;playback=true;active=true;
        replayMinimapAlarmBlit(nullptr,nullptr,0,0,0);
        require(alarmBlits==0,"replay minimap alarm pixels suppressed without skipping renderer lifecycle");
        active=false;replayMinimapAlarmBlit(nullptr,nullptr,0,0,0);
        playback=false;active=true;replayMinimapAlarmBlit(nullptr,nullptr,0,0,0);
        require(alarmBlits==2,"minimap alarms forwarded outside active playback");
        active=false;minimapAlarmBlit=nullptr;
    }
    {
        playback=true;active=true;terminal.store(true);failed.store(false);playbackResultsStarted=true;resultButton=(ResultButton)fakeResultButton;
        for(uint32_t button=700;button<704;++button)
            require(resultButtonHook(nullptr,nullptr,button)==button+1000 && resultButtonValue==button,"native unit/building/resource/total statistic buttons preserved");
        require(resultButtons==4 && resultButtonHook(nullptr,nullptr,704)==1 && resultButtons==4 && playbackExitRequested,"result confirm bypasses absent lobby and requests process shutdown");
        MSG message{};PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE);
        playbackExitRequested=false;playbackResultsStarted=false;terminal.store(false);resultButton=nullptr;
        showHeroPanel=true;recordingWindowProc(nullptr,WM_KEYDOWN,'H',0);require(!showHeroPanel,"H hides hero panel");
        recordingWindowProc(nullptr,WM_KEYDOWN,'H',0);require(showHeroPanel,"H restores hero panel");
        require(observerHeroName(30) && observerHeroName(105) && observerHeroName(106) && observerHeroName(111) && observerHeroName(108) && !observerHeroName(24),"requested hero catalogue includes campaign heroes and excludes ordinary Lich");
        unsigned char object[0x1600]{},sprite[0x98]{};
        *(void**)(object+0x84)=sprite;*(int16_t*)(object+4)=55;*(int*)(object+0x18)=600;*(int*)(object+0x1c)=700;at<int16_t>(0xb36338+55*2)=80;
        prepareObserverBuildingRing(object,(SpriteFrame)fakeBuildingRingFrame);
        require(ringFrame==121 && *(int16_t*)(sprite+0xc)==600 && *(int16_t*)(sprite+0xe)==700 && *(int*)(sprite+0x28)==1,"building selection ring prepares native frame and world position missing from draw-only flags");
        auto manager=(unsigned char*)VirtualAlloc(nullptr,0x34000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        auto definitions=(unsigned char*)VirtualAlloc(nullptr,116*0xaf8,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        unsigned char commands[0x400]{};
        require(manager && definitions,"upgrade fixtures allocated");
        at<void*>(0xb870c0)=manager;*(void**)(manager+0x2c38)=object;*(uint32_t*)(manager+0xdc38)=1;*(uint32_t*)(manager+0xdc3c)=0;*(uint32_t*)(manager+0xdc4c)=11264;
        at<void*>(0xb870f4)=definitions;at<void*>(0xb82824)=commands;
        *(int*)(definitions+55*0xaf8+0xa9c)=1;*(int*)(definitions+55*0xaf8+0xaa8)=1;
        *(int*)(commands+0x204)=90;*(int*)(commands+0x208)=500;*(int*)(commands+0x20c)=5;
        require(observerResearchIcon(object,5)==90 && observerResearchIcon(object,6)==-1,"upgrade icon comes from matching native command definition, not guessed frame number");
        *(int16_t*)(object+6)=1;*(int16_t*)(object+8)=0;*(int16_t*)(object+0x12e)=5;*(int16_t*)(object+0x1488)=1;
        *(int16_t*)(object+0x1484)=200;*(int16_t*)(object+0x1486)=1000;
        producer=GetCurrentThreadId();at<uint32_t>(0xb3bae8)=30;at<uint32_t>(0xb8a280)=100;
        researchProgress=(Process)fakeResearchStep;resetObserverExtras();
        require(!observerUpgradeNearEnd(167,30) && observerUpgradeNearEnd(166,30) && !observerUpgradeNearEnd(1,0),"five-second warning follows simulation tick duration");
        researchProgressHook(object,nullptr);require(!observerUpgrades[0].object,"no premature upgrade warning");
        *(int16_t*)(object+0x1484)=167;researchProgressHook(object,nullptr);
        require(observerUpgrades[0].object==object && observerUpgrades[0].remaining==166 && observerUpgrades[0].icon==90 && !observerUpgrades[0].completed,"countdown appears at five seconds with correct upgrade icon");
        *(int16_t*)(object+0x1484)=1;researchProgressHook(object,nullptr);
        require(observerUpgrades[0].completed && observerUpgrades[0].completedAt==100,"completion effect requires native research completion transition");
        bool wasParticipant=observerPlayers[0].participant;observerPlayers[0].participant=true;
        {
            unsigned char training[0x1600]{};*(int16_t*)(training+6)=1;*(uint16_t*)(training+0x14)=100;
            *(uint16_t*)(training+0xc)=0x28a0;*(int16_t*)(training+0x14c2)=2;*(int16_t*)(training+0x1488)=1;
            *(int16_t*)(training+0x12e)=105;*(int16_t*)(training+0x1484)=167;
            int type=0,remaining=0,owner=0;
            require(!observerTrainingHero(training,30,type,remaining,owner),"hero warning does not appear before five simulation seconds");
            *(int16_t*)(training+0x1484)=166;
            unsigned char preserved[sizeof(training)];memcpy(preserved,training,sizeof(training));
            require(observerTrainingHero(training,30,type,remaining,owner) && type==105 && remaining==166 && owner==0,"hero production warning uses active hero order and actual remaining ticks");
            require(!memcmp(preserved,training,sizeof(training)),"hero production display never modifies simulation state");
            for(int heroType:{29,30,31,32,33,34,35,105,106,107,108,111}) {
                *(int16_t*)(training+0x12e)=(int16_t)heroType;
                require(observerTrainingHero(training,30,type,remaining,owner),"all requested heroes support production countdown");
            }
            *(int16_t*)(training+0x12e)=0;require(!observerTrainingHero(training,30,type,remaining,owner),"ordinary units do not trigger hero warning");
            *(int16_t*)(training+0x12e)=108;*(int16_t*)(training+0x14c2)=1;
            require(!observerTrainingHero(training,30,type,remaining,owner),"supply-blocked production does not claim imminent completion");
            *(int16_t*)(training+0x14c2)=2;*(int16_t*)(training+0x1484)=0;
            require(!observerTrainingHero(training,30,type,remaining,owner),"completed production clears countdown");
            *(int16_t*)(training+0x1484)=100;*(uint16_t*)(training+0xc)=0x27d8;
            require(!observerTrainingHero(training,30,type,remaining,owner),"cancelled production clears countdown");
            *(uint16_t*)(training+0xc)=0x28a0;*(uint16_t*)(training+0x14)=0;
            require(!observerTrainingHero(training,30,type,remaining,owner),"destroyed producer clears countdown");
            *(uint16_t*)(training+0x14)=100;
            require(!observerTrainingHero(training,0,type,remaining,owner) && !observerTrainingHero(training,1001,type,remaining,owner),"invalid simulation interval cannot create hero alert");
        }

        *(int16_t*)(object+6)=0;*(uint16_t*)(object+0x14)=100;
        *(int16_t*)(object+0x1a8)=5;*(int16_t*)(object+0x1aa)=20;
        *(int*)(object+0x35c)=99; // Unrelated unit field: old code read this.
        for(int heroType:{105,108}) {
            *(int16_t*)(object+4)=(int16_t)heroType;observerHeroCount=0;observerOverlayCacheValid=false;refreshObserverHeroes();
            require(observerHeroCount==1 && observerHeroes[0].level==5,"Russelaunt and Lauriana show native base level five rather than information-panel cache offset");
        }
        *(int16_t*)(object+0x1a8)=7;observerHeroCount=0;observerOverlayCacheValid=false;refreshObserverHeroes();
        require(observerHeroes[0].level==7,"hero level-up uses live WORD level without fixed base-level override or adjacent field contamination");
        observerPlayers[0].participant=wasParticipant;
        resetObserverExtras();researchProgress=nullptr;producer=0;playback=false;active=false;
        at<void*>(0xb870c0)=nullptr;at<void*>(0xb870f4)=nullptr;at<void*>(0xb82824)=nullptr;
        VirtualFree(manager,0,MEM_RELEASE);VirtualFree(definitions,0,MEM_RELEASE);
    }
    {
        unsigned char resource[0x1600]{},game[0x700]{},ui[sizeof(observerPanel)]{},player[16]{};
        auto manager=(unsigned char*)VirtualAlloc(nullptr,0x34000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        require(manager!=nullptr,"resource manager fixture");
        at<void*>(0xb870c0)=manager;*(void**)(manager+0x2c38)=resource;
        *(int16_t*)(resource+6)=1;*(int16_t*)(resource+8)=10;*(int*)(resource+0x14f8)=12500;
        for(uint32_t table:{0xa3db54u,0xa3d9c4u,0xa3dce4u}) {
            *(void**)resource=address(table);require(observerInspectable(resource),"all three neutral resource classes remain inspectable with zero HP");
        }
        *(int16_t*)(resource+0x4c)=1;require(!observerInspectable(resource),"removed resource not selectable");*(int16_t*)(resource+0x4c)=0;
        *(void**)resource=address(0xa2b5fc);require(!observerInspectable(resource),"neutral non-resource objects cannot bypass participant ownership validation");
        *(void**)resource=address(0xa3db54);at<void*>(0xb8282c)=game;*(uint32_t*)(game+0x664)=2;*(void**)(game+0x50)=player;
        void* table[5]{};table[4]=(void*)fakeResourcePanel;*(void**)ui=table;*(void**)(ui+0x23c)=player;expectedPanelPlayer=player;
        playback=true;active=true;showObserverHud=true;observerSelected=0;observerPanelUnavailable=false;observerPanelSource=nullptr;
        unsigned char savedResource[sizeof(resource)];memcpy(savedResource,resource,sizeof(resource));
        require(observerPanelHook(ui,nullptr)==654 && *(uint32_t*)(game+0x664)==2 && !observerPanelDrawing && !memcmp(resource,savedResource,sizeof(resource)),"neutral resource panel receives selected object without switching to neutral owner or changing remaining amount/HP");
        *(int*)(resource+0x14f8)=12000;require(observerInspectable(resource),"decreasing resource amount remains live inspectable data");
        observerSelected=-1;observerPanelSource=nullptr;panelSelected=-2;expectedPanelPlayer=nullptr;playback=false;active=false;
        at<void*>(0xb8282c)=nullptr;at<void*>(0xb870c0)=nullptr;VirtualFree(manager,0,MEM_RELEASE);
    }
    {
        require(observerRaceInitial(1000)=='H' && observerRaceInitial(2000)=='D' && observerRaceInitial(3000)=='?',"race initials use actual Human/Devil enums");
        at<int>(0xbf281c)=0x10000;at<uint16_t>(0xbea784)=0xda04;
        at<uint16_t>(0xbea770)=0x6e04;
        char colored[64];observerColoredName("James",0,colored);
        require(!strcmp(colored,"\\cDA04James"),"ID queues a private 565 marker consumed before native palette indexing");
        observerColoredName("a\\1b",0,colored);
        require(!strcmp(colored,"\\cDA04a\\\\1b"),"player ID cannot inject native font markup");
        at<int>(0xbf281c)=0x8000;observerColoredName("James",0,colored);
        require(!strcmp(colored,"\\c6E04James"),"ID follows native 555 player palette");
        require(observerPlayerColor(-1)==0x7fff,"invalid color has safe white fallback");
        require(observerTint(0xffff,0xda04,false)==0xda04 && observerTint(0,0xda04,false)==0,"565 tint preserves foreground hue and black outline");
        require(observerTint(0x7fff,0x6e04,true)==0x6e04,"555 tint uses correct channel masks");
        std::vector<uint16_t> tintedGlyph;
        const uint16_t glyph[]={9,0x02f8,0xffff,0x0000,0x81f8,0xffff,0x0100,0x0000,0xffff};
        require(tintObserverGlyph(glyph,1,0xda04,false,tintedGlyph) && tintedGlyph[2]==0xda04 && tintedGlyph[3]==0 && tintedGlyph[5]==0xda04 && tintedGlyph[1]==0x02f8 && tintedGlyph[6]==0x0100 && glyph[2]==0xffff,"bitmap ID tint recolors literal/repeated glyph pixels, retains outline/alpha/transparency and original asset");
        const uint16_t malformedGlyph[]={3,0x05f8,0xffff};
        require(!tintObserverGlyph(malformedGlyph,1,0xda04,false,tintedGlyph) && !tintObserverGlyph(glyph,129,0xda04,false,tintedGlyph),"glyph tint rejects invalid run bounds and excessive dimensions");
        unsigned char font[0xa0]{},assets[0x120]{};uint16_t palette[32]{};palette[1]=0x7fff;
        *(void**)font=assets;*(void**)(assets+0xf0)=palette;*(uint16_t*)(font+0x46)=0x1234;
        at<int>(0xb40d64)=1;
        playback=true;active=true;producer=GetCurrentThreadId();
        savedNames={1,{}};strcpy_s(savedNames.names[0],"James");observerPlayers[0].participant=true;observerPlayers[0].color=0;
        textDraw=(TextDraw)fakeTextDraw;textExpectTint=true;
        require(observerTextDrawHook(font,nullptr,colored,(void*)0x1234,23,240,30,0)==456 && !*(uint32_t*)(font+0x40) && *(uint16_t*)(font+0x46)==0x1234,"queued ID applies player palette at actual draw and restores font palette");
        require(textExceptionRestored(font,colored),"font override restored after draw exception");
        *(int*)(font+0x84)=4;at<int>(0xb40d64+16)=2;
        require(observerTextDrawHook(font,nullptr,colored,(void*)0x1234,23,240,30,0)==456 && !*(uint32_t*)(font+0x40) && *(uint16_t*)(font+0x46)==0x1234,"actual face 4 GDI foreground uses player RGB despite direct-color bitmap fallback");
        require(textExceptionRestored(font,colored),"face 4 palette and tint context restored on exception");
        *(int*)(font+0x84)=0;
        textExpectTint=false;playback=false;uint16_t tint;
        require(!observerIdText(colored,tint) && observerTextDrawHook(font,nullptr,"Gold",(void*)0x1234,23,240,30,0)==456,"normal game and unrelated text use original draw");
        savedNames={1,{}};observerPlayers[0].participant=false;textDraw=nullptr;
        unsigned char building[0x1500]{};*(uint16_t*)(building+0x14)=100;
        playback=true;active=true;producer=GetCurrentThreadId();displayDepth=0;
        require(observerMinimapBuildingVisible(building,nullptr)==0,"outside display retains original building visibility test");
        require(minimapFlagsPreserveRegisters(building,false),"minimap comparison thunk preserves registers and hidden ZF");
        displayDepth=1;
        require(observerMinimapBuildingVisible(building,nullptr)==1 && *(uint16_t*)(building+0x14)==100 && !building[0x14e8],"observer minimap includes unknown enemy building without changing HP or remembered visibility");
        require(minimapFlagsPreserveRegisters(building,true),"minimap comparison thunk preserves registers and visible ZF");
        playback=false;
        require(observerMinimapBuildingVisible(building,nullptr)==0,"normal gameplay retains minimap restrictions even inside display");
        building[0x14e8]=1;
        require(observerMinimapBuildingVisible(building,nullptr)==1,"normal known building visibility remains intact");
        active=false;displayDepth=0;producer=0;
    }
    {
        unsigned char worker[0x80]{};*(uint16_t*)(worker+0x14)=100;worker[0x67]=1;
        require(liveObserverWorker(worker),"alive worker remains counted despite auxiliary visibility/storage flag");
        *(int16_t*)(worker+4)=15;require(liveObserverWorker(worker),"Devil worker counted alongside Human worker");
        *(uint16_t*)(worker+0x14)=0;require(!liveObserverWorker(worker),"dead worker excluded");
        *(uint16_t*)(worker+0x14)=100;*(int16_t*)(worker+6)=1;require(!liveObserverWorker(worker),"buildings never counted as workers");
        unsigned char building[0x1500]{};building[0x6b]=1;building[0x69]=1;
        playback=true;active=true;producer=GetCurrentThreadId();at<unsigned char>(0xb3ba61)=0;
        require(observerObjectDisplay(building,(Process)fakeObserverDraw)==321 && building[0x6b]==1 && building[0x69]==1,"unscouted building actual sprite draw is visible then fog/detection restored");
        require(buildingExceptionRestored(building),"building display restores flags on exceptional exit");
        *(int16_t*)(building+6)=1;*(uint16_t*)(building+0x14)=100;
        *(int16_t*)(building+0x14a6)=3;building[0x14d9]=0;
        require(observerObjectDisplay(building,(Process)fakeObserverDraw)==321 && !building[0x14d9] && !building[0x14d8] && *(int16_t*)(building+0x14a6)==3,"unscouted construction uses scaffolding draw gate without painting a completed body or changing stage");
        require(buildingExceptionRestored(building) && !building[0x14d9],"construction draw gate restored on exception");
        auto manager=(unsigned char*)VirtualAlloc(nullptr,0x34000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        require(manager!=nullptr,"building preparation manager fixture");
        at<void*>(0xb870c0)=manager;*(uint32_t*)(manager+0xdc38)=1;*(uint32_t*)(manager+0xdc3c)=0;
        *(void**)(manager+0x2c38)=building;*(uint32_t*)(manager+0xdc4c)=11264;
        building[0x4e]=7;building[0x4f]=9;building[0x3d]=5;observerSelected=0;expectSelectionRing=true;
        require(observerObjectDisplay(building,(Process)fakeSelectionRingDraw)==987 && building[0x4e]==7 && building[0x4f]==9,"observer-selected enemy building gets native circle only while drawn then restores original selection");
        *(int16_t*)(building+6)=0;
        require(observerObjectDisplay(building,(Process)fakeSelectionRingDraw)==987 && building[0x4e]==7 && building[0x4f]==9,"observer-selected enemy unit gets circle without changing player selection state");
        require(selectionRingExceptionRestored(building),"both selection display flags restored after exceptional draw");
        observerSelected=-1;expectSelectionRing=false;
        require(observerObjectDisplay(building,(Process)fakeSelectionRingDraw)==987 && building[0x4e]==7 && building[0x4f]==9,"deselection removes observer ring and does not leak recorded selection");
        observerSelected=0;*(uint16_t*)(building+0x14)=0;
        require(observerObjectDisplay(building,(Process)fakeSelectionRingDraw)==987,"dead selected object never draws selection circle");
        observerSelected=-1;*(uint16_t*)(building+0x14)=100;*(int16_t*)(building+6)=1;
        building[0x4e]=0;building[0x4f]=0;building[0x3d]=0;
        buildingBodyPrepare=(Process)fakeBuildingBody;
        require(observerDisplay(building,(Process)fakeObserverDraw)==321 && bodyPrepares==0 && !building[0x14d9],"prepare pass keeps under-construction body absent and restores visibility");
        *(int16_t*)(building+0x14a6)=0;
        require(observerDisplay(building,(Process)fakeObserverDraw)==321 && bodyPrepares==1 && building[0x14d8]==1 && !building[0x14d9] && !building[0x14e8] && *(uint16_t*)(building+0x14)==100,"completion prepares native graphics once without marking building explored or changing HP");
        observerDisplay(building,(Process)fakeObserverDraw);require(bodyPrepares==1,"completed graphics not regenerated each frame");
        building[0x14d8]=0;*(uint16_t*)(building+0xc)=0x3e80;
        require(!observerNeedsBuildingBody(building),"destroyed native state never prepared as finished building");
        *(uint16_t*)(building+0xc)=0;inDispatch=true;displayDepth=1;
        prepareObserverBuildingBody(building,(Process)fakeBuildingBody);require(bodyPrepares==1,"building graphics never initialized during simulation dispatch");
        inDispatch=false;displayDepth=0;buildingBodyPrepare=nullptr;at<void*>(0xb870c0)=nullptr;VirtualFree(manager,0,MEM_RELEASE);
        *(int16_t*)(building+6)=0;
        characterRender=(Process)fakeObserverDraw;
        building[0x3c]=1;*(int16_t*)(building+8)=7;
        require(characterRenderHook(building,nullptr)==321 && building[0x69]==1 && building[0x6b]==1 && building[0x3c]==1 && *(int16_t*)(building+8)==7,"opponent cloaked character actual draw reveals then restores detection without changing owner/culling");
        characterRender=(Process)fakeObserverThrow;
        require(buildingExceptionRestored(building),"character shared display scope restores detection on exception");
        characterRender=nullptr;

        require(observerDisplay(building,(Process)fakeMinimapProduce)==777 && displayDepth==0 && at<unsigned char>(0xb3ba61)==0,"minimap producer has explicit observer scope and restores it");
        playback=false;active=false;producer=0;
    }
    playback=false;
    {
        unsigned char dyingUnit[0x80]{},playerAudio[0x600]{};
        *(int16_t*)(dyingUnit+4)=0;*(int16_t*)(dyingUnit+8)=2;
        *(int*)(playerAudio+0x590)=3; // Observer differs from dying unit owner.
        at<int16_t>(0xb38f50+16)=2;at<int16_t>(0xb38f52+16)=2508;
        at<int16_t>(0xb39a52)=-1;at<int16_t>(0xb39a54)=0;
        at<void*>(0xb83ab0)=playerAudio;spatialAudio=(Audio)fakeDeathAudio;
        at<uint32_t>(0xb8a1e0)=0x11110000;at<uint32_t>(0xb8a1e4)=0x22220000;
        at<uint32_t>(0xb3e648)=777;at<uint32_t>(0xbed1e8)=888;
        playback=true;active=true;producer=GetCurrentThreadId();
        unsigned char savedPlayer[sizeof(playerAudio)];memcpy(savedPlayer,playerAudio,sizeof(playerAudio));
        require(replayUnitAudio(playerAudio,(UnitAudio)fakeUnitAudio,dyingUnit,4,0)==123,"observer hears remote owner's death without native ownership or speech gate");
        require(deathSound==2509 && deathArchive==0x22220000 && unitAudioCalls==0,"death sound keeps original speech archive containing pe109.wav and skips unavailable variant");
        uint32_t previousVoiceCalls=voiceLoadCalls;
        require(replayVoice(playerAudio,(VoiceAudio)fakeVoiceArchive,2509,0x22220000,0)==0x22220000 && voiceLoadCalls==previousVoiceCalls+1,"known death voice survives generic speech mute call sites with original resource archive");
        require(replayVoice(playerAudio,(VoiceAudio)fakeVoiceArchive,2502,0x22220000,0)==0xffffffffu && voiceLoadCalls==previousVoiceCalls+1,"selection dialogue remains muted while death voices are allowed");
        require(!memcmp(savedPlayer,playerAudio,sizeof(playerAudio)) && at<uint32_t>(0xb3e648)==777 && at<uint32_t>(0xbed1e8)==888,"death playback preserves player caches and both RNG states");
        require(replayUnitAudio(playerAudio,(UnitAudio)fakeUnitAudio,dyingUnit,1,0)==99,"non-death response retains native handling");
        playback=false;
        require(replayUnitAudio(playerAudio,(UnitAudio)fakeUnitAudio,dyingUnit,4,0)==99,"normal game death handling remains native");
        active=false;producer=0;at<void*>(0xb83ab0)=nullptr;spatialAudio=nullptr;
    }
    {
        unsigned char gameNames[0x520]{},lobby[0x58]{};
        char nameA[]="superchosun",nameB[]="james";
        *(int*)(lobby+8)=2;*(char**)(lobby+0x38)=nameA;*(char**)(lobby+0x3c)=nameB;
        at<void*>(0xb8282c)=gameNames;at<void*>(0xbed230)=lobby;at<uint16_t>(0xb83ad0)=1;
        *(void**)(gameNames+0x508)=(void*)1; // Unreadable third slot does not erase valid names.
        PlayerNames captured{};captureNames(gameNames,captured);
        require(!strcmp(captured.names[0],nameA) && !strcmp(captured.names[1],nameB) && !captured.names[2][0],"capture actual lobby names even with empty game strings and invalid other slot");
        savedNames=captured;restorePlayerNames(gameNames,(AssignName)fakeAssignName);
        require(nameAssignments==2 && nameDestination[0]==gameNames+0x500 && nameDestination[1]==gameNames+0x504 && !strcmp(assignedNames[0],nameA) && !strcmp(assignedNames[1],nameB),"restore native message recipient names with CString assignment and exact player slots");
        char fallback[16];require(!strcmp(observerName(0,fallback),nameA) && !strcmp(observerName(1,fallback),nameB),"resource overlay uses same captured IDs");
        savedNames={1,{}};restorePlayerNames(gameNames,(AssignName)fakeAssignName);
        require(nameAssignments==2,"legacy replay without names does not assign invented identities");
        at<void*>(0xb8282c)=nullptr;at<void*>(0xbed230)=nullptr;at<uint16_t>(0xb83ad0)=0;
    }
    {
        const unsigned char safeFileQuery[]={0x8b,0x49,0x04,0x85,0xc9,0x75,0x03,0x32,0xc0,0xc3,0x8b,0x01,0x8b,0x40,0x1c,0xff,0xe0};
        memcpy(address(0x5f7710),safeFileQuery,sizeof(safeFileQuery));
        unsigned char fileObject[8]{};using FileQuery=unsigned char(__thiscall*)(void*);
        require(((FileQuery)address(0x5f7710))(fileObject)==0,"native safe file query handles reported null backend");
        void* methods[8]{};methods[7]=(void*)fakeFullSurface;void** backend=methods;
        *(void**)(fileObject+4)=&backend;
        require(((FileQuery)address(0x5f7710))(fileObject)==(unsigned char)456,"native safe file query preserves live backend dispatch");
    }
    ring=(unsigned char*)VirtualAlloc(nullptr,RingSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(ring!=nullptr,"ring memory");
    {
        unsigned char trackManager[0x2c40]{},trackGame[0xa00]{},trackUnit[0x80]{},replacementUnit[0x80]{};
        at<void*>(0xb870c0)=trackManager;*(unsigned char**)(trackManager+0x2c38)=trackUnit;
        *(uint16_t*)(trackUnit+0x14)=100;*(int16_t*)(trackUnit+8)=2;
        *(int*)(trackUnit+0x18)=1500;*(int*)(trackUnit+0x1c)=1400;
        *(int*)(trackGame+0x38)=128;*(int*)(trackGame+0x3c)=128;
        at<int>(0xbf2588)=800;at<int>(0xbf258c)=600;
        playback=true;active=true;observerSelected=0;
        unsigned char beforeUnit[sizeof(trackUnit)];memcpy(beforeUnit,trackUnit,sizeof(trackUnit));
        at<int>(0xb869c4)=900;at<int>(0xb869c8)=700;
        toggleObserverTracking();updateObserverTracking(trackGame);
        require(observerTracked==0 && at<int>(0xb869c4)==1100 && at<int>(0xb869c8)==1100,"tracking centers selected unit regardless of previous screen position");
        unsigned char renderer[0x3c]{};int terrainOrigin[2]{};int* originPointer=terrainOrigin;
        *(void**)(renderer+0x38)=&originPointer;at<void*>(0xb86f14)=renderer;
        for(auto target:{std::pair<uint32_t,void*>(0x67c5c0,(void*)fakeCameraOrigin),{0x630930,(void*)fakeCameraRegion}}) {
            auto stub=(unsigned char*)address(target.first);stub[0]=0xe9;
            int32_t relative=(int32_t)((uintptr_t)target.second-(uintptr_t)stub-5);memcpy(stub+1,&relative,4);
        }
        *(int*)(trackGame+0x9c4)=900;*(int*)(trackGame+0x9c8)=700;
        synchronizeObserverCamera(trackGame);
        require(terrainOrigin[0]==1100 && terrainOrigin[1]==1100 && originCalls==1 && regionCalls==2,"tracking updates terrain and both overlay origins immediately without scrolling");
        require(*(int*)(trackGame+0x9c4)==1100 && *(int*)(trackGame+0x9c8)==1100 && at<int>(0xbea648)==1100 && at<int>(0xbea698)==1100,"tracking display cache matches centered camera");
        require(!memcmp(beforeUnit,trackUnit,sizeof(trackUnit)),"camera tracking preserves unit state");
        *(int*)(trackUnit+0x18)=1800;updateObserverTracking(trackGame);
        require(at<int>(0xb869c4)==1400 && at<int>(0xb869c8)==1100,"tracking keeps moving target centered");
        showObserverHud=false;at<int>(0xbf2578)=1000;at<int>(0xbf2580)=0;
        updateObserverTracking(trackGame);
        require(at<int>(0xb869c4)==1300,"tracking centers against expanded hidden-HUD viewport");
        showObserverHud=true;
        *(unsigned char**)(trackManager+0x2c3c)=replacementUnit;
        *(uint16_t*)(replacementUnit+0x14)=100;
        *(int*)(replacementUnit+0x18)=2700;*(int*)(replacementUnit+0x1c)=2400;
        observerSelected=1;
        synchronizeObserverCamera(trackGame);
        int releasedX=at<int>(0xb869c4),releasedY=at<int>(0xb869c8);
        toggleObserverTracking();require(observerTracked==-1 && !observerTrackedObject,"tracking release stops even after selecting a different live unit");
        *(int*)(trackUnit+0x18)=2000;updateObserverTracking(trackGame);
        require(at<int>(0xb869c4)==releasedX && at<int>(0xb869c8)==releasedY,"released camera stays at its current location and follows neither unit");
        unsigned char nativePlayer[0x1760]{};*(int16_t*)(nativePlayer+0x170c)=1;
        cameraInput=(CameraInput)fakeCameraInput;DWORD oldProducer=producer;producer=GetCurrentThreadId();
        observerCameraInputHook(nativePlayer,nullptr);
        require(at<int>(0xb869c4)==releasedX+7 && *(int16_t*)(nativePlayer+0x170c)==1,"recorded selection cannot retarget camera, manual scroll and player selection preserved");
        at<int>(0xb869c8)=9000;synchronizeObserverCamera(trackGame);
        require(at<int>(0xb869c4)==releasedX && at<int>(0xb869c8)==releasedY && *(int*)(trackGame+0x9c4)==releasedX && terrainOrigin[0]==releasedX,"release frame restores last displayed position across native camera and renderer caches");
        observerCameraInputHook(nativePlayer,nullptr);synchronizeObserverCamera(trackGame);
        require(at<int>(0xb869c4)==releasedX+7,"manual scrolling resumes after one release frame without recorded-unit tracking");
        require(cameraSelectionRestoredOnException(nativePlayer),"native selection count restored on exceptional camera exit");
        producer=oldProducer;cameraInput=nullptr;
        observerSelected=0;
        toggleObserverTracking();*(uint16_t*)(trackUnit+0x14)=0;updateObserverTracking(trackGame);
        require(observerTracked==-1,"tracking stops for dead unit");
        *(uint16_t*)(trackUnit+0x14)=100;toggleObserverTracking();
        *(unsigned char**)(trackManager+0x2c38)=replacementUnit;updateObserverTracking(trackGame);
        require(observerTracked==-1,"tracking stops when target slot changes object");
        *(unsigned char**)(trackManager+0x2c38)=trackUnit;
        *(int16_t*)(trackUnit+4)=30;*(uint16_t*)(trackUnit+0x14)=100;
        observerHeroHitCount=1;observerHeroHits[0]={{12,405,66,459},0,2,30,trackUnit};
        showHeroPanel=true;showObserverHud=true;at<void*>(0xb8282c)=trackGame;
        stopObserverTracking();
        require(clickObserverHero(20,420) && observerTracked==0 && observerSelected==0 && at<int>(0xb869c4)==1600,"portrait click selects and centers that hero for tracking");
        require(clickObserverHero(20,420) && observerTracked==0,"clicking tracked hero portrait keeps tracking instead of toggling off");
        showHeroPanel=false;require(!clickObserverHero(20,420),"H-hidden portrait cannot intercept terrain clicks");showHeroPanel=true;
        showObserverHud=false;require(!clickObserverHero(20,420),"U-hidden portrait cannot intercept terrain clicks");showObserverHud=true;
        stopObserverTracking();*(unsigned char**)(trackManager+0x2c38)=replacementUnit;
        require(clickObserverHero(20,420) && observerTracked==-1,"stale portrait cannot follow replacement object in recycled slot");
        *(unsigned char**)(trackManager+0x2c38)=trackUnit;*(uint16_t*)(trackUnit+0x14)=0;
        require(clickObserverHero(20,420) && observerTracked==-1,"dead hero portrait cannot start tracking");
        *(uint16_t*)(trackUnit+0x14)=100;observerHeroHitCount=0;at<void*>(0xb8282c)=nullptr;
        *(void**)trackUnit=address(0xa2b7dc);
        require(observerUnit(0)==nullptr,"fog proxy is never treated as full gameplay unit");
        toggleObserverTracking();require(observerTracked==-1,"fog proxy cannot enter observer tracking");
        playback=false;active=false;observerSelected=-1;at<void*>(0xb870c0)=nullptr;at<void*>(0xb86f14)=nullptr;
    }
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
    at<int>(0xb869c4)=1300;at<int>(0xb869c8)=1400;
    *(int*)(self+0x9c4)=700;*(int*)(self+0x9c8)=800;
    require(observerAudio(self,(Audio)fakeAudio,1,2,1000,100,5)==8 && observerAudioDepth==0 && at<int>(0xbf2588)==866,"spatial audio uses full hidden-HUD viewport with scoped visibility");
    require(at<int>(0xb869c4)==1300 && at<int>(0xb869c8)==1400,"audio restores simulation camera after playback");
    auto mixer=address(0x700000);observerMixerWidth=0;
    require(observerAudioUpdate(mixer,(Process)fakeMixerUpdate)==876,"continuous mixer follows rendered camera and hidden-HUD viewport, invalidating width cache");
    require(at<int>(0xbf2588)==866 && at<int>(0xb869c4)==1300 && at<int>(0xb869c8)==1400,"mixer update restores shared viewport and camera");
    require(audioExceptionRestored(self) && at<int>(0xb869c4)==1300 && at<int>(0xb869c8)==1400,"audio exception restores camera, width and visibility scope");
    showObserverHud=true;observerHudRedrawFrames=0;
    toggleObserverHud();require(!showObserverHud && !observerHudRedrawFrames,"hide HUD cancels pending full redraw");
    toggleObserverHud();require(showObserverHud && observerHudRedrawFrames==3,"show HUD schedules display buffer restoration");
    unsigned char redrawGame[0x80]{},redrawPlayer[0x600]{},redrawUi[0x20]{};
    auto previousGame=at<void*>(0xb8282c);at<void*>(0xb8282c)=redrawGame;
    ((void**)(redrawGame+0x48))[2]=redrawPlayer;*(void**)(redrawPlayer+0x5e0)=redrawUi;*(void**)(redrawUi+0x10)=self;
    require(redrawObserverSurface(self,(Process)fakeResultRender,(Process)fakeFullSurface)==456,"HUD restore redraws its retained UI surface");
    require(redrawObserverSurface(redrawGame,(Process)fakeResultRender,(Process)fakeFullSurface)==123,"HUD restore preserves incremental world/effect rendering instead of repainting every sprite");
    at<void*>(0xb8282c)=nullptr;
    require(redrawObserverSurface(self,(Process)fakeResultRender,(Process)fakeFullSurface)==123,"missing game cannot force unrelated full redraw");
    at<void*>(0xb8282c)=previousGame;
    observerHudRedrawFrames=0;
    require(redrawObserverSurface(self,(Process)fakeResultRender,(Process)fakeFullSurface)==123,"normal HUD frames retain incremental rendering");
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
    auto nullStreamRead=(unsigned char*)address(0x4943fd);
    const unsigned char faultCode[]={0xa1,8,0,0,0,0xc3};memcpy(nullStreamRead,faultCode,sizeof(faultCode));
    panelKnownFault=true;
    require(observerPanelHook(ui,nullptr)==0 && observerPanelUnavailable && !observerPanelDrawing && *(uint32_t*)(self+0x664)==0,"reported null-stream fault contained with owner restored");
    require(observerPanelHook(ui,nullptr)==0,"failed information panel is not called repeatedly");
    panelKnownFault=false;observerPanelUnavailable=false;
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
    // Population belongs to writer metadata, with ordinals across every command.
    resetState();playback=false;active=true;inDispatch=true;head.store(0);tail.store(0);
    producer=GetCurrentThreadId();populationOrdinal=0;populationSamples=0;initial.state.tick=100;at<uint32_t>(0xb8a280)=101;
    unsigned char populationPlayer[0xe30]{};*(int*)(populationPlayer+0xe24)=12;*(int*)(populationPlayer+0xe28)=10;
    ((void**)(self+0x48))[0]=populationPlayer;
    at<void*>(0xb8282c)=self;
    uint32_t otherOrder=5;commandHook(self,nullptr,4,&otherOrder);
    uint16_t productionOrder[4]={400,0,0,0};commandHook(self,nullptr,8,productionOrder);
    uint32_t position=0;PopulationSample sample{};bool foundPopulation=false;
    while(position<head.load()) {
        Record r{};readRing(position,&r,sizeof(r));
        if(r.kind==Population) {readRing(position+sizeof(r),&sample,sizeof(sample));foundPopulation=true;}
        position+=sizeof(r)+r.size;
    }
    require(foundPopulation && sample.ordinal==1 && sample.tick==101 && sample.player==0 && sample.used==12 && sample.capacity==10,"opening supply captures correct command ordinal, owner and actual over-cap population");
    require(*(int*)(populationPlayer+0xe24)==12,"population metadata does not modify player state");
    active=false;inDispatch=false;((void**)(self+0x48))[0]=nullptr;
    // Independent native-layout evidence: CGame map columns at +38, players at
    // +48 hexadecimal (4c6f40). Old decimal +48 reaches columns for player 2.
    *(uint32_t*)(self+0x38)=256;
    require(legacyPopulationReadFaults(self,2),"pre-fix population capture reproduces access violation on map width interpreted as a player pointer");
    printf("REPRODUCED_OLD_POPULATION_ACCESS_VIOLATION: player 2 aliases map columns\n");
    unsigned char players[8][0xe30]{};
    for(unsigned owner=0;owner<8;++owner) {
        ((void**)(self+0x48))[owner]=players[owner];
        *(int*)(players[owner]+0xe24)=12+owner;*(int*)(players[owner]+0xe28)=10+owner;
    }
    apply=(Apply)fakeProductionApply;
    const uint16_t opcodes[]={400,401,500,170,5170};
    for(unsigned networkMode:{0u,1u}) {
        State baseline{};
        for(bool record:{false,true}) {
            resetState();playback=false;active=record;inDispatch=record;producer=GetCurrentThreadId();
            at<unsigned char>(0xb83ad0)=(unsigned char)networkMode;
            head.store(0);tail.store(0);populationOrdinal=0;populationSamples=0;initial.state.tick=1;
            productionApplyCalls=0;
            for(unsigned owner=0;owner<8;++owner) for(auto opcode:opcodes) {
                uint16_t order[6]={opcode,(uint16_t)owner,1,2,3,4};uint16_t savedOrder[6];memcpy(savedOrder,order,sizeof(order));
                uint32_t length=(opcode==170 || opcode==5170)?12:8;
                require(commandHook(self,nullptr,length,order)==length+opcode+owner,"recording preserves original command return value");
                require(!memcmp(savedOrder,order,sizeof(order)),"recording never edits command bytes");
            }
            require(productionApplyCalls==40 && at<unsigned char>(0xb83ad0)==networkMode && at<void*>(0xb870c8)==&queue && queue==vtable,"all commands reach native handler exactly once without replacing network queue or mode");
            State actual{};require(snapshot(self,actual),"capture A/B fixture state");
            if(!record) baseline=actual;
            else {
                require(same(baseline,actual),"recording on/off produces identical resources, RNG and checksum for all player slots and production opcodes");
                unsigned samples=0;uint32_t offset=0;
                while(offset<head.load()) {
                    Record r{};readRing(offset,&r,sizeof(r));
                    if(r.kind==Population) {
                        PopulationSample p{};readRing(offset+sizeof(r),&p,sizeof(p));
                        require(p.player<8 && p.used==12+p.player && p.capacity==10+p.player,"population comes from the actual command owner's native player slot");++samples;
                    }
                    offset+=sizeof(r)+r.size;
                }
                require(samples==40 && !failed.load(),"all eight owners retain optional metadata in single and multiplayer recording");
            }
        }
    }
    int used=0,capacity=0;
    *(int*)(players[0]+0xe28)=500;*(int*)(players[0]+0xe2c)=7;
    at<uint16_t>(0xb83ad2)=0;self[0xc79]=1;
    require(capturePopulation(self,0,used,capacity) && capacity==277,"base rules population limit and bonus");
    self[0xc79]=2;require(capturePopulation(self,0,used,capacity) && capacity==307,"expanded rules population limit and bonus");
    at<uint16_t>(0xb83ad2)=1;require(capturePopulation(self,0,used,capacity) && capacity==407,"campaign population limit and bonus");
    require(!capturePopulation(self,8,used,capacity),"invalid population owner rejected");
    for(void* invalid:{(void*)nullptr,(void*)0x100}) {
        ((void**)(self+0x48))[2]=invalid;unsigned before=productionApplyCalls;
        uint16_t order[4]={400,2,0,0};
        require(commandHook(self,nullptr,8,order)==410 && productionApplyCalls==before+1 && !failed.load(),"missing or unreadable optional population cannot skip or abort original game command");
    }
    require(populationReadFailures.load()>=2,"optional metadata read failure available to worker diagnostics");
    unsigned beforeOverflow=productionApplyCalls;head.store(RingSize);tail.store(0);
    uint16_t overflowOrder[4]={400,0,0,0};
    require(commandHook(self,nullptr,8,overflowOrder)==408 && productionApplyCalls==beforeOverflow+1 && failed.load(),"full recording ring stops capture but does not stop multiplayer command application");
    head.store(0);tail.store(0);active=false;inDispatch=false;
    nativeSyncReady.store(false);self[0x46c]=1;*(uint32_t*)(self+0x664)=2;
    *(uint32_t*)(self+0x3e4)=123;*(uint32_t*)(self+0x410)=456;
    unsigned char unchangedGame[sizeof(self)];memcpy(unchangedGame,self,sizeof(self));
    playback=true;observeRecordingSync(self);require(!nativeSyncReady.load(),"playback is not diagnosed as a live multiplayer sync error");
    playback=false;at<unsigned char>(0xb83ad0)=0;observeRecordingSync(self);require(!nativeSyncReady.load(),"single player is not diagnosed as multiplayer sync error");
    at<unsigned char>(0xb83ad0)=1;observeRecordingSync(self);
    require(nativeSyncReady.load() && nativeSyncDiagnostic.localPlayer==2 && nativeSyncDiagnostic.reportTicks[0]==123 && nativeSyncDiagnostic.checksums[0]==456 && !memcmp(unchangedGame,self,sizeof(self)),"native sync evidence captured without suppressing flag or changing game state, even after recording failure");
    *(uint32_t*)(self+0x410)=789;observeRecordingSync(self);
    require(nativeSyncDiagnostic.checksums[0]==456,"preserve first sync error evidence for worker without repeated writes");
    printf("NATIVE_ENGINE_TESTS_PASS: capture/replay, live-input separation, RNG/state mismatch, overflow, patch integrity/restoration\n");
    return 0;
}
