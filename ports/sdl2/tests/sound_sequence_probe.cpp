#include "dok/audio.hpp"
#include <iostream>
#include <map>
#include <sstream>
static std::string decode(const std::string& hex){std::string out;for(std::size_t i=0;i<hex.size();i+=2)out+=char(std::stoi(hex.substr(i,2),nullptr,16));return out;}
int main(int argc,char**argv){try{
 if(argc!=2)return 2;auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));const bool og=archive.identify()==dok::DatasetId::old_gold;std::map<std::string,dok::Sound> sounds;
 std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);int count;in>>count;dok::MonoVoice voice;std::string trace;
  for(int i=0;i<count;++i){int action;dok::SoundId id;std::string hex;in>>action>>id>>hex;if(!in)throw std::runtime_error("Truncated commands");const auto before=voice.snapshot();const auto a=static_cast<dok::AudioAction>(action);
   if(a==dok::AudioAction::stop_all)voice.stop();else if(a==dok::AudioAction::stop)voice.stop(id);else if(hex!="-"){const auto key=decode(hex);auto found=sounds.find(key);if(found==sounds.end())found=sounds.emplace(key,archive.sound(key)).first;if(a==dok::AudioAction::priority_once&&voice.active()!=id)voice.stop();if(a!=dok::AudioAction::priority_once||voice.active()!=id)voice.play(id,found->second,a==dok::AudioAction::loop,og);}
   const auto after=voice.snapshot();trace+=std::to_string(action)+","+std::to_string(id)+","+(hex=="-"?"":hex)+","+std::to_string(before.id)+","+std::to_string(after.id)+","+std::to_string(after.loop)+","+std::to_string(before.loop)+";";
  }
  const auto end=voice.snapshot();std::cout<<end.id<<","<<end.loop<<"|"<<trace<<"\n";
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
