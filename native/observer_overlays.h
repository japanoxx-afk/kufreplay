// Observer-only overlays for the verified KUF image; no simulation writes.
struct ObserverSprite {alignas(16) unsigned char bytes[0x98]{};void* resource=nullptr;void* context=nullptr;};
struct ObserverHero {int index,owner,type,hp,maxHp,mp,maxMp,level;unsigned char* object;};
struct ObserverHeroHit {RECT bounds;int index,owner,type;unsigned char* object;};
ObserverHeroHit observerHeroHits[48]{};unsigned observerHeroHitCount=0;
struct ObserverUpgrade {
    unsigned char* object=nullptr;int index=-1,owner=-1,research=-1,icon=-1;
    uint32_t remaining=0,total=0,completedAt=0;bool completed=false;
};
struct ObserverColoredLine {char text[192]{};uint16_t color=0;};
bool showHeroPanel=true;
ObserverHero observerHeroes[48]{};unsigned observerHeroCount=0,observerHeroOverflow=0;
ObserverSprite observerHeroSprites[48]{},observerUpgradeSprites[16]{};
ObserverUpgrade observerUpgrades[64]{};
ObserverColoredLine observerColoredLines[192]{};unsigned observerColoredLineCount=0;
uint32_t overlayRefreshAt=0;
Process researchProgress=nullptr;
std::atomic<uint32_t> observerUpgradeWarnings{0},observerUpgradeCompletions{0};
const char* observerHeroName(int type) {
    // Verified type-indexed portrait catalogue b3e8e0, stride 40, and hero RTTI.
    switch(type) {
    case 29:return "Moonlight";case 30:return "Curian";case 31:return "Keither";
    case 32:return "Likuku";case 33:return "Rick Blood";case 34:return "Amarua";
    case 35:return "Richter";case 105:return "Russelaunt";case 106:return "Cellin";
    case 107:return "Lily";case 108:return "Lauriana";case 111:return "Gernot";
    default:return nullptr;
    }
}
void resetObserverExtras() {
    observerHeroCount=0;observerHeroOverflow=0;overlayRefreshAt=0;
    observerHeroHitCount=0;
    // Sprites only borrow native file/context objects and own no heap resource.
    for(auto& sprite:observerHeroSprites) sprite={};
    for(auto& sprite:observerUpgradeSprites) sprite={};
    for(auto& upgrade:observerUpgrades) upgrade={};
    observerColoredLineCount=0;
}
bool observerExtraColor(const char* text,uint16_t& color) {
    for(unsigned i=0;i<observerColoredLineCount;++i) if(!strcmp(text,observerColoredLines[i].text)) {
        color=observerColoredLines[i].color;return true;
    }
    return false;
}
void observerTintedText(void* game,int x,int y,const char* text,uint16_t color) {
    if(observerColoredLineCount>=192) {observerText(game,x,y,text);return;}
    auto& line=observerColoredLines[observerColoredLineCount++];line.color=color;
    sprintf_s(line.text,"\\c%04X",color);unsigned n=6;
    for(unsigned i=0;text[i] && n+3<sizeof(line.text);++i) {
        if(text[i]=='\\') line.text[n++]='\\';
        line.text[n++]=text[i];
    }
    line.text[n]=0;observerText(game,x,y,line.text);
}
bool drawObserverSprite(void* game,ObserverSprite& sprite,void* resource,int frame,int x,int y) {
    if(!resource || frame<0 || frame>=2048) return false;
    void* context=*(void**)((unsigned char*)game+0x99c);
    if(!context) return false;
    using Init=void*(__thiscall*)(void*);
    using Bind=uint32_t(__thiscall*)(void*,void*,void*,uint32_t);
    using Draw=uint32_t(__thiscall*)(void*,uint32_t,uint32_t);
    if(sprite.resource!=resource || sprite.context!=context) {
        ((Init)address(0x473840))(sprite.bytes);
        ((Bind)address(0x474050))(sprite.bytes,resource,context,0);
        sprite.resource=resource;sprite.context=context;
    }
    // Resource frames are selected exactly as by the native information HUD.
    ((SpriteFrame)address(0x475930))(sprite.bytes,frame,0);
    // 475330 subtracts the frame's hotspot (+68/+6c virtual methods).
    // Treat overlay coordinates as visible top-left, independent of asset origin.
    using Hotspot=int(__thiscall*)(void*,int,uint32_t);
    auto image=*(void**)(sprite.bytes+0x78);if(!image) return false;
    auto methods=*(void***)image;int selected=*(int16_t*)(sprite.bytes+0x18);
    int originX=((Hotspot)methods[0x68/4])(image,selected,0);
    int originY=((Hotspot)methods[0x6c/4])(image,selected,0);
    if(sprite.bytes[0x14]&0x10) {
        using FrameWidth=int(__thiscall*)(void*,int);
        originX=((FrameWidth)methods[0x60/4])(image,selected)-originX;
    }
    if(originX < -8192 || originX > 8192 || originY < -8192 || originY > 8192 ||
       x+originX < -32768 || x+originX > 32767 || y+originY < -32768 || y+originY > 32767) return false;
    *(int16_t*)(sprite.bytes+0xc)=(int16_t)(x+originX);*(int16_t*)(sprite.bytes+0xe)=(int16_t)(y+originY);
    *(int16_t*)(sprite.bytes+0x1c)=0;*(int*)(sprite.bytes+0x28)=0x7ffffff0;
    ((Draw)address(0x475330))(sprite.bytes,0,0);return true;
}
void refreshObserverHeroes() {
    uint32_t tick=at<uint32_t>(0xb8a280);
    if(observerHeroCount && tick-overlayRefreshAt<8) return;
    overlayRefreshAt=tick;observerHeroCount=0;observerHeroOverflow=0;
    auto manager=(unsigned char*)at<void*>(0xb870c0);if(!manager) return;
    uint32_t count=*(uint32_t*)(manager+0xdc38),index=*(uint32_t*)(manager+0xdc3c);
    if(count>11264) return;
    // Player order is stable even if the manager's unit list is rearranged.
    for(uint32_t i=0;i<count && index<11264;++i) {
        auto object=observerUnit((int)index);
        if(object && *(int16_t*)(object+6)==0 && liveObserverUnit(object)) {
            int type=*(int16_t*)(object+4),owner=*(int16_t*)(object+8);
            if(observerHeroName(type) && owner>=0 && owner<8 && observerPlayers[owner].participant) {
                if(observerHeroCount<48) {
                    // Native MP getters 48eb40 and 48eba0 use +2fc/+300.
                    int mp=(*(uint32_t*)(object+0x3f8)&8)?0:*(int*)(object+0x2fc);
                    // Unit vtable+d4 getter 48eb30 reads signed WORD +1a8.
                    // +35c belongs to the information panel's cached level,
                    // not to this unit. The live level includes its base level.
                    observerHeroes[observerHeroCount++]={(int)index,owner,type,*(uint16_t*)(object+0x14),*(uint16_t*)(object+0x16),std::max(0,mp),std::max(0,*(int*)(object+0x300)),std::max(0,(int)*(int16_t*)(object+0x1a8)),object};
                } else ++observerHeroOverflow;
            }
        }
        index=*(uint32_t*)(manager+0xdc4c+index*12);
    }
    std::sort(observerHeroes,observerHeroes+observerHeroCount,[](const ObserverHero& a,const ObserverHero& b){return a.owner!=b.owner?a.owner<b.owner:a.type!=b.type?a.type<b.type:a.index<b.index;});
}
bool clickObserverHero(int x,int y) {
    if(!playback || !active || terminal.load() || !showObserverHud || !showHeroPanel) return false;
    POINT point{x,y};
    for(unsigned i=0;i<observerHeroHitCount;++i) {
        const auto& hit=observerHeroHits[i];if(!PtInRect(&hit.bounds,point)) continue;
        auto unit=observerUnit(hit.index);
        // A removed hero or recycled manager slot must never track another unit.
        if(unit==hit.object && liveObserverUnit(unit) && *(int16_t*)(unit+8)==hit.owner &&
           *(int16_t*)(unit+4)==hit.type && observerHeroName(hit.type)) {
            observerSelected=hit.index;
            stopObserverTracking();toggleObserverTracking();
            auto game=at<void*>(0xb8282c);if(game) updateObserverTracking(game);
        }
        return true;
    }
    return false;
}
bool clickObserverHeroClient(int x,int y) {
    RECT client{};if(!GetClientRect(gameWindow,&client) || client.right<=0 || client.bottom<=0) return false;
    return clickObserverHero((int)((int64_t)x*at<int>(0xbf2578)/client.right),
                            (int)((int64_t)y*at<int>(0xbf257c)/client.bottom));
}
int observerObjectIndex(unsigned char* object) {
    auto manager=(unsigned char*)at<void*>(0xb870c0);if(!manager) return -1;
    uint32_t count=*(uint32_t*)(manager+0xdc38),index=*(uint32_t*)(manager+0xdc3c);
    if(count>11264) return -1;
    for(uint32_t i=0;i<count && index<11264;++i) {
        if(((unsigned char**)(manager+0x2c38))[index]==object) return (int)index;
        index=*(uint32_t*)(manager+0xdc4c+index*12);
    }
    return -1;
}
int observerResearchIcon(unsigned char* object,int research) {
    // Native 514413..51448b: building command indices -> command record icon.
    int type=*(int16_t*)(object+4);if(type<0 || type>=116) return -1;
    auto definitions=(unsigned char*)at<void*>(0xb870f4);
    auto commands=(unsigned char*)at<void*>(0xb82824);if(!definitions || !commands) return -1;
    auto definition=definitions+type*0xaf8;int count=*(int*)(definition+0xa9c);
    if(count<0 || count>20) return -1;
    for(int i=0;i<count;++i) {
        int command=*(int*)(definition+0xaa8+i*4);if(command<0 || command>=4096) continue;
        auto data=commands+command*0x200;
        if(*(int*)(data+8)==500 && *(int*)(data+12)==research) return *(int*)(data+4);
    }
    return -1;
}
bool observerUpgradeNearEnd(int remaining,uint32_t interval) {
    return remaining>=0 && interval>0 && interval<=1000 && (uint64_t)remaining*interval<=5000;
}
void observeResearch(unsigned char* object,int research,int beforePhase,int beforeRemaining,uint32_t completed) {
    if(!playback || !active || failed.load() || GetCurrentThreadId()!=producer) return;
    int owner=*(int16_t*)(object+8);
    if(owner<0 || owner>=8 || research<0 || research>=148) return;
    int phase=*(int16_t*)(object+0x1488),remaining=*(int16_t*)(object+0x1484);
    bool done=completed && beforePhase==1 && beforeRemaining<=1 && phase==0 && remaining<=0;
    uint32_t interval=at<uint32_t>(0xb3bae8),tick=at<uint32_t>(0xb8a280);
    ObserverUpgrade* slot=nullptr;
    for(auto& candidate:observerUpgrades) if(candidate.object==object && candidate.research==research && !candidate.completed) {slot=&candidate;break;}
    if(!done && (phase!=1 || !observerUpgradeNearEnd(remaining,interval))) {
        if(slot) *slot={};return;
    }
    if(!slot) for(auto& candidate:observerUpgrades)
        if(!candidate.object || (candidate.completed && (uint64_t)(tick-candidate.completedAt)*interval>2500)) {slot=&candidate;break;}
    if(!slot) return;
    if(!slot->object) {
        slot->index=observerObjectIndex(object);if(slot->index<0) return;
        slot->object=object;slot->owner=owner;slot->research=research;
        slot->icon=observerResearchIcon(object,research);
        observerUpgradeWarnings.fetch_add(1,std::memory_order_relaxed);
    }
    slot->remaining=(uint32_t)std::max(0,remaining);slot->total=*(uint16_t*)(object+0x1486);
    if(done) {slot->completed=true;slot->completedAt=tick;observerUpgradeCompletions.fetch_add(1,std::memory_order_relaxed);}
}
uint32_t __fastcall researchProgressHook(void* self,void*) {
    auto object=(unsigned char*)self;
    int research=*(int16_t*)(object+0x12e),phase=*(int16_t*)(object+0x1488),remaining=*(int16_t*)(object+0x1484);
    uint32_t result=researchProgress(self);
    observeResearch(object,research,phase,remaining,result);return result;
}
void drawObserverExtras(void* game) {
    observerColoredLineCount=0;
    observerHeroHitCount=0;
    if(!showObserverHud || !active || !playback || terminal.load()) return;
    auto resources=(unsigned char*)at<void*>(0xb8280c);if(!resources) return;
    uint32_t interval=at<uint32_t>(0xb3bae8),tick=at<uint32_t>(0xb8a280);
    if(showHeroPanel) {
        refreshObserverHeroes();
        int height=at<int>(0xbf258c);
        int rows=std::max(1,(height-160)/80);
        // Reserve the right edge for upgrade notifications.
        int columns=std::max(1,(at<int>(0xbf2588)-300)/240);
        unsigned limit=std::min(observerHeroCount,(unsigned)(rows*columns));
        int bottom=(observerHeroCount>limit || observerHeroOverflow)?32:12;
        for(unsigned i=0;i<limit;++i) {
            unsigned column=i/rows;
            int columnRows=std::min((unsigned)rows,limit-column*rows);
            auto& hero=observerHeroes[i];
            int x=12+column*240,y=height-bottom-columnRows*80+(i%rows)*80;
            void* portrait=*(void**)(resources+0x14+hero.type*4);
            // A 54px portrait is vertically centered beside four 16px text rows.
            if(drawObserverSprite(game,observerHeroSprites[i],portrait,0,x,y+5)) {
                observerHeroHits[observerHeroHitCount++]={{x,y+5,x+54,y+59},hero.index,hero.owner,hero.type,hero.object};
            }
            char fallback[16],name[64],line[100];observerColoredName(observerName(hero.owner,fallback),observerPlayers[hero.owner].color,name);
            observerText(game,x+62,y,name);
            sprintf_s(line,"%s Lv.%d",observerHeroName(hero.type),hero.level);observerText(game,x+62,y+16,line);
            sprintf_s(line,"HP %d/%d",hero.hp,hero.maxHp);observerText(game,x+62,y+32,line);
            sprintf_s(line,"MP %d/%d",hero.mp,hero.maxMp);observerText(game,x+62,y+48,line);
        }
        if(observerHeroCount>limit || observerHeroOverflow) {char line[64];sprintf_s(line,"+%u heroes",observerHeroCount-limit+observerHeroOverflow);observerText(game,12,at<int>(0xbf258c)-20,line);}
    }
    unsigned shown=0;int y=at<int>(0xbf258c)-76;
    for(auto& upgrade:observerUpgrades) {
        if(!upgrade.object) continue;
        uint64_t age=(uint64_t)(tick-upgrade.completedAt)*interval;
        if(upgrade.completed) {if(age>2500) {upgrade={};continue;}}
        else {
            auto object=observerUnit(upgrade.index);
            if(object!=upgrade.object || !liveObserverUnit(object) || *(uint16_t*)(object+0xc)!=0x2904 || *(int16_t*)(object+0x12e)!=upgrade.research || *(int16_t*)(object+0x1488)!=1) {upgrade={};continue;}
            upgrade.remaining=*(uint16_t*)(object+0x1484);
        }
        if(shown>=16 || y<150) continue;
        int bounce=upgrade.completed && age<450?(int)((450-age)/90):0;
        char fallback[16],name[64],line[80];observerColoredName(observerName(upgrade.owner,fallback),observerPlayers[upgrade.owner].color,name);
        if(upgrade.completed) strcpy_s(line,"COMPLETE");
        else {uint32_t ms=upgrade.remaining*interval;sprintf_s(line,"Upgrade %u.%us",ms/1000,(ms%1000)/100);}
        char visible[20];unsigned nameWidth=observerDisplayName(observerName(upgrade.owner,fallback),visible);
        int textWidth=(int)std::max(nameWidth,(unsigned)strlen(line))*8;
        int x=std::max(12,at<int>(0xbf2588)-40-56-textWidth);
        drawObserverSprite(game,observerUpgradeSprites[shown],*(void**)resources,upgrade.icon,x,y-bounce);
        observerText(game,x+56,y+8,name);
        if(upgrade.completed) {
            bool bright=age<900 && (age/120)%2==0;
            uint16_t color=at<int>(0xbf281c)==0x8000?(bright?0x7fff:0x3fe0):(bright?0xffff:0x7fe0);
            observerTintedText(game,x+56,y+26,line,color);
        } else {
            observerText(game,x+56,y+26,line);
        }
        y-=64;++shown;
    }
}
