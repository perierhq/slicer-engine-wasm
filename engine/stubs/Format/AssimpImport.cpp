// Replaces src/libslic3r/Format/AssimpImport.cpp (assimp) in the browser engine.
// Model::read_from_file() calls load_assimp_textured_model() for .glb/.gltf/.fbx files.
#include "libslic3r/Format/AssimpImport.hpp"

#include "../Unsupported.hpp"

namespace Slic3r {

bool load_assimp_textured_model(const std::string & /*path*/, TexturedMesh & /*out*/, std::string * /*error_message*/)
{
    browser_engine::throw_unsupported("glTF/GLB/FBX import");
}

} // namespace Slic3r
