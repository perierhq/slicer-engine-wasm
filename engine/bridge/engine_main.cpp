// The one-plate slicing path of Bambu Studio's command line (src/BambuStudio.cpp, CLI::run), for
//   BambuStudio --slice <plate> --outputdir <dir> <project.3mf>
// on a Bambu 3MF that carries its full configuration (no --load-settings / --load-filaments, no
// arrange, no orient). "BS:<line>" refers to src/BambuStudio.cpp at v02.08.02.61.
//
// The desktop CLI gets its Print from the GUI class PartPlate (src/slic3r/GUI/PartPlate.cpp). For the
// first plate of a project that is a plain Print with origin (0,0,0), plate index 0, the plate's 3MF
// config on top of {filament_map_mode = AutoForFlush}, and the printable area as bed shape; set up
// below without the GUI.
//
// Ported: the 3MF load (BS:1876-1891), the version fix-ups (BS:1910-1952), config normalisation
// (BS:4082-4111), the wrapping-detection and skirt-per-object resets (BS:4159-4171), the "keep old
// params" branch for pre-2.0 files (BS:4327-4395), the plate pre-check (BS:6534-6560, single
// extruder), filament_prime_volume = 45 for Bambu 3MFs (BS:6886-6895), the plate config, filament
// maps (BS:6962-6970), apply/validate/process/export (BS:6972-7180).
//
// Not ported (the comparison path never takes them; the engine reports them as unsupported where it
// matters): --load-settings/--load-filaments and machine switches (BS:2300-3750), flush-matrix
// recomputation when the 3MF has none or the extruder count changes (BS:3752-3930), arrange and orient,
// multi-extruder filament mapping (BS:6563-6820, H2D-class printers), skip lists, cached slice data,
// thumbnails (Bambu printers get none on the CLI path anyway: BS:7174), 3MF export.
//
// SPDX-License-Identifier: AGPL-3.0-only
#include <libslic3r/libslic3r.h>
#include <libslic3r/BuildVolume.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/Format/bbs_3mf.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Semver.hpp>
#include <libslic3r/Utils.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define ENGINE_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define ENGINE_EXPORT
#endif

using namespace Slic3r;

namespace {

std::vector<PrintBase::SlicingStatus> g_slicing_warnings;

void default_status_callback(const PrintBase::SlicingStatus &status)
{
    if (status.warning_step != -1)
        g_slicing_warnings.push_back(status);
}

struct Failure {
    int         code;
    std::string message;
};

std::string json_escape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default:
            if ((unsigned char) c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else
                out += c;
        }
    }
    return out;
}

