# libslic3r and libslic3r_cgal as static libraries, built from BAMBU_SRC with the settings of Bambu's
# src/libslic3r/CMakeLists.txt minus the desktop-only parts (BambuSources.cmake), plus shims and stubs.
# Adapted from Muon3D's engine/cmake/Libslic3r.cmake (AGPL-3.0-only).

include(BambuSources)

set(LIBSLIC3R_DIR "${BAMBU_SRC}/src/libslic3r")
set(LIBSLIC3R_GEN_DIR "${CMAKE_BINARY_DIR}/libslic3r")

# libslic3r_version.h as the released app has it (BuildMac.sh: BBL_RELEASE_TO_PUBLIC=1,
# BBL_INTERNAL_TESTING=0). SLIC3R_VERSION ends up in the G-code header.
include("${BAMBU_SRC}/version.inc")
set(BBL_RELEASE_TO_PUBLIC 1)
set(BBL_INTERNAL_TESTING 0)
set(SLIC3R_BUILD_TIME "")
set(SLIC3R_COMPILE_VERSION "")
configure_file("${LIBSLIC3R_DIR}/libslic3r_version.h.in" "${LIBSLIC3R_GEN_DIR}/libslic3r_version.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BAMBU_SRC}/version.inc")

bambu_generate_drainhole_source("${LIBSLIC3R_GEN_DIR}/stubs/SLA/Hollowing.cpp")

bambu_libslic3r_sources(LIBSLIC3R_ALL)
set(LIBSLIC3R_KEPT ${LIBSLIC3R_ALL})
foreach (excluded IN LISTS BAMBU_LIBSLIC3R_EXCLUDED)
    list(FIND LIBSLIC3R_ALL "${excluded}" index)
    if (index EQUAL -1)
        message(WARNING "Excluded libslic3r source ${excluded} is no longer in Bambu's source list.")
    endif ()
    list(REMOVE_ITEM LIBSLIC3R_KEPT "${excluded}")
endforeach ()
list(TRANSFORM LIBSLIC3R_KEPT PREPEND "${LIBSLIC3R_DIR}/")
list(LENGTH LIBSLIC3R_ALL n_all)
list(LENGTH LIBSLIC3R_KEPT n_kept)
message(STATUS "libslic3r: ${n_kept} of Bambu's ${n_all} sources (the rest are excluded or stubbed)")

# GUI-library source libslic3r needs: FlushVolCalc.cpp calls RGB2HSV() from
# src/slic3r/Utils/ColorSpaceConvert.cpp; compiled unchanged with a small wxColour stand-in.
set(BAMBU_GUI_SOURCES_NEEDED "${BAMBU_SRC}/src/slic3r/Utils/ColorSpaceConvert.cpp")
set_source_files_properties(${BAMBU_GUI_SOURCES_NEEDED} PROPERTIES
    INCLUDE_DIRECTORIES "${ENGINE_DIR}/shims/wx"
    SKIP_PRECOMPILE_HEADERS ON)

file(GLOB ENGINE_STUB_SOURCES CONFIGURE_DEPENDS "${ENGINE_DIR}/stubs/*.cpp" "${ENGINE_DIR}/stubs/*.c" "${ENGINE_DIR}/stubs/Format/*.cpp")
list(APPEND ENGINE_STUB_SOURCES "${LIBSLIC3R_GEN_DIR}/stubs/SLA/Hollowing.cpp")

add_library(engine_shims INTERFACE)
target_include_directories(engine_shims SYSTEM INTERFACE "${ENGINE_DIR}/shims/common" "${ENGINE_DIR}/shims/occt")

# --- libslic3r_cgal ----------------------------------------------------------------------------------------------
add_library(libslic3r_cgal STATIC
    "${LIBSLIC3R_DIR}/CutSurface.cpp"
    "${LIBSLIC3R_DIR}/IntersectionPoints.cpp"
    "${LIBSLIC3R_DIR}/MeshBoolean.cpp"
    "${LIBSLIC3R_DIR}/TryCatchSignal.cpp"
    "${LIBSLIC3R_DIR}/Triangulation.cpp"
)
target_include_directories(libslic3r_cgal PRIVATE "${LIBSLIC3R_GEN_DIR}")
target_include_directories(libslic3r_cgal SYSTEM BEFORE PRIVATE "${ENGINE_DIR}/shims/cgal")
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set(_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
endif ()
target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} admesh libigl mcut boost_libs TBB::tbb engine_shims)

# --- libslic3r -----------------------------------------------------------------------------------------------------
add_library(libslic3r STATIC ${LIBSLIC3R_KEPT} ${BAMBU_GUI_SOURCES_NEEDED} ${ENGINE_STUB_SOURCES})
target_compile_definitions(libslic3r PUBLIC USE_TBB TBB_USE_CAPTURED_EXCEPTION=0)
target_compile_features(libslic3r PUBLIC cxx_std_17)
target_include_directories(libslic3r
    PRIVATE "${LIBSLIC3R_DIR}" "${LIBSLIC3R_DIR}/TextureToColor"
    PUBLIC "${LIBSLIC3R_GEN_DIR}")
target_include_directories(libslic3r SYSTEM PUBLIC ${EXPAT_INCLUDE_DIRS} "${BAMBU_SRC}/src")

# Bambu's link list without the desktop-only libraries (OCCT, opencv_world, assimp, freetype, OpenVDB).
target_link_libraries(libslic3r
    PUBLIC
        noise::noise libnest2d admesh cereal::cereal libigl miniz boost_libs clipper nowide
        ${EXPAT_LIBRARIES} glu-libtess qhull semver TBB::tbb TBB::tbbmalloc libslic3r_cgal
        PNG::PNG ZLIB::ZLIB JPEG::JPEG Clipper2 mcut engine_shims
)
engine_without_tbbmalloc(libslic3r)

if (ENGINE_PCH)
    target_precompile_headers(libslic3r PRIVATE "${LIBSLIC3R_DIR}/pchheader.hpp")
    target_compile_options(libslic3r PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
    foreach (c IN LISTS ENGINE_STUB_SOURCES)
        if (c MATCHES "\\.c$")
            set_source_files_properties("${c}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif ()
    endforeach ()
endif ()
