// Exercises the real adapter callbacks against a synthetic ABI-compatible fixture.
// This is not a KUF game and is never packaged as a user replay.
#include "engine.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
uint32_t resource=100,queueCount=0,applied=0;
uint32_t __fastcall fakeChecksum(void*,void*) {return resource+at<uint32_t>(0xbed1e8)+at<uint32_t>(0xb827dc);}
uint32_t __fastcall fakeApply(void*,void*,uint32_t n,const void* bytes) {
    if(n!=4) return 0;
    uint32_t value;memcpy(&value,bytes,4);
    resource+=value;
    at<uint32_t>(0xbed1e8)=at<uint32_t>(0xbed1e8)*0x8088405+1;
    ++at<uint32_t>(0xb827dc);++applied;return 1;
}
uint32_t __fastcall fakeAdvance(void* self,void*,uint32_t,uint32_t) {
    const uint32_t commands[]={5,9};
    for(auto command:commands) commandHook(self,nullptr,4,&command);
    return 1;
}
uint32_t __fastcall fakeCount(void*,void*) {return queueCount;}
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
    base=(uintptr_t)VirtualAlloc(nullptr,0x882000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    require(base!=0,"fixture memory");
    textBase=base;textSize=0x882000;expectedText.resize(textSize);
    ring=(unsigned char*)VirtualAlloc(nullptr,RingSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(ring!=nullptr,"ring memory");
    // The checksum call target is a five-byte tail jump to a fixture function with the same ECX ABI.
    auto code=(unsigned char*)address(0x4dae50);code[0]=0xe9;
    int32_t delta=(int32_t)((uintptr_t)fakeChecksum-(uintptr_t)code-5);memcpy(code+1,&delta,4);
    int self=1;
    at<void*>(0xb870c0)=&self;
    strcpy_s((char*)address(0xb83ad9),256,"res\\map\\Skirmish\\");
    strcpy_s((char*)address(0xb83bd9),256,"fixture.kfm");
    void* vtable[5]{};vtable[2]=(void*)fakePop;vtable[4]=(void*)fakeCount;
    void** queue=vtable;at<void*>(0xb870c8)=&queue;
    apply=reinterpret_cast<Apply>(fakeApply);advance=reinterpret_cast<Advance>(fakeAdvance);
    resetState();playback=false;
    advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);simulate();finish(&self);
    require(!failed.load() && applied==4,"capture two real callback batches");
    input.assign(Magic,Magic+8);
    uint32_t size=head.load();size_t begin=input.size();input.resize(begin+size);readRing(0,input.data()+begin,size);
    resetState();playback=true;queueCount=3;
    advanceHook(&self,nullptr,0,0);require(!failed.load() && queueCount==0,"discard spectator/live commands");
    simulate();advanceHook(&self,nullptr,0,0);simulate();advanceHook(&self,nullptr,0,0);
    require(terminal.load() && !failed.load() && notice.load()==10 && applied==4 && resource==134,"identical state and completed replay");
    resetState();at<uint32_t>(0xbed1e8)=9;advanceHook(&self,nullptr,0,0);
    require(failed.load() && notice.load()==7 && applied==0,"reject mismatched initial RNG before injection");
    resetState();advanceHook(&self,nullptr,0,0);simulate();++resource;advanceHook(&self,nullptr,0,0);
    require(failed.load() && notice.load()==8 && applied==2,"detect later simulation divergence");
    resetState();playback=false;producer=GetCurrentThreadId();tail.store(0);head.store(RingSize-1);
    State s{};emit(Before,&s,sizeof(s));require(failed.load() && notice.load()==2,"bounded ring overflow fails without blocking");
    resetState();head.store(0);tail.store(0);at<void*>(0xb8282c)=&self;
    advanceHook(&self,nullptr,0,0);
    uint32_t beforeClose=head.load();
    require(recordingWindowProc(nullptr,WM_CLOSE,0,0)==0 && closePending.load() && !active,"normal close queues match ending without blocking");
    require(head.load()==beforeClose+sizeof(Record)+sizeof(State),"exactly one close end record");
    uint32_t afterClose=head.load();
    require(recordingWindowProc(nullptr,WM_CLOSE,0,0)==0 && head.load()==afterClose,"repeated close waits for writer acknowledgement");
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
