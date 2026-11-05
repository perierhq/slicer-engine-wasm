# The engine module: bridge/engine_main.cpp linked with libslic3r into an ES6 module factory
# engine-<variant>.mjs + .wasm. Adapted from Muon3D's engine/cmake/Engine.cmake (AGPL-3.0-only).

# Runtime resource files libslic3r reads through resources_dir() on the slicing path, embedded into
# MEMFS under /resources.
set(ENGINE_RESOURCE_PATTERNS
    info/*.json
    flush/*
    filament_mixing/*.json
)
set(ENGINE_RESOURCE_FILES)
foreach (pattern IN LISTS ENGINE_RESOURCE_PATTERNS)
    file(GLOB matches CONFIGURE_DEPENDS RELATIVE "${BAMBU_SRC}/resources" "${BAMBU_SRC}/resources/${pattern}")
    if (NOT matches)
        message(FATAL_ERROR "No runtime resource matches ${BAMBU_SRC}/resources/${pattern}.")
    endif ()
    list(APPEND ENGINE_RESOURCE_FILES ${matches})
endforeach ()

add_executable(bambu_engine "${ENGINE_DIR}/bridge/engine_main.cpp")
target_link_libraries(bambu_engine PRIVATE libslic3r boost_libs)
set_target_properties(bambu_engine PROPERTIES
    OUTPUT_NAME "engine-${ENGINE_VARIANT}"
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")

set(ENGINE_MAXIMUM_MEMORY 4GB)
set(ENGINE_LINK_OPTIONS
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createBambuEngine
    -sENVIRONMENT=web,worker,node
    "-sEXPORTED_FUNCTIONS=_engine_run,_engine_slice,_engine_init"
    -sEXPORTED_RUNTIME_METHODS=FS,ccall,wasmMemory,UTF8ToString
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sMAXIMUM_MEMORY=${ENGINE_MAXIMUM_MEMORY}
    -sSTACK_SIZE=16MB
    -sINVOKE_RUN=0
    -sEXIT_RUNTIME=0
    --emit-symbol-map
    "SHELL:--extern-pre-js \"${ENGINE_DIR}/cmake/licence-banner.js\""
)
if (ENGINE_THREADED)
    list(APPEND ENGINE_LINK_OPTIONS -pthread -sMALLOC=mimalloc "-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency+4"
        -sPTHREAD_POOL_SIZE_STRICT=2 -sDEFAULT_PTHREAD_STACK_SIZE=8MB -sGROWABLE_ARRAYBUFFERS=1)
else ()
    list(APPEND ENGINE_LINK_OPTIONS -sMALLOC=dlmalloc)
endif ()

foreach (resource IN LISTS ENGINE_RESOURCE_FILES)
    list(APPEND ENGINE_LINK_OPTIONS "SHELL:--embed-file \"${BAMBU_SRC}/resources/${resource}@/resources/${resource}\"")
endforeach ()
target_link_options(bambu_engine PRIVATE ${ENGINE_LINK_OPTIONS} ${ENGINE_EXTRA_LINK_OPTIONS})