double ms_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Returns the G-code path.
std::string slice_plate(const std::string &file, const std::string &outdir, int plate_to_slice, std::string &timings)
{
    auto t0 = std::chrono::steady_clock::now();

    // --- BS:1863-1891: load the project --------------------------------------------------------------
    DynamicPrintConfig        config;
    ConfigSubstitutionContext config_substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    PlateDataPtrs             plate_data_src;
    std::vector<Preset *>     project_presets;
    bool                      is_bbl_3mf = false;
    Semver                    file_version;
    LoadStrategy strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances | LoadStrategy::LoadAuxiliary;
    if (!boost::algorithm::iends_with(file, ".3mf"))
        throw Failure{2, "only 3MF projects are supported"};
    // Object ids: Bambu G-code labels objects without a loaded 3MF id ("; OBJECT_ID:", M624, "unique label
    // id") with the process-wide ObjectBase counter, so the CLI's own `Model model;` before the load
    // (BS:1863) shows in every label. Declared and assigned the same way here; the engine must run one
    // slice per process (as the CLI does) for the labels to match.
    Model model;
    model = Model::read_from_file(file, &config, &config_substitutions, strategy, &plate_data_src, &project_presets, &is_bbl_3mf,
                                        &file_version, nullptr, nullptr, nullptr, plate_to_slice);
    // ~Model hands the 3MF's unpacked backup folder to Bambu's backup manager (remove_backup), which starts a
    // boost::thread: the single-threaded engine cannot, so the folder is removed here instead.
    struct BackupCleanup {
        Model &model;
        ~BackupCleanup() { try { model.remove_backup_path_if_exist(); } catch (...) {} }
    } backup_cleanup{model};
    if (!is_bbl_3mf)
        throw Failure{3, "not a Bambu Studio 3MF project"};
    if (config.empty())
        throw Failure{3, "the 3MF has no project configuration"};

    bool translate_old = false, keep_old_params = false, remove_wrapping_detect = false, skirt_per_object_reset = false;
    float old_filament_prime_volume = 0.f;
    Semver old_version(1, 5, 9), old_version3(2, 0, 0), old_version4(2, 2, 0), old_version5(2, 7, 0);
    if (file_version < old_version)
        translate_old = true; // only changes the bed size used for placement: BS:4310
    if (file_version < old_version3) {
        keep_old_params = true;
        if (auto *opt = config.option<ConfigOptionFloats>("filament_prime_volume"); opt && !opt->values.empty()) {
            old_filament_prime_volume = opt->values[0];
            auto *colors = config.option<ConfigOptionStrings>("filament_colour", true);
            if (colors->size() > 1)
                opt->values.resize(colors->size(), old_filament_prime_volume);
        }
    }
    if (file_version < old_version4)
        remove_wrapping_detect = true;
    if (file_version < old_version5)
        skirt_per_object_reset = true;
    (void) translate_old;

    if (auto *pp = config.option<ConfigOptionStrings>("post_process"); pp && !pp->values.empty())
        throw Failure{4, "post-processing scripts are not supported"};

    const std::vector<std::string> filaments_name = config.option<ConfigOptionStrings>("filament_settings_id")->values;
    const int filament_count = int(filaments_name.size());
    std::vector<std::string> different_settings = config.option<ConfigOptionStrings>("different_settings_to_system", true)->values;

    DynamicPrintConfig m_print_config = std::move(config);
    if (model.objects.empty())
        throw Failure{5, "the project has no objects"};

    // --- BS:4082-4111: normalise and synchronise with the full FFF defaults ---------------------------
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

    // BS:4327-4395: pre-2.0 projects keep their old wipe tower and support behaviour.
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

    // --- The plate (PartPlate / PartPlateList::load_from_3mf_structure) --------------------------------
    const int plate_index = plate_to_slice > 0 ? plate_to_slice - 1 : 0;
    if (plate_data_src.size() > 1 && plate_to_slice == 0)
        throw Failure{8, "the project has several plates: name one"};
    DynamicPrintConfig plate_config;
    plate_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmAutoForFlush;
    std::string plate_name;
    if (plate_index < int(plate_data_src.size()) && plate_data_src[plate_index]) {
        plate_config.apply(plate_data_src[plate_index]->config);
        plate_name = plate_data_src[plate_index]->plate_name;
    }
    if (plate_index != 0)
        throw Failure{9, "only the first plate is supported by this engine build (plate origins of other plates are not ported)"};

    model.curr_plate_index = plate_index;

    // BS:6534-6560: print volume state, labels.
    const Pointfs printable_area = m_print_config.opt<ConfigOptionPoints>("printable_area")->values;
    const double  print_height   = m_print_config.opt_float("printable_height");
    std::vector<Pointfs> extruder_areas;
    std::vector<double>  extruder_heights;
    if (auto *o = m_print_config.opt<ConfigOptionPointsGroups>("extruder_printable_area"))
        extruder_areas = o->values;
    if (auto *o = m_print_config.opt<ConfigOptionFloatsNullable>("extruder_printable_height"))
        extruder_heights = o->values;
    BuildVolume build_volume(printable_area, print_height, extruder_areas, extruder_heights);
    if (model.update_print_volume_state(build_volume) == 0)
        throw Failure{10, "nothing to slice: no object is fully inside the print volume"};
    for (ModelObject *object : model.objects)
        for (ModelInstance *instance : object->instances) {
            instance->use_loaded_id_for_label = true;
            if (instance->print_volume_state == ModelInstancePVS_Partly_Outside)
                throw Failure{11, "object " + object->name + " is partly outside the print volume"};
        }

    // BS:6886-6895: the CLI uploads Bambu projects with a 45 mm³ prime volume.
    for (double &v : m_print_config.option<ConfigOptionFloats>("filament_prime_volume")->values)
        v = 45.f;

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

    // --- BS:6972-7180: apply, validate, process, export -----------------------------------------------
    Print                print;
    GCodeProcessorResult gcode_result;
    print.set_plate_origin(Vec3d(0, 0, 0));
    print.set_plate_index(plate_index);
    print.set_plate_name(plate_name);

    print.apply(model, new_print_config);
    print.set_no_check_flag(false);
    print.set_check_multi_filaments_compatibility(true);
    StringObjectException warning;
    auto err = print.validate(&warning);
    if (!err.string.empty())
        throw Failure{12, "validate: " + err.string};
    if (print.empty())
        throw Failure{10, "nothing to slice after apply"};

    print.set_status_callback(default_status_callback);
    std::string &printer_model = new_print_config.opt_string("printer_model", true);
    bool is_bbl = printer_model.compare(0, 9, "Bambu Lab") == 0;
    if (printer_model.empty()) {
        const std::string &printer_name = m_print_config.opt_string("printer_settings_id");
        is_bbl = printer_name.compare(0, 9, "Bambu Lab") == 0;
    }
    print.set_BBL_Printer(is_bbl);

    Model::setExtruderParams(m_print_config, filament_count);
    Model::setPrintSpeedTable(m_print_config, print.config());

    auto t_apply = ms_since(t0);
    std::unordered_map<std::string, long long> slice_time;
    print.process(&slice_time);
    auto t_process = ms_since(t0);

    if (!print.get_conflict_string().empty())
        throw Failure{13, "G-code path conflict: " + print.get_conflict_string()};

    boost::filesystem::create_directories(outdir);
    std::string outfile = outdir + "/plate_" + std::to_string(plate_index + 1) + ".gcode";
    outfile = print.export_gcode(outfile, &gcode_result, nullptr);
    auto t_export = ms_since(t0);

    char buf[256];
    snprintf(buf, sizeof buf, "\"load_apply_ms\":%.1f,\"process_ms\":%.1f,\"export_ms\":%.1f", t_apply, t_process - t_apply, t_export - t_process);
    timings = buf;
    return outfile;
}

} // namespace

