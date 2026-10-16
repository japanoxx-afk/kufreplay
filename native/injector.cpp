#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include "protocol.h"
#include <string>
#include <vector>
#include <cstdio>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

static bool knownGame(const wchar_t* path) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; GetFileSizeEx(f,&size);
    if(size.QuadPart!=8352688 && size.QuadPart!=8352176) { CloseHandle(f); return false; }
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD objectSize=0,used=0;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,4,&used,0)>=0;
    std::vector<unsigned char> obj(objectSize),chunk(65536);
    if(ok) ok=BCryptCreateHash(alg,&hash,obj.data(),objectSize,nullptr,0,0)>=0;
    DWORD count=0;
    while(ok) {
        if(!ReadFile(f,chunk.data(),(DWORD)chunk.size(),&count,nullptr)) {ok=false;break;}
        if(!count) break;
        ok=BCryptHashData(hash,chunk.data(),count,0)>=0;
    }
    unsigned char result[32]{};
    if(ok) ok=BCryptFinishHash(hash,result,32,0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(alg) BCryptCloseAlgorithmProvider(alg,0);
    CloseHandle(f);
    const unsigned char expected[32]={0x69,0xf2,0x57,0x5a,0xd1,0x4c,0x78,0xd3,0xc6,0xf7,0xc1,0x5d,0x01,0x87,0x23,0x4b,0xe0,0x05,0xf0,0x6a,0xbf,0x09,0x5d,0x3f,0x1b,0xd6,0x02,0xf1,0xc3,0x72,0x28,0x68};
    const unsigned char updated[32]={0xd7,0x2b,0xc8,0x81,0x4c,0x4e,0x3c,0x56,0x14,0xb8,0x93,0xc7,0x9f,0xde,0x5a,0xc1,0x3b,0xf1,0x7f,0xca,0xa7,0xf8,0x30,0xdf,0xf8,0xf4,0xe2,0x40,0x12,0x6e,0x5c,0xfc};
    return ok && ((size.QuadPart==8352688 && !memcmp(expected,result,32)) ||
                  (size.QuadPart==8352176 && !memcmp(updated,result,32)));
}

static bool gameRunning() {
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W e{}; e.dwSize=sizeof(e);
    bool found=false;
    for(BOOL ok=Process32FirstW(s,&e);ok;ok=Process32NextW(s,&e)) {
        if(!_wcsicmp(e.szExeFile,L"KingdomUnderFire.exe")) {found=true;break;}
    }
    CloseHandle(s);return found;
}

static DWORD gamePid() {
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);DWORD pid=0;
    for(BOOL ok=Process32FirstW(s,&e);ok;ok=Process32NextW(s,&e))
        if(!_wcsicmp(e.szExeFile,L"KingdomUnderFire.exe")) {pid=e.th32ProcessID;break;}
    CloseHandle(s);return pid;
}

