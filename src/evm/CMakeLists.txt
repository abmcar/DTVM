set(EVM_SRCS interpreter.cpp opcode_handlers.cpp gas_storage_cost.cpp
             merkle_patricia_trie.cpp
)

add_library(evm OBJECT ${EVM_SRCS})
