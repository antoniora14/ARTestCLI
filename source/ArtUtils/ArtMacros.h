#pragma once
#include <iostream>

#define CHECK_SMART_PTR(ptr)                           \
    do {                                               \
        if (!(ptr)) {                                  \
            std::cerr << "Null smart pointer: "        \
                      << #ptr << " at "                \
                      << __FILE__ << ":"               \
                      << __LINE__ << std::endl;        \
            return false;                              \
        }                                              \
    } while(0)

#define ASSERT_SMART_PTR(ptr)                                                               \
    do {                                                                                    \
        if (!(ptr)) {                                                                       \
            throw std::runtime_error(std::string("Null ptr: ") + #ptr +                     \
                                     " at " + __FILE__ + ":" + std::to_string(__LINE__));   \
        }                                                                                   \
    } while(0)