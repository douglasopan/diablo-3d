#include "engine/render/d3d_local_diagnostics.hpp"
#include <cmath>
#include <limits>

#include <algorithm>
#include <array>
#include <sstream>
#include <system_error>
#include <stdexcept>
#include <cstdlib>
#include <map>
#include <iterator>
#include "picosha2.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "utils/log.hpp"

namespace devilution::d3d_local_diagnostics {
namespace {
thread_local Stage ActiveStage = Stage::Launch;
constexpr std::array<std::string_view, 19> StageNames {
    "launch", "sdl_video", "renderer_2d", "device_3d", "world_prepare", "gpu_begin",
    "scene_record", "world_submit", "viewmodel_submit", "readback", "atmosphere",
    "resolve", "hud_compose", "present", "input_layout", "shutdown", "shader_compile", "targets", "camera"
};
constexpr std::array<std::string_view, 6> BackendNames {
    "unknown", "sdl_hardware", "sdl_software", "d3d11_hardware", "d3d11_warp", "cpu_diagnostic"
};
constexpr std::array<std::string_view, 21> ErrorNames {
    "none", "diagnostic_consent_missing", "renderer_2d_unavailable", "renderer_2d_software",
    "device_3d_unavailable", "warp_refused", "gpu_preference_disabled", "gpu_begin_failed",
    "gpu_submission_failed", "gpu_readback_failed", "gpu_output_rejected", "device_removed",
    "shader_compile_failed", "capacity_exceeded", "invalid_gpu_input", "cpu_fallback_refused",
    "hud_presentation_failed", "log_unavailable", "diagnostic_output_rejected", "camera_projection_invalid", "camera_visibility_unavailable"
};
template <size_t N, class T>
std::string_view Name(const std::array<std::string_view, N> &names, T value)
{
    const auto index = static_cast<size_t>(value);
    return index < names.size() ? names[index] : "invalid_enum";
}
bool IsSha256(std::string_view text)
{
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
uint64_t ElapsedMicroseconds(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}
} // namespace

Failure CheckPreflight(const Policy &policy, const Preflight &p) noexcept
{
    if (policy.mode != Mode::GpuRequired && policy.mode != Mode::CpuDiagnosticExplicit)
        return { Error::DiagnosticConsentMissing, Stage::Launch };
    if (policy.mode == Mode::CpuDiagnosticExplicit) {
        return policy.cpuDiagnosticConsent ? Failure {} : Failure { Error::DiagnosticConsentMissing, Stage::Launch };
    }
    if (!p.renderer2DReady) return { Error::Renderer2DUnavailable, Stage::Renderer2D };
    if (!p.renderer2DAccelerated) return { Error::Renderer2DSoftware, Stage::Renderer2D };
    if (!p.device3DReady) return { Error::Device3DUnavailable, Stage::Device3D };
    if (p.device3DWarp) return { Error::WarpRefused, Stage::Device3D };
    return {};
}

Decision CheckFrame(const Policy &policy, const Frame &frame, Failure &failure) noexcept
{
    failure = {};
    if (policy.mode != Mode::GpuRequired && policy.mode != Mode::CpuDiagnosticExplicit) {
        failure = { Error::DiagnosticConsentMissing, Stage::WorldPrepare };
        return Decision::RefuseFrame;
    }
    // Native rendering remains legitimate when the world is outside the 3D
    // renderer's supported context. This is distinct from a failed GPU attempt.
    if (frame.context == Context::NativeContextIncompatible) return Decision::PublishNativeContext;
    if (policy.mode == Mode::CpuDiagnosticExplicit) {
        if (frame.usedGpu || frame.warp) {
            failure = { Error::InvalidGpuInput, Stage::WorldSubmit };
            return Decision::RefuseFrame;
        }
        if (policy.cpuDiagnosticConsent && frame.completeOutput) return Decision::PublishCpuDiagnostic;
        if (policy.cpuDiagnosticConsent) {
            failure = { Error::DiagnosticOutputRejected, Stage::Resolve };
            return Decision::RefuseFrame;
        }
        failure = { Error::DiagnosticConsentMissing, Stage::WorldPrepare };
        return Decision::RefuseFrame;
    }
    if (!frame.requestedGpu) failure = { Error::GpuPreferenceDisabled, Stage::WorldPrepare };
    else if (frame.warp) failure = { Error::WarpRefused, Stage::GpuBegin };
    else if (!frame.usedGpu || !frame.hardware) failure = { Error::CpuFallbackRefused, Stage::WorldSubmit };
    else if (frame.cpuRasterizedTriangles != 0) failure = { Error::CpuFallbackRefused, Stage::WorldSubmit };
    else if (!frame.completeOutput) failure = { Error::GpuOutputRejected, Stage::Readback };
    else return Decision::PublishGpu;
    return Decision::RefuseFrame;
}

std::string_view PublicErrorMessage(Error error) noexcept
{
    switch (error) {
    case Error::None: return "";
    case Error::DiagnosticConsentMissing: return "O modo de diagnostico CPU precisa ser escolhido explicitamente nesta sessao.";
    case Error::GpuPreferenceDisabled: return "Esta build exige GPU. Ative a GPU ou escolha explicitamente o modo de diagnostico CPU.";
    case Error::Renderer2DUnavailable:
    case Error::Renderer2DSoftware: return "A apresentacao 2D por hardware nao esta disponivel. A abertura foi recusada.";
    case Error::LogUnavailable: return "O registro local de diagnostico nao pode ser gravado.";
    case Error::CameraVisibilityUnavailable: return "A visibilidade da camera 3D esta indisponivel. Este frame nao pode ser publicado.";
    case Error::CameraProjectionInvalid: return "A projecao da camera 3D foi recusada. Este frame nao pode ser publicado.";
    case Error::DiagnosticOutputRejected: return "O frame de diagnostico CPU esta incompleto e foi recusado.";
    default: return "A GPU falhou e este frame foi recusado. Consulte o codigo de erro no diagnostico local; CPU exige escolha explicita.";
    }
}

std::filesystem::path Logger::File(unsigned index) const
{
    return config_.directory / (index == 0 ? "gpu-diagnostics.current.jsonl" : "gpu-diagnostics." + std::to_string(index) + ".jsonl");
}
bool Logger::PathsAreOrdinaryFiles() const noexcept
{
    try {
    std::error_code ec;
    if (!std::filesystem::is_directory(config_.directory, ec) || ec) return false;
    if (std::filesystem::is_symlink(config_.directory, ec) || ec) return false;
    const auto resolved = std::filesystem::canonical(config_.directory, ec);
    if (ec || resolved != config_.directory.lexically_normal()) return false;
    for (unsigned i = 0; i < config_.retainedFiles + 1U; ++i) {
        const auto path = i < config_.retainedFiles ? File(i) : config_.directory / "gpu-diagnostics.error.json";
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) return false;
        ec.clear();
        if (std::filesystem::exists(status) && (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))) return false;
    }
    return true;
    } catch (...) { return false; }
}
bool Logger::Rotate(size_t requiredBytes) noexcept
{
    try {
        if (!PathsAreOrdinaryFiles() || requiredBytes > config_.maximumFileBytes) return false;
        if (stream_.is_open() && bytes_ <= config_.maximumFileBytes - requiredBytes) return true;
        stream_.close();
        std::error_code ec;
        std::filesystem::remove(File(config_.retainedFiles - 1), ec);
        if (ec) return false;
        for (unsigned i = config_.retainedFiles - 1; i > 0; --i) {
            const auto source = File(i - 1);
            if (std::filesystem::exists(source, ec)) {
                if (ec) return false;
                std::filesystem::rename(source, File(i), ec);
                if (ec) return false;
            } else if (ec) return false;
        }
        stream_.clear();
        stream_.open(File(0), std::ios::binary | std::ios::trunc);
        bytes_ = 0;
        return stream_.good();
    } catch (...) { return false; }
}
LogResult Logger::Open(const Config &config) noexcept
{
    try {
        {
            std::lock_guard lock(mutex_);
            stream_.close(); stream_.clear(); open_ = false;
            if (!IsSha256(config.executableSha256) || !IsSha256(config.sourceClosureSha256) || config.session == 0
                || config.maximumFileBytes < 4096 || config.maximumFileBytes > 2U * 1024U * 1024U
                || config.retainedFiles < 2 || config.retainedFiles > 8 || !config.directory.is_absolute()
                || (config.policy.mode != Mode::GpuRequired && config.policy.mode != Mode::CpuDiagnosticExplicit)
                || (config.policy.mode == Mode::CpuDiagnosticExplicit && !config.policy.cpuDiagnosticConsent)) return LogResult::InvalidMetadata;
            config_ = config; sequence_ = 0; bytes_ = 0;
            if (!PathsAreOrdinaryFiles()) return LogResult::IoFailure;
            std::error_code ec;
            const auto size = std::filesystem::exists(File(0), ec) ? std::filesystem::file_size(File(0), ec) : 0;
            if (ec) return LogResult::IoFailure;
            if (size > config_.maximumFileBytes) {
                if (!Rotate(config_.maximumFileBytes)) return LogResult::IoFailure;
            } else {
                bytes_ = static_cast<uint32_t>(size);
                stream_.open(File(0), std::ios::binary | std::ios::app);
                if (!stream_) return LogResult::IoFailure;
            }
            open_ = true;
        }
        return Emit(std::string("\"event\":\"session\",\"gpuRequired\":")
            + (config.policy.mode == Mode::GpuRequired ? "true" : "false") + ",\"cpuDiagnosticExplicit\":"
            + (config.policy.mode == Mode::CpuDiagnosticExplicit ? "true" : "false"));
    } catch (...) { return LogResult::IoFailure; }
}
void Logger::Close() noexcept
{
    try { std::lock_guard lock(mutex_); stream_.close(); open_ = false; } catch (...) { }
}
LogResult Logger::Emit(std::string fields, bool errorSidecar) noexcept
{
    try {
        std::string line;
        {
            std::lock_guard lock(mutex_);
            if (!open_) return LogResult::NotOpen;
            if (sequence_ == UINT64_MAX || fields.size() > 2048) return LogResult::InvalidMetadata;
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            line = "{\"schema\":1,\"session\":" + std::to_string(config_.session) + ",\"seq\":" + std::to_string(++sequence_)
                + ",\"utcUnixMs\":" + std::to_string(millis) + ",\"exeSha256\":\"" + config_.executableSha256
                + "\",\"sourceSha256\":\"" + config_.sourceClosureSha256 + "\",\"mode\":\""
                + (config_.policy.mode == Mode::GpuRequired ? "gpu_required" : "cpu_diagnostic_explicit") + "\"," + fields + "}\n";
            if (!Rotate(line.size())) { open_ = false; return LogResult::IoFailure; }
            stream_.write(line.data(), static_cast<std::streamsize>(line.size())); stream_.flush();
            if (!stream_) { open_ = false; return LogResult::IoFailure; }
            bytes_ += static_cast<uint32_t>(line.size());
            if (errorSidecar) {
                std::ofstream error(config_.directory / "gpu-diagnostics.error.json", std::ios::binary | std::ios::trunc);
                error.write(line.data(), static_cast<std::streamsize>(line.size())); error.close();
                if (error.fail()) return LogResult::IoFailure;
            }
        }
        // Only this typed/allow-listed record is mirrored to the existing logger.
        LogInfo(LogCategory::Render, "D3D_DIAG_V1 {}", std::string_view(line.data(), line.size() - 1));
        return LogResult::Written;
    } catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordStage(Stage stage, bool entering, uint64_t frame, uint64_t micros) noexcept
{
    try { return Emit("\"event\":\"stage\",\"stage\":\"" + std::string(Name(StageNames, stage)) + "\",\"enter\":"
        + (entering ? "true" : "false") + ",\"frame\":" + std::to_string(frame) + ",\"microseconds\":" + std::to_string(micros)); }
    catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordBackend(Backend backend, uint32_t vendor, uint32_t device, uint32_t feature, uint32_t width, uint32_t height) noexcept
{
    try { return Emit("\"event\":\"backend\",\"backend\":\"" + std::string(Name(BackendNames, backend)) + "\",\"vendorId\":"
        + std::to_string(vendor) + ",\"deviceId\":" + std::to_string(device) + ",\"featureLevel\":" + std::to_string(feature)
        + ",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height)); }
    catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordFrame(const Frame &f, Decision decision) noexcept
{
    try { return Emit("\"event\":\"frame\",\"frame\":" + std::to_string(f.sequence) + ",\"decision\":" + std::to_string(static_cast<unsigned>(decision))
        + ",\"requestedGpu\":" + (f.requestedGpu ? "true" : "false") + ",\"usedGpu\":" + (f.usedGpu ? "true" : "false")
        + ",\"hardware\":" + (f.hardware ? "true" : "false") + ",\"warp\":" + (f.warp ? "true" : "false")
        + ",\"completeOutput\":" + (f.completeOutput ? "true" : "false") + ",\"cpuTriangles\":" + std::to_string(f.cpuRasterizedTriangles)
        + ",\"triangles\":" + std::to_string(f.submittedTriangles) + ",\"draws\":" + std::to_string(f.drawCalls)
        + ",\"worldUs\":" + std::to_string(f.worldMicroseconds) + ",\"recordUs\":" + std::to_string(f.recordMicroseconds)
        + ",\"readbackUs\":" + std::to_string(f.readbackMicroseconds) + ",\"presentUs\":" + std::to_string(f.presentMicroseconds)); }
    catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordFailure(const Failure &f, uint64_t frame) noexcept
{
    try { return Emit("\"event\":\"error\",\"error\":\"" + std::string(Name(ErrorNames, f.error)) + "\",\"stage\":\""
        + std::string(Name(StageNames, f.stage)) + "\",\"frame\":" + std::to_string(frame) + ",\"nativeCodeKnown\":"
        + (f.nativeCodeKnown ? "true" : "false") + ",\"nativeCode\":" + std::to_string(f.nativeCode), true); }
    catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordHudRect(uint16_t widget, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t vw, uint32_t vh) noexcept
{
    try { return Emit("\"event\":\"hud_rect\",\"widget\":" + std::to_string(widget) + ",\"x\":" + std::to_string(x) + ",\"y\":"
        + std::to_string(y) + ",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height)
        + ",\"viewportWidth\":" + std::to_string(vw) + ",\"viewportHeight\":" + std::to_string(vh)); }
    catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordCathedralContext(const CathedralContext &c) noexcept
{
    if (!std::isfinite(c.fov) || c.fov < std::numeric_limits<int32_t>::min()
        || c.fov > std::numeric_limits<int32_t>::max() || c.fov != std::floor(c.fov)) return LogResult::InvalidMetadata;
    try {
        std::string fields="\"event\":\"cathedral_context\"";
        fields+=",\"diagnosticObservationKnown\":"+std::string(c.diagnosticObservationKnown?"true":"false");
        fields+=",\"materialBindingValid\":"+std::string(c.materialBindingValid?"true":"false");
        fields+=",\"firstKnown\":"+std::string(c.firstKnown?"true":"false");
        fields+=",\"firstPlayerKnown\":"+std::string(c.firstPlayerKnown?"true":"false");
        fields+=",\"firstPlayerChanging\":"+std::string(c.firstPlayerChanging?"true":"false");
        fields+=",\"gpuFailureCodeKnown\":"+std::string(c.gpuFailureCodeKnown?"true":"false");
        fields+=",\"gpuAttempted\":"+std::string(c.gpuAttempted?"true":"false");
        fields+=",\"materialKind\":"+std::to_string(c.materialKind);
        fields+=",\"materialPiece\":"+std::to_string(c.materialPiece);
        fields+=",\"materialAxis\":"+std::to_string(c.materialAxis);
        fields+=",\"materialColumn\":"+std::to_string(c.materialColumn);
        fields+=",\"materialNativeSlot\":"+std::to_string(c.materialNativeSlot);
        fields+=",\"materialX\":"+std::to_string(c.materialX);
        fields+=",\"materialZ\":"+std::to_string(c.materialZ);
        fields+=",\"firstFocusX\":"+std::to_string(c.firstFocusX);
        fields+=",\"firstFocusZ\":"+std::to_string(c.firstFocusZ);
        fields+=",\"firstCameraMode\":"+std::to_string(c.firstCameraMode);
        fields+=",\"firstPlayerMode\":"+std::to_string(c.firstPlayerMode);
        fields+=",\"gpuFailureKind\":"+std::to_string(c.gpuFailureKind);
        fields+=",\"gpuFailureCode\":"+std::to_string(c.gpuFailureCode);
        fields+=",\"firstReason\":"+std::to_string(c.firstReason);
        fields+=",\"firstStage\":"+std::to_string(c.firstStage);
        fields+=",\"firstSeed\":"+std::to_string(c.firstSeed);
        fields+=",\"hostEntries\":"+std::to_string(c.hostEntries);
        fields+=",\"hostEntryLimit\":"+std::to_string(c.hostEntryLimit);
        fields+=",\"hostBytes\":"+std::to_string(c.hostBytes);
        fields+=",\"hostByteLimit\":"+std::to_string(c.hostByteLimit);
        fields+=",\"hostEpoch\":"+std::to_string(c.hostEpoch);
        fields+=",\"firstEpoch\":"+std::to_string(c.firstEpoch);
        fields+=",\"liveEpoch\":"+std::to_string(c.liveEpoch);
        fields+=",\"frameEpoch\":"+std::to_string(c.frameEpoch);
        fields+=",\"gpuSerial\":"+std::to_string(c.gpuSerial);
        fields+=",\"seed\":"+std::to_string(c.seed);
        fields+=",\"reason\":"+std::to_string(c.reason);
        fields+=",\"stage\":"+std::to_string(c.stage);
        fields+=",\"fallbackFlags\":"+std::to_string(c.fallbackFlags);
        fields+=",\"level\":"+std::to_string(c.level);
        fields+=",\"levelType\":"+std::to_string(c.levelType);
        fields+=",\"setId\":"+std::to_string(c.setId);
        fields+=",\"cameraMode\":"+std::to_string(c.cameraMode);
        fields+=",\"focusX\":"+std::to_string(c.focusX);
        fields+=",\"focusZ\":"+std::to_string(c.focusZ);
        fields+=",\"minX\":"+std::to_string(c.minX);
        fields+=",\"minZ\":"+std::to_string(c.minZ);
        fields+=",\"maxX\":"+std::to_string(c.maxX);
        fields+=",\"maxZ\":"+std::to_string(c.maxZ);
        fields+=",\"tickRate\":"+std::to_string(c.tickRate);
        fields+=",\"fov\":"+std::to_string(static_cast<int32_t>(c.fov));
        fields+=",\"enabled\":"+std::string(c.enabled?"true":"false");
        fields+=",\"eligible\":"+std::string(c.eligible?"true":"false");
        fields+=",\"playerPresent\":"+std::string(c.playerPresent?"true":"false");
        fields+=",\"hellfire\":"+std::string(c.hellfire?"true":"false");
        fields+=",\"spawn\":"+std::string(c.spawn?"true":"false");
        fields+=",\"setLevel\":"+std::string(c.setLevel?"true":"false");
        fields+=",\"zoom\":"+std::string(c.zoom?"true":"false");
        fields+=",\"frameReady\":"+std::string(c.frameReady?"true":"false");
        fields+=",\"requiresFallback\":"+std::string(c.requiresFallback?"true":"false");
        fields+=",\"requestedGpu\":"+std::string(c.requestedGpu?"true":"false");
        fields+=",\"usedGpu\":"+std::string(c.usedGpu?"true":"false");
        fields+=",\"preparationRefused\":"+std::string(c.preparationRefused?"true":"false");
        fields+=",\"firstRefusalInEpoch\":"+std::string(c.firstRefusalInEpoch?"true":"false");
        fields+=",\"attemptLimitReached\":"+std::string(c.attemptLimitReached?"true":"false");
        return Emit(std::move(fields));
    } catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordSaveActionContext(const SaveActionContext &c) noexcept
{
    const auto stage=static_cast<unsigned>(c.stage);
    if (c.attempt==0 || stage>static_cast<unsigned>(SaveActionStage::FeedbackFailed)) return LogResult::InvalidMetadata;
    try {
        std::string fields="\"event\":\"save_action_context\"";
        fields+=",\"attempt\":"+std::to_string(c.attempt);
        fields+=",\"saveNumber\":"+std::to_string(c.saveNumber);
        fields+=",\"saveNumberKnown\":"+std::string(c.saveNumberKnown?"true":"false");
        fields+=",\"stage\":"+std::to_string(static_cast<unsigned>(c.stage));
        fields+=",\"level\":"+std::to_string(c.level);
        fields+=",\"levelType\":"+std::to_string(c.levelType);
        fields+=",\"setId\":"+std::to_string(c.setId);
        fields+=",\"cursor\":"+std::to_string(c.cursor);
        fields+=",\"playerMode\":"+std::to_string(c.playerMode);
        fields+=",\"setLevel\":"+std::string(c.setLevel?"true":"false");
        fields+=",\"multiplayer\":"+std::string(c.multiplayer?"true":"false");
        fields+=",\"validBefore\":"+std::string(c.validBefore?"true":"false");
        fields+=",\"hasPlayerState\":"+std::string(c.hasPlayerState?"true":"false");
        fields+=",\"dead\":"+std::string(c.dead?"true":"false");
        fields+=",\"hasValidAfter\":"+std::string(c.hasValidAfter?"true":"false");
        fields+=",\"validAfter\":"+std::string(c.validAfter?"true":"false");
        fields+=",\"cameraEnabledKnown\":"+std::string(c.cameraEnabledKnown?"true":"false");
        fields+=",\"cameraEnabled\":"+std::string(c.cameraEnabled?"true":"false");
        return Emit(std::move(fields));
    } catch (...) { return LogResult::IoFailure; }
}
LogResult Logger::RecordInputLayout(bool focused, bool captured, uint32_t buttons, uint32_t uiButtons, int32_t x, int32_t y) noexcept
{
    try { return Emit("\"event\":\"input_layout\",\"focused\":" + std::string(focused ? "true" : "false") + ",\"captured\":"
        + (captured ? "true" : "false") + ",\"ownedButtons\":" + std::to_string(buttons) + ",\"uiOwnedButtons\":" + std::to_string(uiButtons)
        + ",\"cursorPixelX\":" + std::to_string(x) + ",\"cursorPixelY\":" + std::to_string(y)); }
    catch (...) { return LogResult::IoFailure; }
}
StageScope::StageScope(Logger &logger, Stage stage, uint64_t frame, bool sampled) noexcept
    : logger_(logger), stage_(stage), frame_(frame), start_(std::chrono::steady_clock::now()), previous_(ActiveStage), sampled_(sampled)
{
    ActiveStage = stage;
    if (sampled_) logger_.RecordStage(stage_, true, frame_);
}
StageScope::~StageScope()
{
    if (sampled_) logger_.RecordStage(stage_, false, frame_, ElapsedMicroseconds(start_));
    ActiveStage = previous_;
}
} // namespace devilution::d3d_local_diagnostics

// Appended to the two-file diagnostics module; no additional compiled unit.
#ifndef D3D_SOURCE_CLOSURE_SHA256
#define D3D_SOURCE_CLOSURE_SHA256 ""
#endif
namespace devilution::d3d_local_diagnostics {
namespace {
struct SessionState {
    Policy policy;
    Logger logger;
    std::filesystem::path directory;
    Failure first;
    Failure opening;
    bool selected = false, argumentsFinal = false, diagnostics = true;
    bool contextDiagnosticsEnabled = false; // Explicit opt-in for new context observers only.
    bool opened = false, attempted = false, refusalRecorded = false;
    uint64_t frame = 0;
};
SessionState &Session() { static SessionState state; return state; }

// Restricted flat JSON capability schema. Reject duplicates, escapes, unknown
// values, oversized files and trailing data rather than trusting regex matches.
bool ReadCapabilities(const std::filesystem::path &file, std::map<std::string,std::string> &fields)
{
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(file, ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)
        || std::filesystem::file_size(file,ec) > 4096 || ec) return false;
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    size_t i=0;
    const auto ws=[&](){ while(i<text.size() && (text[i]==' ' || text[i]=='\r' || text[i]=='\n' || text[i]=='\t')) ++i; };
    const auto quoted=[&](std::string &value){
        if (i>=text.size() || text[i++]!='"') return false;
        while(i<text.size() && text[i]!='"') {
            const unsigned char c=static_cast<unsigned char>(text[i++]);
            if(c<32 || c=='\\' || c>126) return false;
            value.push_back(static_cast<char>(c));
        }
        return i<text.size() && text[i++]=='"';
    };
    ws(); if(i>=text.size() || text[i++]!='{') return false;
    for(unsigned count=0;count<12;++count) {
        ws(); std::string key,value;
        if(!quoted(key)) return false;
        ws(); if(i>=text.size() || text[i++]!=':') return false;
        ws();
        const bool isString = key=="format" || key=="executableSha256" || key=="sourceClosureSha256";
        if(i>=text.size() || (text[i]=='"') != isString) return false;
        if(isString) { if(!quoted(value)) return false; }
        else { const size_t start=i;while(i<text.size() && ((text[i]>='a'&&text[i]<='z')||(text[i]>='0'&&text[i]<='9')))++i;value=text.substr(start,i-start); }
        if(value.empty() || !fields.emplace(key,value).second) return false;
        ws(); if(i>=text.size()) return false;
        const char next=text[i++];
        if(next=='}') { ws();return i==text.size(); }
        if(next!=',') return false;
    }
    return false;
}
bool VerifyCapabilities(const std::filesystem::path &file, std::string_view exe, std::string_view source)
{
    std::map<std::string,std::string> fields;
    if(!ReadCapabilities(file,fields) || fields.size()!=9) return false;
    return fields["format"]=="d3d.gpu-diagnostics-capabilities" && fields["version"]=="1"
        && fields["admitted"]=="true" && fields["diagnosticsSchema"]=="1"
        && fields["gpuRequiredPreflight"]=="true" && fields["explicitCpuDiagnostic"]=="true" && fields["nativeContextFallback"]=="true"
        && fields["executableSha256"]==exe && fields["sourceClosureSha256"]==source;
}
} // namespace
bool SetSessionMode(Mode mode) noexcept
{
    auto &s=Session();
    if(s.argumentsFinal || (s.selected && s.policy.mode!=mode)) return false;
    s.selected=true;s.policy={mode,mode==Mode::CpuDiagnosticExplicit};return true;
}
bool SetSessionDirectory(std::string_view path) noexcept
{
    try { auto &s=Session();if(s.argumentsFinal || path.empty() || !s.directory.empty())return false;
        s.directory=std::filesystem::u8path(path);return s.directory.is_absolute();
    } catch(...) { return false; }
}
void EnableSessionDiagnostics() noexcept { auto &s=Session();s.diagnostics=true;s.contextDiagnosticsEnabled=true; }
bool SessionDiagnosticsEnabled() noexcept { return Session().contextDiagnosticsEnabled; }
bool SessionDiagnosticsReady() noexcept { const auto &s=Session();return s.contextDiagnosticsEnabled && s.opened; }
LogResult RecordSessionCathedralContext(const CathedralContext &context) noexcept
{
    if (!SessionDiagnosticsEnabled()) return LogResult::NotOpen;
    if (OpenSession().error != Error::None) return LogResult::IoFailure;
    return SessionLogger().RecordCathedralContext(context);
}
LogResult RecordSessionSaveActionContext(const SaveActionContext &c) noexcept
{
    if (!SessionDiagnosticsEnabled()) return LogResult::NotOpen;
    const auto stage=static_cast<unsigned>(c.stage);
    if (c.attempt==0 || stage>static_cast<unsigned>(SaveActionStage::FeedbackFailed)) return LogResult::InvalidMetadata;
    // Main-thread observer only; not game state. Never retry failed writes per frame.
    static uint64_t lastAttempt=0;static unsigned seen=0,recordAttempts=0;
    if (recordAttempts>=224 || c.attempt<lastAttempt) return LogResult::InvalidMetadata;
    if (c.attempt!=lastAttempt) {lastAttempt=c.attempt;seen=0;}
    const unsigned bit=1U<<stage;
    if ((seen&bit)!=0) return LogResult::InvalidMetadata;
    seen|=bit;++recordAttempts;
    if (OpenSession().error != Error::None) return LogResult::IoFailure;
    return SessionLogger().RecordSaveActionContext(c);
}

bool FinalizeSessionArguments() noexcept
{
    auto &s=Session();s.argumentsFinal=true;
    return s.policy.mode==Mode::GpuRequired || (s.selected && s.policy.cpuDiagnosticConsent);
}
const Policy &SessionPolicy() noexcept { return Session().policy; }
Logger &SessionLogger() noexcept { return Session().logger; }
uint64_t SessionFrame() noexcept { return Session().frame; }
uint64_t BeginSessionFrame() noexcept
{
    auto &s=Session();if(s.frame!=UINT64_MAX)++s.frame;return s.frame;
}
bool SampleSessionFrame() noexcept { const auto frame=SessionFrame();return frame<5 || frame%120==0; }
void CloseSession() noexcept { auto &s=Session();if(s.opened)s.logger.RecordStage(Stage::Shutdown,true,s.frame);s.logger.Close();s.opened=false; }
Failure OpenSession() noexcept
{
    auto &s=Session();if(s.attempted)return s.opening;s.attempted=true;
    try {
#ifdef _WIN32
        std::array<wchar_t,32768> path{};
        const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(length==0 || length>=path.size())throw std::runtime_error("identity");
        const std::filesystem::path exe(std::wstring(path.data(),length));
        std::ifstream binary(exe,std::ios::binary);
        std::array<unsigned char,32> digest{};
        if(!binary || std::filesystem::file_size(exe)>256U*1024U*1024U)throw std::runtime_error("identity");
        picosha2::hash256(binary,digest.begin(),digest.end());
        if(binary.bad())throw std::runtime_error("identity");
        const std::string exeSha=picosha2::bytes_to_hex_string(digest.begin(),digest.end());
        const std::string sourceSha=D3D_SOURCE_CLOSURE_SHA256;
        if(!IsSha256(sourceSha) || !VerifyCapabilities(exe.parent_path()/"d3d-build-capabilities.json",exeSha,sourceSha))
            throw std::runtime_error("identity");
        const auto ticks=std::chrono::system_clock::now().time_since_epoch().count();
        const uint64_t session=static_cast<uint64_t>(ticks)^static_cast<uint64_t>(GetCurrentProcessId());
        if(s.directory.empty())s.directory=exe.parent_path()/"diagnostics"/("gpu-session-"+std::to_string(session));
        std::error_code ec;std::filesystem::create_directories(s.directory,ec);
        if(ec)throw std::runtime_error("log");
        Config config{s.directory,exeSha,sourceSha,session==0?1:session,s.policy};
        if(s.logger.Open(config)!=LogResult::Written)throw std::runtime_error("log");
        s.opened=true;std::atexit(CloseSession);
        return {};
#else
        s.opening={Error::Device3DUnavailable,Stage::Launch};return s.opening;
#endif
    } catch(...) { s.opening={Error::LogUnavailable,Stage::Launch};s.first=s.opening;return s.opening; }
}
Failure SessionFirstFailure() noexcept { return Session().first; }
void RecordSessionFailure(Failure failure) noexcept
{
    auto &s=Session();if(failure.error==Error::None)return;
    if(s.first.error==Error::None) {
        s.first=failure;
        if(s.opened && s.logger.RecordFailure(failure,s.frame)!=LogResult::Written)s.opening={Error::LogUnavailable,Stage::Launch};
    }
}
void RecordNativeFailure(uint8_t kind, int32_t code, bool known) noexcept
{
    Error error=kind==1?Error::CapacityExceeded:kind==4?Error::Device3DUnavailable
        :kind==2?Error::InvalidGpuInput:Error::GpuSubmissionFailed;
    if(ActiveStage==Stage::ShaderCompile)error=Error::ShaderCompileFailed;
    if(known && (static_cast<uint32_t>(code)==0x887A0005U || static_cast<uint32_t>(code)==0x887A0006U || static_cast<uint32_t>(code)==0x887A0007U))error=Error::DeviceRemoved;
    RecordSessionFailure({error,ActiveStage,code,known});
}
void RecordSessionFrame(const Frame &frame, Decision decision) noexcept
{
    auto &s=Session();
    if(s.opened && (SampleSessionFrame() || (decision==Decision::RefuseFrame && !s.refusalRecorded))) {
        if(s.logger.RecordFrame(frame,decision)!=LogResult::Written)s.opening={Error::LogUnavailable,Stage::Resolve};
    }
    s.refusalRecorded=decision==Decision::RefuseFrame;
}
} // namespace devilution::d3d_local_diagnostics
