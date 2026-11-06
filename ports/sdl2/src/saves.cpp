#include "dok/saves.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <type_traits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace dok {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static_assert(sizeof(unsigned)==4&&sizeof(int)==4&&sizeof(float)==4,"Save codec requires 32-bit int/float");
constexpr std::size_t max_file=16*1024*1024;
struct Writer;struct Reader;
template<class A,class T> void fields(A&,T&);
struct Writer {
    std::vector<std::uint8_t> data;
    template<class T> void operator()(T& v){
        if constexpr(std::is_same_v<T,bool>)data.push_back(v?1:0);
        else if constexpr(std::is_enum_v<T>){auto x=static_cast<std::uint32_t>(v);(*this)(x);}
        else if constexpr(std::is_integral_v<T>){using U=std::make_unsigned_t<T>;auto x=static_cast<U>(v);for(unsigned i=0;i<sizeof(T);++i)data.push_back(std::uint8_t(x>>(i*8)));}
        else if constexpr(std::is_same_v<T,float>){std::uint32_t x;static_assert(sizeof(v)==4);std::memcpy(&x,&v,4);(*this)(x);}
        else fields(*this,v);
    }
    template<class T> void operator()(const T& v){auto copy=v;(*this)(copy);}
    template<class T> void index(T& v){auto x=std::uint64_t(v);(*this)(x);}
    template<class T> void operator()(std::vector<T>& v){auto n=std::uint32_t(v.size());(*this)(n);for(auto& x:v)(*this)(x);}
    template<class T,std::size_t N> void operator()(std::array<T,N>& v){for(auto& x:v)(*this)(x);}
    template<class T> void operator()(std::optional<T>& v){bool has=bool(v);(*this)(has);if(has)(*this)(*v);}
    void operator()(std::string& v){auto n=std::uint32_t(v.size());(*this)(n);data.insert(data.end(),v.begin(),v.end());}
    template<class... T> void all(T&... v){((*this)(v),...);}
};
struct Reader {
    const std::vector<std::uint8_t>& data;std::size_t at=0;
    template<class T> void operator()(T& v){
        if constexpr(std::is_same_v<T,bool>){std::uint8_t x;(*this)(x);require(x<=1,"Invalid boolean");v=x!=0;}
        else if constexpr(std::is_enum_v<T>){std::uint32_t x;(*this)(x);v=static_cast<T>(x);}
        else if constexpr(std::is_integral_v<T>){require(sizeof(T)<=data.size()-at,"Truncated save");using U=std::make_unsigned_t<T>;U x=0;for(unsigned i=0;i<sizeof(T);++i)x|=U(data[at++])<<(8*i);std::memcpy(&v,&x,sizeof(T));}
        else if constexpr(std::is_same_v<T,float>){std::uint32_t x;(*this)(x);std::memcpy(&v,&x,4);require(std::isfinite(v),"Invalid floating point value");}
        else fields(*this,v);
    }
    template<class T> void index(T& v){std::uint64_t x;(*this)(x);require(x<=std::numeric_limits<T>::max(),"Index overflow");v=static_cast<T>(x);}
    template<class T> void operator()(std::vector<T>& v){std::uint32_t n;(*this)(n);require(n<=65536&&n<=data.size()-at,"Invalid save count");v.resize(n);for(auto& x:v)(*this)(x);}
    template<class T,std::size_t N> void operator()(std::array<T,N>& v){for(auto& x:v)(*this)(x);}
    template<class T> void operator()(std::optional<T>& v){bool has;(*this)(has);if(has){v.emplace();(*this)(*v);}else v.reset();}
    void operator()(std::string& v){std::uint32_t n;(*this)(n);require(n<=65536&&n<=data.size()-at,"Invalid save string");v.assign(reinterpret_cast<const char*>(data.data()+at),n);at+=n;}
    template<class... T> void all(T&... v){((*this)(v),...);}
};
template<class A> void fields(A& a,Pose& v){a.all(v.x,v.y,v.z,v.heading);}
template<class A> void fields(A& a,GameClock::State& v){a.all(v.ticks,v.remainder);}
template<class A> void fields(A& a,Stopwatch::State& v){a.all(v.started,v.elapsed,v.running);}
template<class A> void fields(A& a,Stopwatch& v){auto s=v.snapshot();a(s);if constexpr(std::is_same_v<A,Reader>)v.restore(s);}
template<class A> void fields(A& a,AnimationState& v){a.all(v.frame,v.parameter_a,v.parameter_b,v.interval,v.remaining);}
template<class A> void fields(A& a,SwitchState& v){a.all(v.native_54,v.native_56,v.native_58,v.interval);}
template<class A> void fields(A& a,SurfaceState& v){a.all(v.phase,v.interval);}
template<class A> void fields(A& a,MonsterState& v){a.all(v.health,v.state,v.next_frame,v.display_frame,v.display_clip,v.course_counter,v.shot_count,v.burst_clock,v.corpse,v.passage_released);}
template<class A> void fields(A& a,WeaponState& v){a.all(v.state,v.shift,v.frame,v.acquired);}
template<class A> void fields(A& a,DoorState& v){a.all(v.phase,v.extent,v.interval,v.saved_flags,v.state,v.ready);}
template<class A> void fields(A& a,PlayerState& v){a.all(v.attributes,v.extra_attributes,v.weapons,v.current_weapon,v.pending_weapon,v.lives,v.score,v.native_da);}
template<class A> void fields(A& a,MotionState& v){a.all(v.clock,v.acceleration,v.turn_acceleration,v.turn_direction,v.bob_direction,v.last_forward,v.last_strafe,v.mouse_remainder);}
template<class A> void fields(A& a,WorldObject& v){a.index(v.definition_index);a.all(v.pose,v.placements,v.owner,v.player_state,v.flags,v.door_occupants,v.motion,v.door,v.weapon,v.monster,v.animation,v.switch_state,v.surface);}
template<class A> void fields(A& a,ProjectileState& v){a(v.pose);a.index(v.definition_index);a.all(v.source,v.damage,v.damage_floor,v.frame,v.speed,v.flags,v.impact,v.actor_impact,v.effect_frame);bool has=bool(v.occupied_cell);a(has);std::size_t cell=v.occupied_cell.value_or(0);if(has)a.index(cell);if constexpr(std::is_same_v<A,Reader>)v.occupied_cell=has?std::optional<std::size_t>(cell):std::nullopt;a(v.clock);}
template<class A> void fields(A& a,Event& v){a.all(v.deadline,v.interval,v.callback,v.parameter);}
template<class A> void fields(A& a,Events::OwnerState& v){a.all(v.id,v.capacity,v.slots);}
template<class A> void fields(A& a,Events::Snapshot& v){a.all(v.owners,v.ring,v.cursor);}
template<class A> void fields(A& a,VoiceSnapshot& v){a.all(v.id,v.loop,v.old_gold,v.output_rate,v.phase);}
template<class A> void fields(A& a,SaveAudio& v){a.all(v.voice,v.resource);}
template<class A> void fields(A& a,GameFlow::Snapshot& v){a.all(v.spawn,v.phase);}
template<class A> void fields(A& a,SaveMetadata& v){a.all(v.dataset,v.level,v.name,v.timestamp,v.ticks,v.health,v.lives,v.fingerprint,v.preview_width,v.preview_height);
    if constexpr(std::is_same_v<A,Writer>){auto n=std::uint32_t(v.preview.size());a(n);for(auto c:v.preview){auto r=std::uint8_t(c>>16),g=std::uint8_t(c>>8),b=std::uint8_t(c);a.all(r,g,b);}}
    else {std::uint32_t n;a(n);require(v.preview_width<=320&&v.preview_height<=200&&n==v.preview_width*v.preview_height,"Invalid preview");v.preview.resize(n);for(auto& c:v.preview){std::uint8_t r,g,b;a.all(r,g,b);c=0xff000000u|(std::uint32_t(r)<<16)|(std::uint32_t(g)<<8)|b;}}}
