# Third-party dependencies of Bambu Studio's libslic3r for the wasm engine.
# Adapted from Muon3D's engine/cmake/Dependencies.cmake (AGPL-3.0-only).
#  * the dependency prefix built by deps/build-deps.sh (Boost, CGAL, cereal, NLopt, libnoise,
#    libjpeg-turbo, oneTBB for mt) plus Emscripten's zlib/libpng ports, found through the prefix's
#    initial-cache.cmake;
#  * the libraries bundled in Bambu's src/ (and deps/EXPAT/expat), added from Bambu's own CMakeLists.

function(slic3r_remap_configs targets from_Cfg to_Cfg)
endfunction()

# Bambu's top-level CMakeLists defines encoding_check() (a source-encoding lint); nothing to do here.
function(encoding_check target)
endfunction()

function(engine_silence_warnings)
    foreach (target IN LISTS ARGN)
        if (NOT TARGET ${target})
            continue()
        endif ()
        get_target_property(type ${target} TYPE)
        if (NOT type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${target} PRIVATE -w)
        endif ()
    endforeach ()
endfunction()

find_package(Threads REQUIRED)

# Boost 1.84: Bambu's components plus the ones its sources use directly.
set(Boost_USE_STATIC_LIBS ON)
set(Boost_NO_SYSTEM_PATHS TRUE)
find_package(Boost 1.83.0 REQUIRED COMPONENTS
    system filesystem thread log log_setup locale regex chrono atomic date_time iostreams program_options nowide)
add_library(boost_headeronly INTERFACE)
add_library(boost_libs INTERFACE)
target_include_directories(boost_headeronly SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
if (NOT Boost_LIBRARIES)
    foreach (c system filesystem thread log log_setup locale regex chrono atomic date_time iostreams program_options nowide)
        list(APPEND Boost_LIBRARIES Boost::${c})
    endforeach ()
endif ()
target_link_libraries(boost_libs INTERFACE boost_headeronly ${Boost_LIBRARIES})
if (NOT TARGET Boost::boost)
    add_library(Boost::boost ALIAS boost_headeronly)
endif ()

find_package(cereal REQUIRED)
if (NOT TARGET cereal::cereal)
    add_library(cereal::cereal ALIAS cereal)
endif ()

find_package(NLopt 1.4 REQUIRED)
find_package(libnoise REQUIRED)

cmake_policy(PUSH)
set(CMAKE_POLICY_DEFAULT_CMP0167 NEW)
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)
find_package(CGAL REQUIRED)
unset(CMAKE_POLICY_DEFAULT_CMP0167)
cmake_policy(POP)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

if (ENGINE_THREADED)
    find_package(TBB 2021 CONFIG REQUIRED COMPONENTS tbb)
else ()
    add_library(engine_tbb_serial INTERFACE)
    target_include_directories(engine_tbb_serial SYSTEM INTERFACE "${ENGINE_DIR}/shims/tbb-serial")
    add_library(engine_tbbmalloc_serial INTERFACE)
    add_library(TBB::tbb ALIAS engine_tbb_serial)
    add_library(TBB::tbbmalloc ALIAS engine_tbbmalloc_serial)
endif ()

function(engine_without_tbbmalloc)
    if (NOT ENGINE_THREADED)
        return()
    endif ()
    foreach (target IN LISTS ARGN)
        foreach (property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(libs ${target} ${property})
            if (libs)
                string(REPLACE "TBB::tbbmalloc" "TBB::tbb" libs "${libs}")
                set_target_properties(${target} PROPERTIES ${property} "${libs}")
            endif ()
        endforeach ()
    endforeach ()
endfunction ()

# Bambu's bundled libraries (src/CMakeLists.txt), minus the GUI ones and build-utils/hints (host tools).
set(MCUT_BUILD_WITH_COMPUTE_HELPER_THREADPOOL OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_AS_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MCUT_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
set(SLIC3R_STATIC ON)
set(LIBDIR "${BAMBU_SRC}/src")

foreach (dir admesh boost clipper clipper2 miniz minilzo glu-libtess qhull Shiny semver libigl mcut libnest2d)
    add_subdirectory("${BAMBU_SRC}/src/${dir}" "${CMAKE_BINARY_DIR}/src/${dir}")
endforeach ()
engine_silence_warnings(admesh nowide clipper Clipper2 miniz_static minilzo_static glu-libtess qhullcpp qhullstatic_r qhullstatic qhull_static Shiny semver mcut libnest2d)
engine_without_tbbmalloc(clipper)

# Bambu's top-level CMakeLists builds the bundled expat when no system one is found (deps/EXPAT/expat).
add_subdirectory("${BAMBU_SRC}/deps/EXPAT/expat" "${CMAKE_BINARY_DIR}/deps/expat" EXCLUDE_FROM_ALL)
target_compile_options(expat PRIVATE -w)
target_compile_definitions(expat PRIVATE XML_DEV_URANDOM)   # Emscripten provides /dev/urandom
set(EXPAT_INCLUDE_DIRS "${BAMBU_SRC}/deps/EXPAT/expat")
set(EXPAT_LIBRARIES expat)
