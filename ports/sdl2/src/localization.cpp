#include "dok/localization.hpp"
#include <cctype>

namespace dok {
namespace {
struct TextEntry {const char* key;const char* ru;const char* en;};
struct AliasEntry {const char* original;const char* key;};
#include "localization_data.inc"
std::string normalize(const std::string& value){
    std::string result;bool space=false;
    for(unsigned char c:value){
        if(!c||c=='$')break;
        if(c<128&&std::isspace(c)){space=!result.empty();continue;}
        if(space){result+=' ';space=false;}
        result+=char(c);
    }
    return result;
}
}
std::string game_text(const std::string& key,LocaleId locale){
    for(const auto& entry:text_entries)if(key==entry.key)
        return locale==LocaleId::ru?entry.ru:entry.en;
    return {};
}
std::string game_message_key(const std::string& original){
    const auto value=normalize(original);
    for(const auto& entry:alias_entries)if(value==entry.original)return entry.key;
    return {};
}
std::string game_story_key(DatasetId dataset,const std::string& name){
    if(dataset==DatasetId::old_gold){
        if(name=="INTRO4B")return "old_gold.story.begin";
        if(name=="INTRO4E")return "old_gold.story.end";
        if(name=="ENDTXT")return "old_gold.story.credits";
    }else if(dataset==DatasetId::kremlin_pkr||dataset==DatasetId::kremlin_pke){
        if(name=="README")return "kremlin.story.prologue";
        if(name=="ENDTXT")return "kremlin.story.credits";
        if(name.size()==7&&name.compare(0,6,"BINTRO")==0&&name[6]>='1'&&name[6]<='3')return "kremlin.story.begin."+name.substr(6);
        if(name.size()==7&&name.compare(0,6,"EINTRO")==0&&name[6]>='1'&&name[6]<='3')return "kremlin.story.end."+name.substr(6);
    }
    return {};
}
std::string game_level_name(DatasetId dataset,int level,LocaleId locale){
    if(dataset==DatasetId::old_gold)return game_text("level.old_gold",locale);
    static const char* keys[]={"level.palace","level.river","level.kremlin"};
    return level>=1&&level<=3?game_text(keys[level-1],locale):std::string{};
}
std::string game_dataset_title(DatasetId dataset,LocaleId locale){
    return game_text(dataset==DatasetId::old_gold?"dataset.old_gold.title":"dataset.kremlin.title",locale);
}
std::string game_dataset_description(DatasetId dataset,LocaleId locale){
    return game_text(dataset==DatasetId::old_gold?"dataset.old_gold.description":dataset==DatasetId::kremlin_pke?"dataset.kremlin_en.description":"dataset.kremlin_ru.description",locale);
}
}