template<class A> void world_fields(A& a,World& w){
    auto clock=w.clock_snapshot();auto ticks=w.simulation_ticks();a.all(clock,ticks);if constexpr(std::is_same_v<A,Reader>)w.restore_time(clock,ticks);
    if constexpr(std::is_same_v<A,Writer>){auto n=std::uint32_t(w.maps.size());a(n);for(auto pair:w.maps){a.all(pair.first,pair.second);}}
    else {std::uint32_t n;a(n);require(n==w.maps.size(),"Invalid map count");std::set<std::string> seen;for(std::uint32_t i=0;i<n;++i){std::string key;std::vector<std::uint8_t> data;a.all(key,data);require(w.maps.count(key)&&seen.insert(key).second&&data.size()==w.maps.at(key).size(),"Invalid map");w.maps.at(key)=std::move(data);}}
    if constexpr(std::is_same_v<A,Reader>){
        a(w.visited);std::uint32_t count=0;a(count);require(count==w.definition.objects.size(),"Invalid object count");w.objects.resize(count);for(auto& object:w.objects)a(object);a.all(w.projectiles,w.projectile_occupancy);
    }else a.all(w.visited,w.objects,w.projectiles,w.projectile_occupancy);
    bool has=bool(w.player);a(has);std::size_t player=w.player.value_or(0);if(has)a.index(player);if constexpr(std::is_same_v<A,Reader>)w.player=has?std::optional<std::size_t>(player):std::nullopt;
    a.all(w.total_money,w.map_revealed,w.level_complete,w.message,w.message_key,w.message_until);
    auto events=w.events.snapshot();a(events);if constexpr(std::is_same_v<A,Reader>){
        // Validate allocation bounds before Events::restore reserves capacity.
        // A tiny checksummed file must not request gigabytes through empty owners.
        require(events.owners.size()<=w.definition.objects.size()+256,"Too many event owners");std::uint64_t capacity=0;
        for(const auto& owner:events.owners){require((owner.id>0&&owner.id<=w.definition.objects.size())||(owner.id>=0x10000&&owner.id<0x10100),"Invalid event owner ID");require(owner.capacity>=1&&owner.capacity<=32767&&owner.slots.size()<owner.capacity,"Invalid event capacity");capacity+=owner.capacity;require(capacity<=65536,"Excessive event capacity");}
        w.events.restore(events);
    }
}
void replace_file(const std::filesystem::path& from,const std::filesystem::path& to){
#ifdef _WIN32
    require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot replace save");
#else
    std::filesystem::rename(from,to);
    const int fd=open(to.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);close(fd);}
