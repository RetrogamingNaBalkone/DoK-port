#include "dok/resources.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool b) { if(!b) throw std::runtime_error("Test failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::runtime_error&) { rejected=true; } check(rejected); }
int main() {
    try {
        rejects([]{dok::Archive a({});});
        std::vector<std::uint8_t> b(36,0); b[0]=1;b[2]='A';b[18]=4;b[22]=32;
        b[26]=0xff;b[27]=0xff; // DOS cache flags ignored.
        dok::Archive a(b);check(a.entries().size()==1);check(a.get("A").size==4);
        check(a.identify()==dok::DatasetId::unknown);rejects([&]{a.get("a");});
        b[22]=31;rejects([&]{dok::Archive invalid(b);});
        b[22]=32;b[18]=5;rejects([&]{dok::Archive invalid(b);});
        b[18]=4;for(int i=2;i<18;++i)b[i]='X';rejects([&]{dok::Archive invalid(b);});
        std::cout<<"RES boundary and name tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
