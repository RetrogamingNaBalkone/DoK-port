#include "dok/events.hpp"
#include "dok/arithmetic.hpp"
#include <iostream>
#include <sstream>
#include <array>
#include <map>
struct Action{unsigned advance=0;bool once=false;unsigned calls=0;std::vector<std::array<unsigned,5>> operations;};
int main(){try{
 dok::Events q;for(unsigned i=1;i<=4;++i)q.create(i);unsigned now=0;std::map<unsigned,Action> actions;
 std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);char code;in>>code;
  if(code=='A'){unsigned id,once;in>>id;auto& a=actions[id];in>>a.advance>>once;a.once=once;}
  else if(code=='B'){unsigned id;in>>id;std::array<unsigned,5> op{};for(auto& n:op)in>>n;actions[id].operations.push_back(op);}
  else if(code=='N')in>>now;
  else if(code=='K')q.clear();
  else if(code=='S'){unsigned owner,callback,interval,parameter;in>>owner>>callback>>interval>>parameter;q.schedule(owner,callback,dok::original::signed32(interval),now,parameter);}
  else if(code=='D')q.dispatch([&]{return now;},[&](auto owner,auto callback){
   std::cout<<"C "<<owner<<' '<<callback<<' '<<now<<'\n';auto& a=actions[callback];now+=a.advance;
   if(!a.once||a.calls==0)for(auto op:a.operations){if(op[0]==1)q.cancel_current();else if(op[0]==2)q.set_first_interval(dok::original::signed32(op[1]));else if(op[0]==3)q.schedule(op[1],op[2],dok::original::signed32(op[3]),now,op[4]);}
   ++a.calls;
  });
 }
 std::cout<<"H "<<q.active_owners()<<'\n';for(unsigned id=1;id<=4;++id){auto events=q.events(id);std::cout<<"O "<<id<<' '<<events.size()<<'\n';for(auto e:events)std::cout<<"E "<<e.deadline<<' '<<e.interval<<' '<<e.callback<<' '<<e.parameter<<'\n';}
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
