#pragma once
#include "InstrumentFactory.h"

#define REGISTER_INSTRUMENT(TYPE, CLASS)                                \
    namespace {                                                         \
        struct CLASS##Register {                                        \
            CLASS##Register() {                                         \
                InstrumentFactory::RegisterInstrument(                  \
                    TYPE,                                               \
                    []() -> std::unique_ptr<IInstrument> {              \
                        return std::make_unique<CLASS>();               \
                    }                                                   \
                );                                                      \
            }                                                           \
        };                                                              \
        static CLASS##Register global_##CLASS##Register;                \
    }