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
uint32_t session=0,frames=0;
Initial initial{};
std::vector<unsigned char> input,expectedText;
size_t cursor=8;
uintptr_t textBase=0;
uint32_t textSize=0;
HANDLE logFile=INVALID_HANDLE_VALUE;
std::atomic<int> notice{0};
HWND gameWindow=nullptr;
WNDPROC oldWindowProc=nullptr;
std::atomic<bool> closePending{false},closeReady{false};

template<class T> T& at(uint32_t va) {return *reinterpret_cast<T*>(base+va-0x400000);}
void* address(uint32_t va) {return reinterpret_cast<void*>(base+va-0x400000);}
using Advance=uint32_t(__thiscall*)(void*,uint32_t,uint32_t);
using Apply=uint32_t(__thiscall*)(void*,uint32_t,const void*);
using Process=uint32_t(__thiscall*)(void*);
using Destroy=uint32_t(__thiscall*)(void*,uint32_t);
Advance advance=nullptr;
Apply apply=nullptr;
Process process=nullptr;
Destroy destroy=nullptr;

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

void finish(void* self) {
    if(!active) return;
    State state{};
    if(!snapshot(self,state)) {invalidate(3);active=false;return;}
    if(!playback) emit(End,&state,sizeof(state));
    active=false;frames=0;
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

LRESULT CALLBACK recordingWindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_CLOSE && closePending.load() && !closeReady.load()) return 0;
    if(message==WM_CLOSE && !playback && !closeReady.load() && active && !failed.load()) {
        if(!closePending.exchange(true)) finish(at<void*>(0xb8282c));
        // The writer completes asynchronously; no disk waits on the simulation thread.
        return 0;
    }
    return CallWindowProcW(oldWindowProc,window,message,wparam,lparam);
}

BOOL CALLBACK findGameWindow(HWND window,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t title[128]{};GetWindowTextW(window,title,128);
    if(pid==GetCurrentProcessId() && !wcscmp(title,L"Kingdom Under Fire") && !GetWindow(window,GW_OWNER)) {
        gameWindow=window;return FALSE;
    }
    return TRUE;
}

