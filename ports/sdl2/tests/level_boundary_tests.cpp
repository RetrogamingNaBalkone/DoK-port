#include "dok/level.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
unsigned checks=0;
void check(bool value){if(!value)throw std::runtime_error("LEVEL assertion failed");}
std::string replace(std::string text,const std::string& from,const std::string& to){
    auto at=text.find(from);check(at!=std::string::npos);text.replace(at,from.size(),to);return text;
}
void profile(const char* path){
    std::ifstream input(path,std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    dok::Archive original(bytes);
    for(const auto& entry:original.entries())if(entry.name.rfind("LEVEL",0)==0){
        auto reference=dok::load_level(original,entry.name);
        auto run=[&](std::string text,bool accepted){
            check(text.size()+2<=entry.size);text+="\n#";text.resize(entry.size,'x');
            auto modified=bytes;std::copy(text.begin(),text.end(),modified.begin()+entry.offset);
            bool success=false;dok::LevelDefinition result;
            try{dok::Archive archive(std::move(modified));check(archive.identify()==reference.dataset);result=dok::load_level(archive,entry.name);success=true;}
            catch(const std::runtime_error&){if(accepted)throw;}
            if(success!=accepted)throw std::runtime_error(entry.name+": unexpected acceptance: "+text.substr(0,300));
            ++checks;return result;
        };
        const std::string ini="[Game Ini]\nLevel Width=1\nLevel Height=1\nLevel Length=1\nCell Side=336\nCell Height=672\nFloor Color=10\nCeiling Color=143\nMin Dis=165\n";
        const std::string head=ini+"[Palette]\nName="+reference.palette+"\n[Light]\nName="+reference.light+"\n[Game Object]\n";
        const std::string maps="[TMap]\n \n[MMap]\n \n[FCMap]\n \n";
        const std::string object="_ 2 0 0 0 0\n";
        const std::string base=head+object+maps;
        auto minimum=run(base,true);check(minimum.maps.at("tmap")==std::vector<std::uint8_t>{' '});
        run(replace(base,object,"_ 2 65535 -32768 32767 -32768\n"),true);
        auto plus=run(replace(base,object,"_ +2 +65535 +32767 -32768 +255\n"),true);check(plus.objects[0].marker==255);
        auto marker=run(replace(base,object,"_ 2 0 0 0 -1\n"),true);check(marker.objects[0].marker==255);
        run(replace(base,"Cell Side=336","Cell Side=+336"),true);
        for(const auto& bad:{"_ 2 65536 0 0 0\n","_ 2 -1 0 0 0\n","_ 2 0 32768 0 0\n","_ 2 0 -32769 0 0\n","_ 2 0 0 0 32768\n","_ 2 0 +-1 0 0\n","_ 2 0 + 0 0\n","_ 2 0 99999999999999999999999 0 0\n","_ 11 0 0 0 0\n","_ 14 0 0 0 0\n","_ 2 0 0 0\n","_ 2 0 0 0 0 junk\n"})run(replace(base,object,bad),false);
        run(replace(base,object,object+object),false);
        run(base+"[Unknown]\n",false);
        run(base+"[Unclosed\n",false);
        run(replace(base,"[TMap]\n \n","[TMap]\n  \n"),false);
        run(replace(base,"[TMap]\n \n","[TMap]\n \n \n"),false);
        run(replace(base,"[TMap]\n \n","[TMap]\nx\n"),false);
        run(replace(base,"[MMap]\n \n","[MMap]\n.\n"),true);
        run(replace(base,"Level Width=1","Level Width=0"),false);
        run(replace(base,"Level Width=1","Level Width=65"),false);
        run(replace(base,"Level Height=1","Level Height=2"),false);
        run(replace(base,"Floor Color=10","Floor Color=256"),false);
        run(base+"[Game Ini]\nCell Side=400\n",false);
        run(replace(base,"Cell Side=336",std::string("Cell Side=3\0" "36",14)),false);
        // All meaningful truncated prefixes must fail; a final newline is optional.
        auto complete=base;while(complete.back()=='\n')complete.pop_back();
        for(std::size_t n=0;n<complete.size();++n)run(complete.substr(0,n),false);
        run(complete,true);
        for(const std::string ending:{"\r","\r\n"}){std::string text;for(char c:base)text+=c=='\n'?ending:std::string(1,c);run(text,true);}
        // An otherwise unused class5 independently exercises signed long + word grammar.
        for(const auto& value:{"-2147483648","2147483647","+2147483647"}){
            auto result=run(head+object+"x 5 0 0 0 0 "+value+" -32768 32767\n"+maps,true);
            check(result.objects[1].number(0x32)==-32768&&result.objects[1].number(0x34)==32767);
        }
        for(const auto& value:{"-2147483649","2147483648","+-1","+","1x"})run(head+object+"x 5 0 0 0 0 "+value+" 0 0\n"+maps,false);
        // Message getline preserves the leading separator and caps at 39 bytes.
        auto message=run(head+object+"x 3 0 0 0 0 1 2 "+std::string(60,'z')+"\n"+maps,true);
        check(message.objects[1].fields.back().bytes==" "+std::string(38,'z'));
        const auto sprite=std::find_if(reference.objects.begin(),reference.objects.end(),[](const auto& o){return !o.sprite.empty();});check(sprite!=reference.objects.end());
        run(head+"Sprite="+sprite->sprite+" 0 extra\n"+object+maps,false);
        std::cout<<dok::dataset_name(reference.dataset)<<" "<<entry.name<<" boundaries PASS\n";
    }
}
}
int main(int argc,char** argv){try{
    if(argc<2)throw std::runtime_error("Pass original RES paths");
    for(int i=1;i<argc;++i)profile(argv[i]);
    std::cout<<checks<<" LEVEL boundary cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
