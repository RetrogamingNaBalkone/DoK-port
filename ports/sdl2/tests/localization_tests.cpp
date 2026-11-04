#include "dok/localization.hpp"
#include "dok/text.hpp"
#include "dok/world.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static dok::LevelDefinition fixture(){
    dok::LevelDefinition d;d.dataset=dok::DatasetId::old_gold;d.width=d.length=3;d.height=1;d.cell_side=480;d.cell_height=640;d.glyph_index.fill(-1);
    dok::ObjectDefinition blank;blank.glyph=' ';dok::ObjectDefinition hero;hero.glyph='P';hero.class_id=1;hero.height_parameter=300;
    for(int i=0;i<11;++i)hero.fields.push_back({dok::FieldKind::word,0x5e + i*2,100,{}});
    hero.fields.push_back({dok::FieldKind::word,0x32,90,{}});d.objects={blank,hero};d.glyph_index[' ']=0;d.glyph_index['P']=1;
    for(const char* map:{"tmap","fcmap","mmap"})d.maps[map]=std::vector<std::uint8_t>(9,' ');d.maps["mmap"][4]='P';return d;
}
int main(){try{
    using dok::LocaleId;
    check(dok::game_story_key(dok::DatasetId::old_gold,"INTRO4B")=="old_gold.story.begin","OG story semantic key");
    check(dok::game_story_key(dok::DatasetId::kremlin_pke,"BINTRO2")=="kremlin.story.begin.2","Kremlin story semantic key");
    check(dok::game_story_key(dok::DatasetId::unknown,"BINTRO2").empty(),"Unknown dataset must not receive another story");
    check(dok::game_text("missing.key",LocaleId::ru).empty(),"Unknown key fallback");
    const auto cheat=dok::cp866("Ах ты хитрый думер!");
    check(dok::game_message_key(cheat)=="cheat.idkfa","Cheat key");
    check(dok::game_message_key(dok::cp866(" ЗАБИЛ  МОНСТРА "))=="old_gold.death.monster","Original whitespace normalization");
    check(dok::game_message_key("TOOK THE COOPER KEY")=="item.key.copper","Original English alias");
    dok::World world(fixture());world.notify(cheat);const auto until=world.message_until;const auto maps=world.maps;
    check(world.message_key=="cheat.idkfa"&&world.visible_message()==cheat,"Original message retained");
    check(world.visible_message(LocaleId::en)=="You sneaky Doomer!","Active cheat immediately translates");
    check(world.visible_message(LocaleId::ru)==cheat,"Active message switches back");
    check(world.simulation_ticks()==0&&world.message_until==until&&world.maps==maps,"Locale presentation must not advance or modify the world");
    world.notify("Custom message");check(world.message_key.empty()&&world.visible_message(LocaleId::ru)=="Custom message","Unknown custom text preserved");
    world.notify(dok::cp866("ВЗЯЛ 1 РУБЛЬ"));
    check(world.visible_message(LocaleId::en)=="TOOK 1 RUBLE","Money message translates");
    world.advance_events(4001000000ULL,[](auto,auto){});check(world.visible_message(LocaleId::ru).empty()&&world.visible_message(LocaleId::en).empty(),"Same expiry in both languages");
    check(dok::game_dataset_title(dok::DatasetId::old_gold,LocaleId::ru)=="Старое золото","Localized Old Gold name");
    check(dok::game_level_name(dok::DatasetId::kremlin_pkr,1,LocaleId::en)=="PALACE OF EVIL","Names follow locale independently of profile");
    std::cout<<"Semantic aliases, single-profile stories, active-message locale switch, cheat translation and expiry passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
