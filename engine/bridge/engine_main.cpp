// The slicing path of Bambu Studio's command line (src/BambuStudio.cpp, CLI::run), for
//   BambuStudio --slice <plate> --outputdir <dir> <project.3mf>
// "BS:<line>" refers to src/BambuStudio.cpp at v02.08.02.61. The desktop CLI gets each plate's Print from the GUI class
// PartPlateList (src/slic3r/GUI/PartPlate.cpp); its effect is ported below without the GUI: plate origins, the plate's
// 3MF config, the build volume moved to the plate.
//
// Two kinds of project go in:
//   * a Bambu Studio project with its full configuration (what Bambu Studio writes: it has `inherits_group` /
//     `different_settings_to_system`). It is sliced as the CLI slices it.
//   * a project that only names its presets (`printer_settings_id`, `print_settings_id`, `filament_settings_id`) and
//     carries the values it changes. The engine resolves the named system presets from the bundled profiles with Bambu's
//     own PresetBundle (inherits, includes, filament variants), applies the project's values on top, and computes the
//     flush matrix from the filament colours when the project has none (BS:3750-3925, FlushVolCalculator). This stands in
//     for the CLI's --load-settings / --load-filaments with flattened presets.
//
// Ported: the 3MF load (BS:1876-1891), the version fix-ups (BS:1910-1952), the flush matrix (BS:3750-3925), config
// normalisation (BS:4082-4111), the wrapping-detection and skirt-per-object resets (BS:4159-4171), plate sizes and
// origins (BS:4215-4320, PartPlateList), the "keep old params" branch for pre-2.0 files (BS:4327-4395), the old-file
// plate translation (BS:4491-4508), the per-plate pre-check and slicing loop for one plate or all of them (BS:6460-7400:
// build volume, checks, prime volume, plate config, filament maps, apply, validate, process, export, the per-plate result).
//
// Not ported (the engine reports them as unsupported where it matters): machine switches and arrange/orient, multi-
// extruder filament mapping (BS:6563-6820, H2D-class printers), skip lists, cached slice data, thumbnails (Bambu printers
// get none on the CLI path either: BS:7174), 3MF export.
//
// SPDX-License-Identifier: AGPL-3.0-only
#include <libslic3r/libslic3r.h>
#include <libslic3r/BuildVolume.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/ExtrusionEntity.hpp>
#include <libslic3r/FlushVolCalc.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/ProjectTask.hpp>
#include <libslic3r/Semver.hpp>
#include <libslic3r/Utils.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#define ENGINE_EXPORT EMSCRIPTEN_KEEPALIVE
// Progress for the host page: Module.onProgress(percent, stage, plate, plateCount), only from the thread that runs the
// slice (worker threads have no host to talk to).
EM_JS(void, engine_js_progress, (int percent, const char *stage, int plate, int plate_count), {
    if (Module["onProgress"]) Module["onProgress"](percent, UTF8ToString(stage), plate, plate_count);
});
#else
#define ENGINE_EXPORT
static void engine_js_progress(int, const char *, int, int) {}
#endif

using namespace Slic3r;
using Json = nlohmann::ordered_json;

namespace {

struct Failure {
    int         code;
    std::string message;
    int         plate = 0;
};

double ms_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------------------------------------------------------------
// Progress: Bambu's print status (Print::set_status, 0-100 per plate) spread over the plates being sliced.

struct Progress {
    int plate       = 0; // 1-based plate being sliced
    int plate_count = 1; // plates sliced in this job
    int done        = 0; // plates finished
    int last        = -1;