static bool imageMatches(HANDLE process,const std::wstring& path) {
    wchar_t actual[32768]{};DWORD size=32768;
    return QueryFullProcessImageNameW(process,0,actual,&size) && !_wcsicmp(actual,path.c_str());
}
struct SessionWindow {DWORD pid;bool found;};
static BOOL CALLBACK sessionWindow(HWND window,LPARAM data) {
    auto state=(SessionWindow*)data;DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    wchar_t title[128]{};GetWindowTextW(window,title,128);
    if(pid==state->pid && !wcscmp(title,L"Kingdom Under Fire") && !GetWindow(window,GW_OWNER)) state->found=true;
    return TRUE;
}
struct SessionLock {
    HANDLE handle;
    SessionLock():handle(CreateMutexW(nullptr,FALSE,L"Local\\KUFReplayLaunchCleanup")) {
        if(handle) WaitForSingleObject(handle,INFINITE);
    }
    ~SessionLock() {if(handle) {ReleaseMutex(handle);CloseHandle(handle);}}
};
static int watchSession(const wchar_t* gamePath,DWORD pid,const wchar_t* dataRoot) {
    // Independent of the launcher and injected DLL: also handles Alt+F4/crash/forced exit.
    HANDLE game=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|PROCESS_TERMINATE,FALSE,pid);
    if(!game || !imageMatches(game,gamePath)) {if(game) CloseHandle(game);return 1;}
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);MODULEENTRY32W module{};module.dwSize=sizeof(module);
    uintptr_t gameBase=0;
    for(BOOL more=Module32FirstW(modules,&module);more;more=Module32NextW(modules,&module)) {
        if(!_wcsicmp(module.szExePath,gamePath)) {gameBase=(uintptr_t)module.modBaseAddr;break;}
    }
    if(modules!=INVALID_HANDLE_VALUE) CloseHandle(modules);
    bool seenWindow=false;ULONGLONG vanished=0;
    while(WaitForSingleObject(game,250)==WAIT_TIMEOUT) {
        SessionWindow state{pid,false};EnumWindows(sessionWindow,(LPARAM)&state);
        if(state.found) {seenWindow=true;vanished=0;}
        else if(seenWindow) {
            if(!vanished) vanished=GetTickCount64();
            uint32_t object=1,manager=1;SIZE_T read=0;
            // Only an already destroyed match and absent main window qualify for orphan cleanup.
            if(gameBase && GetTickCount64()-vanished>=5000 &&
               ReadProcessMemory(game,(void*)(gameBase+0xb8282c-0x400000),&object,4,&read) && read==4 && !object &&
               ReadProcessMemory(game,(void*)(gameBase+0xb870c0-0x400000),&manager,4,&read) && read==4 && !manager) {
                TerminateProcess(game,0);WaitForSingleObject(game,3000);break;
            }
        }
    }
    CloseHandle(game);
    std::wstring logPath=std::wstring(dataRoot)+L"\\session-cleanup.log";
    HANDLE log=CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
    if(log!=INVALID_HANDLE_VALUE) {
        char line[160]{};sprintf_s(line,"Game PID %lu exited; game-only session cleanup\r\n",pid);
        DWORD count=0;WriteFile(log,line,(DWORD)strlen(line),&count,nullptr);CloseHandle(log);
    }
    return 0;
}
static bool startWatcher(const wchar_t* game,DWORD pid,const wchar_t* dataRoot) {
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768)) return false;
    std::wstring command=L"\""+std::wstring(executable)+L"\" --watch \""+game+L"\" "+std::to_wstring(pid)+L" \""+dataRoot+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP,nullptr,nullptr,&startup,&process)) return false;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;
}

