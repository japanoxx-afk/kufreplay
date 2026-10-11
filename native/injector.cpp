#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdio>
#pragma comment(lib, "bcrypt.lib")

static bool knownGame(const wchar_t* path) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; GetFileSizeEx(f,&size);
    if(size.QuadPart!=8352688) { CloseHandle(f); return false; }
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
    return ok && !memcmp(expected,result,32);
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

int wmain(int argc,wchar_t** argv) {
    // Only the launcher supplies these paths. Never loads a DLL contained in a replay.
    if(argc!=6) {fwprintf(stderr,L"Usage: injector GAME DLL ROOT record|play INPUT\n");return 2;}
    if(!knownGame(argv[1])) {fwprintf(stderr,L"Unsupported game SHA256. No process was patched.\n");return 3;}
    if(gameRunning()) {fwprintf(stderr,L"Close the running game before starting a recorded session.\n");return 4;}
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
        printf("%lu\n",pi.dwProcessId);
    } else {
        // This process has never run user/game code; terminate only this failed child.
        TerminateProcess(pi.hProcess,6);
        fwprintf(stderr,L"Engine initialization failed. Check engine.log. Original game files preserved.\n");
    }
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(ready);
    return ok?0:6;
}