    void report(int plate_percent, const std::string &stage)
    {
#ifdef __EMSCRIPTEN__
        if (!emscripten_is_main_runtime_thread())
            return;
#endif
        plate_percent = std::clamp(plate_percent, 0, 100);
        int overall   = int((done * 100.0 + plate_percent) / std::max(plate_count, 1));
        overall       = std::max(overall, last); // Bambu's own percentages can step back between stages
        last          = overall;
        engine_js_progress(overall, stage.c_str(), plate, plate_count);
    }
};
Progress g_progress;
std::vector<PrintBase::SlicingStatus> g_slicing_warnings;

void status_callback(const PrintBase::SlicingStatus &status)
{
    if (status.warning_step != -1)
        g_slicing_warnings.push_back(status);
    if (status.percent >= 0)
        g_progress.report(status.percent, status.text);
}

// ---------------------------------------------------------------------------------------------------------------------
// Preset resolution: the named system presets from the bundled profiles, as Bambu Studio's PresetBundle loads them.

constexpr const char *VENDOR = "BBL";

// Keys a project sets that name or describe presets rather than set values.
const std::set<std::string> PRESET_IDENTITY_KEYS = {
    "printer_settings_id", "print_settings_id", "filament_settings_id", "inherits", "inherits_group",
    "different_settings_to_system", "compatible_printers", "compatible_printers_condition", "compatible_prints",
    "compatible_prints_condition", "print_compatible_printers", "filament_ids", "filament_self_index",
    "filament_extruder_variant", "print_extruder_id", "print_extruder_variant", "printer_extruder_id",
    "printer_extruder_variant", "extruder_variant_list", "version", "name", "from", "setting_id",
};

bool is_full_project_config(const DynamicPrintConfig &config)
{
    // Bambu Studio writes both into every project (PresetBundle::full_fff_config); a project that only names presets
    // has neither.
    return config.has("inherits_group") || config.has("different_settings_to_system");
}

Json read_json(const std::string &path)
{
    boost::nowide::ifstream ifs(path);
    if (!ifs)
        throw Failure{20, "cannot read " + path};
    Json j;
    ifs >> j;
    return j;
}

struct Resolved {
    DynamicPrintConfig       config;
    std::vector<std::string> notes;
};

// Loads only the presets the project names, plus their parents and includes, with
// PresetBundle::load_vendor_configs_from_json on a vendor index cut down to them: the same code path as Bambu Studio
// loading resources/profiles, at a fraction of the cost.
Resolved resolve_presets(const DynamicPrintConfig &project, const ConfigSubstitutions &substitutions)
{
    Resolved out;
    const std::string profiles = resources_dir() + "/profiles";
    if (!boost::filesystem::exists(profiles + "/" + VENDOR + ".json"))
        throw Failure{21, "the project names presets but no profiles are installed (" + profiles + "/BBL.json)"};

    auto opt_string = [&](const char *key) -> std::string {
        if (auto *o = project.option<ConfigOptionString>(key))
            return o->value;
        if (auto *o = project.option<ConfigOptionStrings>(key); o && !o->values.empty())
            return o->values.front();
        return {};
    };
    const std::string printer_name = opt_string("printer_settings_id");
    const std::string print_name   = opt_string("print_settings_id");
    std::vector<std::string> filament_names;
    if (auto *o = project.option<ConfigOptionStrings>("filament_settings_id"))
        filament_names = o->values;
    if (printer_name.empty() || print_name.empty() || filament_names.empty())
        throw Failure{22, "the project has no full configuration and does not name its printer, process and filament presets"};

    // The vendor index and the subset of it this project needs.
    Json vendor = read_json(profiles + "/" + VENDOR + ".json");
    struct Entry { std::string list, sub_path; };
    std::map<std::string, std::map<std::string, std::string>> sub_paths; // list -> name -> sub_path
    for (const char *list : {"machine_list", "process_list", "filament_list"})
        for (auto &item : vendor[list])
            sub_paths[list][item["name"].get<std::string>()] = item["sub_path"].get<std::string>();
    std::map<std::string, std::set<std::string>> needed;
    std::function<void(const std::string &, const std::string &, int)> need = [&](const std::string &list, const std::string &name, int depth) {
        if (depth > 32 || needed[list].count(name))
            return;
        auto it = sub_paths[list].find(name);
        if (it == sub_paths[list].end())
            throw Failure{23, "no system preset is called \"" + name + "\" (" + list.substr(0, list.find('_')) + ")"};
        needed[list].insert(name);
        Json data = read_json(profiles + "/" + VENDOR + "/" + it->second);
        if (data.contains("inherits") && data["inherits"].is_string() && !data["inherits"].get<std::string>().empty())
            need(list, data["inherits"].get<std::string>(), depth + 1);
        if (data.contains("include")) {
            if (data["include"].is_string())
                need(list, data["include"].get<std::string>(), depth + 1);
            else if (data["include"].is_array())
                for (auto &inc : data["include"])
                    if (inc.is_string())
                        need(list, inc.get<std::string>(), depth + 1);
        }
    };
    need("machine_list", printer_name, 0);
    need("process_list", print_name, 0);
    for (const std::string &f : filament_names)
        need("filament_list", f, 0);
    for (const char *list : {"machine_list", "process_list", "filament_list"}) {
        Json kept = Json::array();
        for (auto &item : vendor[list])
            if (needed[list].count(item["name"].get<std::string>()))
                kept.push_back(item);
        vendor[list] = kept;
    }
    const std::string index_dir = "/tmp/engine-profiles";
    boost::filesystem::remove_all(index_dir);
    boost::filesystem::create_directories(index_dir);
    boost::nowide::ofstream(index_dir + "/" + VENDOR + ".json") << vendor.dump();
    boost::filesystem::create_symlink(profiles + "/" + VENDOR, index_dir + "/" + VENDOR);

    PresetBundle bundle;
    auto [substitutions_loaded, loaded] =
        bundle.load_vendor_configs_from_json(index_dir, VENDOR, PresetBundle::LoadSystem, ForwardCompatibilitySubstitutionRule::EnableSilent);
    (void) substitutions_loaded;
    BOOST_LOG_TRIVIAL(info) << "engine: loaded " << loaded << " system presets for the project";

    auto select = [](PresetCollection &collection, const std::string &name, const char *what) {
        if (!collection.find_preset(name, false))
            throw Failure{23, std::string("the ") + what + " preset \"" + name + "\" did not load from the profiles"};
        collection.select_preset_by_name(name, true);
    };
    select(bundle.printers, printer_name, "printer");
    select(bundle.prints, print_name, "process");
    for (const std::string &f : filament_names)
        select(bundle.filaments, f, "filament");
    bundle.filaments.select_preset_by_name(filament_names.front(), true);
    bundle.filament_presets = filament_names;

    // Compatibility is Bambu Studio's business in its window; the engine slices what it is given and says so.
    const Preset &printer = bundle.printers.get_selected_preset();
    auto compatible = [&](const Preset &p) {
        auto *cp = p.config.option<ConfigOptionStrings>("compatible_printers");
        return !cp || cp->values.empty() || std::find(cp->values.begin(), cp->values.end(), printer.name) != cp->values.end();
    };
    if (!compatible(bundle.prints.get_selected_preset()))
        out.notes.push_back("process preset \"" + print_name + "\" is not listed for printer \"" + printer_name + "\"");
    for (const std::string &f : filament_names)
        if (const Preset *p = bundle.filaments.find_preset(f, false); p && !compatible(*p))
            out.notes.push_back("filament preset \"" + f + "\" is not listed for printer \"" + printer_name + "\"");

    // The full configuration as the presets combine it, with one value per extruder variant (as --load-settings gives
    // the CLI); Print::apply picks the variant later.
    DynamicPrintConfig full = bundle.full_config(false);

    // The project's own values on top. A value the engine could not read (it was substituted on load) is left to the
    // preset rather than replaced with Bambu's default.
    std::set<std::string> substituted;
    for (const ConfigSubstitution &s : substitutions)
        if (s.opt_def)
            substituted.insert(s.opt_def->opt_key);
    const size_t filament_count = filament_names.size();
    std::vector<int> self_index;
    if (auto *o = full.option<ConfigOptionInts>("filament_self_index"))
        self_index = o->values;
    std::vector<std::string> overridden;
    for (const std::string &key : project.keys()) {
        if (PRESET_IDENTITY_KEYS.count(key))
            continue;
        if (substituted.count(key)) {
            out.notes.push_back("kept the preset's " + key + ": the project's value is not one Bambu Studio reads");
            continue;
        }
        const ConfigOption *src = project.option(key);
        ConfigOption       *dst = full.option(key, false);
        if (!dst || src->is_scalar()) {
            full.set_key_value(key, src->clone());
            overridden.push_back(key);
            continue;
        }
        auto *vsrc = static_cast<const ConfigOptionVectorBase *>(src);
        auto *vdst = static_cast<ConfigOptionVectorBase *>(dst);
        const size_t n = vsrc->size(), m = vdst->size();
        if (n == 0 || n == m) {
            dst->set(src);
        } else if (filament_options_with_variant.count(key) && n == filament_count && self_index.size() == m) {
            // One value per filament, the preset has one per filament and extruder variant.
            for (size_t i = 0; i < m; ++i)
                vdst->set_at(vsrc, i, size_t(std::max(self_index[i] - 1, 0)));
        } else if (n == 1 && (print_options_with_variant.count(key) || printer_options_with_variant_1.count(key) ||
                              filament_options_with_variant.count(key))) {
            // One value, the preset has one per extruder variant: it applies to every variant.
            for (size_t i = 0; i < m; ++i)
                vdst->set_at(vsrc, i, 0);
        } else if (n == 2 && printer_options_with_variant_2.count(key) && m % 2 == 0) {
            // Machine limits: normal and silent mode, per variant.
            for (size_t i = 0; i < m; ++i)
                vdst->set_at(vsrc, i, i % 2);
        } else {
            dst->set(src);
        }
        overridden.push_back(key);
    }
    BOOST_LOG_TRIVIAL(info) << "engine: the project sets " << overridden.size() << " values over its presets";

    // As the CLI's --load-settings path leaves them for system presets: no parents, no differences (BS:3198-3201).
    full.option<ConfigOptionStrings>("inherits_group", true)->values.assign(filament_count + 2, std::string());
    full.option<ConfigOptionStrings>("different_settings_to_system", true)->values.assign(filament_count + 2, std::string());
    out.config = std::move(full);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Flush volumes for projects without a matrix: GUI::get_min_flush_volumes (src/slic3r/GUI/Plater.cpp) and the CLI's
// colour-based matrix (BS:3750-3925), with BitmapCache::parse_color4 (src/slic3r/GUI/BitmapCache.cpp).

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool parse_color4(const std::string &scolor, unsigned char *rgba_out)
{
    rgba_out[0] = rgba_out[1] = rgba_out[2] = 0;
    rgba_out[3] = 255;
    if ((scolor.size() != 7 && scolor.size() != 9) || scolor.front() != '#')
        return false;
    const char *c = scolor.data() + 1;
    for (size_t i = 0; i < scolor.size() / 2; ++i) {
        int d1 = hex_digit(*c++), d2 = hex_digit(*c++);
        if (d1 == -1 || d2 == -1)
            return false;
        rgba_out[i] = (unsigned char) (d1 * 16 + d2);
    }
    return true;
}

std::vector<int> get_min_flush_volumes(const DynamicPrintConfig &full_config, size_t nozzle_id)
{
    std::vector<int> extra_flush_volumes;
    const ConfigOptionFloatsNullable *nozzle_volume_opt = full_config.option<ConfigOptionFloatsNullable>("nozzle_volume");
    int nozzle_volume_val = nozzle_volume_opt ? (int) nozzle_volume_opt->get_at(nozzle_id) : 0;

    const ConfigOptionInt *enable_long_retraction_when_cut_opt = full_config.option<ConfigOptionInt>("enable_long_retraction_when_cut");
    int machine_enabled_level = enable_long_retraction_when_cut_opt ? enable_long_retraction_when_cut_opt->value : 0;
    const ConfigOptionBoolsNullable *long_retractions_when_cut_opt = full_config.option<ConfigOptionBoolsNullable>("long_retractions_when_cut");
    bool machine_activated = long_retractions_when_cut_opt ? long_retractions_when_cut_opt->values[nozzle_id] == 1 : false;

    size_t filament_size = full_config.option<ConfigOptionFloats>("filament_diameter")->values.size();
    std::vector<double> filament_retraction_distance_when_cut(filament_size, 18.0f), printer_retraction_distance_when_cut(filament_size, 18.0f);
    std::vector<unsigned char> filament_long_retractions_when_cut(filament_size, 0);
    if (auto *o = full_config.option<ConfigOptionFloatsNullable>("filament_retraction_distances_when_cut"))
        filament_retraction_distance_when_cut = o->values;
    if (auto *o = full_config.option<ConfigOptionFloatsNullable>("retraction_distances_when_cut"))
        printer_retraction_distance_when_cut = o->values;
    if (auto *o = full_config.option<ConfigOptionBoolsNullable>("filament_long_retractions_when_cut"))
        filament_long_retractions_when_cut = o->values;

    for (size_t idx = 0; idx < filament_size; ++idx) {
        int extra_flush_volume = nozzle_volume_val;
        int retract_length = machine_enabled_level && machine_activated ? printer_retraction_distance_when_cut[nozzle_id] : 0;
        unsigned char filament_activated = filament_long_retractions_when_cut[idx];
        double filament_retract_length = filament_retraction_distance_when_cut[idx];
        if (filament_activated == 0)
            retract_length = 0;
        else if (filament_activated == 1 && machine_enabled_level == LongRectrationLevel::EnableFilament) {
            if (!std::isnan(filament_retract_length))
                retract_length = (int) filament_retraction_distance_when_cut[idx];
            else
                retract_length = printer_retraction_distance_when_cut[nozzle_id];
        }
        extra_flush_volume -= PI * 1.75 * 1.75 / 4 * retract_length;
        extra_flush_volumes.emplace_back(extra_flush_volume);
    }
    return extra_flush_volumes;
}

// BS:3750-3925 without the CLI's --filament-colour overrides: the project's own colours.
void compute_flush_matrix(DynamicPrintConfig &config, int extruder_count, int printer_variant_count)
{
    auto *colors_opt = config.option<ConfigOptionStrings>("filament_colour");
    if (!colors_opt || colors_opt->values.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "engine: no filament colours, no flush matrix";
        return;
    }
    const std::vector<std::string> &colors = colors_opt->values;
    const size_t count = colors.size();
    ConfigOptionBools *filament_is_support = config.option<ConfigOptionBools>("filament_is_support", true);
    const std::vector<int> min_flush_volumes = get_min_flush_volumes(config, 0);
    if (filament_is_support->size() != count)
        throw Failure{24, "filament_is_support has " + std::to_string(filament_is_support->size()) + " values for " + std::to_string(count) + " filaments"};

    std::vector<double> &matrix = config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values;
    matrix.resize(count * count * extruder_count, 0.f);
    const std::vector<std::string> &ids = config.option<ConfigOptionStrings>("filament_ids", true)->values;
    auto id_of = [&ids](int i) { return (i >= 0 && i < (int) ids.size()) ? ids[i] : std::string(); };
    config.option<ConfigOptionFloats>("flush_multiplier", true)->values.resize(extruder_count, 1.f);
    config.option<ConfigOptionFloats>("flush_multiplier_fast", true)->values.resize(extruder_count, 1.2f);

    std::vector<int> nozzle_flush_dataset(extruder_count, 0);
    {
        std::vector<int> dataset_full = config.option<ConfigOptionIntsNullable>("nozzle_flush_dataset", true)->values;
        dataset_full.resize(config.has("printer_extruder_variant") ? printer_variant_count : 1, 0);
        std::vector<int> extruders = config.has("extruder_type") ? config.option<ConfigOptionEnumsGeneric>("extruder_type")->values
                                                                 : std::vector<int>(1, int(ExtruderType::etDirectDrive));
        std::vector<int> volume_types = config.has("nozzle_volume_type") ? config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values
                                                                         : std::vector<int>(1, int(NozzleVolumeType::nvtStandard));
        for (int e = 0; e < extruder_count; ++e) {
            int index = 0;
            if (config.has("printer_extruder_id") && config.has("printer_extruder_variant"))
                index = config.get_index_for_extruder(e + 1, "printer_extruder_id", ExtruderType(extruders[e]), NozzleVolumeType(volume_types[e]), "printer_extruder_variant");
            nozzle_flush_dataset[e] = dataset_full[index];
        }
    }

    for (int nozzle = 0; nozzle < extruder_count; ++nozzle) {
        std::vector<double> mtx = get_flush_volumes_matrix(matrix, nozzle, extruder_count);
        for (int from = 0; from < (int) count; ++from) {
            unsigned char from_rgb[4] = {};
            parse_color4(colors[from], from_rgb);
            const bool from_support = filament_is_support->get_at(from);
            for (int to = 0; to < (int) count; ++to) {
                const bool to_support = filament_is_support->get_at(to);
                int volume = 0;
                if (from != to) {
                    if (to_support)
                        volume = g_flush_volume_to_support;
                    else {
                        unsigned char to_rgb[4] = {};
                        parse_color4(colors[to], to_rgb);
                        FlushVolCalculator calculator(min_flush_volumes[from], g_max_flush_volume, nozzle_flush_dataset[nozzle]);
                        volume = calculator.calc_flush_vol(id_of(from), id_of(to), from_rgb[3], from_rgb[0], from_rgb[1], from_rgb[2], to_rgb[3],
                                                           to_rgb[0], to_rgb[1], to_rgb[2]);
                        if (from_support)
                            volume = std::max(g_min_flush_volume_from_support, volume);
                    }
                }
                mtx[count * from + to] = volume;
            }
            set_flush_volumes_matrix(matrix, mtx, nozzle, extruder_count);
        }
    }
    BOOST_LOG_TRIVIAL(info) << "engine: flush_volumes_matrix computed: " << config.option<ConfigOptionFloats>("flush_volumes_matrix")->serialize();
}

// ---------------------------------------------------------------------------------------------------------------------
// Plates: PartPlateList's layout (src/slic3r/GUI/PartPlate.{hpp,cpp}).

constexpr double LOGICAL_PART_PLATE_GAP = 1. / 5.;
const float      bed3d_ax3s_default_tip_radius = 2.5f * 0.5f; // BS:649

int compute_colum_count(int count)
{
    float value = sqrt((float) count);
    float round_value = round(value);
    return value > round_value ? int(round_value) + 1 : int(round_value);
}

Vec3d plate_origin(int index, int cols, int plate_width, int plate_depth)
{
    const int row = index / cols, col = index % cols;
    return Vec3d(col * (plate_width * (1. + LOGICAL_PART_PLATE_GAP)), -row * (plate_depth * (1. + LOGICAL_PART_PLATE_GAP)), 0);
}

Pointfs translated(const Pointfs &pts, const Vec3d &origin)
{
    Pointfs out;
    for (const Vec2d &p : pts)
        out.emplace_back(p.x() + origin.x(), p.y() + origin.y());
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// The job.

struct Options {
    std::string input;
    std::string outdir  = "/out";
    int         plate   = 0;      // 0: every plate (BambuStudio --slice 0), n: plate n
    std::string presets = "auto"; // auto | resolve | project
};

std::string role_name(ExtrusionRole role) { return ExtrusionEntity::role_to_string(role); }

Json plate_info(int index, const std::string &name, const Print &print, GCodeProcessorResult &result, const DynamicPrintConfig &config,
                const Model &model, const std::vector<std::pair<int, int>> &objects_on_plate, const std::string &gcode_file, double ms)
{
    const PrintEstimatedStatistics &ps   = result.print_statistics;
    const auto                     &mode = ps.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    Json info;
    info["index"] = index + 1;
    info["name"]  = name;
    info["gcode"] = gcode_file;

    // Bambu Studio's slice_info.config for this plate (PartPlateList::store_to_3mf_structure, PlateData::parse_filament_info,
    // _BBS_3MF_Exporter::_add_slice_info_config_file_to_archive).
    PlateData pd;
    pd.parse_filament_info(&result);
    char weight[32];
    snprintf(weight, sizeof weight, "%.2f", print.print_statistics().total_weight);
    Json slice_info;
    slice_info["prediction"]       = std::to_string(int(mode.time));
    slice_info["weight"]           = print.print_statistics().total_weight != 0.0 ? std::string(weight) : std::string();
    slice_info["first_layer_time"] = std::to_string(result.initial_layer_time);
    slice_info["outside"]          = result.toolpath_outside;
    slice_info["support_used"]     = print.is_support_used();
    slice_info["label_object_enabled"]           = result.label_object_enabled;
    slice_info["support_material_on_wipe_tower"] = print.support_material_on_wipe_tower();
    slice_info["timelapse_warning_code"]         = result.timelapse_warning_code;
    if (auto *o = config.option<ConfigOptionFloatsNullable>("nozzle_diameter"))
        slice_info["nozzle_diameters"] = o->serialize();
    slice_info["printer_model_id"] = config.opt_string("printer_model");
    slice_info["filament_maps"]    = print.get_filament_maps();
    const auto &types  = config.option<ConfigOptionStrings>("filament_type")->values;
    const auto &colors = config.option<ConfigOptionStrings>("filament_colour")->values;
    const std::vector<std::string> *ids = config.has("filament_ids") ? &config.option<ConfigOptionStrings>("filament_ids")->values : nullptr;
    Json filaments = Json::array();
    for (const FilamentInfo &f : pd.slice_filaments_info) {
        Json j;
        j["id"]            = f.id + 1;
        j["tray_info_idx"] = ids && f.id < (int) ids->size() ? (*ids)[f.id] : std::string();
        j["type"]          = f.id < (int) types.size() ? types[f.id] : std::string();
        j["color"]         = f.id < (int) colors.size() ? colors[f.id] : std::string();
        j["used_m"]        = f.used_m;
        j["used_g"]        = f.used_g;
        j["used_for_object"]   = f.used_for_object;
        j["used_for_support"]  = f.used_for_support;
        j["total_load_time"]   = f.total_load_time;
        j["total_unload_time"] = f.total_unload_time;
        filaments.push_back(j);
    }
    slice_info["filaments"] = filaments;
    Json objects = Json::array();
    for (auto [obj_idx, inst_idx] : objects_on_plate) {
        const ModelObject   *obj  = model.objects[obj_idx];
        const ModelInstance *inst = obj->instances[inst_idx];
        size_t identify_id = inst->loaded_id > 0 ? inst->loaded_id : inst->id().id;
        if (identify_id & 0xFF000000)
            identify_id &= 0x00FFFFFF;
        objects.push_back({{"identify_id", identify_id}, {"name", obj->name}, {"skipped", false}});
    }
    slice_info["objects"] = objects;
    Json warnings = Json::array();
    for (const auto &w : result.warnings)
        warnings.push_back({{"msg", w.msg}, {"level", w.level}, {"error_code", w.error_code}});
    slice_info["warnings"] = warnings;
    info["slice_info"] = slice_info;

    // The numbers behind them, per filament: grams and metres from the G-code processor's volumes (as the CLI's
    // result.json and the Preview's legend compute them).
    auto grams  = [&](size_t f, double volume) { return volume * result.filament_densities[f] * 0.001; };
    auto metres = [&](size_t f, double volume) {
        double area = PI * sqr(0.5 * result.filament_diameters[f]);
        return area < EPSILON ? 0. : 0.001 * volume / area;
    };
    auto at = [](const std::map<size_t, double> &m, size_t k) { auto it = m.find(k); return it == m.end() ? 0. : it->second; };
    Json usage = Json::array();
    double flush_g = 0, total_g = 0;
    for (auto &[f, volume] : ps.total_volumes_per_extruder) {
        Json j;
        j["id"]          = f + 1;
        j["filament_id"] = ids && f < ids->size() ? (*ids)[f] : std::string("unknown");
        j["total_g"]     = grams(f, volume);
        j["total_m"]     = metres(f, volume);
        j["model_g"]     = grams(f, at(ps.model_volumes_per_extruder, f));
        j["support_g"]   = grams(f, at(ps.support_volumes_per_extruder, f));
        j["flush_g"]     = grams(f, at(ps.flush_per_filament, f));
        j["tower_g"]     = grams(f, at(ps.wipe_tower_volumes_per_extruder, f));
        flush_g += grams(f, at(ps.flush_per_filament, f));
        total_g += grams(f, volume);
        usage.push_back(j);
    }
    info["filaments"] = usage;
    info["total_g"]   = total_g;
    info["flush_g"]   = flush_g;
    info["time_s"]    = mode.time;
    // As the CLI's result.json: time without start/end, prime tower and flush.
    double main_time = mode.time - mode.prepare_time;
    Json feature_times = Json::object();
    for (auto &[role, t] : mode.roles_times) {
        if (role == ExtrusionRole::erWipeTower || role == ExtrusionRole::erFlush)
            main_time -= t;
        feature_times[role_name(role)] = feature_times.value(role_name(role), 0.0) + t;
    }
    for (auto &[type, t] : mode.moves_times)
        if (type == EMoveType::Travel) {
            feature_times["Travel"] = t;
            break;
        }
    info["main_time_s"]           = main_time;
    info["feature_times_s"]       = feature_times;
    info["filament_changes"]      = ps.total_flush_filament_changes;
    Json objects_cli = Json::array();
    for (auto [obj_idx, inst_idx] : objects_on_plate) {
        const ModelObject *obj = model.objects[obj_idx];
        BoundingBoxf3 bb = obj->bounding_box();
        objects_cli.push_back({{"id", obj->id().id}, {"name", obj->name}, {"triangle_count", obj->facets_count()},
                               {"bbox", {{"x", bb.min.x()}, {"y", bb.min.y()}, {"z", bb.min.z()}, {"width", bb.size().x()},
                                         {"depth", bb.size().y()}, {"height", bb.size().z()}}}});
    }
    info["objects"] = objects_cli;
    info["slice_ms"] = ms;
    return info;
}

Json run(const Options &opt)
{
    auto t0 = std::chrono::steady_clock::now();
    Json result;
    Json notes = Json::array();

    // --- BS:1863-1891: load the project --------------------------------------------------------------------------
    DynamicPrintConfig        config;
    ConfigSubstitutionContext config_substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    PlateDataPtrs             plate_data_src;
    std::vector<Preset *>     project_presets;
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    LoadStrategy strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances | LoadStrategy::LoadAuxiliary;
    if (!boost::algorithm::iends_with(opt.input, ".3mf"))
        throw Failure{2, "only 3MF projects are supported"};
    g_progress.report(0, "Loading the project");
    // Object ids: Bambu G-code labels objects without a loaded 3MF id ("; OBJECT_ID:", M624, "unique label id") with
    // the process-wide ObjectBase counter, so the CLI's own `Model model;` before the load (BS:1863) shows in every
    // label. Declared and assigned the same way here; the engine must run one job per instance (as the CLI runs one
    // per process) for the labels to match.
    Model model;
    model = Model::read_from_file(opt.input, &config, &config_substitutions, strategy, &plate_data_src, &project_presets, &is_bbl_3mf,
                                  &file_version, nullptr, nullptr, nullptr, opt.plate);
    // ~Model hands the 3MF's unpacked backup folder to Bambu's backup manager (remove_backup), which starts a
    // boost::thread: the folder is removed here instead.
    struct BackupCleanup {
        Model &model;
        ~BackupCleanup() { try { model.remove_backup_path_if_exist(); } catch (...) {} }
    } backup_cleanup{model};
    struct PlateDataCleanup {
        PlateDataPtrs &p;
        ~PlateDataCleanup() { release_PlateData_list(p); }
    } plate_data_cleanup{plate_data_src};
    if (!is_bbl_3mf)
        throw Failure{3, "not a Bambu Studio 3MF project"};
    if (config.empty())
        throw Failure{3, "the 3MF has no project configuration"};

    bool translate_old = false, keep_old_params = false, remove_wrapping_detect = false, skirt_per_object_reset = false;
    float old_filament_prime_volume = 0.f;
    Semver old_version(1, 5, 9), old_version3(2, 0, 0), old_version4(2, 2, 0), old_version5(2, 7, 0);
    if (file_version < old_version)
        translate_old = true;
    if (file_version < old_version3) {
        keep_old_params = true;
        if (auto *o = config.option<ConfigOptionFloats>("filament_prime_volume"); o && !o->values.empty()) {
            old_filament_prime_volume = o->values[0];
            auto *colors = config.option<ConfigOptionStrings>("filament_colour", true);
            if (colors->size() > 1)
                o->values.resize(colors->size(), old_filament_prime_volume);
        }
    }
    if (file_version < old_version4)
        remove_wrapping_detect = true;
    if (file_version < old_version5)
        skirt_per_object_reset = true;

    // The bed the project was laid out on (BS:2045-2052): plates sit 1.2 × its size apart.
    int old_printable_width = 0, old_printable_depth = 0;
    if (auto *o = config.option<ConfigOptionPoints>("printable_area"); o && o->values.size() >= 3) {
        old_printable_width = int(o->values[2].x() - o->values[0].x());
        old_printable_depth = int(o->values[2].y() - o->values[0].y());
    }

    if (auto *pp = config.option<ConfigOptionStrings>("post_process"); pp && !pp->values.empty())
        throw Failure{4, "post-processing scripts are not supported"};

    // --- Presets: as the project carries them, or resolved from the profiles -----------------------------------------
    bool resolve = opt.presets == "resolve" || (opt.presets == "auto" && !is_full_project_config(config));
    bool presets_loaded = false;
    const bool project_has_flush_matrix = config.has("flush_volumes_matrix");
    if (resolve) {
        g_progress.report(0, "Resolving presets");
        Resolved r = resolve_presets(config, config_substitutions.substitutions);
        for (auto &n : r.notes)
            notes.push_back(n);
        config         = std::move(r.config);
        presets_loaded = true;
    }
    result["presets"] = resolve ? "resolved" : "project";

    const std::vector<std::string> filaments_name = config.option<ConfigOptionStrings>("filament_settings_id")->values;
    const int filament_count = int(filaments_name.size());
    std::vector<std::string> different_settings = config.option<ConfigOptionStrings>("different_settings_to_system", true)->values;

    DynamicPrintConfig m_print_config = std::move(config);
    // BS:3198-3201: one entry per preset (process, filaments, printer), as in a Bambu-written project.
    m_print_config.option<ConfigOptionStrings>("inherits_group", true)->values.resize(filament_count + 2, std::string());
    m_print_config.option<ConfigOptionStrings>("different_settings_to_system", true)->values.resize(filament_count + 2, std::string());
    different_settings = m_print_config.option<ConfigOptionStrings>("different_settings_to_system")->values;
    if (model.objects.empty())
        throw Failure{5, "the project has no objects"};

    // --- BS:3750-3925: the flush matrix, when the project has none or it doesn't fit ------------------------------------
    {
        const int extruder_count = int(m_print_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size());
        int printer_variant_count = 1;
        if (auto *o = m_print_config.option<ConfigOptionStrings>("printer_extruder_variant"))
            printer_variant_count = int(o->values.size());
        auto *matrix = m_print_config.option<ConfigOptionFloats>("flush_volumes_matrix");
        const size_t want = size_t(filament_count) * filament_count * extruder_count;
        if (!project_has_flush_matrix || !matrix || matrix->values.size() != want) {
            compute_flush_matrix(m_print_config, extruder_count, printer_variant_count);
            result["flush_matrix"] = "computed";
        } else
            result["flush_matrix"] = "project";
    }

    // --- BS:4082-4111: normalise and synchronise with the full FFF defaults -------------------------------------------
    m_print_config.normalize_fdm();
    m_print_config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology", true)->value = ptFFF;
    {
        FullPrintConfig fff_print_config;
        fff_print_config.apply(m_print_config, true);
        m_print_config.apply(fff_print_config, true);
    }
    {
        std::map<std::string, std::string> validity = m_print_config.validate(true);
        if (!validity.empty()) {
            std::string msg = "invalid values in the 3MF:";
            for (auto &kv : validity)
                msg += " " + kv.first + ": " + kv.second + ";";
            throw Failure{6, msg};
        }
    }
    const int new_extruder_count = int(m_print_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size());
    if (new_extruder_count > 1)
        throw Failure{7, "multi-extruder printers are not supported by this engine build yet"};

    // BS:4159-4171
    ConfigOptionBool *wrapping = m_print_config.option<ConfigOptionBool>("enable_wrapping_detection", true);
    if (remove_wrapping_detect)
        wrapping->value = false;
    if (skirt_per_object_reset)
        m_print_config.option<ConfigOptionBool>("skirt_per_object", true)->value = false;

    // --- BS:4215-4320: plate size --------------------------------------------------------------------------------------
    const Pointfs printable_area = m_print_config.opt<ConfigOptionPoints>("printable_area")->values;
    const double  print_height   = m_print_config.opt_float("printable_height");
    std::vector<Pointfs> extruder_areas;
    std::vector<double>  extruder_heights;
    if (auto *o = m_print_config.opt<ConfigOptionPointsGroups>("extruder_printable_area"))
        extruder_areas = o->values;
    if (auto *o = m_print_config.opt<ConfigOptionFloatsNullable>("extruder_printable_height"))
        extruder_heights = o->values;
    const int current_printable_width = int(printable_area[2].x() - printable_area[0].x());
    const int current_printable_depth = int(printable_area[2].y() - printable_area[0].y());
    if (old_printable_width == 0)
        old_printable_width = current_printable_width;
    if (old_printable_depth == 0)
        old_printable_depth = current_printable_depth;
    if (old_printable_width != current_printable_width || old_printable_depth != current_printable_depth)
        throw Failure{14, "the presets' bed differs from the bed the project was laid out on (machine switches are not supported)"};
    int plate_width = old_printable_width, plate_depth = old_printable_depth;
    if (translate_old) {
        plate_width = int(old_printable_width + bed3d_ax3s_default_tip_radius);
        plate_depth = int(old_printable_depth + bed3d_ax3s_default_tip_radius);
    }

    // --- BS:4327-4395: pre-2.0 projects keep their old wipe tower and support behaviour --------------------------------
    if (keep_old_params) {
        std::vector<std::string> different_keys;
        if (!different_settings.empty())
            Slic3r::unescape_strings_cstyle(different_settings[0], different_keys);
        std::set<std::string> different_key_set(different_keys.begin(), different_keys.end());
        m_print_config.option<ConfigOptionBool>("prime_tower_rib_wall", true)->value      = false;
        m_print_config.option<ConfigOptionPercent>("prime_tower_infill_gap", true)->value = 100;
        auto &adh = m_print_config.option<ConfigOptionInts>("filament_adhesiveness_category", true)->values;
        adh.assign(filament_count, 100);
        auto &prime = m_print_config.option<ConfigOptionFloats>("filament_prime_volume", true)->values;
        prime.resize(filament_count);
        for (int i = 0; i < filament_count; i++)
            prime[i] = old_filament_prime_volume != 0.f ? old_filament_prime_volume : prime[0];
        auto *enable_support = m_print_config.option<ConfigOptionBool>("enable_support", true);
        auto *support_type   = m_print_config.option<ConfigOptionEnum<SupportType>>("support_type", true);
        auto *support_style  = m_print_config.option<ConfigOptionEnum<SupportMaterialStyle>>("support_style", true);
        auto *support_top_z  = m_print_config.option<ConfigOptionFloat>("support_top_z_distance", true);
        if (support_type->value == stTreeAuto && different_key_set.find("support_type") == different_key_set.end())
            support_type->value = stNormalAuto;
        for (ModelObject *object : model.objects) {
            DynamicPrintConfig object_config = object->config.get();
            auto *obj_enable = object_config.option<ConfigOptionBool>("enable_support");
            if (enable_support->value || (obj_enable && obj_enable->value)) {
                auto *obj_type  = object_config.option<ConfigOptionEnum<SupportType>>("support_type");
                auto *obj_style = object_config.option<ConfigOptionEnum<SupportMaterialStyle>>("support_style");
                auto *obj_topz  = object_config.option<ConfigOptionFloat>("support_top_z_distance");
                SupportType          t = obj_type ? obj_type->value : support_type->value;
                SupportMaterialStyle s = obj_style ? obj_style->value : support_style->value;
                if (t == stTreeAuto && s == smsDefault) {
                    float z = obj_topz ? obj_topz->value : support_top_z->value;
                    if (!object->has_custom_layering() && z == 0) {
                        // As in the CLI, this edits a copy of the object's config (BS:4374-4378).
                        object_config.option<ConfigOptionEnum<SupportMaterialStyle>>("support_style", true)->value = smsTreeOrganic;
                    }
                }
            }
        }
        m_print_config.option<ConfigOptionFloatsNullable>("travel_acceleration", true)->values =
            m_print_config.option<ConfigOptionFloatsNullable>("default_acceleration")->values;
        m_print_config.option<ConfigOptionFloatsNullable>("initial_layer_travel_acceleration", true)->values =
            m_print_config.option<ConfigOptionFloatsNullable>("initial_layer_acceleration")->values;
    }

    // --- Plates (PartPlateList::load_from_3mf_structure): count, origins, configs, names ----------------------------------
    const int plate_count = std::max<int>(1, int(plate_data_src.size()));
    if (opt.plate < 0 || opt.plate > plate_count)
        throw Failure{15, "the project has " + std::to_string(plate_count) + " plate(s), not plate " + std::to_string(opt.plate)};
    const int cols = compute_colum_count(plate_count);
    std::vector<Vec3d> origins(plate_count);
    for (int i = 0; i < plate_count; ++i)
        origins[i] = plate_origin(i, cols, plate_width, plate_depth);
    // BS:4491-4508: projects from before 1.5.9 laid plates out on a slightly larger bed; move their objects.
    if (translate_old) {
        for (int i = 1; i < plate_count; ++i) {
            Vec3d new_origin = plate_origin(i, cols, old_printable_width, old_printable_depth);
            Vec3d offset     = new_origin - origins[i];
            BuildVolume bv(translated(printable_area, origins[i]), print_height, {}, {});
            model.update_print_volume_state(bv);
            for (ModelObject *object : model.objects)
                for (ModelInstance *inst : object->instances)
                    if (inst->print_volume_state == ModelInstancePVS_Inside) {
                        inst->set_offset(inst->get_offset() + offset);
                        object->invalidate_bounding_box();
                    }
            origins[i] = new_origin;
        }
        plate_width = old_printable_width;
        plate_depth = old_printable_depth;
        for (int i = 0; i < plate_count; ++i)
            origins[i] = plate_origin(i, cols, plate_width, plate_depth);
    }

    // The CLI's prime volume for Bambu projects it slices without loaded filaments (BS:6886-6895).
    if (!presets_loaded)
        for (double &v : m_print_config.option<ConfigOptionFloats>("filament_prime_volume")->values)
            v = 45.f;

    // --- BS:6460-7400: the plates ---------------------------------------------------------------------------------------
    std::vector<int> plates;
    for (int i = 0; i < plate_count; ++i)
        if (opt.plate == 0 || opt.plate == i + 1)
            plates.push_back(i);
    g_progress.plate_count = int(plates.size());

    std::string &printer_model = m_print_config.opt_string("printer_model", true);
    bool is_bbl = printer_model.compare(0, 9, "Bambu Lab") == 0;
    if (printer_model.empty())
        is_bbl = m_print_config.opt_string("printer_settings_id").compare(0, 9, "Bambu Lab") == 0;

    struct PlateJob {
        int                           index;
        std::string                   name;
        std::unique_ptr<Print>        print;
        DynamicPrintConfig            config;
        std::vector<std::pair<int, int>> objects; // (object, instance) on the plate
    };
    std::vector<PlateJob> jobs;
    for (int index : plates) {
        PlateJob job;
        job.index = index;
        job.print = std::make_unique<Print>();
        job.print->set_plate_origin(origins[index]);
        job.print->set_plate_index(index);
        if (index < int(plate_data_src.size()) && plate_data_src[index])
            job.name = plate_data_src[index]->plate_name;
        job.print->set_plate_name(job.name);
        jobs.push_back(std::move(job));
    }

    // One plate's apply and validate (BS:6460-7010). Called twice per plate when the CLI would pre-check: --slice 0 on a
    // project with several plates applies every plate first, then slices them (BS:6420, 7021); the second apply is what
    // the G-code's config block shows.
    auto prepare = [&](PlateJob &job) {
        const int index = job.index;
        model.curr_plate_index = index;
        std::vector<Pointfs> plate_extruder_areas;
        for (const Pointfs &area : extruder_areas)
            plate_extruder_areas.push_back(translated(area, origins[index]));
        BuildVolume build_volume(translated(printable_area, origins[index]), print_height, plate_extruder_areas, extruder_heights);
        if (model.update_print_volume_state(build_volume) == 0)
            throw Failure{10, "nothing to slice on plate " + std::to_string(index + 1) + ": no object is fully inside its print volume", index + 1};
        job.objects.clear();
        for (size_t o = 0; o < model.objects.size(); ++o)
            for (size_t i = 0; i < model.objects[o]->instances.size(); ++i) {
                ModelInstance *instance = model.objects[o]->instances[i];
                instance->use_loaded_id_for_label = true;
                if (instance->print_volume_state == ModelInstancePVS_Partly_Outside)
                    throw Failure{11, "object " + model.objects[o]->name + " is partly outside plate " + std::to_string(index + 1), index + 1};
                if (instance->print_volume_state == ModelInstancePVS_Inside)
                    job.objects.emplace_back(int(o), int(i));
            }

        DynamicPrintConfig plate_config;
        plate_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmAutoForFlush;
        if (index < int(plate_data_src.size()) && plate_data_src[index])
            plate_config.apply(plate_data_src[index]->config);
        DynamicPrintConfig new_print_config = m_print_config;
        new_print_config.apply(plate_config);
        if (!new_print_config.has("nozzle_volume_type"))
            new_print_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values.resize(new_extruder_count, nvtStandard);
        std::vector<int> &final_filament_maps = new_print_config.option<ConfigOptionInts>("filament_map", true)->values;
        std::vector<int> &final_volume_maps   = new_print_config.option<ConfigOptionInts>("filament_volume_map", true)->values;
        const int default_extruder_id = 1;
        const int default_volume      = new_print_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values[default_extruder_id - 1];
        if (int(final_filament_maps.size()) < filament_count)
            final_filament_maps.resize(filament_count, default_extruder_id);
        if (int(final_volume_maps.size()) < filament_count)
            final_volume_maps.resize(filament_count, default_volume);
        for (int i = 0; i < filament_count; i++) {
            final_filament_maps[i] = default_extruder_id;
            final_volume_maps[i]   = default_volume;
        }

        job.print->apply(model, new_print_config);
        job.print->set_no_check_flag(false);
        job.print->set_check_multi_filaments_compatibility(true);
        StringObjectException warning;
        auto err = job.print->validate(&warning);
        if (!err.string.empty())
            throw Failure{12, "plate " + std::to_string(index + 1) + ": " + err.string, index + 1};
        if (job.print->empty())
            throw Failure{10, "nothing to slice on plate " + std::to_string(index + 1) + " after apply", index + 1};
        job.config = std::move(new_print_config);
    };

    const bool pre_check = opt.plate == 0 && plate_count > 1;
    if (pre_check)
        for (PlateJob &job : jobs)
            prepare(job);

    boost::filesystem::create_directories(opt.outdir);
    Json plates_out = Json::array();
    for (PlateJob &job : jobs) {
        auto tp = std::chrono::steady_clock::now();
        const int index = job.index;
        g_progress.plate = index + 1;
        g_progress.report(0, "Preparing plate " + std::to_string(index + 1));
        prepare(job);
        Print &print = *job.print;
        print.set_status_callback(status_callback);
        print.set_BBL_Printer(is_bbl);
        Model::setExtruderParams(m_print_config, filament_count);
        Model::setPrintSpeedTable(m_print_config, print.config());

        std::unordered_map<std::string, long long> slice_time;
        print.process(&slice_time);
        if (!print.get_conflict_string().empty())
            throw Failure{13, "G-code path conflict on plate " + std::to_string(index + 1) + ": " + print.get_conflict_string(), index + 1};
        for (const auto &status : g_slicing_warnings)
            if (status.warning_step != -1 && status.message_type != PrintStateBase::SlicingDefaultNotification &&
                status.warning_level != PrintStateBase::WarningLevel::NON_CRITICAL &&
                (status.message_type == PrintStateBase::SlicingEmptyGcodeLayers || status.message_type == PrintStateBase::SlicingGcodeOverlap))
                throw Failure{16, "plate " + std::to_string(index + 1) + ": " + status.text, index + 1};
        g_slicing_warnings.clear();

        GCodeProcessorResult gcode_result;
        std::string outfile = opt.outdir + "/plate_" + std::to_string(index + 1) + ".gcode";
        outfile = print.export_gcode(outfile, &gcode_result, nullptr);
        if (const int code = gcode_result.gcode_check_result.error_code) {
            // BS:7183-7206; 1 << 11 (too heavy for an i3-type bed) is only a warning.
            if (code & 0b1100)
                throw Failure{17, "plate " + std::to_string(index + 1) + ": G-code goes outside the printable area", index + 1};
            if (code & 0b10000)
                throw Failure{18, "plate " + std::to_string(index + 1) + ": G-code in the wrapping detection area", index + 1};
            if (code & 0b00011)
                throw Failure{19, "plate " + std::to_string(index + 1) + ": G-code in an unprintable area", index + 1};
        }
        if (gcode_result.filament_printable_reuslt.has_value())
            throw Failure{25, "plate " + std::to_string(index + 1) + ": a filament cannot be printed on this plate's first layer", index + 1};

        plates_out.push_back(plate_info(index, job.name, print, gcode_result, job.config, model, job.objects,
                                        boost::filesystem::path(outfile).filename().string(), ms_since(tp)));
        // As the CLI does between plates: free the slice data.
        job.print.reset();
        g_progress.done++;
        g_progress.report(0, "Plate " + std::to_string(index + 1) + " done");
    }
    result["plates"] = plates_out;
    result["notes"]  = notes;
    result["ms"]     = ms_since(t0);
    return result;
}

Options parse_options(const char *options_json)
{
    Options opt;
    Json j = Json::parse(options_json ? options_json : "{}");
    opt.input   = j.value("input", std::string());
    opt.outdir  = j.value("outdir", opt.outdir);
    opt.plate   = j.value("plate", 0);
    opt.presets = j.value("presets", opt.presets);
    if (opt.presets != "auto" && opt.presets != "resolve" && opt.presets != "project")
        throw Failure{1, "presets must be auto, resolve or project"};
    return opt;
}

int run_and_write(const Options &opt)
{
    Json result;
    int code = 0;
    try {
        result = Json{{"return_code", 0}};
        result.update(run(opt));
    } catch (const Failure &f) {
        code   = f.code;
        result = {{"return_code", f.code}, {"error_string", f.message}};
        if (f.plate)
            result["plate"] = f.plate;
    } catch (const std::exception &e) {
        code   = 100;
        result = {{"return_code", 100}, {"error_string", e.what()}};
    }
    result["engine"] = "Bambu Studio " SLIC3R_VERSION " libslic3r (wasm)";
    try {
        boost::filesystem::create_directories(opt.outdir);
        boost::nowide::ofstream(opt.outdir + "/result.json") << result.dump(1) << "\n";
    } catch (...) {}
    return code;
}

} // namespace

extern "C" {

// Slices a Bambu 3MF project. options: JSON {"input": "/in/x.3mf", "outdir": "/out", "plate": 0, "presets": "auto"}.
// plate 0 slices every plate (BambuStudio --slice 0), n only plate n. Writes <outdir>/plate_<n>.gcode for each plate
// and <outdir>/result.json; returns 0 or an error code (also in result.json).
ENGINE_EXPORT int engine_run(const char *options_json)
{
    Options opt;
    try {
        opt = parse_options(options_json);
    } catch (const Failure &f) {
        std::cerr << f.message << std::endl;
        return f.code;
    } catch (const std::exception &e) {
        std::cerr << "options: " << e.what() << std::endl;
        return 1;
    }
    return run_and_write(opt);
}

// The spike's entry point, kept: engine_slice(input, outdir, plate).
ENGINE_EXPORT int engine_slice(const char *input_3mf, const char *outdir, int plate)
{
    Options opt;
    opt.input  = input_3mf;
    opt.outdir = outdir;
    opt.plate  = plate;
    return run_and_write(opt);
}

ENGINE_EXPORT void engine_init(const char *resources_dir, int log_level)
{
    set_resources_dir(resources_dir);
    set_temporary_dir("/tmp");
    set_data_dir("/data");
    set_logging_level(log_level);
}

} // extern "C"
