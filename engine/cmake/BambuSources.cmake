# Source selection for the out-of-tree libslic3r build (adapted from Muon3D's OrcaSources.cmake).
#
# The file list is read from Bambu Studio's own src/libslic3r/CMakeLists.txt at configure time: only the
# unconditional `set(lisbslic3r_sources ...)` block is used (the APPLE block adds Objective-C files).

function(bambu_libslic3r_sources out_var)
    set(cmake_file "${BAMBU_SRC}/src/libslic3r/CMakeLists.txt")
    file(READ "${cmake_file}" text)
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    string(REGEX MATCH "set\\(lisbslic3r_sources([^)]*)\\)" block "${text}")
    if (NOT block)
        message(FATAL_ERROR "Could not find `set(lisbslic3r_sources ...)` in ${cmake_file}.")
    endif ()
    string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.cpp" sources "${CMAKE_MATCH_1}")
    list(REMOVE_DUPLICATES sources)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cmake_file}")
    set(${out_var} "${sources}" PARENT_SCOPE)
endfunction()

# Sources the wasm engine does not compile, each with the reason. Files whose symbols kept code still
# references are replaced by a stub in engine/stubs/ (same relative path).
set(BAMBU_LIBSLIC3R_EXCLUDED
    # OCCT (Open CASCADE): STEP and SVG import, text shapes.
    Format/STEP.cpp                    # stub
    Format/svg.cpp                     # stub
    Shape/TextShape.cpp                # unreferenced (GUI text tool)
    # assimp.
    Format/AssimpImport.cpp            # stub
    # OpenCV: texture-to-colour tools (GUI only).
    ObjColorUtils.cpp
    TexturePainting.cpp
    TextureToColor/TextureToColor.cpp
    TextureToColor/ColorUtils.cpp
    # OpenVDB: SLA hollowing, and the SLA print pipeline built on it.
    SLA/Hollowing.cpp                  # stub: sla::DrainHole members, generated from Bambu's own file
    SLAPrint.cpp
    SLAPrintSteps.cpp
    Format/SL1.cpp
    # Encrypted log files (OpenSSL AES, libcurl key download): the engine logs to stderr only.
    LogSink.cpp                        # stub
    # static_assert(false, "Unknown platform detected") on Emscripten.
    Platform.cpp                       # stub
    pchheader.cpp
)

# Writes the replacement for SLA/Hollowing.cpp: the sla::DrainHole member definitions copied verbatim
# from Bambu's own file (plain geometry used by FFF code; the rest of the file needs OpenVDB).
function(bambu_generate_drainhole_source out_file)
    set(src_file "${BAMBU_SRC}/src/libslic3r/SLA/Hollowing.cpp")
    file(READ "${src_file}" text)
    set(begin_marker "indexed_triangle_set DrainHole::to_mesh() const")
    set(end_marker "void cut_drainholes(")
    string(FIND "${text}" "${begin_marker}" begin_pos)
    string(FIND "${text}" "${end_marker}" end_pos)
    if (begin_pos EQUAL -1 OR end_pos EQUAL -1 OR NOT end_pos GREATER begin_pos)
        message(FATAL_ERROR "Could not find the sla::DrainHole member definitions in ${src_file}.")
    endif ()
    math(EXPR length "${end_pos} - ${begin_pos}")
    string(SUBSTRING "${text}" ${begin_pos} ${length} ORCA_DRAINHOLE_METHODS)
    string(STRIP "${ORCA_DRAINHOLE_METHODS}" ORCA_DRAINHOLE_METHODS)
    set(ORCA_HOLLOWING_SOURCE "${src_file}")
    configure_file("${ENGINE_DIR}/stubs/SLA/Hollowing.cpp.in" "${out_file}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src_file}")
endfunction()
