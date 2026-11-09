include_directories("${SS_PS4_ROOT}/platform/ps4/include" "${SS_PS4_ROOT}/build/ps4-usb")
# PS4 links the engine modules into one executable.
set_property(TARGET ${ENGINELIB} PROPERTY LINK_LIBRARIES engine_safemath${MP})
set_property(TARGET SeriousSam${MP} PROPERTY LINK_FLAGS "")
set(SS_NATIVE_STACK "/home/nichlas/Doom3-Ps4/build/native" CACHE PATH "Built native PS4 graphics stack")
set(vorbis_dir "${CMAKE_SOURCE_DIR}/External/libvorbis")
set(ogg_dir "${CMAKE_SOURCE_DIR}/External/libogg")
set(vorbis_sources analysis bitrate block codebook envelope floor0 floor1 info lookup
  lpc lsp mapping0 mdct psy registry res0 sharedbook smallft synthesis vorbisfile window)
list(TRANSFORM vorbis_sources PREPEND "${vorbis_dir}/lib/")
list(TRANSFORM vorbis_sources APPEND ".c")
add_library(ps4_vorbis STATIC ${vorbis_sources} "${ogg_dir}/src/bitwise.c" "${ogg_dir}/src/framing.c")
target_include_directories(ps4_vorbis PRIVATE "${vorbis_dir}/include" "${ogg_dir}/include")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/static_exports.cpp"
  COMMAND python3 "${SS_PS4_ROOT}/scripts/generate-static-exports.py"
    "${CMAKE_BINARY_DIR}/static_exports.cpp" "$<TARGET_FILE:${GAMEMPLIB}>" "$<TARGET_FILE:${ENTITIESMPLIB}>" "$<TARGET_FILE:${SHADERSLIB}>"
    "$<TARGET_FILE:amp11lib${MP}>" "$<TARGET_FILE:ps4_vorbis>"
  DEPENDS ${GAMEMPLIB} ${ENTITIESMPLIB} ${SHADERSLIB} amp11lib${MP} ps4_vorbis "${SS_PS4_ROOT}/scripts/generate-static-exports.py")
set(SS_PS4_GFX "opengnm" CACHE STRING "PS4 graphics device: opengnm (boot) or radv")
set_property(CACHE SS_PS4_GFX PROPERTY STRINGS opengnm radv)
if(SS_PS4_GFX STREQUAL "radv")
  set(SS_PS4_WSI "${SS_PS4_ROOT}/platform/ps4/videoout_wsi.cpp" "${SS_PS4_ROOT}/platform/ps4/radv_vk_dispatch.cpp")
  target_compile_definitions(SeriousSam${MP} PRIVATE SS_PS4_RADV)
else()
  set(SS_PS4_WSI "${SS_PS4_ROOT}/platform/ps4/videoout_wsi_opengnm.cpp")
endif()
target_sources(SeriousSam${MP} PRIVATE "${CMAKE_BINARY_DIR}/static_exports.cpp"
  "${SS_PS4_ROOT}/platform/ps4/input.cpp"
  "${SS_PS4_ROOT}/platform/ps4/storage.cpp"
  ${SS_PS4_WSI})
if(INTERNAL_NAME STREQUAL "serioussamse")
  target_compile_definitions(SeriousSam${MP} PRIVATE SS_PS4_TSE)
endif()
if(SS_PS4_GFX STREQUAL "radv")
  set(SS_MESA_BUILD "$ENV{HOME}/dolphinps4-build-pacbrew/build/mesa" CACHE PATH "Dolphin PS4 Mesa build (Mesa 26.0.8 + mesa-ps4.patch)")
  set(_mesa_libs
    src/amd/vulkan/libvulkan_radeon.a
    src/amd/addrlib/libaddrlib.a
    src/amd/common/libamd_common.a
    src/amd/compiler/libaco.a
    src/c11/impl/libmesa_util_c11.a
    src/compiler/libcompiler.a
    src/compiler/nir/libnir.a
    src/compiler/spirv/libvtn.a
    src/util/blake3/libblake3.a
    src/util/libmesa_util.a
    src/util/libmesa_util_clflush.a
    src/util/libmesa_util_clflushopt.a
    src/util/libmesa_util_simd.a
    src/util/libparson.a
    src/util/libxmlconfig.a
    src/vulkan/util/libvulkan_util.a)
  set(_mesa_link "")
  foreach(_lib ${_mesa_libs})
    list(APPEND _mesa_link "${SS_MESA_BUILD}/${_lib}")
  endforeach()
  set(_sdl "${SS_SDL_LIBRARY}")
  if(NOT _sdl)
    set(_sdl "${SS_PS4_ROOT}/build/ps4-sdl-radv/libSDL2.a")
  endif()
  target_sources(SeriousSam${MP} PRIVATE
    "${SS_PS4_ROOT}/platform/ps4/ps4_heap.c"
    "${SS_PS4_ROOT}/platform/ps4/ps4_dlmalloc.c"
    "${SS_PS4_ROOT}/platform/ps4/ps4_modules.c")
  set_property(TARGET SeriousSam${MP} PROPERTY LINK_LIBRARIES
    "-Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=__memalign,--wrap=aligned_alloc,--wrap=posix_memalign"
    "-Wl,--start-group" ${GAMEMPLIB} ${ENTITIESMPLIB} ${SHADERSLIB} ${ENGINELIB} engine_safemath${MP} amp11lib${MP} ps4_vorbis
    "${_sdl}"
    "-Wl,--whole-archive" ${_mesa_link} "-Wl,--no-whole-archive"
    "${SS_PS4_ROOT}/build/ps4-usb/libss-jbc.a"
    "-Wl,--end-group" SceGnmDriver SceSystemService SceSysmodule z pthread)
else()
  set_property(TARGET SeriousSam${MP} PROPERTY LINK_LIBRARIES
    "-Wl,--start-group" ${GAMEMPLIB} ${ENTITIESMPLIB} ${SHADERSLIB} ${ENGINELIB} engine_safemath${MP} amp11lib${MP} ps4_vorbis
    "${SS_PS4_ROOT}/build/ps4-sdl/libSDL2.a"
    "${SS_NATIVE_STACK}/vulkan-ps4/libvulkan_ps4.a"
    "${SS_NATIVE_STACK}/opengnm/libopengnm.a"
    "${SS_NATIVE_STACK}/libpsbc-private.a"
    "${SS_PS4_ROOT}/build/ps4-usb/libss-jbc.a"
    "-Wl,--end-group" SceGnmDriver)
endif()
