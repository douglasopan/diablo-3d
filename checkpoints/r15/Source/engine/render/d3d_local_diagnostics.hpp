#pragma once

// Isolated proposal: no device creation, profile access, save access or upload.
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace devilution::d3d_local_diagnostics {

enum class Mode : uint8_t { GpuRequired, CpuDiagnosticExplicit };
enum class Context : uint8_t { GpuWorld, NativeContextIncompatible };
enum class Decision : uint8_t { PublishGpu, PublishNativeContext, PublishCpuDiagnostic, RefuseFrame };
enum class Backend : uint8_t { Unknown, SdlHardware, SdlSoftware, Direct3D11Hardware, Direct3D11Warp, CpuDiagnostic };
enum class Stage : uint8_t {
    Launch, SdlVideo, Renderer2D, Device3D, WorldPrepare, GpuBegin,
    SceneRecord, WorldSubmit, ViewModelSubmit, Readback, Atmosphere,
    Resolve, HudCompose, Present, InputLayout, Shutdown, ShaderCompile, Targets, Camera
};
enum class Error : uint8_t {
    None, DiagnosticConsentMissing, Renderer2DUnavailable, Renderer2DSoftware,
    Device3DUnavailable, WarpRefused, GpuPreferenceDisabled, GpuBeginFailed,
    GpuSubmissionFailed, GpuReadbackFailed, GpuOutputRejected, DeviceRemoved,
    ShaderCompileFailed, CapacityExceeded, InvalidGpuInput, CpuFallbackRefused,
    HudPresentationFailed, LogUnavailable, DiagnosticOutputRejected, CameraProjectionInvalid, CameraVisibilityUnavailable
};
enum class LogResult : uint8_t { Written, NotOpen, InvalidMetadata, IoFailure };

struct Policy {
    Mode mode = Mode::GpuRequired;
    // This must come from an explicit session CLI action, never an INI fallback.
    bool cpuDiagnosticConsent = false;
};
struct Preflight {
    bool renderer2DReady = false;
    bool renderer2DAccelerated = false;
    bool device3DReady = false;
    bool device3DWarp = false;
};
struct Frame {
    uint64_t sequence = 0;
    Context context = Context::GpuWorld;
    bool requestedGpu = false;
    bool usedGpu = false;
    bool hardware = false;
    bool warp = false;
    bool completeOutput = false;
    uint64_t cpuRasterizedTriangles = 0;
    uint64_t submittedTriangles = 0;
    uint64_t drawCalls = 0;
    uint64_t worldMicroseconds = 0;
    uint64_t recordMicroseconds = 0;
    uint64_t readbackMicroseconds = 0;
    uint64_t presentMicroseconds = 0;
};
struct Failure {
    Error error = Error::None;
    Stage stage = Stage::Launch;
    int32_t nativeCode = 0; // Numeric HRESULT/SDL/OS code only, no raw error text.
    bool nativeCodeKnown = false;
};

Failure CheckPreflight(const Policy &policy, const Preflight &preflight) noexcept;
Decision CheckFrame(const Policy &policy, const Frame &frame, Failure &failure) noexcept;
std::string_view PublicErrorMessage(Error error) noexcept;

struct Config {
    // Caller supplies a dedicated, approved local log directory. Never derive
    // this from PrefPath, hero/save paths or environment credentials.
    std::filesystem::path directory;
    std::string executableSha256;
    std::string sourceClosureSha256;
    uint64_t session = 0; // A locally generated nonzero run number, not an auth token.
    Policy policy;
    uint32_t maximumFileBytes = 512U * 1024U;
    uint8_t retainedFiles = 4;
};

// Allow-listed context only: no save bytes, secrets, personal names or paths.
struct CathedralContext {
    bool diagnosticObservationKnown = false;
    bool materialBindingValid = false;
    bool firstKnown = false;
    bool firstPlayerKnown = false;
    bool firstPlayerChanging = false;
    bool gpuFailureCodeKnown = false;
    bool gpuAttempted = false;
    int32_t materialKind = 0;
    int32_t materialPiece = 0;
    int32_t materialAxis = 0;
    int32_t materialColumn = 0;
    int32_t materialNativeSlot = 0;
    int32_t materialX = 0;
    int32_t materialZ = 0;
    int32_t firstFocusX = 0;
    int32_t firstFocusZ = 0;
    int32_t firstCameraMode = 0;
    int32_t firstPlayerMode = 0;
    int32_t gpuFailureKind = 0;
    int32_t gpuFailureCode = 0;
    uint32_t firstReason = 0;
    uint32_t firstStage = 0;
    uint32_t firstSeed = 0;
    uint64_t hostEntries = 0;
    uint64_t hostEntryLimit = 0;
    uint64_t hostBytes = 0;
    uint64_t hostByteLimit = 0;
    uint64_t hostEpoch = 0;
    uint64_t firstEpoch = 0;
    uint64_t liveEpoch = 0;
    uint64_t frameEpoch = 0;
    uint64_t gpuSerial = 0;
    uint32_t seed = 0;
    uint32_t reason = 0;
    uint32_t stage = 0;
    uint32_t fallbackFlags = 0;
    int32_t level = 0;
    int32_t levelType = 0;
    int32_t setId = 0;
    int32_t cameraMode = 0;
    int32_t focusX = 0;
    int32_t focusZ = 0;
    int32_t minX = 0;
    int32_t minZ = 0;
    int32_t maxX = 0;
    int32_t maxZ = 0;
    int32_t tickRate = 0;
    double fov = 0;
    bool enabled = false;
    bool eligible = false;
    bool playerPresent = false;
    bool hellfire = false;
    bool spawn = false;
    bool setLevel = false;
    bool zoom = false;
    bool frameReady = false;
    bool requiresFallback = false;
    bool requestedGpu = false;
    bool usedGpu = false;
    bool preparationRefused = false;
    bool firstRefusalInEpoch = false;
    bool attemptLimitReached = false;
};
enum class SaveActionStage : uint8_t { Entry, RefusedCursor, RefusedDead, BeforeFull, AfterFull, FeedbackSaved, FeedbackFailed };
struct SaveActionContext {
    uint32_t saveNumber = 0;
    bool saveNumberKnown = false;
    uint64_t attempt = 0;
    SaveActionStage stage = SaveActionStage::Entry;
    int32_t level = 0;
    int32_t levelType = 0;
    int32_t setId = 0;
    int32_t cursor = 0;
    int32_t playerMode = 0;
    bool setLevel = false;
    bool multiplayer = false;
    bool validBefore = false;
    bool hasPlayerState = false;
    bool dead = false;
    bool hasValidAfter = false;
    bool validAfter = false;
    bool cameraEnabledKnown = false;
    bool cameraEnabled = false;
};

class Logger {
public:
    Logger() = default;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    LogResult Open(const Config &config) noexcept;
    void Close() noexcept;
    LogResult RecordStage(Stage stage, bool entering, uint64_t frame, uint64_t microseconds = 0) noexcept;
    LogResult RecordCathedralContext(const CathedralContext &context) noexcept;
    LogResult RecordSaveActionContext(const SaveActionContext &context) noexcept;
    LogResult RecordBackend(Backend backend, uint32_t vendorId, uint32_t deviceId,
        uint32_t featureLevel, uint32_t width, uint32_t height) noexcept;
    LogResult RecordFrame(const Frame &frame, Decision decision) noexcept;
    LogResult RecordFailure(const Failure &failure, uint64_t frame) noexcept;
    LogResult RecordHudRect(uint16_t widget, int32_t x, int32_t y, uint32_t width, uint32_t height,
        uint32_t viewportWidth, uint32_t viewportHeight) noexcept;
    LogResult RecordInputLayout(bool focused, bool captured, uint32_t ownedButtonMask,
        uint32_t uiOwnedButtonMask, int32_t cursorPixelX, int32_t cursorPixelY) noexcept;

private:
    LogResult Emit(std::string fields, bool errorSidecar = false) noexcept;
    bool Rotate(size_t requiredBytes) noexcept;
    bool PathsAreOrdinaryFiles() const noexcept;
    std::filesystem::path File(unsigned index) const;
    std::mutex mutex_;
    std::ofstream stream_;
    Config config_;
    uint64_t sequence_ = 0;
    uint32_t bytes_ = 0;
    bool open_ = false;
};

// Enter is flushed before work begins, so a future hang leaves its last stage.
// Owners must instantiate it only at the selected sampled/initial frame hooks.
class StageScope {
public:
    StageScope(Logger &logger, Stage stage, uint64_t frame, bool sampled = true) noexcept;
    ~StageScope();
    StageScope(const StageScope &) = delete;
    StageScope &operator=(const StageScope &) = delete;
private:
    Logger &logger_;
    Stage stage_;
    uint64_t frame_;
    std::chrono::steady_clock::time_point start_;
    Stage previous_;
    bool sampled_;
};

// Process/session policy only; no option setter, profile or save access.
bool SetSessionMode(Mode mode) noexcept;
bool SetSessionDirectory(std::string_view utf8Path) noexcept;
void EnableSessionDiagnostics() noexcept;
bool SessionDiagnosticsEnabled() noexcept;
bool SessionDiagnosticsReady() noexcept; // Explicit opt-in and existing open session.
LogResult RecordSessionCathedralContext(const CathedralContext &context) noexcept;
LogResult RecordSessionSaveActionContext(const SaveActionContext &context) noexcept;
bool FinalizeSessionArguments() noexcept;
const Policy &SessionPolicy() noexcept;
Logger &SessionLogger() noexcept;
uint64_t SessionFrame() noexcept;
uint64_t BeginSessionFrame() noexcept;
bool SampleSessionFrame() noexcept;
Failure OpenSession() noexcept;
Failure SessionFirstFailure() noexcept;
void RecordSessionFailure(Failure failure) noexcept;
void RecordNativeFailure(uint8_t kind, int32_t code, bool codeKnown) noexcept;
void RecordSessionFrame(const Frame &frame, Decision decision) noexcept;
void CloseSession() noexcept;

} // namespace devilution::d3d_local_diagnostics