#endif
}
void write_bytes(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){
    {std::ofstream f(path,std::ios::binary|std::ios::trunc);require(bool(f),"Cannot open save for writing");f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.flush();require(bool(f),"Cannot write save");f.close();require(!f.fail(),"Cannot close save");}
#ifdef _WIN32
    auto h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(h!=INVALID_HANDLE_VALUE,"Cannot flush save");const auto ok=FlushFileBuffers(h);CloseHandle(h);require(ok!=0,"Cannot flush save");
#else
    const int fd=open(path.c_str(),O_WRONLY);require(fd>=0,"Cannot flush save");const int ok=fsync(fd);close(fd);require(ok==0,"Cannot flush save");
#endif
}
std::vector<std::uint8_t> payload(const std::vector<std::uint8_t>& bytes){
    require(bytes.size()>=44&&bytes.size()<=max_file,"Invalid save size");
    require(std::equal(bytes.begin(),bytes.begin()+4,"DKSV"),"Unknown save format");
    std::vector<std::uint8_t> body(bytes.begin(),bytes.end()-32);const auto hash=sha256(body);
    require(std::equal(hash.begin(),hash.end(),bytes.end()-32),"Save checksum mismatch");return body;
}
}
Digest sha256(const std::vector<std::uint8_t>& data){
    constexpr std::uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    auto bytes=data;const auto bits=std::uint64_t(bytes.size())*8;bytes.push_back(128);while(bytes.size()%64!=56)bytes.push_back(0);for(int i=7;i>=0;--i)bytes.push_back(std::uint8_t(bits>>(i*8)));
    std::uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rr=[](std::uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    for(std::size_t off=0;off<bytes.size();off+=64){std::uint32_t w[64]{};for(int i=0;i<16;++i)for(int j=0;j<4;++j)w[i]=(w[i]<<8)|bytes[off+i*4+j];for(int i=16;i<64;++i)w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];for(int i=0;i<64;++i){auto t=hh+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];auto u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));hh=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }Digest result{};for(int i=0;i<8;++i)for(int j=0;j<4;++j)result[i*4+j]=std::uint8_t(h[i]>>(24-j*8));return result;
}

