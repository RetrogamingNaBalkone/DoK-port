#include "dok/resources.hpp"
#include "dok/level.hpp"
#include <iostream>
namespace dok { void diagnostic_json(const Archive&); }
int main(int argc, char** argv) {
    if(argc==3 && std::string(argv[1])=="--json") {
        try {dok::diagnostic_json(dok::Archive::open(std::filesystem::u8path(argv[2])));return 0;}
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc < 2) { std::cerr << "Usage: dok_inspect RES...\n"; return 2; }
    bool failed=false;
    for (int i=1;i<argc;++i) {
        try {
            auto a=dok::Archive::open(std::filesystem::u8path(argv[i]));
            std::cout << argv[i] << ": " << dok::dataset_name(a.identify()) << ", " << a.entries().size() << " resources\n";
            for (const auto& r:a.entries()) std::cout<<r.name<<'\t'<<r.size<<'\n';
            for (const auto& r:a.entries()) if(r.name.compare(0,5,"LEVEL")==0) {
                auto level=dok::load_level(a,r.name);
                std::cout<<"LEVEL_OK "<<r.name<<" objects="<<level.objects.size()<<" side="<<level.cell_side<<'\n';
            }
            if(a.identify()==dok::DatasetId::unknown) failed=true;
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; failed=true; }
    }
    return failed ? 1 : 0;
}
