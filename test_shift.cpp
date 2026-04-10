#include <iostream>
#include <stdint.h>

int main() {
    uint32_t v102 = 0xffffffff;
    uint64_t v103 = 0x30;
    uint64_t v104 = v102 << v103;
    
    uint32_t v105 = v104;
    uint64_t v106 = 0x30;
    uint64_t v107 = ((int32_t)v105) >> v106;
    
    std::cout << std::hex << "v104=" << v104 << " v107=" << v107 << std::endl;
    return 0;
}