Digest save_fingerprint(const Archive& archive,const std::string& level){
    auto d=load_level(archive,level);Writer a;
    a.all(d.dataset,d.name,d.palette,d.light,d.width,d.height,d.length,d.cell_side,d.cell_height,d.floor_color,d.ceiling_color,d.min_distance);
    for(auto pair:d.maps)a.all(pair.first,pair.second);
    std::set<std::pair<FieldKind,std::string>> resources;
    for(auto o:d.objects){a.all(o.glyph,o.marker,o.class_id,o.flags,o.sprite_flags,o.width_parameter,o.height_parameter,o.sprite,o.animation,o.sound);
        if(!o.sprite.empty())resources.emplace(FieldKind::image,o.sprite);if(!o.animation.empty())resources.emplace(FieldKind::animation,o.animation);if(!o.sound.empty())resources.emplace(FieldKind::sound,o.sound);
        for(auto field:o.fields){a.all(field.kind,field.offset,field.number,field.bytes);if((field.kind==FieldKind::image||field.kind==FieldKind::animation||field.kind==FieldKind::sound)&&!field.bytes.empty())resources.emplace(field.kind,field.bytes);}}
    auto image=[&](Image i){a.all(i.width,i.height,i.flags,i.pixels);};
    for(const auto& pair:resources){auto kind=pair.first;auto name=pair.second;a.all(kind,name);
        if(kind==FieldKind::image)image(archive.image(name));
        else if(kind==FieldKind::sound){auto sound=archive.sound(name);a.all(sound.frequency,sound.parameter_c,sound.parameter_d,sound.samples);}
        else {auto frames=archive.animation(name);auto count=std::uint32_t(frames.size());a(count);for(auto frame:frames){a.all(frame.interval,frame.parameter_a,frame.parameter_b);image(frame.image);}}}
    auto colors=archive.palette(d.palette);a(colors);auto light=archive.light(d.dataset==DatasetId::old_gold?"\x91\x82\x85\x92_\x97":d.light);a.data.insert(a.data.end(),light.data,light.data+light.size);
    return sha256(a.data);
}
namespace {
void validate_world(const World& w,const World& base,const Archive& archive,const GameFlow::Snapshot& flow){
    const auto& d=w.definition;const auto cells=std::size_t(d.width)*d.length;
    require(w.player==base.player&&w.objects.size()==base.objects.size()&&w.visited.size()==cells&&w.projectile_occupancy.size()==cells,"Invalid world dimensions/player");
    require(!w.level_complete&&flow.phase==SessionPhase::playing,"Save is not a live session");
    auto valid_pose=[&](Pose p){return p.x>=0&&p.z>=0&&p.x<d.width*d.cell_side&&p.z<d.length*d.cell_side&&p.heading>=0&&p.heading<360;};
    require(valid_pose(flow.spawn)&&flow.spawn.x==base.objects.at(*base.player).pose->x&&flow.spawn.y==base.objects.at(*base.player).pose->y&&flow.spawn.z==base.objects.at(*base.player).pose->z&&flow.spawn.heading==base.objects.at(*base.player).pose->heading,"Invalid respawn point");
    auto clip_size=[&](const std::string& name){return name.empty()?std::size_t(0):archive.animation(name).size();};
    for(std::size_t i=0;i<w.objects.size();++i){const auto& o=w.objects[i];const auto& b=base.objects[i];const auto& def=d.objects[i];
        require(o.owner==b.owner&&o.definition_index==i&&bool(o.player_state)==bool(b.player_state)&&bool(o.motion)==bool(b.motion)&&bool(o.door)==bool(b.door)&&bool(o.weapon)==bool(b.weapon)&&bool(o.monster)==bool(b.monster)&&bool(o.switch_state)==bool(b.switch_state)&&bool(o.surface)==bool(b.surface),"Invalid object identity/components");
        if(o.pose)require(valid_pose(*o.pose),"Invalid actor pose");
        if(o.player_state){const auto& p=*o.player_state;require(o.pose&&p.health()>0&&p.lives>0&&p.current_weapon>=0&&p.current_weapon<9&&p.pending_weapon>=0&&p.pending_weapon<9,"Invalid player state");for(auto id:p.weapons)require(!id||(id<=w.objects.size()&&w.objects[id-1].weapon.has_value()),"Invalid weapon reference");}
        if(o.weapon)require(o.weapon->frame>=0&&std::size_t(o.weapon->frame)<clip_size(def.animation)&&o.weapon->state>=0&&o.weapon->state<=7&&o.weapon->shift>=0&&o.weapon->shift<=3,"Invalid weapon animation");
        if(o.door)require(o.door->state<=2&&o.door->phase>=0&&o.door->extent>0&&o.door->phase<=o.door->extent,"Invalid door state");
        if(o.monster&&o.monster->state&&!o.monster->corpse)require(o.pose.has_value(),"Active monster has no pose");
        require(bool(o.animation)==bool((def.flags&0x800)&&def.class_id!=8&&def.class_id!=9&&def.class_id!=10),"Missing or unexpected base animation");
        if(o.animation)require((def.flags&0x800)&&def.class_id!=8&&def.class_id!=9&&def.class_id!=10&&o.animation->frame>=0&&std::size_t(o.animation->frame)<clip_size(def.animation),"Invalid base animation");
        if(o.monster){const auto& m=*o.monster;require(m.state>=0&&m.state<=4&&m.next_frame>=-1&&m.display_clip>=0&&m.display_clip<=2&&m.display_frame>=0,"Invalid monster clip");const auto name=m.display_clip==0?def.animation:def.resource(m.display_clip==1?0xe8:0xec);require(std::size_t(m.display_frame)<clip_size(name),"Invalid monster frame");}
    }
    for(const auto& pair:w.maps)for(auto glyph:pair.second)require(d.object(glyph)!=nullptr,"Invalid map glyph");
    for(auto v:w.visited)require(v<=1,"Invalid visited cell");
    for(unsigned slot=0;slot<w.projectiles.size();++slot)if(w.projectiles[slot]){const auto& p=*w.projectiles[slot];require(!p.effect_frame||p.impact,"Effect without impact");if(d.dataset==DatasetId::old_gold&&p.impact&&!p.effect_frame)require(p.frame==1,"Invalid OG contact cursor");require(p.definition_index<d.objects.size()&&d.objects[p.definition_index].class_id==8&&p.source>0&&p.source<=w.objects.size()&&p.frame>=0,"Invalid projectile");
        const auto& def=d.objects[p.definition_index];const auto name=!p.effect_frame?def.animation:d.dataset==DatasetId::old_gold?def.animation:def.resource(p.actor_impact?0x77:0x7b);require(std::size_t(p.frame)<clip_size(name),"Invalid projectile frame");
        if(p.occupied_cell)require(*p.occupied_cell<cells&&w.projectile_occupancy[*p.occupied_cell]==int(slot),"Invalid projectile cell");}
    for(std::size_t cell=0;cell<cells;++cell){int slot=w.projectile_occupancy[cell];require(slot>=-1&&slot<256,"Invalid occupancy slot");if(slot>=0)require(w.projectiles[slot]&&w.projectiles[slot]->occupied_cell==cell,"Broken occupancy reference");}
    const auto es=w.events.snapshot();std::set<OwnerId> owners;
    for(const auto& owner:es.owners){owners.insert(owner.id);const bool object=owner.id>0&&owner.id<=w.objects.size();const bool shot=owner.id>=0x10000&&owner.id<0x10100;require(object||shot,"Invalid event owner ID");
        for(auto event:owner.slots){require(event.callback>=1&&event.callback<=9,"Unknown callback ID");const auto cb=GameEvent(event.callback);
            if(shot)require(cb==GameEvent::projectile_tick&&w.projectiles[owner.id-0x10000].has_value(),"Invalid projectile callback");
            else {const auto& o=w.objects[owner.id-1];require((cb==GameEvent::player_tick&&o.player_state)||(cb==GameEvent::weapon_switch&&o.player_state)||(cb==GameEvent::weapon_tick&&o.weapon)||(cb==GameEvent::monster_tick&&o.monster)||(cb==GameEvent::base_animation&&o.animation)||((cb==GameEvent::door_open||cb==GameEvent::door_close||cb==GameEvent::door_auto)&&o.door),"Callback/component mismatch");}}}
    for(const auto& o:w.objects)require(owners.count(o.owner)!=0,"Missing object event owner");
}
SaveMetadata metadata_from(Reader& reader,std::uint32_t& rules){
    std::uint32_t format=0;reader.at=4;reader.all(format,rules);require(format==save_format_version&&(rules==1||rules==save_rules_version),"Incompatible save version");SaveMetadata meta;reader(meta);
    require(meta.dataset>=DatasetId::old_gold&&meta.dataset<=DatasetId::kremlin_pke&&meta.level.size()==6&&meta.level.substr(0,5)=="LEVEL"&&meta.name.size()<=240,"Invalid save metadata");return meta;
}
}
std::vector<std::uint8_t> encode_save(const World& world,const GameFlow::Snapshot& flow,const SaveAudio& sound,SaveMetadata metadata){
    require(flow.phase==SessionPhase::playing&&!world.level_complete&&world.objects.at(*world.player).player_state->health()>0,"Cannot save outside a live level");
    metadata.dataset=world.definition.dataset;metadata.level=world.definition.name;metadata.ticks=world.simulation_ticks();metadata.health=world.objects.at(*world.player).player_state->health();metadata.lives=world.objects.at(*world.player).player_state->lives;
    require(metadata.name.size()<=240&&metadata.preview_width<=320&&metadata.preview_height<=200&&metadata.preview.size()==std::size_t(metadata.preview_width)*metadata.preview_height,"Invalid save metadata/preview");
    Writer a;a.data={'D','K','S','V'};std::uint32_t format=save_format_version,rules=save_rules_version;a.all(format,rules,metadata);auto f=flow;auto audio=sound;a.all(f,audio);
    // Codec writes fields explicitly; const_cast does not mutate in Writer.
    world_fields(a,const_cast<World&>(world));require(a.data.size()+32<=max_file,"Save exceeds size limit");auto sum=sha256(a.data);a.data.insert(a.data.end(),sum.begin(),sum.end());return a.data;
}
SaveGame decode_save(const std::vector<std::uint8_t>& bytes,const Archive& archive){
    auto body=payload(bytes);Reader a{body};SaveGame result;std::uint32_t source_rules=0;result.metadata=metadata_from(a,source_rules);auto& meta=result.metadata;
    require(meta.dataset==archive.identify(),"Save belongs to another dataset");require(meta.fingerprint==save_fingerprint(archive,meta.level),"Incompatible game data");
    a.all(result.flow,result.audio);require(result.audio.resource.empty()||archive.find(result.audio.resource),"Missing sound resource");require(!result.audio.voice.id||(!result.audio.resource.empty()&&result.audio.voice.old_gold==(meta.dataset==DatasetId::old_gold)),"Invalid sound state");
    {MonoVoice validator;std::optional<Sound> sound;if(result.audio.voice.id)sound=archive.sound(result.audio.resource);require(result.audio.voice.id||result.audio.resource.empty(),"Invalid silent sound resource");require(validator.restore(result.audio.voice,sound?&*sound:nullptr),"Invalid logical sound state");}
    auto definition=load_level(archive,meta.level);World base(definition);result.world=std::make_unique<World>(std::move(definition));world_fields(a,*result.world);require(a.at==body.size(),"Trailing save data");
    // Published r2 saves use rules1. OG contact used frame0 for both the
    // animation cursor and retained flight image; the recovered native cursor
    // is1 while that image is still0. Preserve already displayed legacy effect
    // frames and continue them normally; never discard the user's projectile.
    if(source_rules==1&&meta.dataset==DatasetId::old_gold)for(auto& p:result.world->projectiles)
        if(p&&p->impact&&!p->effect_frame&&p->frame==0)p->frame=1;
    validate_world(*result.world,base,archive,result.flow);
    require(meta.ticks==result.world->simulation_ticks()&&meta.health==result.world->objects.at(*result.world->player).player_state->health()&&meta.lives==result.world->objects.at(*result.world->player).player_state->lives,"Inconsistent save metadata");return result;
}
std::filesystem::path save_path(const std::filesystem::path& profile,DatasetId id,int slot){
    require(id>=DatasetId::old_gold&&id<=DatasetId::kremlin_pke&&slot>=0&&slot<=10,"Invalid save slot");
    const char* folder=id==DatasetId::old_gold?"old_gold":id==DatasetId::kremlin_pkr?"pkr":"pke";
    const auto name=slot==0?std::string("quick.sav"):"slot"+std::string(slot<10?"0":"")+std::to_string(slot)+".sav";return profile/"saves"/folder/name;
}
std::vector<std::uint8_t> read_save_file(const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"Cannot open save");auto size=f.tellg();require(size>=0&&std::uint64_t(size)<=max_file,"Invalid save file size");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());require(bool(f),"Cannot read save");return bytes;
}
void write_save_file(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes,const Archive& archive){
    decode_save(bytes,archive);std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=".tmp";auto backup=path;backup+=".bak";auto backup_temp=path;backup_temp+=".bak.tmp";
    write_bytes(temp,bytes);require(read_save_file(temp)==bytes,"Save verification failed");
    if(std::filesystem::exists(path)){auto old=read_save_file(path);bool valid=false;try{decode_save(old,archive);valid=true;}catch(const std::exception&){}if(valid){write_bytes(backup_temp,old);require(read_save_file(backup_temp)==old,"Backup verification failed");replace_file(backup_temp,backup);}}
    replace_file(temp,path);
}
std::vector<SaveEntry> list_saves(const std::filesystem::path& profile,const Archive& archive){
    std::vector<SaveEntry> entries;
    for(int slot=0;slot<=10;++slot){SaveEntry entry;entry.slot=slot;const auto path=save_path(profile,archive.identify(),slot);
        std::error_code ec;entry.exists=std::filesystem::exists(path,ec);if(ec){entry.exists=true;entry.error=ec.message();}
        if(entry.exists)try{auto saved=decode_save(read_save_file(path),archive);entry.metadata=std::move(saved.metadata);entry.valid=true;}catch(const std::exception& e){entry.error=e.what();}
        if(!entry.valid){auto backup=path;backup+=".bak";if(std::filesystem::exists(backup,ec))try{auto saved=decode_save(read_save_file(backup),archive);entry.metadata=std::move(saved.metadata);entry.backup=entry.valid=entry.exists=true;}catch(const std::exception&){} }
        entries.push_back(std::move(entry));
    }return entries;
}
void delete_save(const std::filesystem::path& path){
    for(const auto* suffix:{".bak",".tmp",".bak.tmp",""}){auto file=path;file+=suffix;std::filesystem::remove(file);}
}
}