static int steamRecord(wchar_t** argv) {
    if((INT_PTR)ShellExecuteW(nullptr,L"open",L"steam://rungameid/2183600",nullptr,nullptr,SW_SHOWNORMAL)<=32) {
        fwprintf(stderr,L"Steam launch request failed.\n");return 5;
    }
    DWORD pid=0;
    for(unsigned wait=0;wait<600 && !pid;++wait) {Sleep(100);pid=gamePid();}
    if(!pid) {fwprintf(stderr,L"Steam did not start the game within 60 seconds.\n");return 5;}
    HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
    if(!process) return 6;
    wchar_t actual[32768]{};DWORD capacity=32768;
    bool ok=QueryFullProcessImageNameW(process,0,actual,&capacity) && !_wcsicmp(actual,argv[1]) && knownGame(actual);
    std::wstring eventName=L"Local\\KUFReplayReady_"+std::to_wstring(GetCurrentProcessId());
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    void* remote=nullptr;HANDLE thread=nullptr;DWORD loaded=0;
    if(ok) {
        SIZE_T bytes=(wcslen(argv[2])+1)*sizeof(wchar_t);
        remote=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        ok=remote && WriteProcessMemory(process,remote,argv[2],bytes,nullptr);
        if(ok) thread=CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote,0,nullptr);
        ok=thread && WaitForSingleObject(thread,15000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&loaded) && loaded;
        if(thread) CloseHandle(thread);
        if(ok) VirtualFreeEx(process,remote,0,MEM_RELEASE);
    }
    if(ok) {
        HMODULE local=LoadLibraryExW(argv[2],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        FARPROC start=local?GetProcAddress(local,"StartReplay"):nullptr;
        ReplayStartConfig config{};
        ok=start && wcslen(argv[3])<1024 && wcslen(argv[5])<1024;
        if(ok) {
            wcscpy_s(config.root,argv[3]);wcscpy_s(config.event,eventName.c_str());
            config.playback=!wcscmp(argv[4],L"steam-play");
            if(config.playback) wcscpy_s(config.input,argv[5]);
            remote=VirtualAllocEx(process,nullptr,sizeof(config),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            ok=remote && WriteProcessMemory(process,remote,&config,sizeof(config),nullptr);
            if(ok) thread=CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)(loaded+(uintptr_t)start-(uintptr_t)local),remote,0,nullptr);
            DWORD result=0;
            ok=thread && WaitForSingleObject(thread,15000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&result) && result==1;
            if(thread) CloseHandle(thread);
            if(ok) VirtualFreeEx(process,remote,0,MEM_RELEASE);
        }
        if(local) FreeLibrary(local);
    }
    if(ok) ok=WaitForSingleObject(ready,15000)==WAIT_OBJECT_0;
    if(ok) {
        std::wstring markerPath=std::wstring(argv[3])+L"\\engine.ready";
        HANDLE marker=CreateFileW(markerPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        char value=0;DWORD count=0;
        ok=marker!=INVALID_HANDLE_VALUE && ReadFile(marker,&value,1,&count,nullptr) && count==1 && value=='1';
        if(marker!=INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    CloseHandle(ready);CloseHandle(process);
    if(!ok) {fwprintf(stderr,L"Steam game started, but recording engine attachment failed. See engine.log. Game was not terminated.\n");return 6;}
    if(!startWatcher(argv[1],pid,argv[3])) {fwprintf(stderr,L"Session exit watcher could not start.\n");return 7;}
    printf("%lu\n",pid);return 0;
}

int wmain(int argc,wchar_t** argv) {
    if(argc==5 && !wcscmp(argv[1],L"--watch")) {
        wchar_t* end=nullptr;unsigned long pid=wcstoul(argv[3],&end,10);
        if(!pid || !end || *end || !knownGame(argv[2])) return 2;
        return watchSession(argv[2],pid,argv[4]);
    }
    // Only the launcher supplies these paths. Never loads a DLL contained in a replay.
    if(argc!=6) {fwprintf(stderr,L"Usage: injector GAME DLL ROOT record|play INPUT\n");return 2;}
    if(!knownGame(argv[1])) {fwprintf(stderr,L"Unsupported game SHA256. No process was patched.\n");return 3;}
    SessionLock lock;
    if(gameRunning()) {fwprintf(stderr,L"Close the running game before starting a recorded session.\n");return 4;}
    if(!wcscmp(argv[4],L"steam-record") || !wcscmp(argv[4],L"steam-play")) return steamRecord(argv);
    if(wcscmp(argv[4],L"record") && wcscmp(argv[4],L"play")) return 2;
    SetEnvironmentVariableW(L"KUF_REPLAY_ROOT",argv[3]);
    SetEnvironmentVariableW(L"KUF_REPLAY_MODE",argv[4]);
    SetEnvironmentVariableW(L"KUF_REPLAY_INPUT",argv[5]);
    std::wstring eventName=L"Local\\KUFReplayReady_"+std::to_wstring(GetCurrentProcessId());
    SetEnvironmentVariableW(L"KUF_REPLAY_READY",eventName.c_str());
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    std::wstring game=argv[1],cmd=L"\""+game+L"\"",directory=game.substr(0,game.find_last_of(L"\\/"));
    STARTUPINFOW start{};start.cb=sizeof(start); PROCESS_INFORMATION pi{};
    if(!CreateProcessW(game.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,directory.c_str(),&start,&pi)) {
        fwprintf(stderr,L"CreateProcess failed: %lu\n",GetLastError());CloseHandle(ready);return 5;
    }
    const SIZE_T length=(wcslen(argv[2])+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(pi.hProcess,nullptr,length,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    bool ok=remote && WriteProcessMemory(pi.hProcess,remote,argv[2],length,nullptr);
    HANDLE thread=nullptr;
    if(ok) thread=CreateRemoteThread(pi.hProcess,nullptr,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote,0,nullptr);
    ok=thread && WaitForSingleObject(thread,15000)==WAIT_OBJECT_0;
    DWORD loaded=0;
    if(ok) {GetExitCodeThread(thread,&loaded);ok=loaded!=0;}
    if(thread) CloseHandle(thread);
    if(remote && ok) VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);
    if(ok) ok=WaitForSingleObject(ready,15000)==WAIT_OBJECT_0;
    if(ok) {
        std::wstring status=std::wstring(argv[3])+L"\\engine.ready";
        HANDLE marker=CreateFileW(status.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        char value=0;DWORD n=0;
        ok=marker!=INVALID_HANDLE_VALUE && ReadFile(marker,&value,1,&n,nullptr) && n==1 && value=='1';
        if(marker!=INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    if(ok) {
        ResumeThread(pi.hThread);
        startWatcher(argv[1],pi.dwProcessId,argv[3]);
        printf("%lu\n",pi.dwProcessId);
    } else {
        // This process has never run user/game code; terminate only this failed child.
        TerminateProcess(pi.hProcess,6);
        fwprintf(stderr,L"Engine initialization failed. Check engine.log. Original game files preserved.\n");
    }
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(ready);
    return ok?0:6;
}
