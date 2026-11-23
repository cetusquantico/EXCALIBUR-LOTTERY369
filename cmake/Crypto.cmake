include(CheckCXXCompilerFlag)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SECP REQUIRED IMPORTED_TARGET libsecp256k1)

# Prefer the static reference library if shipped by the distribution.
find_file(SECP_ARCHIVE NAMES libsecp256k1.a HINTS ${SECP_LIBRARY_DIRS})
set(EXLOTTERY_FIELD "5x52" CACHE STRING "Field representation for the EXCALIBUR369 EC core")
set_property(CACHE EXLOTTERY_FIELD PROPERTY STRINGS 5x52 4x64)
set(ex_core ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ex369)
add_library(exlottery_crypto STATIC
  src/crypto.cpp
  ${ex_core}/ec/group.cpp
  ${ex_core}/hash/sha256.cpp
  ${ex_core}/hash/ripemd160.cpp
  ${ex_core}/hash/hash_multi.cpp
  ${ex_core}/cpu/cpu.cpp)
target_include_directories(exlottery_crypto PUBLIC include PRIVATE ${ex_core})
target_link_libraries(exlottery_crypto PRIVATE OpenSSL::Crypto Threads::Threads)
if(SECP_ARCHIVE)
  target_include_directories(exlottery_crypto PRIVATE ${SECP_INCLUDE_DIRS})
  target_link_libraries(exlottery_crypto PRIVATE ${SECP_ARCHIVE})
else()
  target_link_libraries(exlottery_crypto PRIVATE PkgConfig::SECP)
endif()
if(EXLOTTERY_FIELD STREQUAL "4x64")
  target_compile_definitions(exlottery_crypto PRIVATE EX369_FIELD_4X64)
elseif(NOT EXLOTTERY_FIELD STREQUAL "5x52")
  message(FATAL_ERROR "EXLOTTERY_FIELD must be 5x52 or 4x64")
endif()
option(EXLOTTERY_SSE "Compile SSSE3 multi-buffer hashing with runtime dispatch" ON)
check_cxx_compiler_flag("-mssse3" EXLOTTERY_HAS_SSSE3_FLAG)
if(EXLOTTERY_SSE AND EXLOTTERY_HAS_SSSE3_FLAG)
  target_sources(exlottery_crypto PRIVATE ${ex_core}/hash/simd/backend_sse.cpp)
  set_source_files_properties(${ex_core}/hash/simd/backend_sse.cpp
    PROPERTIES COMPILE_OPTIONS "-mssse3;-fno-lto")
  target_compile_definitions(exlottery_crypto PRIVATE EX369_HAVE_SSE)
endif()
