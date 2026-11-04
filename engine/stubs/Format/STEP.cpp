// Replaces src/libslic3r/Format/STEP.cpp (OCCT) in the wasm engine. Model.cpp constructs a Step for
// .step files; STEP import needs OCCT, which this engine does not build, so load() and mesh() throw.
// Adapted from Muon3D's engine/stubs/Format/STEP.cpp (AGPL-3.0-only).
#include "libslic3r/Format/STEP.hpp"

#include "../Unsupported.hpp"

namespace Slic3r {

Step::Step(fs::path path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn)
    : m_path(path.string()), m_stepFn(std::move(stepFn)), m_utf8Fn(std::move(isUtf8Fn))
{}
Step::Step(std::string path, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn)
    : m_path(std::move(path)), m_stepFn(std::move(stepFn)), m_utf8Fn(std::move(isUtf8Fn))
{}
Step::~Step() = default;
Step::Step_Status Step::load() { browser_engine::throw_unsupported("STEP import"); }
Step::Step_Status Step::mesh(Model *, bool &, bool, double, double) { browser_engine::throw_unsupported("STEP import"); }
unsigned int Step::get_triangle_num(double, double) { browser_engine::throw_unsupported("STEP import"); }
unsigned int Step::get_triangle_num_tbb(double, double) { browser_engine::throw_unsupported("STEP import"); }
void Step::clean_mesh_data() {}
void Step::update_process(int, int, int, bool &) {}

bool StepPreProcessor::preprocess(const char *, std::string &) { browser_engine::throw_unsupported("STEP import"); }
bool StepPreProcessor::isUtf8File(const char *) { return true; }
bool StepPreProcessor::isUtf8(const std::string) { return true; }

} // namespace Slic3r
