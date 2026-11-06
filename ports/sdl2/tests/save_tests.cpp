#include "dok/saves.hpp"
#include "dok/animations.hpp"
#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/projectiles.hpp"
#include "dok/interactions.hpp"
#include <iostream>
#include <fstream>
using namespace dok;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string hex(Digest d){const char* digits="0123456789abcdef";std::string out;for(auto c:d){out+=digits[c>>4];out+=digits[c&15];}return out;}
int main(int argc,char** argv){try{
    check(hex(sha256({}))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA empty");
    check(hex(sha256({'a','b','c'}))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA abc");
    Events e;e.create(1,5);e.create(2);e.schedule(1,1,3,0);e.schedule(2,2,4,0);e.schedule(1,2,5,0);auto snapshot=e.snapshot();Events restored;restored.restore(snapshot);
    std::vector<std::pair<OwnerId,CallbackId>> left,right;for(std::uint32_t time=0;time<30;++time){e.dispatch([&]{return time;},[&](auto id,auto cb){left.emplace_back(id,cb);});restored.dispatch([&]{return time;},[&](auto id,auto cb){right.emplace_back(id,cb);});}check(left==right,"Event order continuation");
    bool rejected=false;snapshot.ring.push_back(snapshot.ring.front());try{restored.restore(snapshot);}catch(...){rejected=true;}check(rejected,"Duplicate event ring accepted");
    GameClock clock;clock.advance_nanoseconds(16666667);GameClock clock2;clock2.restore(clock.snapshot());clock.advance_nanoseconds(9999999);clock2.advance_nanoseconds(9999999);check(clock.ticks()==clock2.ticks()&&clock.remainder()==clock2.remainder(),"Clock remainder");
    Stopwatch sw;sw.start(0xfffffff0u);Stopwatch sw2;sw2.restore(sw.snapshot());check(sw.read(100)==sw2.read(100),"Stopwatch wrap");
    if(argc<3){std::cout<<"save primitives PASS\n";return 0;}
    const auto archive=Archive::open(std::filesystem::u8path(argv[1]));const std::string level=argv[2];World world(load_level(archive,level));Weapons weapons(world,archive);Projectiles shots(world,archive);Monsters monsters(world,archive,shots);Animations animations(world,archive);GameFlow flow(world);
    SaveMetadata meta;meta.name="Test";meta.fingerprint=save_fingerprint(archive,level);meta.preview_width=2;meta.preview_height=1;meta.preview={0xff123456,0xffabcdef};
    auto invoke=[&](World& w,Weapons& guns,Projectiles& projectiles,Monsters& actors,Animations& effects,OwnerId id,CallbackId cb){const auto event=GameEvent(cb);if(event==GameEvent::player_tick)return;if(event==GameEvent::weapon_tick||event==GameEvent::weapon_switch)guns.event(id,event);else if(event==GameEvent::projectile_tick)projectiles.event(id);else if(event==GameEvent::monster_tick)actors.event(id);else if(event==GameEvent::base_animation)effects.event(id);else door_event(w,id,event);};
    weapons.grant_all();animations.consider(original_cell_bounds(*world.objects.at(*world.player).pose,world.definition.cell_side,world.definition.width,world.definition.length));
    for(auto& object:world.objects){if(object.monster&&object.pose)monsters.wake(object.owner);if(object.door)use_door(world,object.owner,world.objects.at(*world.player).owner);}
    for(const auto& def:world.definition.objects)if(def.class_id==8){shots.spawn(world.objects.at(*world.player).owner,def.glyph);break;}
    world.advance_events(123456789,[&](auto id,auto cb){invoke(world,weapons,shots,monsters,animations,id,cb);});
    auto bytes=encode_save(world,flow.snapshot(),{},meta);auto saved=decode_save(bytes,archive);check(bytes==encode_save(*saved.world,saved.flow,saved.audio,saved.metadata),"Roundtrip bytes differ");
    Weapons guns2(*saved.world,archive);Projectiles shots2(*saved.world,archive);Monsters monsters2(*saved.world,archive,shots2);Animations animations2(*saved.world,archive);GameFlow flow2(*saved.world);flow2.restore(saved.flow);
    for(int i=0;i<50;++i){world.advance_events(16666667,[&](auto id,auto cb){invoke(world,weapons,shots,monsters,animations,id,cb);});saved.world->advance_events(16666667,[&](auto id,auto cb){invoke(*saved.world,guns2,shots2,monsters2,animations2,id,cb);});auto checkpoint=encode_save(world,flow.snapshot(),{},meta);auto restored_checkpoint=decode_save(checkpoint,archive);check(checkpoint==encode_save(*restored_checkpoint.world,restored_checkpoint.flow,restored_checkpoint.audio,restored_checkpoint.metadata),"Intermediate phase differs");}
    check(encode_save(world,flow.snapshot(),{},meta)==encode_save(*saved.world,flow2.snapshot(),{},meta),"World continuation differs");
    auto reseal=[](std::vector<std::uint8_t> value){value.resize(value.size()-32);auto sum=sha256(value);value.insert(value.end(),sum.begin(),sum.end());return value;};
    auto legacy_header=bytes;legacy_header[8]=1;legacy_header=reseal(legacy_header);auto legacy=decode_save(legacy_header,archive);check(encode_save(*legacy.world,legacy.flow,legacy.audio,legacy.metadata)==bytes,"Rules1 general migration changed state");
    auto unknown_rules=bytes;unknown_rules[8]=255;unknown_rules=reseal(unknown_rules);bool unknown_rejected=false;try{decode_save(unknown_rules,archive);}catch(...){unknown_rejected=true;}check(unknown_rejected,"Unknown rules accepted");
    if(world.definition.dataset==DatasetId::old_gold){auto old=decode_save(bytes,archive);std::size_t slot=0;while(slot<256&&world.definition.glyph_index[slot]>=0)++slot;check(slot<256,"No dynamic slot");std::size_t definition=0;while(definition<world.definition.objects.size()&&world.definition.objects[definition].class_id!=8)++definition;check(definition<world.definition.objects.size(),"No OG projectile template");
        ProjectileState state;state.pose=*old.world->objects.at(*old.world->player).pose;state.definition_index=definition;state.source=old.world->objects.at(*old.world->player).owner;state.impact=true;state.frame=0;old.world->projectiles[slot]=state;for(auto& cell:old.world->projectile_occupancy)if(cell==int(slot))cell=-1;old.world->events.create(Projectiles::owner_base+slot);if(old.world->events.events(Projectiles::owner_base+slot).empty())old.world->events.schedule(Projectiles::owner_base+slot,static_cast<CallbackId>(GameEvent::projectile_tick),416,static_cast<std::uint32_t>(old.world->simulation_ticks()));
        auto old_contact=encode_save(*old.world,old.flow,old.audio,old.metadata);old_contact[8]=1;old_contact=reseal(old_contact);auto migrated=decode_save(old_contact,archive);check(migrated.world->projectiles[slot]->frame==1&&!migrated.world->projectiles[slot]->effect_frame,"Legacy contact cursor was not migrated");
        old.world->projectiles[slot]->effect_frame=true;old.world->projectiles[slot]->frame=1;auto old_effect=encode_save(*old.world,old.flow,old.audio,old.metadata);old_effect[8]=1;old_effect=reseal(old_effect);auto visible=decode_save(old_effect,archive);check(visible.world->projectiles[slot]->frame==1&&visible.world->projectiles[slot]->effect_frame,"Legacy displayed effect was not preserved");}
    // Semantic corruption with a valid checksum must also fail.
    auto invalid=decode_save(bytes,archive);invalid.world->objects.front().owner=0xffffffffu;bool semantic_rejected=false;
    try{decode_save(encode_save(*invalid.world,invalid.flow,invalid.audio,invalid.metadata),archive);}catch(...){semantic_rejected=true;}check(semantic_rejected,"Invalid owner accepted");
    auto huge=decode_save(bytes,archive);auto huge_events=huge.world->events.snapshot();check(huge_events.owners.size()>=3,"Not enough event owners for quota test");for(int i=0;i<3;++i)huge_events.owners[i].capacity=32767;huge.world->events.restore(huge_events);semantic_rejected=false;try{decode_save(encode_save(*huge.world,huge.flow,huge.audio,huge.metadata),archive);}catch(...){semantic_rejected=true;}check(semantic_rejected,"Unbounded event capacity accepted");
    auto wrong=meta;wrong.fingerprint[0]^=1;semantic_rejected=false;try{decode_save(encode_save(world,flow.snapshot(),{},wrong),archive);}catch(...){semantic_rejected=true;}check(semantic_rejected,"Wrong data fingerprint accepted");
    auto version=bytes;version[4]=2;version.resize(version.size()-32);auto version_sum=sha256(version);version.insert(version.end(),version_sum.begin(),version_sum.end());semantic_rejected=false;try{decode_save(version,archive);}catch(...){semantic_rejected=true;}check(semantic_rejected,"Unknown version accepted");
    // Changing directory cache bytes and relocating payload blocks is canonical.
    std::ifstream original(std::filesystem::u8path(argv[1]),std::ios::binary);std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(original)),{});
    const auto count=archive.entries().size();std::vector<std::uint8_t> repacked(raw.begin(),raw.begin()+2+count*30);
    for(std::size_t j=0;j<count;++j){const auto& e=archive.entries()[count-1-j];const auto offset=std::uint32_t(repacked.size());const auto at=2+(count-1-j)*30;for(unsigned k=0;k<4;++k)repacked[at+20+k]=std::uint8_t(offset>>(k*8));for(unsigned k=24;k<30;++k)repacked[at+k]=0xa5;repacked.insert(repacked.end(),raw.begin()+e.offset,raw.begin()+e.offset+e.size);}
    Archive relocated(repacked);check(save_fingerprint(relocated,level)==meta.fingerprint,"Relocation changed canonical fingerprint");
    for(const auto& def:world.definition.objects)if(!def.sprite.empty()){const auto* entry=archive.find(def.sprite);if(!entry)continue;auto cache=raw;for(auto k:{6,7,8,9,14,15,16,17})cache[entry->offset+k]^=0x55;check(save_fingerprint(Archive(cache),level)==meta.fingerprint,"Image cache changed fingerprint");cache=raw;cache[entry->offset+18]^=1;check(save_fingerprint(Archive(cache),level)!=meta.fingerprint,"Changed image was not detected");break;}
    // Exercise the real projectile destructor/reallocation path, then serialize
    // a second live instance occupying the same lowest dynamic owner slot.
    {World reuse(load_level(archive,level));Weapons reuse_guns(reuse,archive);Projectiles reuse_shots(reuse,archive);Animations reuse_animation(reuse,archive);GameFlow reuse_flow(reuse);
        for(const auto& def:reuse.definition.objects)if(def.class_id==8){const auto source=reuse.objects.at(*reuse.player).owner;reuse_shots.spawn(source,def.glyph);int first=-1;for(int slot=0;slot<256;++slot)if(reuse.projectiles[slot]){first=slot;break;}check(first>=0,"Projectile was not spawned");
            for(int i=0;i<2000&&reuse.projectiles[first];++i)reuse.advance_events(16666667,[&](OwnerId id,CallbackId cb){if(GameEvent(cb)==GameEvent::projectile_tick)reuse_shots.event(id);});
            check(!reuse.projectiles[first],"Projectile did not release its slot");reuse_shots.spawn(source,def.glyph);check(reuse.projectiles[first].has_value(),"Lowest projectile slot was not reused");auto reused_bytes=encode_save(reuse,reuse_flow.snapshot(),{},meta);auto reused=decode_save(reused_bytes,archive);check(reused.world->projectiles[first].has_value()&&reused_bytes==encode_save(*reused.world,reused.flow,reused.audio,reused.metadata),"Reused projectile slot differs");break;}}
    auto bad=bytes;bad[20]^=1;rejected=false;try{decode_save(bad,archive);}catch(...){rejected=true;}check(rejected,"Corrupt save accepted");
    bad.resize(20);rejected=false;try{decode_save(bad,archive);}catch(...){rejected=true;}check(rejected,"Truncated save accepted");
    auto name=std::filesystem::u8path(argc>3?argv[3]:"build/sdl2/save-test.sav");write_save_file(name,bytes,archive);write_save_file(name,bytes,archive);check(read_save_file(name)==bytes,"Storage roundtrip");auto backup=name;backup+=".bak";check(read_save_file(backup)==bytes,"Backup differs");
    auto blocked=name;blocked+=".blocked";std::filesystem::create_directories(blocked);{std::ofstream keep(blocked/"keep");keep<<"keep";}bool failed=false;try{write_save_file(blocked,bytes,archive);}catch(...){failed=true;}check(failed&&std::filesystem::exists(blocked/"keep"),"Failed replacement changed old destination");
    // Respawn remains the original spawn even after a changed saved position.
    auto respawn=decode_save(bytes,archive);GameFlow retry(*respawn.world);retry.restore(respawn.flow);auto& hero=respawn.world->objects.at(*respawn.world->player);hero.pose->heading=(hero.pose->heading+45)%360;hero.player_state->attributes[0]=0;const auto lives=hero.player_state->lives;check(retry.observe()==SessionPhase::retry&&hero.player_state->lives==lives-1,"Death after load");check(retry.retry(),"Retry after load");check(hero.pose->x==respawn.flow.spawn.x&&hero.pose->z==respawn.flow.spawn.z&&hero.pose->heading==respawn.flow.spawn.heading,"Respawn point changed after load");
    if(argc>4&&std::string(argv[4])=="load"){auto fresh=decode_save(read_save_file(name),archive);check(fresh.metadata.level==level,"New process load");}
    std::cout<<level<<" save roundtrip/continuation/storage PASS bytes="<<bytes.size()<<" fingerprint="<<hex(meta.fingerprint)<<"\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