uint32_t __fastcall commandHook(void* self,void*,uint32_t length,const void* bytes) {
    if(active && inDispatch && !playback && !failed.load()) {
        if(length<2 || length>MaxCommand) invalidate(4);
        else {
            unsigned char buffer[sizeof(CommandHeader)+MaxCommand];
            CommandHeader h{at<uint32_t>(0xb8a280),at<uint32_t>(0xbed1e8),at<uint32_t>(0xb827dc),length};
            memcpy(buffer,&h,sizeof(h));memcpy(buffer+sizeof(h),bytes,length);
            emit(Command,buffer,sizeof(h)+length);
        }
    }
    return apply(self,length,bytes);
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
    if(closePending.load()) return advance(self,a,b);
    if(failed.load()) return playback?1:advance(self,a,b);
    // Multiplayer recording/replay is deliberately disabled until host latency and sync are measured.
    if(at<unsigned char>(0xb83ad0)!=0) {
        if(active) {invalidate(5);active=false;}
        return advance(self,a,b);
    }
    if(!producer) producer=GetCurrentThreadId();
    State state{};
    if(!snapshot(self,state)) {invalidate(3);return advance(self,a,b);}
    if(!active) {
        fillInitial(self,initial);
        if(playback) {
            const unsigned char* p;uint32_t n;
            if(!getRecord(Begin,p,n) || n!=sizeof(Initial)) {invalidate(6);return 1;}
            const Initial* saved=reinterpret_cast<const Initial*>(p);
            // This prototype requires the same game-native match setup to have been loaded.
            // It never overwrites arbitrary state or RNG to conceal a mismatch.
            if(memcmp(saved,&initial,sizeof(Initial))) {invalidate(7);return 1;}
        } else emit(Begin,&initial,sizeof(initial));
        active=true;++session;frames=0;
    }
    if(!playback) {
        emit(Before,&state,sizeof(state));inDispatch=true;
        uint32_t result=advance(self,a,b);
        inDispatch=false;
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
    if(!getRecord(Before,p,n) || n!=sizeof(State) || !same(state,*reinterpret_cast<const State*>(p))) {invalidate(8);return 1;}
    if(!discardLiveQueue()) {invalidate(9);return 1;}
    while(nextKind(Command)) {
        if(!getRecord(Command,p,n) || n<sizeof(CommandHeader)) {invalidate(6);return 1;}
        const CommandHeader* h=reinterpret_cast<const CommandHeader*>(p);
        if(h->size<2 || h->size>MaxCommand || n!=sizeof(*h)+h->size ||
           h->tick!=at<uint32_t>(0xb8a280) || h->seed!=at<uint32_t>(0xbed1e8) || h->randomCount!=at<uint32_t>(0xb827dc)) {invalidate(8);return 1;}
        // Pad the native decoder's fixed stack-sized input. Inputs are locally authenticated by launcher.
        unsigned char safe[MaxCommand]{};memcpy(safe,p+sizeof(*h),h->size);
        apply(self,h->size,safe);
    }
    if(!snapshot(self,state) || !getRecord(After,p,n) || n!=sizeof(State) || !same(state,*reinterpret_cast<const State*>(p))) {invalidate(8);return 1;}
    ++frames;return 1;
}

uint32_t __fastcall destroyHook(void* self,void*,uint32_t flags) {
    if(GetCurrentThreadId()==producer) finish(self);
    return destroy(self,flags);
}

void __cdecl resetHook() {
    if(active) finish(at<void*>(0xb8282c));
    using Reset=void(__cdecl*)();reinterpret_cast<Reset>(address(0x4d6d10))();
}

void restore() {
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

uint32_t __fastcall processHook(void* self,void*) {
    if(playback && terminal.load()) {
        static bool shown=false;
        if(!shown) {
            shown=true;
            const wchar_t* prompt=failed.load()?L"기록 상태와 다른 상태를 발견해 재생을 중단했습니다. 종료하시겠습니까?":L"재생이 끝났습니다. 종료하시겠습니까?";
            if(MessageBoxW(GetActiveWindow(),prompt,L"KUF Replay",MB_YESNO|MB_ICONQUESTION)==IDYES) {
                restore();PostQuitMessage(0);
            }
        }
        return 1;
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
    std::vector<unsigned char> disk(8352688);DWORD count=0;
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
    CloseHandle(f);return ok;
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
            for(uint32_t va:{0x4cf2b0u,0x4c09f0u}) {
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
        const unsigned char destroyPrefix[]={0x55,0x8b,0xec,0x56,0x8b,0xf1};
        if(ok) {process=reinterpret_cast<Process>(patchEntry(0x4cf2b0,procPrefix,sizeof(procPrefix),(void*)processHook));ok=process!=nullptr;}
        if(ok) {destroy=reinterpret_cast<Destroy>(patchEntry(0x4c09f0,destroyPrefix,sizeof(destroyPrefix),(void*)destroyHook));ok=destroy!=nullptr;}
    }
    if(!ok) {restore();log("Initialization failed; all installed owned patches restored.");}
    for(auto h:paused) {ResumeThread(h);CloseHandle(h);}
    HANDLE marker=CreateFileW((root+L"\\engine.ready").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
    DWORD n=0;char ready=ok?'1':'0';if(marker!=INVALID_HANDLE_VALUE) {WriteFile(marker,&ready,1,&n,nullptr);CloseHandle(marker);}
    if(argument) wcscpy_s(buffer,config.event);
    else GetEnvironmentVariableW(L"KUF_REPLAY_READY",buffer,32768);
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,buffer);if(event) {SetEvent(event);CloseHandle(event);}
    if(!ok) return 1;
    log(playback?"Playback adapter ready. Waiting for identical game-native match setup.":"Recording adapter ready. Single-player only; multiplayer intentionally excluded.");
    HANDLE file=INVALID_HANDLE_VALUE;std::wstring partial;uint32_t fileBytes=0,number=0;
    ULONGLONG checkAt=GetTickCount64();int lastNotice=0;
    for(;;) {
        if(!playback && !gameWindow) {
            EnumWindows(findGameWindow,0);
            if(gameWindow) {
                oldWindowProc=(WNDPROC)GetWindowLongPtrW(gameWindow,GWLP_WNDPROC);
                if(!oldWindowProc || !SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,(LONG_PTR)recordingWindowProc)) {
                    gameWindow=nullptr;oldWindowProc=nullptr;
                }
            }
        }
        uint32_t t=tail.load(std::memory_order_relaxed),h=head.load(std::memory_order_acquire);
        if(h-t>=sizeof(Record)) {
            Record record{};readRing(t,&record,sizeof(record));
            unsigned char payload[4096];
            if(record.size>sizeof(payload) || h-t<sizeof(record)+record.size) {invalidate(6);break;}
            readRing(t+sizeof(record),payload,record.size);
            if(record.kind==Begin) {
                if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
                partial=inbox+L"\\"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++number)+L".partial";
                file=CreateFileW(partial.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                fileBytes=8;
                if(file==INVALID_HANDLE_VALUE || !WriteFile(file,Magic,8,&n,nullptr) || n!=8) invalidate(11);
            }
            if(file!=INVALID_HANDLE_VALUE && !failed.load()) {
                fileBytes+=sizeof(record)+record.size;
                if(fileBytes>MaxFile || !WriteFile(file,&record,sizeof(record),&n,nullptr) || n!=sizeof(record) ||
                   !WriteFile(file,payload,record.size,&n,nullptr) || n!=record.size) invalidate(11);
                if(record.kind==End && !failed.load()) {
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
        if(issue!=lastNotice) {char line[100];wsprintfA(line,"Engine status code: %d (see docs/ENGINE_NATIVE.md)",issue);log(line);lastNotice=issue;}
        if(GetTickCount64()-checkAt>2000 && !terminal.load()) {
            checkAt=GetTickCount64();
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
