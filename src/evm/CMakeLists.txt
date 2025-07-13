set(EVM_SRCS interpreter.cpp)

add_library(evm OBJECT ${EVM_SRCS})

target_link_libraries(evm PRIVATE uint256_t)