#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include "protocol.h"

// Experimental adapter for exactly the SHA256 verified by injector.cpp.
// All addresses are RVAs of this KUF build, never addresses borrowed from another game.
namespace {
using namespace kuf;
uintptr_t base=0;
std::wstring root;
bool playback=false;
std::atomic<bool> failed{false}, terminal{false};
std::atomic<uint32_t> head{0},tail{0};
constexpr uint32_t RingSize=16*1024*1024;
unsigned char* ring=nullptr;
DWORD producer=0;
bool active=false,inDispatch=false;
struct ReplayCommand { CommandHeader header; const unsigned char* bytes; };
std::vector<ReplayCommand> replayBatch;
size_t replayPop=0,replayApply=0;
uint32_t session=0,frames=0;
uint32_t populationOrdinal=0,populationSamples=0;
Initial initial{};
State lastBefore{};
Initial savedInitial{};
MatchSetup savedSetup{};
bool automaticStarted=false,hasSetup=false;
NetworkBootstrap capturedBootstrap{},savedBootstrap{};
bool hasCapturedBootstrap=false,hasBootstrap=false;
bool bootstrapEmitted=false;
bool restoredEmptyNetworkInitial=false;
bool exitConfirmationOpen=false,playbackExitRequested=false;
bool playbackResultsStarted=false;
using ChangeState=void(__thiscall*)(void*,uint32_t);
std::vector<unsigned char> input,expectedText;
size_t cursor=8;
uintptr_t textBase=0;
uint32_t textSize=0;
HANDLE logFile=INVALID_HANDLE_VALUE;
std::atomic<int> notice{0};
HWND gameWindow=nullptr;
WNDPROC oldWindowProc=nullptr;
std::atomic<bool> closePending{false},closeReady{false};
std::atomic<bool> windowDestroyed{false};
constexpr UINT RecordPulse=WM_APP+0x4b1;
std::atomic<ULONGLONG> lastAdvanceAt{0};

template<class T> T& at(uint32_t va) {return *reinterpret_cast<T*>(base+va-0x400000);}
void* address(uint32_t va) {return reinterpret_cast<void*>(base+va-0x400000);}
using Advance=uint32_t(__thiscall*)(void*,uint32_t,uint32_t);
using Apply=uint32_t(__thiscall*)(void*,uint32_t,const void*);
using Process=uint32_t(__thiscall*)(void*);
using Destroy=uint32_t(__thiscall*)(void*,uint32_t);
Advance advance=nullptr;
Advance initializeMap=nullptr;
Apply apply=nullptr;
Process process=nullptr;
Process resultRender=nullptr;
using ResultButton=uint32_t(__thiscall*)(void*,uint32_t);
ResultButton resultButton=nullptr;
using SpriteFrame=void(__thiscall*)(void*,int,int);
Process applicationProcess=nullptr;
using CameraInput=void(__thiscall*)(void*);
CameraInput cameraInput=nullptr;
Process prepareDisplay=nullptr,drawDisplay=nullptr;
Process interfaceRender=nullptr;
Process buildingRender=nullptr;
Process buildingBodyPrepare=nullptr;
std::atomic<uint32_t> observerBuildingBodies{0},observerConstructionDraws{0};
Process characterRender=nullptr;
using MinimapAlarmBlit=void(__cdecl*)(const void*,void*,int,int,int);
MinimapAlarmBlit minimapAlarmBlit=nullptr;
void __cdecl replayMinimapAlarmBlit(const void* image,void* surface,int pitch,int width,int height) {
    // Only the KUFSMALLMAPEFFECT_RENDERER call site is redirected. Its timer
    // and expired-effect cleanup still run; terrain/unit/camera drawing is intact.
    if(playback && active) return;
    minimapAlarmBlit(image,surface,pitch,width,height);
}
using TextDraw=uint32_t(__thiscall*)(void*,const char*,void*,int,int,int,int);
TextDraw textDraw=nullptr;
using GlyphDraw=uint32_t(__cdecl*)(const uint16_t*,int,int,int,int,void*,int);
GlyphDraw glyphDraw=nullptr;
unsigned observerIdTintDepth=0;
uint16_t observerIdTintColor=0;
std::atomic<uint32_t> observerIdPaints{0},observerIdGlyphs{0},observerIdGlyphFailures{0};
using Audio=uint32_t(__thiscall*)(void*,uint32_t,uint32_t,int,int,uint32_t);
Audio spatialAudio=nullptr,spatialLoopAudio=nullptr;
Process spatialUpdate=nullptr;
int observerMixerWidth=0;
using UnitAudio=uint32_t(__thiscall*)(void*,void*,int,uint32_t);
UnitAudio localUnitAudio=nullptr,remoteUnitAudio=nullptr;
std::atomic<uint32_t> observerDeathEvents{0},observerDeathSubmitted{0};
std::atomic<uint32_t> observerDeathAccepted{0},observerDeathRejected{0},observerDeathOnscreen{0},observerLastDeath{0};
unsigned observerAudioDepth=0;
std::atomic<uint32_t> observerAudioCalls{0},observerAudioOnscreen{0},observerAudioRejected{0},observerAudioDisabled{0},observerAudioCameraFixes{0},observerAudioTick{0};
using Selection=uint32_t(__thiscall*)(void*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
Selection originalSelection=nullptr;
uint32_t __fastcall selectionHook(void* self,void*,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g,uint32_t h) {
    if(playback && active && !inDispatch) return 0;
    return originalSelection(self,a,b,c,d,e,f,g,h);
}
Destroy destroy=nullptr;
struct VisibilityBackup {unsigned char* object;unsigned char fog;unsigned char invisible;unsigned char buildingDraw;bool building;};
VisibilityBackup visibilityBackups[11264]{};
unsigned visibilityCount=0,displayDepth=0;
unsigned char savedFog=0;
int savedViewportWidth=0;
struct ClipBackup {unsigned char* object;int width;};
ClipBackup clipBackups[2]{};
unsigned clipCount=0;
bool showResources=true,showObserverHud=true,showReplayTime=true;
unsigned observerHudRedrawFrames=0;
void toggleObserverHud() {
    showObserverHud=!showObserverHud;
    // Refresh all retained sprite surfaces across the display buffer cycle.
    observerHudRedrawFrames=showObserverHud?3:0;
}
uint32_t redrawObserverSurface(void* self,Process original,Process fullDraw) {
    return playback && active && showObserverHud && observerHudRedrawFrames?fullDraw(self):original(self);
}
unsigned replaySpeed=1;
uint32_t clockWall=0,clockValue=0,clockTarget=0;
State savedEnd{};
int observerSelected=-1;
alignas(16) unsigned char observerPanel[0x1908]{}; // Native deleting destructor 500471 verifies this size.
void* observerPanelSource=nullptr;
int panelSelected=-2;
int16_t observerSelectionSlots[16]{};
bool observerPanelDrawing=false;
bool observerPanelUnavailable=false;
unsigned char* observerTerrainPixels=nullptr;
unsigned char* observerTerrainRows=nullptr;
size_t observerTerrainCapacity=0,observerRowCapacity=0;
unsigned char* observerUnit(int index);
void synchronizeObserverCamera(void* self);
void log(const char* text);
int observerPanelException(EXCEPTION_POINTERS* info) {
    const auto exception=info->ExceptionRecord;
    // Only the reported panel null-stream read; simulation and other failures
    // keep their normal exception handling. Never bypass replay state checks.
    if(exception->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || exception->ExceptionAddress!=address(0x4943fd) ||
       exception->NumberParameters<2 || exception->ExceptionInformation[0]!=0 || exception->ExceptionInformation[1]!=8) return EXCEPTION_CONTINUE_SEARCH;
    observerPanelUnavailable=true;
    log("RP01: Native observer information panel null-stream read prevented; basic observer information remains available.");
    return EXCEPTION_EXECUTE_HANDLER;
}

uint32_t observerAudio(void* self,Audio original,uint32_t a,uint32_t b,int x,int y,uint32_t c) {
    if(!playback || !active || GetCurrentThreadId()!=producer) return original(self,a,b,x,y,c);
    int savedWidth=at<int>(0xbf2588),savedX=at<int>(0xb869c4),savedY=at<int>(0xb869c8);uint32_t result=0;
    __try {
        ++observerAudioDepth;++observerAudioCalls;
        if(!showObserverHud) at<int>(0xbf2588)=at<int>(0xbf2578)-at<int>(0xbf2580);
        // Rendering and picking use CGame's camera cache (4c6acf/4ced63).
        // Use that same visible origin for distance and pan, then restore it.
        auto game=(unsigned char*)at<void*>(0xb8282c);
        if(game) {
            int visibleX=*(int*)(game+0x9c4),visibleY=*(int*)(game+0x9c8);
            if(visibleX>=128 && visibleY>=128) {
                if(visibleX!=savedX || visibleY!=savedY) ++observerAudioCameraFixes;
                at<int>(0xb869c4)=visibleX;at<int>(0xb869c8)=visibleY;
            }
        }
        if(x>=at<int>(0xb869c4) && (int64_t)x< (int64_t)at<int>(0xb869c4)+at<int>(0xbf2588) &&
           y>=at<int>(0xb869c8) && (int64_t)y< (int64_t)at<int>(0xb869c8)+at<int>(0xbf258c)) ++observerAudioOnscreen;
        if(!at<int>(0xbf2660) || (at<unsigned char>(0xbea2af) && !at<unsigned char>(0xbea2ae))) ++observerAudioDisabled;
        result=original(self,a,b,x,y,c);
        if(result==0xffffffffu) ++observerAudioRejected;
    } __finally {
        --observerAudioDepth;at<int>(0xbf2588)=savedWidth;
        at<int>(0xb869c4)=savedX;at<int>(0xb869c8)=savedY;
    }
    return result;
}
uint32_t observerAudioUpdate(void* self,Process original) {
    if(!playback || !active || GetCurrentThreadId()!=producer) return original(self);
    int width=at<int>(0xbf2588),x=at<int>(0xb869c4),y=at<int>(0xb869c8);uint32_t result=0;
    __try {
        if(!showObserverHud) at<int>(0xbf2588)=at<int>(0xbf2578)-at<int>(0xbf2580);
        auto game=(unsigned char*)at<void*>(0xb8282c);
        if(game && *(int*)(game+0x9c4)>=128 && *(int*)(game+0x9c8)>=128) {
            at<int>(0xb869c4)=*(int*)(game+0x9c4);at<int>(0xb869c8)=*(int*)(game+0x9c8);
        }
        if(observerMixerWidth!=at<int>(0xbf2588)) {
            // Native 540c26 only checks camera movement, ignoring HUD width.
            *(int*)((unsigned char*)self+0xfcd20)=(int)0x80000000u;
            observerMixerWidth=at<int>(0xbf2588);
        }
        result=original(self);
    } __finally {at<int>(0xbf2588)=width;at<int>(0xb869c4)=x;at<int>(0xb869c8)=y;}
    return result;
}
uint32_t __fastcall spatialUpdateHook(void* self,void*) {return observerAudioUpdate(self,spatialUpdate);}
using VoiceAudio=uint32_t(__thiscall*)(void*,uint32_t,uint32_t,uint32_t);
bool replayDeathSound(uint32_t sound) {
    if(!base) return false;
    // fx.map explicitly labels these native event-4 ranges as ';dead'.
    for(unsigned type=0;type<116;++type) {
        int count=at<int16_t>(0xb38f50+type*20+16),first=at<int16_t>(0xb38f52+type*20+16);
        if(count>0 && count<=16 && first>=0 && sound>=(uint32_t)first && sound<(uint32_t)(first+count)) return true;
    }
    return false;
}
uint32_t replayVoice(void* self,VoiceAudio original,uint32_t sound,uint32_t archive,uint32_t flags) {
    // The second argument is a resource-file object, NOT volume: 5413be passes
    // it to 5f6af0. Nulling it can break file loading and using the FX archive
    // for death sounds cannot locate assets from the speech archive.
    if(playback && !(active && replayDeathSound(sound))) return 0xffffffffu;
    return original(self,sound,archive,flags);
}
uint32_t __fastcall replayVoiceHook(void* self,void*,uint32_t sound,uint32_t volume,uint32_t flags) {
    return replayVoice(self,(VoiceAudio)address(0x541a40),sound,volume,flags);
}
uint32_t __fastcall replaySpatialVoiceHook(void* self,void*,uint32_t sound,uint32_t volume,int x,int y,uint32_t flags) {
    // Death events are handled before ownership/cooldown gates below. Remaining
    // spatial responses (e.g. forced acknowledgements) are dialogue, too.
    if(playback && !(active && replayDeathSound(sound))) return 0xffffffffu;
    return observerAudio(self,spatialAudio,sound,volume,x,y,flags);
}
uint32_t replayUnitAudio(void* self,UnitAudio original,void* object,int event,uint32_t flags) {
    if(!playback || !active || event!=4 || GetCurrentThreadId()!=producer)
        return original(self,object,event,flags);
    // Death animation routines 481a13/481b23 and faction-specific counterparts
    // dispatch event 4. Native 521090/53d570 reject other owners or an occupied
    // speech channel before reaching spatial audio; neither gate applies here.
    if(!object) return 0;
    auto unit=(unsigned char*)object;
    int type=*(int16_t*)(unit+4);
    if(type<0 || type>=116) return 0;
    uint32_t serial=observerDeathEvents.fetch_add(1);
    int count=at<int16_t>(0xb38f50+type*20+16);
    int first=at<int16_t>(0xb38f52+type*20+16);
    if(count<=0 || count>16 || first<0 || !spatialAudio || !at<void*>(0xb83ab0) || !at<void*>(0xb8a1e4)) return 0;
    for(int n=0;n<count;++n) {
        int variant=(serial+n)%count;
        // Same availability marker as native 521211/53d6a8. Retry available
        // variants without touching native RNG, player response caches or units.
        if(at<int16_t>(0xb39a52+type*44+variant*2)==-1) continue;
        ++observerDeathSubmitted;
        observerLastDeath=first+variant;
        auto game=(unsigned char*)at<void*>(0xb8282c);
        int cameraX=game?*(int*)(game+0x9c4):at<int>(0xb869c4);
        int cameraY=game?*(int*)(game+0x9c8):at<int>(0xb869c8);
        int width=showObserverHud?at<int>(0xbf2588):at<int>(0xbf2578)-at<int>(0xbf2580);
        int x=*(int*)(unit+0x18),y=*(int*)(unit+0x1c);
        if(x>=cameraX && (int64_t)x<(int64_t)cameraX+width && y>=cameraY && (int64_t)y<(int64_t)cameraY+at<int>(0xbf258c)) ++observerDeathOnscreen;
        uint32_t result=observerAudio(at<void*>(0xb83ab0),spatialAudio,first+variant,
            at<uint32_t>(0xb8a1e4),*(int*)(unit+0x18),*(int*)(unit+0x1c),0);
        if(result==0xffffffffu) ++observerDeathRejected;else ++observerDeathAccepted;
        return result;
    }
    return 0;
}
uint32_t __fastcall localUnitAudioHook(void* self,void*,void* unit,int event,uint32_t flags) {
    return replayUnitAudio(self,localUnitAudio,unit,event,flags);
}
uint32_t __fastcall remoteUnitAudioHook(void* self,void*,void* unit,int event,uint32_t flags) {
    return replayUnitAudio(self,remoteUnitAudio,unit,event,flags);
}
uint32_t __fastcall spatialAudioHook(void* self,void*,uint32_t a,uint32_t b,int x,int y,uint32_t c) {return observerAudio(self,spatialAudio,a,b,x,y,c);}
uint32_t __fastcall spatialLoopAudioHook(void* self,void*,uint32_t a,uint32_t b,int x,int y,uint32_t c) {return observerAudio(self,spatialLoopAudio,a,b,x,y,c);}
int __fastcall observerAudioVisibility(void* self,void*,int player,int x,int y) {
    // Only the two spatial audio call sites use this hook. Simulation fog stays intact.
    if(observerAudioDepth && GetCurrentThreadId()==producer) return 1;
    using Visible=int(__thiscall*)(void*,int,int,int);
    return ((Visible)address(0x4369f0))(self,player,x,y);
}

uint32_t drawExpandedTerrain(void* self,Process original) {
    auto terrain=(unsigned char*)self;
    auto surface=*(unsigned char**)(terrain+4);
    auto image=*(void**)(terrain+0xc);
    using Dimension=int(__thiscall*)(void*);
    if(!image || *(int*)(terrain+0x30)<0) return original(self);
    int tileWidth=((Dimension)address(0x67bfe0))(image);
    int width=at<int>(0xbf2588),height=*(int*)(terrain+0x20);
    if(!surface || tileWidth<=0 || tileWidth>512 || width<=0 || width>4096 || height<=0 || height>4096) return original(self);
    int columns=(width+tileWidth-1)/tileWidth+1,pitch=columns*tileWidth;
    size_t pixels=(size_t)pitch*height*2,rows=(size_t)height*4;
    if(pixels>observerTerrainCapacity) {
        auto next=(unsigned char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,pixels);
        if(!next) return original(self);
        if(observerTerrainPixels) HeapFree(GetProcessHeap(),0,observerTerrainPixels);
        observerTerrainPixels=next;observerTerrainCapacity=pixels;
    }
    if(rows>observerRowCapacity) {
        auto next=(unsigned char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,rows);
        if(!next) return original(self);
        if(observerTerrainRows) HeapFree(GetProcessHeap(),0,observerTerrainRows);
        observerTerrainRows=next;observerRowCapacity=rows;
    }
    unsigned char saved[0x3c];memcpy(saved,terrain,sizeof(saved));
    int surfaceWidth=*(int*)(surface+4);uint32_t result=0;
    using Tile=void*(__thiscall*)(void*,int);
    auto tile=(unsigned char*)((Tile)address(0x67eea0))(image,*(int*)(terrain+0x30));
    if(!tile) return original(self);
    int originX=*(int*)(tile+0x30),originY=*(int*)(tile+0x34);
    __try {
        *(int*)(surface+4)=width;
        *(void**)(terrain+0x10)=observerTerrainPixels;
        *(void**)(terrain+0x2c)=observerTerrainRows;
        *(int*)(terrain+0x1c)=pitch;*(int*)(terrain+0x24)=columns;
        memset(observerTerrainRows,0,rows);terrain[8]=1;
        using Refresh=void(__thiscall*)(void*,int,int);
        ((Refresh)address(0x682430))(self,originX,originY);
        result=original(self);
    } __finally {
        memcpy(terrain,saved,sizeof(saved));*(int*)(surface+4)=surfaceWidth;
    }
    return result;
}

bool observerNeedsBuildingBody(unsigned char* object) {
    if(!object || *(int16_t*)(object+6)!=1 || *(int16_t*)(object+0x4c)!=0 ||
       !*(uint16_t*)(object+0x14) || object[0x14d8] || *(int16_t*)(object+0x14a6)!=0) return false;
    // Exact native 47aa6f..47aaa3 readiness and state exclusions. Never paint
    // a completed body over construction scaffolding or destruction states.
    uint16_t state=*(uint16_t*)(object+0xc);
    return state!=0x3e80 && state!=0x283c && state!=0x2a30 && state!=0x27d8;
}
void prepareObserverBuildingBody(unsigned char* object,Process prepare) {
    if(!playback || !active || inDispatch || GetCurrentThreadId()!=producer || !displayDepth || !observerNeedsBuildingBody(object)) return;
    prepare(object);
    // Native 478440 owns/initializes graphics at +168 and sets +14d8 ready.
    // Keep that resource-cache readiness truthful; it is not explored/detection
    // state. The normal upgrade/reset/destructor paths retain ownership.
    observerBuildingBodies.fetch_add(1,std::memory_order_relaxed);
}
void enterObserverDisplay() {
    if(displayDepth++) return;
    savedFog=at<unsigned char>(0xb3ba61);visibilityCount=0;
    savedViewportWidth=at<int>(0xbf2588);
    clipCount=0;
    if(!showObserverHud) at<int>(0xbf2588)=at<int>(0xbf2578)-at<int>(0xbf2580);
    auto game=(unsigned char*)at<void*>(0xb8282c);
    if(game) synchronizeObserverCamera(game);
    if(game && !showObserverHud) for(unsigned offset:{0x9b4u,0x9b8u}) {
        auto context=*(unsigned char**)(game+offset);
        if(context && *(void**)context==address(0xa2cea8)) {
            clipBackups[clipCount++]={context,*(int*)(context+0x974)};
            *(int*)(context+0x974)=at<int>(0xbf2588);
        }
    }
    void* manager=at<void*>(0xb870c0);
    if(manager) {
        auto bytes=(unsigned char*)manager;
        uint32_t count=*(uint32_t*)(bytes+0xdc38),index=*(uint32_t*)(bytes+0xdc3c);
        if(count>11264) return;
        for(uint32_t i=0;i<count && index<11264;++i) {
            auto unit=((unsigned char**)(bytes+0x2c38))[index];
            if(unit) {
                bool building=*(int16_t*)(unit+6)==1;
                visibilityBackups[visibilityCount++]={unit,unit[0x6b],unit[0x69],building?unit[0x14d9]:(unsigned char)0,building};
                if(building) unit[0x14d9]=1; // 47afdd construction/completed-detail draw gate.
                unit[0x6b]=0; // The native display-only fog cache (517560/496f26).
                unit[0x69]=0; // Native invisibility gates, scoped strictly to display callbacks.
            }
            index=*(uint32_t*)(bytes+0xdc4c+index*12);
        }
    }
    at<unsigned char>(0xb3ba61)=1;
    if(buildingBodyPrepare) for(unsigned i=0;i<visibilityCount;++i)
        if(visibilityBackups[i].building) prepareObserverBuildingBody(visibilityBackups[i].object,buildingBodyPrepare);
}

void leaveObserverDisplay() {
    if(--displayDepth) return;
    for(unsigned i=0;i<visibilityCount;++i) {
        visibilityBackups[i].object[0x6b]=visibilityBackups[i].fog;
        visibilityBackups[i].object[0x69]=visibilityBackups[i].invisible;
        if(visibilityBackups[i].building) visibilityBackups[i].object[0x14d9]=visibilityBackups[i].buildingDraw;
    }
    visibilityCount=0;at<unsigned char>(0xb3ba61)=savedFog;
    at<int>(0xbf2588)=savedViewportWidth;
    for(unsigned i=0;i<clipCount;++i) *(int*)(clipBackups[i].object+0x974)=clipBackups[i].width;
    clipCount=0;
}

uint32_t observerDisplay(void* self,Process original) {
    if(!playback || !active || inDispatch || GetCurrentThreadId()!=producer) return original(self);
    uint32_t result=0;
    __try {enterObserverDisplay();result=original(self);}
    __finally {leaveObserverDisplay();}
    return result;
}
uint32_t __fastcall prepareDisplayHook(void* self,void*) {return observerDisplay(self,prepareDisplay);}
uint32_t __fastcall drawDisplayHook(void* self,void*) {
    uint32_t result=observerDisplay(self,drawDisplay);
    if(playback && active && showObserverHud && observerHudRedrawFrames) --observerHudRedrawFrames;
    return result;
}
bool observerResource(const unsigned char* object) {
    if(!object || *(const int16_t*)(object+6)!=1) return false;
    // Exact RTTI tables of RESOURCE_GOLD/STEEL/MANA in the supported builds.
    auto table=*(void* const*)object;
    return table==address(0xa3db54) || table==address(0xa3d9c4) || table==address(0xa3dce4);
}
bool observerInspectable(const unsigned char* object) {
    if(!object || *(const int16_t*)(object+0x4c)!=0) return false;
    int kind=*(const int16_t*)(object+6);
    if(kind!=0 && kind!=1) return false;
    if(observerResource(object)) return true; // Neutral resource HP can be zero.
    int owner=*(const int16_t*)(object+8);
    return owner>=0 && owner<8 && *(const uint16_t*)(object+0x14)>0;
}
void prepareObserverBuildingRing(unsigned char* object,SpriteFrame setFrame) {
    auto sprite=*(unsigned char**)(object+0x84);int type=*(int16_t*)(object+4);
    if(!sprite || type<0 || type>=116) return;
    // 47872d..478770 normally prepares this during simulation selection.
    // Observer selection deliberately never invokes that simulation function.
    *(int16_t*)(sprite+0xc)=(int16_t)*(int*)(object+0x18);
    *(int16_t*)(sprite+0xe)=(int16_t)*(int*)(object+0x1c);
    int frame=at<int16_t>(0xb36338+type*2)+27+2*7; // Native +4f selection relation.
    setFrame(sprite,frame,0);*(int*)(sprite+0x28)=1;
}
uint32_t observerObjectDisplay(void* self,Process original) {
    if(!playback || !active || inDispatch || GetCurrentThreadId()!=producer) return original(self);
    auto building=(unsigned char*)self;
    unsigned char fog=building[0x6b],invisible=building[0x69],globalFog=at<unsigned char>(0xb3ba61);
    bool isBuilding=*(int16_t*)(building+6)==1;
    unsigned char buildingDraw=isBuilding?building[0x14d9]:0;
    unsigned char selected=building[0x4e],enemySelected=building[0x4f];
    bool observerChoice=observerSelected>=0 && observerUnit(observerSelected)==building &&
        observerInspectable(building);

    uint32_t result=0;
    // Actual sprite callbacks run after preparation has restored detection.
    // Building 47ad13 and character 494a08 -> 48ffe0 recheck these bytes.
    __try {
        building[0x6b]=0;building[0x69]=0;at<unsigned char>(0xb3ba61)=1;
        // 494f61/47af36 gate native selection-ring sprites on +4e;
        // enemy characters additionally require +4f at 494f82.
        // Never invoke player selection or modify its command/selection arrays.
        building[0x4e]=observerChoice?1:0;
        building[0x4f]=observerChoice?1:0;

        if(isBuilding) {
            building[0x14d9]=1;
            if(observerChoice) prepareObserverBuildingRing(building,(SpriteFrame)address(0x475930));
            if(*(int16_t*)(building+0x14a6)!=0) observerConstructionDraws.fetch_add(1,std::memory_order_relaxed);
        }
        result=original(self);
    } __finally {
        building[0x6b]=fog;building[0x69]=invisible;at<unsigned char>(0xb3ba61)=globalFog;
        building[0x4e]=selected;building[0x4f]=enemySelected;
        if(isBuilding) building[0x14d9]=buildingDraw;
    }
    return result;
}
uint32_t __fastcall characterRenderHook(void* self,void*) {return observerObjectDisplay(self,characterRender);}
uint32_t __fastcall buildingRenderHook(void* self,void*) {return observerObjectDisplay(self,buildingRender);}
uint32_t __fastcall observerPanelHook(void* self,void*) {
    if(playback && active && !showObserverHud) return 0;
    auto original=(Process)(*(void***)self)[4];
    if(!playback || !active) return original(self);
    if(observerPanelUnavailable) return 0;
    auto game=(unsigned char*)at<void*>(0xb8282c);
    auto ui=(unsigned char*)self;
    if(!game || *(void**)(ui+0x23c)!=((void**)(game+0x48))[*(uint32_t*)(game+0x664)]) return original(self);
    if(observerPanelSource!=self) {
        memcpy(observerPanel,self,sizeof(observerPanel));
        observerPanelSource=self;panelSelected=-2;
    }
    auto unit=observerUnit(observerSelected);
    int owner=unit?*(int16_t*)(unit+8):-1;
    bool valid=observerInspectable(unit);
    bool playerOwned=valid && !observerResource(unit);
    int selected=valid?observerSelected:-1;
    memset(observerSelectionSlots,0xff,sizeof(observerSelectionSlots));
    observerSelectionSlots[0]=(int16_t)selected;
    *(int16_t*)(observerPanel+0x1898)=valid?1:0;
    *(void**)(observerPanel+0x189c)=observerSelectionSlots;
    if(panelSelected!=selected) {
        panelSelected=selected;
        *(int*)(observerPanel+0x18a4)=-1;
        *(int*)(observerPanel+0x6a4)=-1;
        *(int*)(observerPanel+0x2b4)=-1;
        *(uint16_t*)(observerPanel+0x6a8)=0xffff;
        *(uint16_t*)(observerPanel+0x6ac)=0xffff;
    }
    uint32_t savedPlayer=*(uint32_t*)(game+0x664),result=0;
    *(void**)(observerPanel+0x23c)=playerOwned?((void**)(game+0x48))[owner]:*(void**)(ui+0x23c);
    __try {
        observerPanelDrawing=true;
        // Neutral resources use the existing viewing player, as in normal play.
        // Never index the eight participant slots with neutral owner 10.
        if(playerOwned) *(uint32_t*)(game+0x664)=(uint32_t)owner;
        __try {result=original(observerPanel);}
        __except(observerPanelException(GetExceptionInformation())) {result=0;}
    } __finally {*(uint32_t*)(game+0x664)=savedPlayer;observerPanelDrawing=false;}
    return result;
}
uint32_t __fastcall observerSelectionSync(void* self,void*,uint32_t writePlayerSelection) {
    using Sync=uint32_t(__thiscall*)(void*,uint32_t);
    return ((Sync)address(0x512a00))(self,observerPanelDrawing?0:writePlayerSelection);
}
uint32_t __fastcall observerMinimapBuildingVisible(unsigned char* building,void*) {
    // Type +6==1 goes to 4b3372, independently of terrain/unit visibility.
    // Native 4b3385 hides buildings whose remembered-visibility byte is zero.
    if(playback && active && displayDepth && GetCurrentThreadId()==producer) return 1;
    return building[0x14e8]!=0;
}
__declspec(naked) void observerMinimapBuildingFlags() {
    __asm {
        push eax
        push ecx
        push edx
        mov ecx,edi
        call observerMinimapBuildingVisible
        test al,al
        pop edx
        pop ecx
        pop eax
        ret
    }
}
uint32_t __fastcall observerLayerHook(void* self,void*) {
    auto original=(Process)(*(void***)self)[2];
    if(original==(Process)address(0x624f00) && playback && active && showObserverHud && observerHudRedrawFrames)
        return redrawObserverSurface(self,original,(Process)address(0x62dda0));
    if(playback && active && showObserverHud && original==(Process)address(0x4b3ff0)) {
        // 4b2fa0 produces the minimap pixels and tests the display fog flag.
        // Regenerate in the existing display scope, without changing exploration data.
        auto map=(unsigned char*)self;
        // The first loading frames register this layer before its terrain source.
        // Native messages initialize 3c/1c later; never run the producer early.
        auto source=*(unsigned char**)(map+0x3c);
        if(*(int*)(map+0x1c)<=0 || (map[0x9f] && (!source || !*(void**)(source+0x28)))) return original(self);
        unsigned char dirty=map[0xa2];uint32_t stamp=at<uint32_t>(0xb8a244),result=0;
        __try {map[0xa2]=1;observerDisplay(self,(Process)address(0x4b2fa0));result=original(self);}
        __finally {map[0xa2]=dirty;at<uint32_t>(0xb8a244)=stamp;}
        return result;
    }
    if(playback && active && !showObserverHud) {
        if(original==(Process)address(0x6819e0)) return drawExpandedTerrain(self,original);
        if(original==(Process)address(0x4b3ff0) || original==(Process)address(0x505100)) return 1;
        auto game=(unsigned char*)at<void*>(0xb8282c);
        if(game) for(unsigned i=0;i<8;++i) {
            auto player=((unsigned char**)(game+0x48))[i];
            auto ui=player?*(unsigned char**)(player+0x5e0):nullptr;
            if(ui && *(void**)(ui+0x10)==self) return 1;
        }
    }
    return original(self);
}
uint32_t __fastcall observerInputOne(void*,void*,uint32_t) {return 0;}
uint32_t __fastcall observerInputTwo(void*,void*,uint32_t,uint32_t) {return 0;}

uint32_t __stdcall playbackClock() {
    using Timer=uint32_t(__stdcall*)();
    uint32_t now=((Timer)at<void*>(0xa16cfc))();
    if(exitConfirmationOpen) {clockWall=now;return clockValue;}
    if(playback && savedInitial.networkMode==1 && automaticStarted) {
        if(!clockWall) {clockWall=now;clockValue=now;clockTarget=now;return now;}
        clockTarget+=(now-clockWall)*replaySpeed;
        uint32_t delta=clockTarget-clockValue;
        uint32_t interval=savedBootstrap.tickInterval;
        if(delta>interval) delta=interval;
        clockValue+=delta;clockWall=now;return clockValue;
    }
    if(!clockWall || !active) {clockWall=now;clockValue=now;return now;}
    clockValue+=(now-clockWall)*replaySpeed;clockWall=now;
    return clockValue;
}

struct ObserverPlayer {bool participant;int steel,gold,mana,population,capacity,farmers;int race,color;};
ObserverPlayer observerPlayers[8]{};
PlayerNames savedNames{1,{}};
bool loadRecordedNames(const unsigned char* data,uint32_t size) {
    if(size!=sizeof(PlayerNames)) return false;
    PlayerNames names{};memcpy(&names,data,sizeof(names));
    if(names.version!=1) return false;
    for(auto& name:names.names) if(!memchr(name,0,sizeof(name))) return false;
    savedNames=names;return true;
}
bool copyPlayerName(const char* text,int length,char (&out)[64]) {
    __try {
        if(!text || length<1 || length>1024) return false;
        unsigned n=(unsigned)std::min(length,63),end=0;
        while(end<n) {
            unsigned width=IsDBCSLeadByteEx(949,(BYTE)text[end])?2:1;
            if(end+width>n || (BYTE)text[end]<32 || (BYTE)text[end]==127) break;
            end+=width;
        }
        if(!end) return false;
        memcpy(out,text,end);out[end]=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool captureCStringName(void* field,char (&out)[64]) {
    __try {
        const char* text=*(const char**)field;
        return text && copyPlayerName(text,*(const int*)(text-12),out);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool captureLobbyName(unsigned index,char (&out)[64]) {
    __try {
        auto lobby=(unsigned char*)at<void*>(0xbed230);
        if(!lobby) return false;
        int count=*(int*)(lobby+8);
        if(count<1 || count>8 || index>=(unsigned)count) return false;
        const char* text=*(const char**)(lobby+0x38+index*4);
        if(!text) return false;
        int length=0;while(length<=1024 && text[length]) ++length;
        return copyPlayerName(text,length,out);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void captureNames(void* self,PlayerNames& names) {
    names={1,{}};
    // 58ed30..58ed6d copies lobby names into eight CString fields at +500.
    // CString stores byte length at data-12; read bounded bytes, no game writes.
    for(unsigned i=0;i<8;++i) {
        // Isolate invalid/empty slots: one unreadable CString must not discard
        // all other players. Native 58ed30 uses these same lobby pointers.
        captureCStringName((unsigned char*)self+0x500+i*4,names.names[i]);
        if(base && self==at<void*>(0xb8282c) && at<uint16_t>(0xb83ad0)==1) {
            char lobbyName[64]{};
            if(captureLobbyName(i,lobbyName)) memcpy(names.names[i],lobbyName,64);
        }
    }
}
using AssignName=void(__thiscall*)(void*,const char*,int);
void restorePlayerNames(void* self,AssignName assign) {
    for(unsigned i=0;i<8;++i) if(savedNames.names[i][0])
        assign((unsigned char*)self+0x500+i*4,savedNames.names[i],(int)strlen(savedNames.names[i]));
}
const char* observerName(unsigned player,char (&fallback)[16]) {
    if(player<8 && savedNames.names[player][0]) return savedNames.names[player];
    sprintf_s(fallback,"P%u",player+1);return fallback;
}
uint32_t observerRefreshAt=0;
unsigned char* observerUnit(int index) {
    auto manager=(unsigned char*)at<void*>(0xb870c0);
    auto unit=manager && index>=0 && index<11264?((unsigned char**)(manager+0x2c38))[index]:nullptr;
    // KUFCHARFOG is a shorter display proxy, not a KUFCHAR_ gameplay object.
    // Its apparent +208 vtable entry aliases KUFCHAR_::stream reader 4943d0;
    // invoking it from panel 506e19 interprets UI stack data as a stream.
    if(unit && *(void**)unit==address(0xa2b7dc)) {
        static bool reported=false;
        if(!reported) {reported=true;log("RP02: KUFCHARFOG display proxy excluded from observer selection, information and tracking.");}
        return nullptr;
    }
    return unit;
}
int observerTracked=-1;
unsigned char* observerTrackedObject=nullptr;
bool observerCameraValid=false,observerCameraRelease=false;
int observerCameraX=0,observerCameraY=0;
void stopObserverTracking(bool hold=false) {
    observerCameraRelease=hold && observerTracked>=0 && observerCameraValid;
    observerTracked=-1;observerTrackedObject=nullptr;observerCameraValid=false;
}
void __fastcall observerCameraInputHook(void* self,void*) {
    if(!playback || !active || inDispatch || GetCurrentThreadId()!=producer) {cameraInput(self);return;}
    // 53cee0 derives an automatic camera target from the recorded player's
    // +170c/+1710 selection. Leave manual edge/keyboard scrolling intact.
    auto count=(int16_t*)((unsigned char*)self+0x170c);int16_t saved=*count;
    __try {*count=0;cameraInput(self);}
    __finally {*count=saved;}
}
bool liveObserverUnit(const unsigned char* unit) {
    return unit && *(const int16_t*)(unit+0x4c)==0 && *(const uint16_t*)(unit+0x14)>0;
}
void toggleObserverTracking() {
    // Ctrl+C is a stop operation while tracking, even if the observer has
    // since selected another unit. Never retarget as a side effect of release.
    if(observerTracked>=0) {stopObserverTracking(true);return;}
    auto unit=observerUnit(observerSelected);
    if(!liveObserverUnit(unit)) {
        stopObserverTracking();return;
    }
    observerTracked=observerSelected;observerTrackedObject=unit;
    observerCameraValid=false;observerCameraRelease=false;

}
void updateObserverTracking(void* self) {
    if(!playback || !active || observerTracked<0) return;
    auto unit=observerUnit(observerTracked);
    if(unit!=observerTrackedObject || !liveObserverUnit(unit)) {stopObserverTracking(true);return;}
    auto game=(unsigned char*)self;
    int width=showObserverHud?at<int>(0xbf2588):at<int>(0xbf2578)-at<int>(0xbf2580);
    int height=at<int>(0xbf258c);
    int columns=*(int*)(game+0x38),rows=*(int*)(game+0x3c);
    if(width<=0 || height<=0 || columns<1 || columns>65535 || rows<1 || rows>65535) return;
    // Native camera bounds: 53e973..53e9db. Only display origins change;
    // no player selection, orders, RNG or unit coordinates are modified.
    int maximumX=std::max(128,columns*32-width-130),maximumY=std::max(128,rows*32-height-130);
    at<int>(0xb869c4)=std::max(128,std::min(maximumX,*(int*)(unit+0x18)-width/2));
    at<int>(0xb869c8)=std::max(128,std::min(maximumY,*(int*)(unit+0x1c)-height/2));
}
void synchronizeObserverCamera(void* self) {
    if(!playback || !active || (observerTracked<0 && !observerCameraRelease)) return;
    if(observerTracked>=0) updateObserverTracking(self);
    if(observerTracked<0 && !observerCameraRelease) return;
    if(observerCameraRelease) {
        at<int>(0xb869c4)=observerCameraX;at<int>(0xb869c8)=observerCameraY;
    }
    auto renderer=(unsigned char*)at<void*>(0xb86f14);
    if(!renderer) return;
    auto holder=*(unsigned char***)(renderer+0x38);
    if(!holder || !*holder) return;
    int x=at<int>(0xb869c4),y=at<int>(0xb869c8);
    // Mirror native 4d8e8d..4d8eec, after its input/timer processing and before
    // drawing. Global camera changes alone leave terrain/overlay origins stale.
    using Origin=void(__thiscall*)(void*,int,int);
    using Region=void(__thiscall*)(void*,int,int,int,int);
    ((Origin)address(0x67c5c0))(*holder,x,y);
    ((Region)address(0x630930))(address(0xbea648),x,y,-1,-1);
    ((Region)address(0x630930))(address(0xbea698),x,y,-1,-1);
    auto game=(unsigned char*)self;
    *(int*)(game+0x9c4)=x;*(int*)(game+0x9c8)=y;
    observerCameraX=x;observerCameraY=y;observerCameraValid=observerTracked>=0;
    observerCameraRelease=false;
}
bool liveObserverWorker(const unsigned char* unit) {
    int type=*(const int16_t*)(unit+4);
    return (type==0 || type==15) && *(const int16_t*)(unit+6)==0 &&
        *(const int16_t*)(unit+0x4c)==0 && *(const uint16_t*)(unit+0x14)>0;
}
void refreshObserver(void* self) {
    if(at<uint32_t>(0xb8a280)-observerRefreshAt<8 && observerRefreshAt) return;
    observerRefreshAt=at<uint32_t>(0xb8a280);
    auto game=(unsigned char*)self;
    for(unsigned i=0;i<8;++i) {
        auto player=((unsigned char**)(game+0x48))[i];
        auto& row=observerPlayers[i];row={};
        if(!player || !player[0xc] || *(uint32_t*)(player+8)==3000 || *(uint32_t*)(player+8)==0xffffffff) continue;
        row.participant=true;
        row.race=*(int*)(player+8);row.color=*(int*)(player+0x5c0);
        row.steel=*(int*)(player+0xa08);row.gold=*(int*)(player+0xa0c);row.mana=*(int*)(player+0xa10);
        row.population=*(int*)(player+0xe24);
        using Capacity=int(__thiscall*)(void*);
        row.capacity=((Capacity)address(0x522760))(player);
    }
    auto manager=(unsigned char*)at<void*>(0xb870c0);
    if(!manager) return;
    uint32_t count=*(uint32_t*)(manager+0xdc38),index=*(uint32_t*)(manager+0xdc3c);
    if(count>11264) return;
    for(uint32_t i=0;i<count && index<11264;++i) {
        auto unit=observerUnit((int)index);
        if(unit) {
            int owner=*(int16_t*)(unit+8);
            if(owner>=0 && owner<8 && observerPlayers[owner].participant && liveObserverWorker(unit)) ++observerPlayers[owner].farmers;
        }
        index=*(uint32_t*)(manager+0xdc4c+index*12);
    }
}
void observerText(void* self,int x,int y,const char* text) {
    using Text=void*(__thiscall*)(void*,int,int,const char*,int,int,int);
    void* font=*(void**)((unsigned char*)self+0x678);
    if(font) ((Text)address(0x549cf0))(font,x,y,text,(int)strlen(text),0,-1);
}
unsigned observerDisplayName(const char* name,char* output) {
    unsigned end=0;
    while(name[end] && end<16) {
        unsigned width=IsDBCSLeadByteEx(949,(BYTE)name[end])?2:1;
        if(end+width>16) break;
        end+=width;
    }
    memcpy(output,name,end);output[end]=0;
    if(name[end]) {output[end++]='.';output[end++]='.';output[end]=0;}
    return end;
}
char observerRaceInitial(int race) {return race==1000?'H':race==2000?'D':'?';}
uint16_t observerPlayerColor(int color) {
    if(color<0 || color>9) return at<int>(0xbf281c)==0x8000?0x7fff:0xffff;
    // Same 555/565 player palette used by minimap 4b3216/4b3228.
    return at<uint16_t>((at<int>(0xbf281c)==0x8000?0xbea770:0xbea784)+color*2);
}
void observerColoredName(const char* name,int color,char (&output)[64]) {
    char visible[20];observerDisplayName(name,visible);
    // Private queued-ID marker, consumed by observerTextDrawHook before parsing.
    // Native \cHHHH is a FONT PALETTE INDEX, never a raw RGB value.
    sprintf_s(output,"\\c%04X",observerPlayerColor(color));unsigned end=6;
    for(unsigned i=0;visible[i];) {
        unsigned width=IsDBCSLeadByteEx(949,(BYTE)visible[i])?2:1;
        if(width==1 && visible[i]=='\\') output[end++]='\\';
        for(unsigned j=0;j<width && visible[i];++j) output[end++]=visible[i++];
    }
    output[end]=0;
}
// 54a665 indexes a 64-byte font palette with the parsed color value.
// Queueing a color is insufficient: native font+40 can override that palette.
// Scope the override to the actual draw of our exact queued participant ID.
bool observerExtraColor(const char* text,uint16_t& color);
bool observerIdText(const char* text,uint16_t& color) {
    if(!text || text[0]!='\\' || text[1]!='c' || !playback || !active || inDispatch || GetCurrentThreadId()!=producer) return false;
    if(observerExtraColor(text,color)) return true;
    for(unsigned i=0;i<8;++i) if(observerPlayers[i].participant) {
        char fallback[16],expected[64];
        observerColoredName(observerName(i,fallback),observerPlayers[i].color,expected);
        if(!strcmp(text,expected)) {color=observerPlayerColor(observerPlayers[i].color);return true;}
    }
    return false;
}
uint16_t observerTint(uint16_t shade,uint16_t color,bool rgb555) {
    unsigned shift=rgb555?10:11,greenMask=rgb555?31:63;
    unsigned r=(shade>>shift)&31,g=(shade>>5)&greenMask,b=shade&31;
    unsigned brightness=std::max(std::max(r*255/31,g*255/greenMask),b*255/31);
    return (uint16_t)(((((color>>shift)&31)*brightness/255)<<shift) |
                     ((((color>>5)&greenMask)*brightness/255)<<5) |
                     ((color&31)*brightness/255));
}
bool tintObserverGlyph(const uint16_t* source,int height,uint16_t color,bool rgb555,std::vector<uint16_t>& tinted) {
    if(!source || height<=0 || height>128) return false;
    tinted.clear();
    for(int y=0;y<height;++y) {
        unsigned length=source[0];
        if(length<2 || length>2048 || tinted.size()+length>32768) return false;
        size_t start=tinted.size();tinted.insert(tinted.end(),source,source+length);
        unsigned pos=1;
        while(pos<length && source[pos]!=0xffff) {
            uint16_t command=source[pos++];
            unsigned count=(command>>8)&0x7f;
            // Native 638750: transparent and repeated runs contain one value;
            // literal runs contain count 16-bit pixels. Alpha/header stay intact.
            unsigned values=(!(command&0xff) || (command&0x8000))?1:count;
            if(pos+values>length) return false;
            if(command&0xff) for(unsigned n=0;n<values;++n)
                tinted[start+pos+n]=observerTint(source[pos+n],color,rgb555);
            pos+=values;
        }
        if(pos>=length || source[pos]!=0xffff) return false;
        source+=length;
    }
    return true;
}
uint32_t __cdecl observerGlyphDraw(const uint16_t* source,int width,int height,int skipX,int skipY,void* pixels,int pitch) {
    if(observerIdTintDepth && GetCurrentThreadId()==producer) {
        std::vector<uint16_t> tinted;
        if(tintObserverGlyph(source,height,observerIdTintColor,at<int>(0xbf281c)==0x8000,tinted)) {
            observerIdGlyphs.fetch_add(1,std::memory_order_relaxed);
            return glyphDraw(tinted.data(),width,height,skipX,skipY,pixels,pitch);
        }
        observerIdGlyphFailures.fetch_add(1,std::memory_order_relaxed);
    }
    return glyphDraw(source,width,height,skipX,skipY,pixels,pitch);
}
uint32_t __fastcall observerTextDrawHook(void* self,void*,const char* text,void* pixels,int x,int pitch,int height,int translate) {
    uint16_t color;
    if(!observerIdText(text,color)) return textDraw(self,text,pixels,x,pitch,height,translate);
    auto font=(unsigned char*)self;
    int face=*(int*)(font+0x84);
    if(face<0 || face>=10) return textDraw(self,text+6,pixels,x,pitch,height,translate);
    bool indexed=at<int>(0xb40d64+face*4)==1;
    auto assets=*(unsigned char**)font;
    auto palette=indexed?*(uint16_t**)(assets+0xf0+face*4):nullptr;
    uint32_t savedOverride=*(uint32_t*)(font+0x40);
    uint16_t savedPalette[32];memcpy(savedPalette,font+0x44,sizeof(savedPalette));
    bool rgb555=at<int>(0xbf281c)==0x8000;
    uint32_t result=0;
    unsigned savedDepth=observerIdTintDepth;uint16_t savedColor=observerIdTintColor;
    __try {
        observerIdTintDepth=savedDepth+1;observerIdTintColor=color;
        observerIdPaints.fetch_add(1,std::memory_order_relaxed);
        // Use the current override's shading if active, otherwise native palette 0.
        auto source=savedOverride?savedPalette:palette;
        if(indexed) {
            for(unsigned i=0;i<32;++i) ((uint16_t*)(font+0x44))[i]=observerTint(source[i],color,rgb555);
        } else {
            // GDI glyph path 54a8d2 reads this exact foreground slot even
            // for face 4 (whose bitmap fallback uses direct RGB pixels).
            ((uint16_t*)(font+0x44))[1]=color;
        }
        *(uint32_t*)(font+0x40)=1;
        result=textDraw(self,text+6,pixels,x,pitch,height,translate);
    } __finally {
        memcpy(font+0x44,savedPalette,sizeof(savedPalette));*(uint32_t*)(font+0x40)=savedOverride;
        observerIdTintDepth=savedDepth;observerIdTintColor=savedColor;
    }
    return result;
}
#include "observer_overlays.h"

int observerIdColumnWidth() {
    unsigned longest=2;
    for(unsigned i=0;i<8;++i) if(observerPlayers[i].participant) {
        char fallback[16],display[20];
        longest=std::max(longest,observerDisplayName(observerName(i,fallback),display));
    }
    // The game bitmap font uses approximately eight pixels per CP949 byte.
    return (int)longest*8+12;
}
uint32_t __fastcall interfaceHook(void* self,void*) {
    uint32_t result=interfaceRender(self);
    if(!active || !playback || playbackResultsStarted) return result;
    refreshObserver(self);
    drawObserverExtras(self);
    char line[256]{};
    if(showObserverHud && showReplayTime) {
        // The renderer's native dimensions are used; no recorded-resolution coordinates.
        uint32_t tick=at<uint32_t>(0xb8a280),interval=at<uint32_t>(0xb3bae8);
        uint32_t elapsed=(uint32_t)((uint64_t)(tick-savedInitial.state.tick)*interval/1000);
        uint32_t duration=(uint32_t)((uint64_t)(savedEnd.tick-savedInitial.state.tick)*interval/1000);
        sprintf_s(line,"REPLAY %ux  %u:%02u/%u:%02u",replaySpeed,elapsed/60,elapsed%60,duration/60,duration%60);
        observerText(self,std::max(12,at<int>(0xbf2588)-300),8,line);
    }
    if(showResources) {
        const int idWidth=observerIdColumnWidth();
        const int columns[]={0,idWidth,idWidth+28,idWidth+83,idWidth+133,idWidth+178,idWidth+243};
        const char* labels[]={"ID","R","Gold","Steel","Mana","Pop","Workers"};
        int x=std::max(12,at<int>(0xbf2588)-(idWidth+323));
        for(unsigned c=0;c<7;++c) observerText(self,x+columns[c],28,labels[c]);
        int y=46;
        for(unsigned i=0;i<8;++i) if(observerPlayers[i].participant) {
            auto& row=observerPlayers[i];
            char fallback[16];const char* name=observerName(i,fallback);
            char colored[64];observerColoredName(name,row.color,colored);
            observerText(self,x+columns[0],y,colored);
            char race[]={observerRaceInitial(row.race),0};observerText(self,x+columns[1],y,race);
            int values[]={row.gold,row.steel,row.mana,row.population,row.farmers};
            for(unsigned c=2;c<7;++c) {
                if(c==5) sprintf_s(line,"%d/%d",row.population,row.capacity);
                else sprintf_s(line,"%d",values[c-2]);
                observerText(self,x+columns[c],y,line);
            }
            y+=17;
        }
    }
    return result;
}
bool selectObserverUnit(int x,int y) {
    auto game=(unsigned char*)at<void*>(0xb8282c);
    auto manager=(unsigned char*)at<void*>(0xb870c0);
    if(!game || !manager) return false;
    RECT client{};GetClientRect(gameWindow,&client);
    if(client.right<=0 || client.bottom<=0) return false;
    x=x*at<int>(0xbf2578)/client.right;y=y*at<int>(0xbf257c)/client.bottom;
    int width=showObserverHud?at<int>(0xbf2588):at<int>(0xbf2578)-at<int>(0xbf2580);
    if(x<at<int>(0xbf2580) || y<at<int>(0xbf2584) || x>=at<int>(0xbf2580)+width || y>=at<int>(0xbf2584)+at<int>(0xbf258c)) return false;
    x+=*(int*)(game+0x9c4)-at<int>(0xbf2580);y+=*(int*)(game+0x9c8)-at<int>(0xbf2584);
    uint32_t count=*(uint32_t*)(manager+0xdc38),index=*(uint32_t*)(manager+0xdc3c);
    if(count>11264) return true;
    observerSelected=-1;int distance=0x7fffffff;
    for(uint32_t i=0;i<count && index<11264;++i) {
        auto unit=observerUnit((int)index);
        if(observerInspectable(unit)) {
            int dx=*(int*)(unit+0x18)-x,dy=*(int*)(unit+0x1c)-y;
            int radius=*(int16_t*)(unit+6)?80:40;
            if(abs(dx)<=radius && abs(dy)<=radius) {
                int score=dx*dx+dy*dy;
                if(score<distance) {distance=score;observerSelected=(int)index;}
            }
        }
        index=*(uint32_t*)(manager+0xdc4c+index*12);
    }
    return true;
}

struct Patch {unsigned char* p;std::vector<unsigned char> before,after;};
std::vector<Patch> patches;
SRWLOCK codeLock=SRWLOCK_INIT;

void log(const char* text) {
    if(logFile==INVALID_HANDLE_VALUE) return;
    DWORD n=0;WriteFile(logFile,text,(DWORD)strlen(text),&n,nullptr);
    WriteFile(logFile,"\r\n",2,&n,nullptr);FlushFileBuffers(logFile);
}

void invalidate(int reason) {
    failed.store(true,std::memory_order_release);
    notice.store(reason,std::memory_order_release);
    if(playback) terminal.store(true,std::memory_order_release);
}

void copyRing(uint32_t offset,const void* data,uint32_t size) {
    uint32_t p=offset%RingSize,first=std::min(size,RingSize-p);
    memcpy(ring+p,data,first);
    if(first<size) memcpy(ring,(const unsigned char*)data+first,size-first);
}
void readRing(uint32_t offset,void* data,uint32_t size) {
    uint32_t p=offset%RingSize,first=std::min(size,RingSize-p);
    memcpy(data,ring+p,first);
    if(first<size) memcpy((unsigned char*)data+first,ring,size-first);
}
void emit(uint32_t kind,const void* data,uint32_t size) {
    if(failed.load(std::memory_order_acquire)) return;
    if(GetCurrentThreadId()!=producer || size>4096) {invalidate(1);return;}
    uint32_t h=head.load(std::memory_order_relaxed),t=tail.load(std::memory_order_acquire);
    if(h-t+size+sizeof(Record)>RingSize) {invalidate(2);return;}
    Record r{kind,size};copyRing(h,&r,sizeof(r));copyRing(h+sizeof(r),data,size);
    head.store(h+sizeof(r)+size,std::memory_order_release);
}

bool snapshot(void* self,State& state) {
    // The existing native checksum covers resources, unit positions/type/HP fields and RNG.
    // It is a 32-bit diagnostic checksum, not proof of complete state equivalence.
    __try {
        if(!self || !at<void*>(0xb870c0)) return false;
        using Checksum=uint32_t(__thiscall*)(void*);
        auto checksum=reinterpret_cast<Checksum>(address(0x4dae50));
        state={at<uint32_t>(0xb8a280),at<uint32_t>(0xbed1e8),at<uint32_t>(0xb827dc),checksum(self)};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool same(const State& a,const State& b) {return !memcmp(&a,&b,sizeof(State));}

bool getRecord(uint32_t kind,const unsigned char*& data,uint32_t& size,bool consume=true) {
    if(cursor+sizeof(Record)>input.size()) return false;
    Record r{};memcpy(&r,input.data()+cursor,sizeof(r));
    if(r.kind!=kind || r.size>4096 || cursor+sizeof(r)+r.size>input.size()) return false;
    data=input.data()+cursor+sizeof(r);size=r.size;
    if(consume) cursor+=sizeof(r)+r.size;
    return true;
}
bool nextKind(uint32_t kind) {
    const unsigned char* p;uint32_t n;
    return getRecord(kind,p,n,false);
}

void finish(void* /*self*/) {
    if(!active) return;
    // Cleanup may already have freed resources/unit lists. Never dereference them here.
    // Cut off the final batch and end at its validated Before boundary, which is
    // exactly the state playback observes before dispatching that batch.
    if(!playback) {
        if(frames<2) invalidate(3);
        else emit(TrimEnd,&lastBefore,sizeof(lastBefore));
    }
    active=false;frames=0;hasCapturedBootstrap=false;bootstrapEmitted=false;
}

void fillInitial(void* self,Initial& value) {
    memset(&value,0,sizeof(value));
    snapshot(self,value.state);
    value.networkMode=at<unsigned char>(0xb83ad0);
    const char* directory=(const char*)address(0xb83ad9);
    const char* map=(const char*)address(0xb83bd9);
    memcpy(value.directory,directory,strnlen_s(directory,255));
    memcpy(value.map,map,strnlen_s(map,255));
    for(uint32_t i=0;i<11;++i) memcpy(value.slots[i],address(0xb83cdc+i*0x40c),12);
}

bool validSetup(const MatchSetup& setup) {
    if((setup.version!=1 && setup.version!=2) || setup.scenarioMode!=0 || setup.rtsMode!=1 || setup.gameType>5 || setup.localPlayer>=8) return false;
    for(unsigned i=0;i<8;++i) if(setup.players[i]>1 || setup.resources[i]>3) return false;
    for(unsigned i=0;i<9;++i) if(setup.nativeOptions[i]>2) return false;
    return setup.players[setup.localPlayer]==0;
}

void captureSetup(void* self,MatchSetup& setup) {
    setup={};setup.version=2;setup.scenarioMode=at<uint16_t>(0xb83ad2);
    setup.rtsMode=at<unsigned char>(0xb83ad4);setup.gameType=at<uint32_t>(0xbecd74);
    setup.localPlayer=*(uint32_t*)((unsigned char*)self+0x664);
    memcpy(setup.players,address(0xbecd78),sizeof(setup.players));
    memcpy(setup.resources,address(0xbecd98),sizeof(setup.resources));
    memcpy(setup.nativeOptions,(unsigned char*)self+0xc78,sizeof(setup.nativeOptions));
}

bool validBootstrap(const NetworkBootstrap& value) {
    if(value.version!=1 || value.scenarioMode!=0 || value.rtsMode!=1 || value.participants<1 ||
       value.participants>11 || value.localPlayer>=8 || value.tickInterval<1 || value.tickInterval>1000) return false;
    for(unsigned i=0;i<11;++i) {
        // Slot 10 is native auxiliary state: command 1007 leaves an opaque
        // controller word; map initialization assigns it 2 at 4c7ee9.
        if((i<10 && value.slots[i][0]>3) || value.slots[i][1]<1000 || value.slots[i][1]>4000 ||
           value.slots[i][1]%1000 || value.slots[i][2]>65535 || value.slots[i][3]>1) return false;
    }
    for(unsigned i=0;i<9;++i) if(value.nativeOptions[i]>2) return false;
    return true;
}

uint32_t __fastcall initializeMapHook(void* self,void*,uint32_t a,uint32_t b) {
    if(!playback) {
        hasCapturedBootstrap=false;
        if(at<unsigned char>(0xb83ad0)==1) {
            NetworkBootstrap value{};value.version=1;
            value.scenarioMode=at<uint16_t>(0xb83ad2);value.rtsMode=at<unsigned char>(0xb83ad4);
            value.participants=at<uint16_t>(0xb83ad6);value.localPlayer=*(uint32_t*)((unsigned char*)self+0x664);
            value.seed=at<uint32_t>(0xbed1e8);value.randomCount=at<uint32_t>(0xb827dc);
            value.tickInterval=at<uint32_t>(0xb3bae8);
            for(unsigned i=0;i<11;++i) {
                memcpy(value.slots[i],address(0xb83cdc+i*0x40c),12);
                value.slots[i][3]=at<unsigned char>(0xb83ce8+i*0x40c);
            }
            memcpy(value.nativeOptions,(unsigned char*)self+0xc78,16);
            capturedBootstrap=value;hasCapturedBootstrap=validBootstrap(value);
            // Network command 1007 initializes the map AFTER Begin/Before.
            // Preserve this sample at its actual position in the command journal.
            if(active && inDispatch && !bootstrapEmitted) {
                emit(Bootstrap,&value,sizeof(value));bootstrapEmitted=true;
            }
        }
        return initializeMap(self,a,b);
    }
    if(savedInitial.networkMode!=1) {
        uint32_t result=initializeMap(self,a,b);
        restorePlayerNames(self,(AssignName)address(0x429000));return result;
    }
    if(!hasBootstrap) {invalidate(14);return 0;}
    if(!inDispatch && savedInitial.state.checksum==0 && savedInitial.state.seed==0 && savedInitial.state.randomCount==0) {
        // The recorded session begins with an empty world. Command 1007 will
        // perform the real native map initialization at the recorded tick.
        for(unsigned i=0;i<11;++i) memcpy(address(0xb83cdc+i*0x40c),savedInitial.slots[i],12);
        return 1;
    }
    // Only native map generation sees the original network rules. The outer loader
    // remains offline, so it never waits for peers or sends network messages.
    unsigned char mode=at<unsigned char>(0xb83ad0);
    uint32_t result=0;
    __try { __try {
        at<unsigned char>(0xb83ad0)=1;at<uint16_t>(0xb83ad2)=(uint16_t)savedBootstrap.scenarioMode;
        at<unsigned char>(0xb83ad4)=(unsigned char)savedBootstrap.rtsMode;
        at<uint16_t>(0xb83ad6)=(uint16_t)savedBootstrap.participants;
        at<unsigned char>(0xb83ad8)=0;
        for(unsigned i=0;i<11;++i) {
            memcpy(address(0xb83cdc+i*0x40c),savedBootstrap.slots[i],12);
            at<unsigned char>(0xb83ce8+i*0x40c)=(unsigned char)savedBootstrap.slots[i][3];
        }
        memcpy((unsigned char*)self+0xc78,savedBootstrap.nativeOptions,16);
        *(uint32_t*)((unsigned char*)self+0x664)=savedBootstrap.localPlayer;
        at<uint32_t>(0xbed1e8)=savedBootstrap.seed;at<uint32_t>(0xb827dc)=savedBootstrap.randomCount;
        at<uint32_t>(0xb3bae8)=savedBootstrap.tickInterval;
        result=initializeMap(self,a,b);
    } __finally {at<unsigned char>(0xb83ad0)=mode;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {invalidate(13);return 0;}
    restorePlayerNames(self,(AssignName)address(0x429000));
    return result;
}

void legacyOptions(MatchSetup& setup) {
    // Standard custom-game options used by the native profile setup. Legacy
    // archives are accepted only if the original state/RNG checks also match.
    const unsigned char defaults[16]={1,1,0,1,1,0,0,0,1,0,0,0,0,0,0,0};
    memcpy(setup.nativeOptions,defaults,sizeof(defaults));
}

void restore();
void showPlaybackNotice();
void confirmPlaybackExit(HWND window);

bool beginReplayResults(void* application,ChangeState changeState) {
    if(!playback || !terminal.load() || failed.load() || notice.load()!=10) return false;
    if(playbackResultsStarted) return true;
    // State 16 (5978d4) is the native custom-match result entry; it creates
    // CGameResultMenu via CGame vtable+6c, then advances to result state 17.
    // This runs on the next application frame, after command/queue scopes unwind.
    playbackResultsStarted=true;
    resetObserverExtras();
    stopObserverTracking();
    changeState(application,16);
    log("RS01 verified replay end; opening native game results.");
    return true;
}
uint32_t __fastcall applicationHook(void* self,void*) {
    if(playback && (exitConfirmationOpen || playbackExitRequested)) return 1;
    if(playback && terminal.load()) {
        if(beginReplayResults(self,(ChangeState)address(0x598820))) return applicationProcess(self);
        showPlaybackNotice();return 1;
    }
    // 19 is the initialized main menu, measured in this exact Steam build.
    // Invoke the same viewport/loading and state transitions used by custom-game OK.
    if(playback && !automaticStarted && !failed.load() && *(uint32_t*)((unsigned char*)self+0x128)==19) {
        automaticStarted=true;
        memcpy(address(0xb83ad9),savedInitial.directory,256);
        memcpy(address(0xb83bd9),savedInitial.map,256);
        if(savedInitial.networkMode==1) {
            if(!hasBootstrap) {invalidate(14);return applicationProcess(self);}
            savedSetup={};savedSetup.version=2;savedSetup.rtsMode=1;savedSetup.localPlayer=savedBootstrap.localPlayer;
            memcpy(savedSetup.nativeOptions,savedBootstrap.nativeOptions,16);
            for(unsigned i=0;i<8;++i) {
                savedSetup.players[i]=i<savedBootstrap.participants?0:1;
                uint32_t race=savedBootstrap.slots[i][1];
                savedSetup.resources[i]=race==2000?1:race==4000?2:race==3000?3:0;
            }
        } else if(!hasSetup) {
            // Old archives lack menu settings. Infer the bounded default custom setup;
            // the original complete Initial comparison still refuses any mismatch.
            savedSetup={};savedSetup.version=1;savedSetup.rtsMode=1;
            legacyOptions(savedSetup);
            std::string filename=savedInitial.directory;filename+=savedInitial.map;
            HANDLE f=CreateFileA(filename.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            unsigned char header[108]{};DWORD count=0;
            bool ok=f!=INVALID_HANDLE_VALUE && ReadFile(f,header,sizeof(header),&count,nullptr) && count==sizeof(header);
            if(f!=INVALID_HANDLE_VALUE) CloseHandle(f);
            uint32_t players=0;if(ok) memcpy(&players,header+0x2c,4);
            if(!ok || memcmp(header,"KUF Map File v0.67",17) || players<1 || players>8) {invalidate(13);return applicationProcess(self);}
            for(unsigned i=0;i<8;++i) {
                savedSetup.players[i]=i<players?0:1;
                uint32_t value=savedInitial.slots[i][1];
                savedSetup.resources[i]=value==2000?1:value==4000?2:value==3000?3:0;
                if(savedInitial.slots[i][0]==1) savedSetup.localPlayer=i;
            }
        }
        if(!validSetup(savedSetup)) {invalidate(13);return applicationProcess(self);}
        at<unsigned char>(0xb83ad0)=0;at<uint16_t>(0xb83ad2)=0;at<unsigned char>(0xb83ad4)=1;
        at<unsigned char>(0xb83cd9)=0;at<unsigned char>(0xbececb)=0;at<unsigned char>(0xbecfd4)=0;
        at<uint32_t>(0xbecd74)=savedSetup.gameType;
        memcpy(address(0xbecd78),savedSetup.players,sizeof(savedSetup.players));
        memcpy(address(0xbecd98),savedSetup.resources,sizeof(savedSetup.resources));
        memcpy(address(0xbeda08),savedSetup.nativeOptions,sizeof(savedSetup.nativeOptions));
        at<unsigned char>(0xbed0dc)=0;
        using GameKind=int(__thiscall*)(void*);
        int kind=reinterpret_cast<GameKind>(address(0x58e1e0))(self);
        if(kind<0 || kind>6) {invalidate(13);return applicationProcess(self);}
        using Viewport=void(__thiscall*)(void*,int,bool,bool);
        reinterpret_cast<Viewport>(address(0x598900))(self,kind,true,at<unsigned char>(0xb41727)!=0);
        void* world=at<void*>(0xb82808);
        if(world) *((unsigned char*)world+0x7f)=1;
        using ChangeState=void(__thiscall*)(void*,uint32_t);
        reinterpret_cast<ChangeState>(address(0x598820))(self,3);
        log("Automatic replay map/setup loading started.");
    }
    return applicationProcess(self);
}

bool observerTrackingShortcut(WPARAM key, SHORT controlState, LPARAM flags) {
    return key=='C' && (controlState&0x8000)!=0 && !(flags&(1L<<30));
}

LRESULT CALLBACK recordingWindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(playback && active && wparam=='C' && (message==WM_KEYDOWN || message==WM_KEYUP)) {
        if(message==WM_KEYDOWN && observerTrackingShortcut(wparam,GetKeyState(VK_CONTROL),lparam)) toggleObserverTracking();
        return 0;
    }
    if(playback && (wparam==3 || wparam=='c' || wparam=='C') && (message==WM_CHAR || message==WM_SYSCHAR)) return 0;
    if(playback && ((wparam=='O' && (message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP)) ||
       ((wparam=='o' || wparam=='O') && (message==WM_CHAR || message==WM_SYSCHAR)))) return 0;
    if(playback && active && !terminal.load() && (message==WM_KEYDOWN || message==WM_KEYUP) && wparam==VK_ESCAPE) {
        if(message==WM_KEYDOWN && !(lparam&(1L<<30)) && !exitConfirmationOpen && !playbackExitRequested) confirmPlaybackExit(window);
        return 0;
    }
    if(playback && active && wparam=='T' && (message==WM_KEYDOWN || message==WM_KEYUP)) {
        if(message==WM_KEYDOWN && !(lparam&(1L<<30))) showReplayTime=!showReplayTime;
        return 0;
    }
    if(playback && active && (wparam=='t' || wparam=='T' || wparam=='h' || wparam=='H') && (message==WM_CHAR || message==WM_SYSCHAR)) return 0;
    if(playback && active && message==WM_KEYUP && (wparam=='I' || wparam=='U' || wparam=='H' || wparam=='1' || wparam=='2' || wparam=='4')) return 0;
    if(playback && active && message==WM_KEYDOWN && !(lparam&(1L<<30))) {
        if(wparam=='H') {showHeroPanel=!showHeroPanel;return 0;}
        if(wparam=='I') {showResources=!showResources;return 0;}
        if(wparam=='U') {toggleObserverHud();return 0;}
        if(wparam=='1' || wparam=='2' || wparam=='4') {
            playbackClock();replaySpeed=(unsigned)(wparam-'0');return 0;
        }
    }
    if(playback && active && !terminal.load() && message==WM_LBUTTONDOWN) {
        if(clickObserverHeroClient((int16_t)LOWORD(lparam),(int16_t)HIWORD(lparam))) return 0;
        if(selectObserverUnit((int16_t)LOWORD(lparam),(int16_t)HIWORD(lparam))) return 0;
    }
    if(message==RecordPulse) {
        // Custom-game quit returns the map directory to the menu directory while
        // retaining the game object. A pause alone leaves the directory unchanged.
        if(!playback && active && !inDispatch && GetCurrentThreadId()==producer &&
           GetTickCount64()-lastAdvanceAt.load()>1000 &&
           strncmp(initial.directory,(const char*)address(0xb83ad9),255)) finish(nullptr);
        return 0;
    }
    if(message==WM_CLOSE && closePending.load() && !closeReady.load()) return 0;
    if(message==WM_CLOSE && !playback && !closeReady.load() && active && !failed.load()) {
        if(!closePending.exchange(true)) finish(at<void*>(0xb8282c));
        // The writer completes asynchronously; no disk waits on the simulation thread.
        return 0;
    }
    LRESULT result=CallWindowProcW(oldWindowProc,window,message,wparam,lparam);
    if(message==WM_NCDESTROY) windowDestroyed.store(true,std::memory_order_release);
    return result;
}

BOOL CALLBACK findGameWindow(HWND window,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t title[128]{};GetWindowTextW(window,title,128);
    if(pid==GetCurrentProcessId() && !wcscmp(title,L"Kingdom Under Fire") && !GetWindow(window,GW_OWNER)) {
        gameWindow=window;return FALSE;
    }
    return TRUE;
}

bool bootstrapCounterOnly(const State& actual,const CommandHeader& expected,const State& after) {
    return actual.tick==expected.tick && actual.seed==expected.seed && after.tick==actual.tick && after.seed==actual.seed &&
        after.randomCount==expected.randomCount && actual.randomCount!=expected.randomCount &&
        actual.checksum-actual.randomCount==after.checksum-after.randomCount;
}

uint32_t __fastcall commandHook(void* self,void*,uint32_t length,const void* bytes) {
    if(playback && inDispatch) {
        if(failed.load() || replayApply>=replayBatch.size()) {invalidate(8);return 0;}
        const auto& command=replayBatch[replayApply++];
        const auto& h=command.header;
        if(length!=h.size || memcmp(bytes,command.bytes,length) ||
           h.tick!=at<uint32_t>(0xb8a280) || h.seed!=at<uint32_t>(0xbed1e8) ||
           h.randomCount!=at<uint32_t>(0xb827dc)) {
            char line[200];sprintf_s(line,"Command mismatch opcode=%u tick=%u/%u seed=%u/%u RNG=%u/%u",*(const uint16_t*)bytes,h.tick,at<uint32_t>(0xb8a280),h.seed,at<uint32_t>(0xbed1e8),h.randomCount,at<uint32_t>(0xb827dc));log(line);
            State diagnostic{};if(snapshot(self,diagnostic)){char details[100];sprintf_s(details,"Command mismatch actual checksum=%u",diagnostic.checksum);log(details);}
            invalidate(8);return 0;
        }
    }
    if(active && inDispatch && !playback && !failed.load()) {
        if(length<2 || length>MaxCommand) invalidate(4);
        else {
            unsigned char buffer[sizeof(CommandHeader)+MaxCommand];
            CommandHeader h{at<uint32_t>(0xb8a280),at<uint32_t>(0xbed1e8),at<uint32_t>(0xb827dc),length};
            memcpy(buffer,&h,sizeof(h));memcpy(buffer+sizeof(h),bytes,length);
            emit(Command,buffer,sizeof(h)+length);
            const uint32_t ordinal=populationOrdinal++;
            const uint16_t opcode=*(const uint16_t*)bytes;
            if(populationSamples<1500 && h.tick>=initial.state.tick &&
               (uint64_t)(h.tick-initial.state.tick)*(hasCapturedBootstrap?capturedBootstrap.tickInterval:30)<=300000 &&
               ((length==8 && (opcode==400 || opcode==401 || opcode==500)) ||
                (length==12 && (opcode==170 || opcode==5170)))) {
                const uint16_t owner=((const uint16_t*)bytes)[1];
                if(owner<8) {
                    auto player=((unsigned char**)((unsigned char*)self+48))[owner];
                    if(player) {
                        int used=*(int*)(player+0xe24);
                        int capacity=((int(__thiscall*)(void*))address(0x522760))(player);
                        if(used>=0 && used<=1000000 && capacity>=0 && capacity<=1000000) {
                            PopulationSample sample{ordinal,h.tick,owner,(uint32_t)used,(uint32_t)capacity};
                            emit(Population,&sample,sizeof(sample));++populationSamples;
                        }
                    }
                }
            }
        }
    }
    uint32_t result=apply(self,length,bytes);
    if(playback && inDispatch && hasBootstrap && frames<=1 && savedInitial.state.randomCount==0 && savedInitial.state.checksum==0 && length==96 && *(const uint16_t*)bytes==1007 && replayApply<replayBatch.size()) {
        // Native RNG draws use seed, not the diagnostic draw counter. Map setup
        // resets seed per player. The reported observer trace differs only in
        // this counter at startup. Normalize only this bootstrap boundary, with equal
        // generator state and equal native gameplay checksum excluding counter.
        const auto& expected=replayBatch[replayApply].header;
        const unsigned char* afterBytes=nullptr;uint32_t afterSize=0;State actual{};
        bool transportOnly=true;
        for(size_t i=replayApply;i<replayBatch.size();++i) {
            uint16_t op=*(const uint16_t*)replayBatch[i].bytes;
            if(op!=98 && op!=1004 && op!=1005 && op!=1009) transportOnly=false;
        }
        if(transportOnly && getRecord(After,afterBytes,afterSize,false) && afterSize==sizeof(State) && snapshot(self,actual)) {
            const State& after=*(const State*)afterBytes;
            if(bootstrapCounterOnly(actual,expected,after)) {
                char line[160];sprintf_s(line,"Bootstrap diagnostic RNG counter normalized %u -> %u; generator seed and gameplay checksum unchanged",actual.randomCount,expected.randomCount);log(line);
                at<uint32_t>(0xb827dc)=expected.randomCount;
            }
        }
    }
    return result;
}

uint32_t __fastcall replayQueueCount(void*,void*) {return static_cast<uint32_t>(replayBatch.size()-replayPop);}
uint32_t __fastcall replayQueuePop(void*,void*,void* bytes,uint32_t* metadata) {
    if(replayPop>=replayBatch.size()) {invalidate(8);return 0;}
    const auto& command=replayBatch[replayPop++];
    memcpy(bytes,command.bytes,command.header.size);*metadata=0;return command.header.size;
}
uint32_t __fastcall offlinePeerStatus(void*,void*,uint32_t) {return 0;}

uint32_t dispatchReplay(void* self,uint32_t a,uint32_t b) {
    void* queue=at<void*>(0xb870c8);
    void*** object=reinterpret_cast<void***>(queue);
    void** original=*object;void* replacement[13]{};
    memcpy(replacement,original,12*sizeof(void*));
    replacement[2]=(void*)replayQueuePop;replacement[4]=(void*)replayQueueCount;
    // Native +0x30 checks live peer checksum reports. Playback instead validates
    // every Before/After state against the authenticated recording.
    replacement[12]=(void*)offlinePeerStatus;
    unsigned char mode=at<unsigned char>(0xb83ad0);
    replayPop=0;replayApply=0;inDispatch=true;
    uint32_t result=1;
    // Preserve native command batching, bookkeeping and AI updates. Restore even on SEH.
    __try {*object=replacement;if(savedInitial.networkMode==1) at<unsigned char>(0xb83ad0)=1;result=advance(self,a,b);}
    __finally {*object=original;at<unsigned char>(0xb83ad0)=mode;inDispatch=false;}
    if(replayPop!=replayBatch.size() || replayApply!=replayBatch.size()) invalidate(8);
    return result;
}

bool discardLiveQueue() {
    __try {
        void* queue=at<void*>(0xb870c8);
        if(!queue) return false;
        void** table=*reinterpret_cast<void***>(queue);
        using Count=uint32_t(__thiscall*)(void*);
        using Pop=uint32_t(__thiscall*)(void*,void*,uint32_t*);
        auto count=reinterpret_cast<Count>(table[4]);
        auto pop=reinterpret_cast<Pop>(table[2]);
        unsigned char buffer[MaxCommand+16];uint32_t metadata=0;
        for(uint32_t limit=0;limit<4096;++limit) {
            if(!count(queue)) return true;
            if(pop(queue,buffer,&metadata)>MaxCommand) return false;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

uint32_t __fastcall advanceHook(void* self,void*,uint32_t a,uint32_t b) {
    lastAdvanceAt.store(GetTickCount64());
    if(closePending.load()) return advance(self,a,b);
    if(failed.load()) return playback?1:advance(self,a,b);
    // Both native branches dispatch here after queue synchronization. The network
    // branch sorts commands by participant before calling our command hook.
    // Observe that order without replacing the network queue or changing its mode.
    unsigned char networkMode=at<unsigned char>(0xb83ad0);
    if(!playback && active && initial.networkMode!=networkMode) finish(self);
    if(networkMode>1 || (playback && networkMode!=0)) {
        if(active) {invalidate(5);active=false;}
        return advance(self,a,b);
    }
    if(!producer) producer=GetCurrentThreadId();
    if(playback && savedInitial.networkMode==1) {
        const unsigned char* upcoming=nullptr;uint32_t size=0;
        uint32_t expected=savedInitial.state.tick;
        if(active && (getRecord(Before,upcoming,size,false) || getRecord(End,upcoming,size,false)) && size==sizeof(State)) expected=((const State*)upcoming)->tick;
        if(at<uint32_t>(0xb8a280)<expected) return 1;
    }
    State state{};
    if(!snapshot(self,state)) {invalidate(3);return advance(self,a,b);}
    if(!active) {
        fillInitial(self,initial);
        if(playback) {
            const unsigned char* p;uint32_t n;
            if(!getRecord(Begin,p,n) || n!=sizeof(Initial)) {invalidate(6);return 1;}
            const Initial* saved=reinterpret_cast<const Initial*>(p);
            // Automatic bootstrap must produce the original game-native initial state.
            // It never overwrites arbitrary state or RNG to conceal a mismatch.
            Initial comparable=initial;
            if(saved->networkMode==1 && hasBootstrap) comparable.networkMode=1;
            if(memcmp(saved,&comparable,sizeof(Initial))) {
                for(unsigned offset=0;offset<sizeof(Initial);offset+=4) {
                    uint32_t expected,actual;memcpy(&expected,(const char*)saved+offset,4);memcpy(&actual,(const char*)&comparable+offset,4);
                    if(expected!=actual) {char line[160];sprintf_s(line,"Initial mismatch offset=%u expected=%u actual=%u",offset,expected,actual);log(line);}
                }
                invalidate(7);return 1;
            }
            // Reapply at match start, after native menu/map initialization;
            // do not merely consume and discard this display metadata.
            if(nextKind(Names) && (!getRecord(Names,p,n) || !loadRecordedNames(p,n))) {invalidate(6);return 1;}
            if(nextKind(Setup) && (!getRecord(Setup,p,n) || (n!=84 && n!=sizeof(MatchSetup)))) {invalidate(6);return 1;}
            if(nextKind(Bootstrap) && (!getRecord(Bootstrap,p,n) || n!=sizeof(NetworkBootstrap))) {invalidate(6);return 1;}
        } else {
            MatchSetup setup{};captureSetup(self,setup);
            bootstrapEmitted=false;populationOrdinal=0;populationSamples=0;
            emit(Begin,&initial,sizeof(initial));
            PlayerNames names{};captureNames(self,names);emit(Names,&names,sizeof(names));
            // Custom-menu globals do not describe the multiplayer lobby.
            if(networkMode==0 && validSetup(setup)) emit(Setup,&setup,sizeof(setup));
            if(networkMode==1 && hasCapturedBootstrap) {emit(Bootstrap,&capturedBootstrap,sizeof(capturedBootstrap));bootstrapEmitted=true;}
        }
        if(playback) restorePlayerNames(self,(AssignName)address(0x429000));
        active=true;++session;frames=0;
    }
    if(!playback) {
        lastBefore=state;
        emit(Before,&state,sizeof(state));inDispatch=true;
        uint32_t result=1;
        __try {result=advance(self,a,b);}
        __finally {inDispatch=false;}
        if(!snapshot(self,state)) invalidate(3);
        else emit(After,&state,sizeof(state));
        ++frames;return result;
    }
    const unsigned char* p;uint32_t n;
    if(nextKind(End)) {
        if(!getRecord(End,p,n) || n!=sizeof(State) || !same(state,*reinterpret_cast<const State*>(p)) || cursor!=input.size()) invalidate(8);
        else {terminal.store(true);notice.store(10);}
        return 1;
    }
    if(!getRecord(Before,p,n) || n!=sizeof(State)) {invalidate(8);return 1;}
    if(!same(state,*reinterpret_cast<const State*>(p))) {
        auto expected=*(const State*)p;char line[240];sprintf_s(line,"Before mismatch tick=%u/%u seed=%u/%u RNG=%u/%u checksum=%u/%u",expected.tick,state.tick,expected.seed,state.seed,expected.randomCount,state.randomCount,expected.checksum,state.checksum);log(line);invalidate(8);return 1;
    }
    if(!discardLiveQueue()) {invalidate(9);return 1;}
    replayBatch.clear();
    while(nextKind(Command) || nextKind(Bootstrap)) {
        if(nextKind(Bootstrap)) {
            if(!getRecord(Bootstrap,p,n) || n!=sizeof(NetworkBootstrap)) {invalidate(6);return 1;}
            continue;
        }
        if(!getRecord(Command,p,n) || n<sizeof(CommandHeader)) {invalidate(6);return 1;}
        const CommandHeader* h=reinterpret_cast<const CommandHeader*>(p);
        if(h->size<2 || h->size>MaxCommand || n!=sizeof(*h)+h->size || replayBatch.size()>=4096) {invalidate(6);return 1;}
        replayBatch.push_back({*h,p+sizeof(*h)});
    }
    uint32_t result=dispatchReplay(self,a,b);
    if(failed.load()) return 1;
    if(!snapshot(self,state) || !getRecord(After,p,n) || n!=sizeof(State)) {invalidate(8);return 1;}
    if(!same(state,*reinterpret_cast<const State*>(p))) {
        auto expected=*(const State*)p;char line[240];sprintf_s(line,"After mismatch tick=%u/%u seed=%u/%u RNG=%u/%u checksum=%u/%u",expected.tick,state.tick,expected.seed,state.seed,expected.randomCount,state.randomCount,expected.checksum,state.checksum);log(line);invalidate(8);return 1;
    }
    ++frames;return result;
}

uint32_t __fastcall destroyHook(void* self,void*,uint32_t flags) {
    if(GetCurrentThreadId()==producer) finish(self);
    return destroy(self,flags);
}

uint32_t __fastcall resultButtonHook(void* self,void*,uint32_t button) {
    if(playback && playbackResultsStarted && !failed.load() && button==704) {
        // The native button returns to a multiplayer lobby (597a48), absent in
        // offline replay. Exit the process message loop instead, after this event.
        playbackExitRequested=true;stopObserverTracking();
        log("RS02 result confirm: exiting replay process without returning to network lobby.");
        restore();PostQuitMessage(0);return 1;
    }
    return resultButton(self,button);
}
uint32_t __fastcall resultRenderHook(void* self,void*) {
    // CGameResultMenu's third virtual method draws the completed-match screen.
    // Unlike map-directory changes this also covers maps retained on results.
    if(!playback && active && !inDispatch && GetCurrentThreadId()==producer) finish(nullptr);
    return resultRender(self);
}

void __cdecl resetHook() {
    if(active) finish(at<void*>(0xb8282c));
    using Reset=void(__cdecl*)();reinterpret_cast<Reset>(address(0x4d6d10))();
}

void restore() {
    stopObserverTracking();
    AcquireSRWLockExclusive(&codeLock);
    for(auto i=patches.rbegin();i!=patches.rend();++i) {
        DWORD old;
        if(!memcmp(i->p,i->after.data(),i->after.size()) && VirtualProtect(i->p,i->before.size(),PAGE_EXECUTE_READWRITE,&old)) {
            memcpy(i->p,i->before.data(),i->before.size());FlushInstructionCache(GetCurrentProcess(),i->p,i->before.size());
            const size_t offset=(uintptr_t)i->p-textBase;
            if(offset+i->before.size()<=expectedText.size()) memcpy(expectedText.data()+offset,i->before.data(),i->before.size());
            DWORD ignored;VirtualProtect(i->p,i->before.size(),old,&ignored);
        }
    }
    ReleaseSRWLockExclusive(&codeLock);
}

struct DialogCursor {
    RECT clip{};
    bool clipped=false;
    int increments=0;
    HCURSOR previous=nullptr;
    DialogCursor() {
        clipped=GetClipCursor(&clip)!=FALSE;
        ClipCursor(nullptr);
        // KUF draws its own cursor; its render loop stops during a modal dialog.
        do { ++increments; } while(ShowCursor(TRUE)<0);
        previous=SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    }
    ~DialogCursor() {
        SetCursor(previous);
        while(increments>0) {ShowCursor(FALSE);--increments;}
        if(clipped) ClipCursor(&clip);
    }
};

void confirmPlaybackExit(HWND window) {
    // Freeze the virtual simulation clock as well as reentrant modal processing.
    playbackClock();exitConfirmationOpen=true;
    int answer;
    { DialogCursor cursor;answer=MessageBoxW(window,L"리플레이를 종료하시겠습니까?",L"KUF Replay",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2); }
    using Timer=uint32_t(__stdcall*)();
    clockWall=((Timer)at<void*>(0xa16cfc))();
    exitConfirmationOpen=false;
    if(answer==IDYES) {playbackExitRequested=true;restore();PostQuitMessage(0);}
}

void showPlaybackNotice() {
        static bool shown=false;
        if(!shown) {
            shown=true;
            const wchar_t* prompt=notice.load()==14?L"멀티플레이 초기화 데이터가 없는 이전 기록입니다. 새 런처로 경기를 다시 기록해 주세요. 종료하시겠습니까?":
                notice.load()==13?L"이 기록의 지도 또는 경기 설정을 자동으로 복원할 수 없습니다. 최신 런처로 새 경기를 기록해 주세요. 종료하시겠습니까?":
                failed.load()?L"기록 상태와 다른 상태를 발견해 재생을 중단했습니다. 종료하시겠습니까?":L"재생이 끝났습니다. 종료하시겠습니까?";
            int answer;
            { DialogCursor dialogCursor; answer=MessageBoxW(GetActiveWindow(),prompt,L"KUF Replay",MB_YESNO|MB_ICONQUESTION); }
            if(answer==IDYES) {
                restore();PostQuitMessage(0);
            }
        }
}

uint32_t __fastcall replayQueueTick(void*,void*,uint32_t) {return at<uint32_t>(0xb8a280);}

uint32_t __fastcall processHook(void* self,void*) {
    if(playback && (exitConfirmationOpen || playbackExitRequested)) return 1;
    if(playback && terminal.load()) {
        // A successful End waits for applicationHook to open the result UI.
        // Do not show the old exit modal if CGame ticks first in this frame.
        if(failed.load() || notice.load()!=10) showPlaybackNotice();
        return 1;
    }
    observerAudioTick.store(at<uint32_t>(0xb8a280),std::memory_order_relaxed);
    updateObserverTracking(self);
    if(playback && savedInitial.networkMode==1 && !restoredEmptyNetworkInitial &&
       savedInitial.state.seed==0 && savedInitial.state.randomCount==0 && savedInitial.state.checksum==0) {
        void* units=at<void*>(0xb870c0);
        State empty{};
        if(!units || *(uint32_t*)((char*)units+0xdc38)!=0 || !snapshot(self,empty) ||
           empty.randomCount!=0 || empty.checksum!=empty.seed) {invalidate(13);return 1;}
        // Restore the recorded EMPTY pre-map checkpoint before any simulation.
        // The offline custom loader writes default seed 999 and AI slot settings.
        for(unsigned i=0;i<11;++i) memcpy(address(0xb83cdc+i*0x40c),savedInitial.slots[i],12);
        at<uint32_t>(0xbed1e8)=0;at<uint32_t>(0xb827dc)=0;
        at<uint32_t>(0xb8a280)=0;
        at<uint32_t>(0xb3bae8)=savedBootstrap.tickInterval;
        restoredEmptyNetworkInitial=true;
        log("Restored verified empty pre-map network checkpoint before simulation.");
    }
    if(playback && savedInitial.networkMode==1) {
        // Offline queue batches every three ticks; network recordings batch at
        // other boundaries. Poll every simulation tick and let advanceHook wait
        // for the exact recorded boundary, preserving intervening world updates.
        void* queue=at<void*>(0xb870c8);
        if(!queue) {invalidate(9);return 1;}
        auto object=(void***)queue;void** original=*object;void* replacement[13];
        memcpy(replacement,original,sizeof(replacement));replacement[6]=(void*)replayQueueTick;
        uint32_t result=1;
        uint16_t speedIndex=at<uint16_t>(0xb3baf4);
        if(speedIndex>5) {invalidate(13);return 1;}
        uint16_t& batchInterval=at<uint16_t>(0xb3baf8+speedIndex*2);
        uint16_t originalInterval=batchInterval;
        __try {
            *object=replacement;batchInterval=1;
            unsigned steps=0;
            do {result=process(self);++steps;} while(!terminal.load() && clockValue<clockTarget && steps<64);
        }
        __finally {*object=original;batchInterval=originalInterval;}
        return result;
    }
    return process(self);
}

bool change(unsigned char* p,const std::vector<unsigned char>& bytes) {
    AcquireSRWLockExclusive(&codeLock);
    Patch patch{p,std::vector<unsigned char>(p,p+bytes.size()),bytes};
    DWORD old;
    if(!VirtualProtect(p,bytes.size(),PAGE_EXECUTE_READWRITE,&old)) {ReleaseSRWLockExclusive(&codeLock);return false;}
    memcpy(p,bytes.data(),bytes.size());FlushInstructionCache(GetCurrentProcess(),p,bytes.size());
    DWORD ignored;VirtualProtect(p,bytes.size(),old,&ignored);
    size_t offset=(uintptr_t)p-textBase;
    if(offset+bytes.size()<=expectedText.size()) memcpy(expectedText.data()+offset,bytes.data(),bytes.size());
    patches.push_back(std::move(patch));ReleaseSRWLockExclusive(&codeLock);return true;
}

bool patchCall(uint32_t va,uint32_t target,void* hook) {
    unsigned char* p=(unsigned char*)address(va);
    if(*p!=0xe8 || (uintptr_t)p+5+*reinterpret_cast<int32_t*>(p+1)!=(uintptr_t)address(target)) return false;
    std::vector<unsigned char> bytes(5);bytes[0]=0xe8;
    int32_t displacement=(int32_t)((uintptr_t)hook-(uintptr_t)p-5);memcpy(bytes.data()+1,&displacement,4);
    return change(p,bytes);
}

void* patchEntry(uint32_t va,const unsigned char* prefix,size_t length,void* hook) {
    unsigned char* p=(unsigned char*)address(va);
    if(memcmp(p,prefix,length)) return nullptr;
    auto trampoline=(unsigned char*)VirtualAlloc(nullptr,length+5,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!trampoline) return nullptr;
    memcpy(trampoline,p,length);trampoline[length]=0xe9;
    int32_t back=(int32_t)((uintptr_t)p+length-(uintptr_t)trampoline-length-5);
    memcpy(trampoline+length+1,&back,4);
    DWORD old;VirtualProtect(trampoline,length+5,PAGE_EXECUTE_READ,&old);
    std::vector<unsigned char> bytes(length,0x90);bytes[0]=0xe9;
    int32_t jump=(int32_t)((uintptr_t)hook-(uintptr_t)p-5);memcpy(bytes.data()+1,&jump,4);
    if(!change(p,bytes)) {VirtualFree(trampoline,0,MEM_RELEASE);return nullptr;}
    return trampoline;
}

bool verifyText() {
    wchar_t path[MAX_PATH];if(!GetModuleFileNameW(nullptr,path,MAX_PATH)) return false;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(f,&size) || (size.QuadPart!=8352688 && size.QuadPart!=8352176)) {CloseHandle(f);return false;}
    std::vector<unsigned char> disk((size_t)size.QuadPart);DWORD count=0;
    bool ok=ReadFile(f,disk.data(),(DWORD)disk.size(),&count,nullptr) && count==disk.size();CloseHandle(f);
    if(!ok) return false;
    auto dos=(IMAGE_DOS_HEADER*)disk.data();auto nt=(IMAGE_NT_HEADERS32*)(disk.data()+dos->e_lfanew);
    auto sections=IMAGE_FIRST_SECTION(nt);
    IMAGE_SECTION_HEADER* text=nullptr;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) if(!memcmp(sections[i].Name,".text",5)) text=&sections[i];
    if(!text || text->PointerToRawData+text->SizeOfRawData>disk.size()) return false;
    textBase=base+text->VirtualAddress;textSize=text->Misc.VirtualSize;
    expectedText.resize(textSize,0);
    memcpy(expectedText.data(),disk.data()+text->PointerToRawData,std::min<uint32_t>(text->SizeOfRawData,textSize));
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint32_t offset=0,delta=(uint32_t)(base-nt->OptionalHeader.ImageBase);
    while(offset<dir.Size) {
        auto block=(IMAGE_BASE_RELOCATION*)(base+dir.VirtualAddress+offset);
        if(block->SizeOfBlock<8 || offset+block->SizeOfBlock>dir.Size) return false;
        auto entries=(uint16_t*)(block+1);
        for(uint32_t i=0;i<(block->SizeOfBlock-8)/2;++i) {
            uint32_t rva=block->VirtualAddress+(entries[i]&0xfff);
            if((entries[i]>>12)==IMAGE_REL_BASED_HIGHLOW && rva>=text->VirtualAddress && rva+4<=text->VirtualAddress+textSize) {
                uint32_t value;size_t index=rva-text->VirtualAddress;
                memcpy(&value,expectedText.data()+index,4);value+=delta;memcpy(expectedText.data()+index,&value,4);
            }
        }
        offset+=block->SizeOfBlock;
    }
    return !memcmp(expectedText.data(),(void*)textBase,textSize);
}

bool readInput(const wchar_t* path) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};GetFileSizeEx(f,&length);
    if(length.QuadPart<8 || length.QuadPart>MaxFile) {CloseHandle(f);return false;}
    input.resize((size_t)length.QuadPart);DWORD count=0;
    bool ok=ReadFile(f,input.data(),(DWORD)input.size(),&count,nullptr) && count==input.size() && !memcmp(input.data(),Magic,8);
    CloseHandle(f);
    if(ok) {
        const unsigned char* p;uint32_t size;
        if(!getRecord(Begin,p,size) || size!=sizeof(savedInitial)) return false;
        memcpy(&savedInitial,p,size);
        savedNames={1,{}};
        if(nextKind(Names)) {
            if(!getRecord(Names,p,size) || !loadRecordedNames(p,size)) return false;
        }
        if(nextKind(Setup)) {
            if(!getRecord(Setup,p,size) || (size!=84 && size!=sizeof(savedSetup))) return false;
            memcpy(&savedSetup,p,size);if(size==84) legacyOptions(savedSetup);
            hasSetup=validSetup(savedSetup);if(!hasSetup) return false;
        }
        if(nextKind(Bootstrap)) {
            if(!getRecord(Bootstrap,p,size) || size!=sizeof(savedBootstrap) || savedInitial.networkMode!=1) return false;
            memcpy(&savedBootstrap,p,size);hasBootstrap=validBootstrap(savedBootstrap);if(!hasBootstrap) return false;
        }
        if(savedInitial.networkMode==1 && !hasBootstrap) {
            size_t scan=8;
            while(scan+sizeof(Record)<=input.size()) {
                Record record{};memcpy(&record,input.data()+scan,sizeof(record));scan+=sizeof(record);
                if(record.size>4096 || scan+record.size>input.size()) return false;
                if(record.kind==Bootstrap) {
                    if(hasBootstrap || record.size!=sizeof(savedBootstrap)) return false;
                    memcpy(&savedBootstrap,input.data()+scan,sizeof(savedBootstrap));
                    hasBootstrap=validBootstrap(savedBootstrap);if(!hasBootstrap) return false;
                }
                scan+=record.size;
            }
            if(scan!=input.size()) return false;
        }
        if(!memchr(savedInitial.directory,0,256) || !memchr(savedInitial.map,0,256) || savedInitial.networkMode>1 ||
           (savedInitial.networkMode==1 && !hasBootstrap)) return false;
        if(input.size()<sizeof(Record)+sizeof(State)) return false;
        Record ending{};memcpy(&ending,input.data()+input.size()-sizeof(Record)-sizeof(State),sizeof(ending));
        if(ending.kind!=End || ending.size!=sizeof(State)) return false;
        memcpy(&savedEnd,input.data()+input.size()-sizeof(State),sizeof(savedEnd));
        if(savedEnd.tick<savedInitial.state.tick) return false;
        cursor=8;
    }
    return ok;
}

DWORD WINAPI worker(void* argument) {
    ReplayStartConfig config{};
    if(argument) {config=*(ReplayStartConfig*)argument;delete (ReplayStartConfig*)argument;}
    wchar_t buffer[32768]{};
    if(argument) root=config.root;
    else {GetEnvironmentVariableW(L"KUF_REPLAY_ROOT",buffer,32768);root=buffer;}
    CreateDirectoryW(root.c_str(),nullptr);
    std::wstring inbox=root+L"\\CaptureInbox";CreateDirectoryW(inbox.c_str(),nullptr);
    logFile=CreateFileW((root+L"\\engine.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(argument) playback=config.playback!=0;
    else {GetEnvironmentVariableW(L"KUF_REPLAY_MODE",buffer,32768);playback=!wcscmp(buffer,L"play");}
    base=(uintptr_t)GetModuleHandleW(nullptr);
    bool ok=verifyText();
    if(!ok) log("Rejected: runtime executable code differs from relocated original image.");
    if(ok && argument && at<void*>(0xb8282c)) {
        ok=false;log("Rejected: match object already exists. Start recording before entering a match.");
    }
    if(ok && playback) {
        if(argument) ok=readInput(config.input);
        else {GetEnvironmentVariableW(L"KUF_REPLAY_INPUT",buffer,32768);ok=readInput(buffer);}
        if(ok) for(unsigned i=0;i<8;++i) {
            char line[144];sprintf_s(line,"NM01 recorded-name slot=%u name=%s",i+1,savedNames.names[i][0]?savedNames.names[i]:"<missing>");log(line);
        }
    }
    ring=(unsigned char*)VirtualAlloc(nullptr,RingSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ok=ok && ring;
    std::vector<HANDLE> paused;
    if(ok && argument) {
        HANDLE threads=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
        for(BOOL next=Thread32First(threads,&entry);next;next=Thread32Next(threads,&entry)) {
            if(entry.th32OwnerProcessID==GetCurrentProcessId() && entry.th32ThreadID!=GetCurrentThreadId()) {
                HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
                if(h) paused.push_back(h);else ok=false;
            }
        }
        CloseHandle(threads);
        size_t suspended=0;
        for(auto h:paused) {
            if(SuspendThread(h)==(DWORD)-1) {ok=false;break;}
            ++suspended;
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(h,&context)) ok=false;
            // Do not replace a prologue already being executed by another thread.
            for(uint32_t va:{0x4cf2b0u,0x4c09f0u,0x7245d0u,0x525c30u,0x4c6a70u,0x4ced30u,0x4c77d0u}) {
                uintptr_t p=(uintptr_t)address(va);
                if(context.Eip>p && context.Eip<p+6) ok=false;
            }
        }
        for(size_t i=suspended;i<paused.size();++i) CloseHandle(paused[i]);
        paused.resize(suspended);
    }
    if(ok) {
        advance=reinterpret_cast<Advance>(address(0x4daf50));apply=reinterpret_cast<Apply>(address(0x4db870));
        ok=patchCall(0x4cfc55,0x4daf50,(void*)advanceHook) && patchCall(0x4db124,0x4db870,(void*)commandHook) && patchCall(0x4db170,0x4db870,(void*)commandHook);
        const uint32_t resets[]={0x58f313,0x58f981,0x596894,0x596a44,0x59737e,0x597de3};
        for(auto va:resets) if(ok) ok=patchCall(va,0x4d6d10,(void*)resetHook);
        const unsigned char procPrefix[]={0x55,0x8b,0xec,0x6a,0xff};
        if(ok) {initializeMap=(Advance)patchEntry(0x4c77d0,procPrefix,sizeof(procPrefix),(void*)initializeMapHook);ok=initializeMap!=nullptr;}
        const unsigned char destroyPrefix[]={0x55,0x8b,0xec,0x56,0x8b,0xf1};
        if(ok) {process=reinterpret_cast<Process>(patchEntry(0x4cf2b0,procPrefix,sizeof(procPrefix),(void*)processHook));ok=process!=nullptr;}
        if(ok) {destroy=reinterpret_cast<Destroy>(patchEntry(0x4c09f0,destroyPrefix,sizeof(destroyPrefix),(void*)destroyHook));ok=destroy!=nullptr;}
        if(ok) {resultRender=reinterpret_cast<Process>(patchEntry(0x7245d0,procPrefix,sizeof(procPrefix),(void*)resultRenderHook));ok=resultRender!=nullptr;}
        const unsigned char resultButtonPrefix[]={0x55,0x8b,0xec,0x6a,0xff};
        if(ok && playback) {resultButton=(ResultButton)patchEntry(0x7241e0,resultButtonPrefix,sizeof(resultButtonPrefix),(void*)resultButtonHook);ok=resultButton!=nullptr;}
        if(ok && playback) {applicationProcess=reinterpret_cast<Process>(address(0x593f80));ok=patchCall(0x4fee4b,0x593f80,(void*)applicationHook);}
        if(ok && playback) {
            // Offline loading has no peer start timestamp (+9f0/+9f4). Skip only
            // the network loading-time diagnostic, never map/unit initialization.
            const unsigned char timingBranch[]={0x75,0x66};
            if(!memcmp(address(0x4c8db9),timingBranch,2)) ok=change((unsigned char*)address(0x4c8db9),{0xeb,0x66});
            else ok=false;
            const unsigned char selectionPrefix[]={0x55,0x8b,0xec,0xff,0x75,0x20};
            if(ok) originalSelection=(Selection)patchEntry(0x525c30,selectionPrefix,sizeof(selectionPrefix),(void*)selectionHook);
            ok=ok && originalSelection!=nullptr;
            unsigned char renderPrefix[]={0x56,0x57,0x8b,0x3d,0,0,0,0};
            uint32_t timer=static_cast<uint32_t>((uintptr_t)address(0xa16cfc));memcpy(renderPrefix+4,&timer,4);
            if(ok) {prepareDisplay=reinterpret_cast<Process>(patchEntry(0x4c6a70,renderPrefix,sizeof(renderPrefix),(void*)prepareDisplayHook));ok=prepareDisplay!=nullptr;}
            const unsigned char drawPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x54};
            if(ok) {drawDisplay=reinterpret_cast<Process>(patchEntry(0x4ced30,drawPrefix,sizeof(drawPrefix),(void*)drawDisplayHook));ok=drawDisplay!=nullptr;}
            // 478440 is visual body preparation, not the simulation update 47aa30.
            const unsigned char bodyPrefix[]={0x55,0x8b,0xec,0x51,0x56,0x8b,0xf1};
            if(ok) {ok=!memcmp(address(0x478440),bodyPrefix,sizeof(bodyPrefix));if(ok) buildingBodyPrepare=(Process)address(0x478440);}
            const unsigned char researchPrefix[]={0x56,0x8b,0xf1,0x0f,0xbf,0x86,0x88,0x14,0,0};
            if(ok) {researchProgress=(Process)patchEntry(0x47c590,researchPrefix,sizeof(researchPrefix),(void*)researchProgressHook);ok=researchProgress!=nullptr;}
            const unsigned char cameraPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x30};
            if(ok) {cameraInput=(CameraInput)patchEntry(0x53cee0,cameraPrefix,sizeof(cameraPrefix),(void*)observerCameraInputHook);ok=cameraInput!=nullptr;}
            const unsigned char buildingPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x0c};
            if(ok) {buildingRender=(Process)patchEntry(0x47ad00,buildingPrefix,sizeof(buildingPrefix),(void*)buildingRenderHook);ok=buildingRender!=nullptr;}
            const unsigned char characterPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x64};
            if(ok) {characterRender=(Process)patchEntry(0x4949f0,characterPrefix,sizeof(characterPrefix),(void*)characterRenderHook);ok=characterRender!=nullptr;}
            if(ok) {glyphDraw=(GlyphDraw)address(0x638750);ok=patchCall(0x54a72a,0x638750,(void*)observerGlyphDraw);}
            const unsigned char textDrawPrefix[]={0x55,0x8b,0xec,0x6a,0xff};
            if(ok) {textDraw=(TextDraw)patchEntry(0x54a000,textDrawPrefix,sizeof(textDrawPrefix),(void*)observerTextDrawHook);ok=textDraw!=nullptr;}
            if(ok) {interfaceRender=(Process)address(0x4d5f00);ok=patchCall(0x4c6b78,0x4d5f00,(void*)interfaceHook);}
            if(ok) ok=patchCall(0x5056d5,0x512a00,(void*)observerSelectionSync);
            if(ok) {
                minimapAlarmBlit=(MinimapAlarmBlit)address(0x4b1e60);
                ok=patchCall(0x4b46b6,0x4b1e60,(void*)replayMinimapAlarmBlit);
            }
            const unsigned char minimapBuildingCompare[]={0x80,0xbf,0xe8,0x14,0,0,0};
            if(ok && !memcmp(address(0x4b3385),minimapBuildingCompare,sizeof(minimapBuildingCompare))) {
                std::vector<unsigned char> bytes(7,0x90);bytes[0]=0xe8;
                int32_t delta=(int32_t)((uintptr_t)observerMinimapBuildingFlags-(uintptr_t)address(0x4b3385)-5);
                memcpy(bytes.data()+1,&delta,4);ok=change((unsigned char*)address(0x4b3385),bytes);
            } else ok=false;
            // Report 261013-015445: 5f7703 dereferences a null internal file handle
            // from audio loader 5400df. Native 5f7710 is the same query with a null check.
            const unsigned char fileQueryPrefix[]={0x8b,0x49,0x04,0x8b,0x01};
            if(ok && !memcmp(address(0x5f7700),fileQueryPrefix,sizeof(fileQueryPrefix))) {
                std::vector<unsigned char> jump(5);jump[0]=0xe9;
                int32_t delta=(int32_t)((uintptr_t)address(0x5f7710)-(uintptr_t)address(0x5f7700)-5);
                memcpy(jump.data()+1,&delta,4);ok=change((unsigned char*)address(0x5f7700),jump);
            } else ok=false;
            const unsigned char audioPrefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8};
            if(ok) {spatialAudio=(Audio)patchEntry(0x540e70,audioPrefix,sizeof(audioPrefix),(void*)spatialAudioHook);ok=spatialAudio!=nullptr;}
            if(ok) {spatialLoopAudio=(Audio)patchEntry(0x5410a0,audioPrefix,sizeof(audioPrefix),(void*)spatialLoopAudioHook);ok=spatialLoopAudio!=nullptr;}
            if(ok) {spatialUpdate=(Process)patchEntry(0x540c10,audioPrefix,sizeof(audioPrefix),(void*)spatialUpdateHook);ok=spatialUpdate!=nullptr;}
            if(ok) ok=patchCall(0x540eb0,0x4369f0,(void*)observerAudioVisibility);
            if(ok) ok=patchCall(0x5410e0,0x4369f0,(void*)observerAudioVisibility);
            // These calls pass speech resource archive b8a1e4; FX uses b8a1e0.
            for(uint32_t call:{0x4656eau,0x480f09u,0x48cc48u,0x48cecfu,0x48cf4cu,0x49179bu,
                0x4ad7a1u,0x4ad7c3u,0x4c1d0fu,0x4c54e3u,0x4f5626u,0x5011deu,0x5213ebu,
                0x52f377u,0x5d79cau,0x5e0ffcu,0x5e8a05u})
                if(ok) ok=patchCall(call,0x541a40,(void*)replayVoiceHook);
            for(uint32_t call:{0x521409u,0x53d87au})
                if(ok) ok=patchCall(call,0x540e70,(void*)replaySpatialVoiceHook);
            const unsigned char localAudioPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x48};
            const unsigned char remoteAudioPrefix[]={0x55,0x8b,0xec,0x83,0xec,0x44};
            if(ok) {localUnitAudio=(UnitAudio)patchEntry(0x521090,localAudioPrefix,sizeof(localAudioPrefix),(void*)localUnitAudioHook);ok=localUnitAudio!=nullptr;}
            if(ok) {remoteUnitAudio=(UnitAudio)patchEntry(0x53d570,remoteAudioPrefix,sizeof(remoteAudioPrefix),(void*)remoteUnitAudioHook);ok=remoteUnitAudio!=nullptr;}
            const unsigned char panelCall[]={0x8b,0x01,0xff,0x50,0x10};
            if(ok && !memcmp(address(0x4d6c38),panelCall,sizeof(panelCall))) {
                std::vector<unsigned char> bytes={0xe8,0,0,0,0};
                int32_t delta=(int32_t)((uintptr_t)observerPanelHook-(uintptr_t)address(0x4d6c38)-5);
                memcpy(bytes.data()+1,&delta,4);ok=change((unsigned char*)address(0x4d6c38),bytes);
            } else ok=false;
            const unsigned char layerCall[]={0x8b,0x01,0xff,0x50,0x08};
            if(ok && !memcmp(address(0x49e452),layerCall,sizeof(layerCall))) {
                std::vector<unsigned char> bytes={0xe8,0,0,0,0};
                int32_t delta=(int32_t)((uintptr_t)observerLayerHook-(uintptr_t)address(0x49e452)-5);
                memcpy(bytes.data()+1,&delta,4);ok=change((unsigned char*)address(0x49e452),bytes);
            } else ok=false;
            // Only CGame::Process uses this timer. Rendering continues using real time.
            unsigned char clockPrefix[]={0xa1,0,0,0,0};
            uint32_t timerSlot=(uint32_t)(uintptr_t)address(0xa16cfc);memcpy(clockPrefix+1,&timerSlot,4);
            if(ok && !memcmp(address(0x4cf302),clockPrefix,sizeof(clockPrefix))) {
                std::vector<unsigned char> bytes={0xb8,0,0,0,0};
                uint32_t callback=(uint32_t)(uintptr_t)playbackClock;memcpy(bytes.data()+1,&callback,4);
                ok=change((unsigned char*)address(0x4cf302),bytes);
            } else ok=false;
            // These four native input senders already return without dispatch in
            // the game's own playback mode. Camera processing is left intact.
            unsigned char inputPrefix[]={0x55,0x8b,0xec,0x80,0x3d,0,0,0,0,0x01};
            uint32_t flag=static_cast<uint32_t>((uintptr_t)address(0xb83ad8));memcpy(inputPrefix+5,&flag,4);
            const uint32_t senders[]={0x5259e0,0x525a10,0x525a30,0x525a50};
            for(unsigned i=0;i<4 && ok;++i) ok=patchEntry(senders[i],inputPrefix,sizeof(inputPrefix),i?(void*)observerInputTwo:(void*)observerInputOne)!=nullptr;
        }
    }
    if(!ok) {restore();log("Initialization failed; all installed owned patches restored.");}
    for(auto h:paused) {ResumeThread(h);CloseHandle(h);}
    HANDLE marker=CreateFileW((root+L"\\engine.ready").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
    DWORD n=0;char ready=ok?'1':'0';if(marker!=INVALID_HANDLE_VALUE) {WriteFile(marker,&ready,1,&n,nullptr);CloseHandle(marker);}
    if(argument) wcscpy_s(buffer,config.event);
    else GetEnvironmentVariableW(L"KUF_REPLAY_READY",buffer,32768);
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,buffer);if(event) {SetEvent(event);CloseHandle(event);}
    if(!ok) return 1;
    log(playback?"Playback adapter ready. Automatic single-player/network bootstrap and spectator display enabled.":"Recording adapter ready. Single-player and multiplayer capture with network bootstrap enabled.");
    HANDLE populationFile=INVALID_HANDLE_VALUE;
    HANDLE file=INVALID_HANDLE_VALUE;std::wstring partial;uint32_t fileBytes=0,number=0;
    uint32_t lastFrameOffset=0,writtenFrames=0;State writerBefore{};
    ULONGLONG checkAt=GetTickCount64(),pulseAt=0;int lastNotice=0;
    for(;;) {
        if(!gameWindow) {
            EnumWindows(findGameWindow,0);
            if(gameWindow) {
                oldWindowProc=(WNDPROC)GetWindowLongPtrW(gameWindow,GWLP_WNDPROC);
                if(!oldWindowProc || !SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,(LONG_PTR)recordingWindowProc)) {
                    gameWindow=nullptr;oldWindowProc=nullptr;
                }
            }
        }
        if(gameWindow && !playback && GetTickCount64()-pulseAt>250) {
            pulseAt=GetTickCount64();PostMessageW(gameWindow,RecordPulse,0,0);
        }
        if(playback) {
            static ULONGLONG audioLogAt=0;
            if(GetTickCount64()-audioLogAt>=10000) {
                audioLogAt=GetTickCount64();char line[512];
                sprintf_s(line,"AU01 audio tick=%u calls=%u onscreen=%u rejected=%u disabled=%u camera-aligned=%u death-events=%u death-submitted=%u death-accepted=%u death-rejected=%u death-onscreen=%u last-death=%u",observerAudioTick.load(),observerAudioCalls.load(),observerAudioOnscreen.load(),observerAudioRejected.load(),observerAudioDisabled.load(),observerAudioCameraFixes.load(),observerDeathEvents.load(),observerDeathSubmitted.load(),observerDeathAccepted.load(),observerDeathRejected.load(),observerDeathOnscreen.load(),observerLastDeath.load());
                log(line);
                sprintf_s(line,"ID01 participant-name paints=%u bitmap-glyphs=%u tint-fallbacks=%u",observerIdPaints.load(),observerIdGlyphs.load(),observerIdGlyphFailures.load());log(line);
                sprintf_s(line,"BD01 observer building-body-prepares=%u construction-draws=%u",observerBuildingBodies.load(),observerConstructionDraws.load());log(line);
                sprintf_s(line,"OV01 hero-count=%u upgrade-warning=%u upgrade-complete=%u",observerHeroCount,observerUpgradeWarnings.load(),observerUpgradeCompletions.load());log(line);
            }
        }
        uint32_t t=tail.load(std::memory_order_relaxed),h=head.load(std::memory_order_acquire);
        if(windowDestroyed.load(std::memory_order_acquire) && t==h) {
            if(file!=INVALID_HANDLE_VALUE) {FlushFileBuffers(file);CloseHandle(file);}
            if(populationFile!=INVALID_HANDLE_VALUE) {FlushFileBuffers(populationFile);CloseHandle(populationFile);}
            log("Game window destroyed; engine worker stopped.");
            return 0;
        }
        if(h-t>=sizeof(Record)) {
            Record record{};readRing(t,&record,sizeof(record));
            unsigned char payload[4096];
            if(record.size>sizeof(payload) || h-t<sizeof(record)+record.size) {invalidate(6);break;}
            readRing(t+sizeof(record),payload,record.size);
            if(record.kind==Bootstrap && record.size==sizeof(NetworkBootstrap)) {
                NetworkBootstrap boot{};memcpy(&boot,payload,sizeof(boot));
                char details[256]{};
                sprintf_s(details,"Network bootstrap captured: valid=%u, participants=%u, local=%u, RTS=%u, scenario=%u, interval=%u, first-controller=%u",validBootstrap(boot),boot.participants,boot.localPlayer,boot.rtsMode,boot.scenarioMode,boot.tickInterval,boot.slots[0][0]);
                log(details);
            }
            if(record.kind==Begin) {
                if(record.size==sizeof(Initial)) {
                    Initial captured{};memcpy(&captured,payload,sizeof(captured));
                    log(captured.networkMode==1?"Multiplayer match capture started: native synchronized command application order.":"Single-player match capture started.");
                }
                if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
                if(populationFile!=INVALID_HANDLE_VALUE) CloseHandle(populationFile);
                partial=inbox+L"\\"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++number)+L".partial";
                file=CreateFileW(partial.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                populationFile=CreateFileW((partial+L".population").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(populationFile!=INVALID_HANDLE_VALUE && (!WriteFile(populationFile,"KUFPOP01",8,&n,nullptr) || n!=8)) {
                    CloseHandle(populationFile);populationFile=INVALID_HANDLE_VALUE;log("PO01 population metadata unavailable.");
                }
                fileBytes=8;
                lastFrameOffset=0;writtenFrames=0;
                if(file==INVALID_HANDLE_VALUE || !WriteFile(file,Magic,8,&n,nullptr) || n!=8) invalidate(11);
            }
            if(record.kind==Population) {
                if(populationFile!=INVALID_HANDLE_VALUE && record.size==sizeof(PopulationSample) &&
                   (!WriteFile(populationFile,payload,record.size,&n,nullptr) || n!=record.size)) {
                    CloseHandle(populationFile);populationFile=INVALID_HANDLE_VALUE;log("PO01 population metadata write failed.");
                }
                tail.store(t+sizeof(record)+record.size,std::memory_order_release);continue;
            }
            if(file!=INVALID_HANDLE_VALUE && !failed.load()) {
                if(record.kind==Before && record.size==sizeof(State)) {
                    lastFrameOffset=fileBytes;++writtenFrames;memcpy(&writerBefore,payload,sizeof(State));
                }
                if(record.kind==TrimEnd) {
                    if(writtenFrames<2 || record.size!=sizeof(State) || memcmp(payload,&writerBefore,sizeof(State))) invalidate(6);
                    else {
                        LARGE_INTEGER offset{};offset.QuadPart=lastFrameOffset;
                        if(!SetFilePointerEx(file,offset,nullptr,FILE_BEGIN) || !SetEndOfFile(file)) invalidate(11);
                        else {fileBytes=lastFrameOffset;record.kind=End;}
                    }
                }
                if(failed.load()) {
                    tail.store(t+sizeof(record)+record.size,std::memory_order_release);
                    if(closePending.load()) {closeReady.store(true);PostMessageW(gameWindow,WM_CLOSE,0,0);}
                    log("Writer ending boundary/truncation failed; incomplete capture preserved.");return 1;
                }
                fileBytes+=sizeof(record)+record.size;
                if(fileBytes>MaxFile || !WriteFile(file,&record,sizeof(record),&n,nullptr) || n!=sizeof(record) ||
                   !WriteFile(file,payload,record.size,&n,nullptr) || n!=record.size) invalidate(11);
                if(record.kind==End && !failed.load()) {
                    if(populationFile!=INVALID_HANDLE_VALUE) {FlushFileBuffers(populationFile);CloseHandle(populationFile);populationFile=INVALID_HANDLE_VALUE;}
                    if(!FlushFileBuffers(file)) invalidate(11);
                    CloseHandle(file);file=INVALID_HANDLE_VALUE;
                    if(!failed.load() && !MoveFileW(partial.c_str(),(partial+L".complete").c_str())) invalidate(11);
                    else if(!failed.load()) log("Match capture finalized. Launcher can validate and package it.");
                    if(closePending.load()) {closeReady.store(true);PostMessageW(gameWindow,WM_CLOSE,0,0);}
                }
            }
            tail.store(t+sizeof(record)+record.size,std::memory_order_release);
        } else Sleep(10);
        if(closePending.load() && failed.load() && !closeReady.exchange(true)) PostMessageW(gameWindow,WM_CLOSE,0,0);
        int issue=notice.load();
        if(issue!=lastNotice) {char line[100];wsprintfA(line,"Engine status code: %d (see docs/ENGINE_NATIVE.md)",issue);log(line);if(playback && issue==10){wsprintfA(line,"Observer spatial audio callbacks: %u",observerAudioCalls.load());log(line);}lastNotice=issue;}
        if(GetTickCount64()-checkAt>2000 && !terminal.load()) {
            checkAt=GetTickCount64();
            if(populationFile!=INVALID_HANDLE_VALUE) FlushFileBuffers(populationFile);
            if(file!=INVALID_HANDLE_VALUE && !failed.load() && !FlushFileBuffers(file)) invalidate(11);
            AcquireSRWLockShared(&codeLock);
            bool valid=!memcmp(expectedText.data(),(void*)textBase,textSize);
            ReleaseSRWLockShared(&codeLock);
            if(!valid) invalidate(12);
        }
    }
    return 0;
}
}

#pragma comment(linker,"/EXPORT:StartReplay=_StartReplay@4")
extern "C" DWORD WINAPI StartReplay(void* argument) {
    static std::atomic<bool> started{false};
    if(!argument || started.exchange(true)) return 0;
    auto config=new ReplayStartConfig(*(ReplayStartConfig*)argument);
    config->root[1023]=0;config->input[1023]=0;config->event[127]=0;
    HANDLE thread=CreateThread(nullptr,0,worker,config,0,nullptr);
    if(!thread) {delete config;return 0;}
    CloseHandle(thread);return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        wchar_t rootBuffer[1024]{};
        if(GetEnvironmentVariableW(L"KUF_REPLAY_ROOT",rootBuffer,1024)) {
            HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
            if(thread) CloseHandle(thread);else return FALSE;
        }
    }
    // Abrupt shutdown leaves .partial; no loader-lock disk waits or falsely complete replay.
    return TRUE;
}
