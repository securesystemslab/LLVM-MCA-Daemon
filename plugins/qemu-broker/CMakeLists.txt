
if (WIN32)
  message(FATAL_ERROR "This plugin does not support Windows")
endif()

add_llvm_library(MCADQemuBroker SHARED
  Broker.cpp
  )
