cmake_minimum_required(VERSION 3.20)
project(nv-broadcast-linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_CUDA "Build CUDA GPU denoiser backend (requires CUDA toolkit)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(RNNOISE IMPORTED_TARGET rnnoise)

if(NOT PIPEWIRE_FOUND)
  message(WARNING "libpipewire-0.3 not found via pkg-config; daemon builds in stub mode")
endif()
if(NOT RNNOISE_FOUND)
  message(WARNING "rnnoise not found via pkg-config; RNNoise backend becomes passthrough")
endif()

if(ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    add_compile_definitions(NVB_HAVE_CUDA=1)
  else()
    message(FATAL_ERROR "ENABLE_CUDA=ON but no CUDA compiler (nvcc) found. Install 'cuda' package.")
  endif()
endif()

# ---- common lib (config + device scan, no audio deps) ----
add_library(nvb_common STATIC
  src/common/config.cpp
  src/common/devices.cpp
  src/common/status.cpp
)
target_include_directories(nvb_common PUBLIC src src/common)

# ---- denoiser backends ----
add_library(nvb_denoise STATIC
  src/rnnoise_backend.cpp
  src/cuda_backend.cpp
  src/maxine_backend.cpp
)
target_link_libraries(nvb_denoise PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(nvb_denoise PUBLIC src)
target_link_libraries(nvb_denoise PUBLIC nvb_common)
if(RNNOISE_FOUND)
  target_link_libraries(nvb_denoise PUBLIC PkgConfig::RNNOISE)
  target_compile_definitions(nvb_denoise PUBLIC NVB_HAVE_RNNOISE=1)
endif()

# ---- daemon ----
add_executable(nvbcastd src/nvbcastd.cpp)
target_link_libraries(nvbcastd PRIVATE nvb_common nvb_denoise)
if(PIPEWIRE_FOUND)
  target_link_libraries(nvbcastd PRIVATE PkgConfig::PIPEWIRE)
  target_compile_definitions(nvbcastd PRIVATE NVB_HAVE_PIPEWIRE=1)
endif()

# ---- CLI ----
add_executable(nvbcast src/nvbcast.cpp)
target_link_libraries(nvbcast PRIVATE nvb_common)

install(TARGETS nvbcastd nvbcast RUNTIME DESTINATION bin)

# ---- GUI (GTK4 + libadwaita, optional) ----
pkg_check_modules(GTK IMPORTED_TARGET gtk4 libadwaita-1)
if(GTK_FOUND)
  add_executable(nvbcast-gui src/gui/nvbcast_gui.cpp)
  target_link_libraries(nvbcast-gui PRIVATE nvb_common PkgConfig::GTK)
  install(TARGETS nvbcast-gui RUNTIME DESTINATION bin)
else()
  message(WARNING "gtk4/libadwaita-1 not found; skipping nvbcast-gui")
endif()

enable_testing()
add_executable(nvb_tests tests/test_denoise.cpp)
target_link_libraries(nvb_tests PRIVATE nvb_common nvb_denoise)
add_test(NAME denoise_frames COMMAND nvb_tests)
