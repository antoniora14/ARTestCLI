#pragma once
#include "CCommandFactory.h"

#define REGISTER_COMMAND(TYPE, CLASS)                                   \
    namespace {                                                         \
        struct CLASS##Register {                                        \
            CLASS##Register() {                                         \
                CommandFactory::RegisterCommand(                        \
                    TYPE,                                               \
                    []() -> std::unique_ptr<ICommand> {                 \
                        return std::make_unique<CLASS>();               \
                    }                                                   \
                );                                                      \
            }                                                           \
        };                                                              \
        static CLASS##Register global_##CLASS##Register;                \
    }