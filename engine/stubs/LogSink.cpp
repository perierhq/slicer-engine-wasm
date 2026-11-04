// Replaces src/libslic3r/LogSink.cpp in the wasm engine. Bambu's encrypted log files need OpenSSL (AES)
// and libcurl (key download); the engine only logs to stderr and never installs this sink
// (set_log_path_and_level is not called), so the members exist only to satisfy the linker.
#include "libslic3r/LogSink.hpp"

namespace Slic3r {

LogSinkBackend::LogSinkBackend(const std::string &base_path, const LogEncOptions &options)
    : boost::log::sinks::text_file_backend(boost::log::keywords::file_name = base_path), m_log_enc_options(options)
{}
bool LogSinkBackend::update_enc_option(const std::string &, const LogEncOptions &) { return false; }
void LogSinkBackend::consume(const boost::log::record_view &rec, const std::string &formatted_message)
{
    boost::log::sinks::text_file_backend::consume(rec, formatted_message);
}
bool LogSinkBackend::DecodeAES256LogFile(const std::string &, const std::string &, const std::string &, const std::string &) { return false; }
std::string LogSinkUtil::get_log_filaname_format(const LogEncOptions &) { return "%Y-%m-%d_%H-%M-%S.log"; }

} // namespace Slic3r
