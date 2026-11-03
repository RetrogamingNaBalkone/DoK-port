#include "dok/arithmetic.hpp"
#include <iostream>
#include <stdexcept>
using namespace dok::original;
static void check(bool b){if(!b)throw std::runtime_error("Arithmetic vector failed");}
int main(){
    try{
        check(signed16(0x8000)==-32768);check(negate16(-32768)==-32768);
        check(add32(2147483647,1)==(-2147483647-1));check(sub32(-2147483647-1,1)==2147483647);
        check(multiply32(65536,65536)==0);check(multiply32(-1,2)==-2);
        check(sar32(-3,1)==-2);check(sar32(-1,31)==-1);check(sar32(3,1)==1);
        check(divide32(-3,2)==-1);check(divide32(3,-2)==-1);
        for(auto pair:{std::pair<std::int32_t,std::int32_t>{1,0},{-2147483647-1,-1}}){
            bool failed=false;try{divide32(pair.first,pair.second);}catch(const std::domain_error&){failed=true;}check(failed);
        }
        // Published PKR/PKE camera reference: D=200,k=8,K=4096,depth=3360.
        check(divide32(multiply32(multiply32(8,200),4096),3360+200)==1840);
        std::cout<<"Arithmetic boundary vectors passed; x86 oracle pending\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
