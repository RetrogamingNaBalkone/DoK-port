#include "dok/arithmetic.hpp"
#include <iostream>
#include <string>
int main(){std::string op;std::uint32_t x,y;while(std::cin>>op>>x>>y){auto a=dok::original::signed32(x),b=dok::original::signed32(y);try{
 std::int32_t v=0;
 if(op=="neg")v=dok::original::negate16(dok::original::signed16(static_cast<std::uint16_t>(x)));
 else if(op=="add")v=dok::original::add32(a,b);else if(op=="sub")v=dok::original::sub32(a,b);
 else if(op=="mul")v=dok::original::multiply32(a,b);else if(op=="sar")v=dok::original::sar32(a,y);
 else if(op=="div")v=dok::original::divide32(a,b);else return 2;
 std::cout<<std::uint32_t(v)<<'\n';
 }catch(const std::domain_error&){std::cout<<"fault\n";}}
}
