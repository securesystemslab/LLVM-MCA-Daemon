set(LLVM_LINK_COMPONENTS
    Support)

add_llvm_executable(qemu-broker-client
  qemu-broker-client.cpp
  )
