#include "dok/resources.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
static void check(bool b) { if(!b) throw std::runtime_error("Test failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::runtime_error&) { rejected=true; } check(rejected); }
static void put32(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)b[at+n]=std::uint8_t(value>>(8*n));}
static void source_tests(const char* path){
    std::ifstream f(path,std::ios::binary);std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    dok::Archive original(bytes);const auto id=original.identify();check(id!=dok::DatasetId::unknown);
    // Reorder the directory and payloads, clean cache/name tails, insert gaps.
    std::vector<std::uint8_t> relocated(2+30*original.entries().size(),0);relocated[0]=std::uint8_t(original.entries().size());relocated[1]=std::uint8_t(original.entries().size()>>8);
    std::size_t n=0;
    for(auto it=original.entries().rbegin();it!=original.entries().rend();++it,++n){
        auto at=2+30*n;std::copy(it->name.begin(),it->name.end(),relocated.begin()+at);
        relocated.insert(relocated.end(),7,0xab);put32(relocated,at+16,it->size);put32(relocated,at+20,std::uint32_t(relocated.size()));
        const auto payload=original.get(it->name);relocated.insert(relocated.end(),payload.data,payload.data+payload.size);
    }
    relocated.insert(relocated.end(),21,0xcd);dok::Archive repacked(relocated);
    check(original.same_resources(repacked)&&repacked.identify()==id);
    dok::ResourceSources sources;check(!sources.selected(id));check(sources.add(path,original)==0);check(sources.selected(id)==0);
    check(sources.add("relocated.res",repacked)==0&&sources.sources().size()==1);
    check(sources.find_source("relocated.res")==0&&!sources.find_source("missing.res"));
    auto changed=bytes;bool edited=false;
    for(const auto& entry:original.entries()){
        try{original.image(entry.name);}catch(const std::runtime_error&){continue;}
        changed[entry.offset+entry.size-1]^=1;edited=true;break;
    }
    check(edited);dok::Archive variant(changed);check(variant.identify()==id&&!variant.same_resources(original));
    check(sources.add("variant.res",variant)==1&&!sources.selected(id));
    check(sources.add("original-copy.res",original)==0&&!sources.selected(id));
    rejects([&]{sources.select(dok::DatasetId::unknown,0);});rejects([&]{sources.select(id,2);});
    sources.select(id,1);check(sources.selected(id)==1);
    check(sources.add("variant-copy.res",variant)==1&&sources.selected(id)==1);
    rejects([&]{sources.add("broken.res",dok::Archive({}));});check(sources.selected(id)==1);
    sources.select(id,0);check(sources.selected(id)==0);
    // Unknown catalogs never alter an existing choice.
    auto unknown=bytes;unknown[2]='?';rejects([&]{sources.add("unknown.res",dok::Archive(unknown));});check(sources.selected(id)==0);
    for(const auto& entry:original.entries()){
        try{original.image(entry.name);}catch(const std::runtime_error&){continue;}
        changed[entry.offset+entry.size-1]^=2;break;
    }
    sources.add("third.res",dok::Archive(changed));check(!sources.selected(id));
    std::cout<<"Source selection/repacking checks: "<<dok::dataset_name(id)<<"\n";
}
int main(int argc,char** argv) {
    try {
        rejects([]{dok::Archive a({});});
        std::vector<std::uint8_t> b(36,0); b[0]=1;b[2]='A';b[18]=4;b[22]=32;
        b[26]=0xff;b[27]=0xff; // DOS cache flags ignored.
        dok::Archive a(b);check(a.entries().size()==1);check(a.get("A").size==4);
        check(a.identify()==dok::DatasetId::unknown);rejects([&]{a.get("a");});
        b[22]=31;rejects([&]{dok::Archive invalid(b);});
        b[22]=32;b[18]=5;rejects([&]{dok::Archive invalid(b);});
        b[18]=4;for(int i=2;i<18;++i)b[i]='X';rejects([&]{dok::Archive invalid(b);});
        for(int i=1;i<argc;++i)source_tests(argv[i]);
        std::cout<<"RES boundary and name tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
