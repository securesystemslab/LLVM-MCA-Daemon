set(QEMU_INCLUDE_DIR /usr/include CACHE PATH
    "Folder that contains qemu/qemu-plugin.h")

include_directories(${QEMU_INCLUDE_DIR})

add_llvm_library(QemuRelay SHARED
  Plugin.cpp

  LINK_COMPONENTS
  Support
  )