extern "C" {

// Slices one plate of a Bambu 3MF project (plate 0 = the only plate) into <outdir>/plate_<n>.gcode and
// writes <outdir>/result.json. Returns 0 on success, an error code otherwise (also in result.json).
ENGINE_EXPORT int engine_slice(const char *input_3mf, const char *outdir, int plate)
{
    std::string out = outdir;
    std::string result;
    int         code = 0;
    try {
        boost::filesystem::create_directories(out);
        std::string timings;
        std::string gcode = slice_plate(input_3mf, out, plate, timings);
        result = "{\"return_code\":0,\"gcode\":\"" + json_escape(gcode) + "\"," + timings + ",\"engine\":\"Bambu Studio " SLIC3R_VERSION " libslic3r (wasm)\"}";
    } catch (const Failure &f) {
        code   = f.code;
        result = "{\"return_code\":" + std::to_string(f.code) + ",\"error_string\":\"" + json_escape(f.message) + "\"}";
    } catch (const std::exception &e) {
        code   = 100;
        result = "{\"return_code\":100,\"error_string\":\"" + json_escape(e.what()) + "\"}";
    }
    std::ofstream(out + "/result.json") << result << "\n";
    std::cerr << result << std::endl;
    return code;
}

ENGINE_EXPORT void engine_init(const char *resources_dir, int log_level)
{
    set_resources_dir(resources_dir);
    set_temporary_dir("/tmp");
    set_data_dir("/data");
    set_logging_level(log_level);
}

} // extern "C"
