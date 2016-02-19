set(KEXT_NAME "traceexec")
set(KEXT_PATH "${KEXT_NAME}.kext")
set(KEXT_CONTENTS_DIR "${KEXT_PATH}/Contents")
set(KEXT_BINARY "${KEXT_CONTENTS_DIR}/MacOS/${KEXT_NAME}")
set(KEXT_INFOPLIST "${KEXT_CONTENTS_DIR}/Info.plist")

set(KERNEL_C_FLAGS
  "-arch x86_64 -mkernel -nostdinc -fno-builtin")

set(KERNEL_LINKER_FLAGS
  "-arch x86_64 -mkernel -Xlinker -kext -nostdlib -lkmod")

add_executable(traceexec_kext
  traceexec.c
  traceexec_info.c)

add_custom_command(
  TARGET traceexec_kext
  COMMAND mkdir -p "${KEXT_CONTENTS_DIR}"
  COMMAND cp "${CMAKE_CURRENT_LIST_DIR}/Info.plist" "${KEXT_CONTENTS_DIR}/Info.plist"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Info.plist"
  COMMENT "Copy kernel extension Info.plist"
  VERBATIM)

target_include_directories(traceexec_kext
  PRIVATE
  "/System/Library/Frameworks/Kernel.framework/Headers"
  "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/Kernel.framework/Headers")
set_target_properties(traceexec_kext PROPERTIES
  COMPILE_FLAGS "${KERNEL_C_FLAGS}"
  LINK_FLAGS "${KERNEL_LINKER_FLAGS}"
  EXCLUDE_FROM_ALL true
  OUTPUT_NAME "${KEXT_BINARY}")
