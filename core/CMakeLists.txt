# libnimblepdf-core — toolkit-free. Renders PDF pages to RGBA buffers via
# poppler-cpp (the stable, ABI-neutral C++ API). No Qt, no OS GUI calls; this
# is the layer shared by every frontend (Qt, Haiku, KevRexOS).
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

add_library(nimblepdf-core STATIC
	Document.cpp
)
target_include_directories(nimblepdf-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nimblepdf-core PUBLIC PkgConfig::POPPLER_CPP)
target_compile_features(nimblepdf-core PUBLIC cxx_std_17)
