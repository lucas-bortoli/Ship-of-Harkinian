#include "DiscordPresence.h"

#include <discord_rpc.h>

#include "DiscordPresenceConfig.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/util.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

extern "C" {
#include "variables.h"
#include "z64.h"
extern PlayState* gPlayState;
}

namespace DiscordPresence {
namespace {
using SteadyClock = std::chrono::steady_clock;
constexpr auto kPublishInterval = std::chrono::seconds(15);
constexpr std::size_t kRpcTextLimit = 127; // The protocol field is 128 bytes including its terminator.

enum class Screen {
    Title,
    FileSelect,
    Gameplay,
};

class PresenceManager;
PresenceManager* sCallbackTarget = nullptr;

class PresenceManager {
  public:
    void Initialize() {
        if (mHooksRegistered) {
            return;
        }
        mHooksRegistered = true;
        mSessionStart = static_cast<int64_t>(std::time(nullptr));

        auto* interactor = GameInteractor::Instance;
        interactor->RegisterGameHook<GameInteractor::OnZTitleInit>([this](void*) { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnPresentFileSelect>([this]() { mScreen = Screen::FileSelect; });
        interactor->RegisterGameHook<GameInteractor::OnLoadGame>([this](int32_t) { mScreen = Screen::Gameplay; });
        interactor->RegisterGameHook<GameInteractor::OnExitGame>([this](int32_t) { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnSceneInit>([this](int16_t) { mScreen = Screen::Gameplay; });
        interactor->RegisterGameHook<GameInteractor::OnPlayDestroy>([this]() { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([this]() { Tick(); });
        sCallbackTarget = this;
    }

    void Tick() {
        if (mInitialized) {
            Discord_RunCallbacks();
        }

        const bool enabled = CVarGetInteger(CVAR_SETTING("DiscordRichPresence"), 1) != 0;
        if (!enabled) {
            if (mEnabled && mInitialized) {
                Discord_ClearPresence();
                mHasPublished = false;
            }
            mEnabled = false;
            return;
        }

        if (!mEnabled) {
            mEnabled = true;
            mHasPublished = false;
        }

        if (!mInitialized) {
            InitializeRpc();
        }
        if (!mConnected) {
            return;
        }

        // A load hook can run before PlayState is fully constructed. Do not read game state until it exists.
        if (mScreen == Screen::Gameplay && gPlayState == nullptr) {
            return;
        }

        auto [details, state] = BuildActivityText();
        details = BoundRpcText(details);
        state = BoundRpcText(state);
        const auto now = SteadyClock::now();
        if (now < mRetryAfter || (mHasPublished && now - mLastPublish < kPublishInterval)) {
            return;
        }

        DiscordRichPresence presence{};
        presence.details = details.c_str();
        presence.state = state.empty() ? nullptr : state.c_str();
        presence.startTimestamp = mSessionStart;
        Discord_UpdatePresence(&presence);
        mLastPublish = now;
        mHasPublished = true;
        mRetryAfter = SteadyClock::time_point{};
    }

    void Shutdown() {
        if (mInitialized) {
            Discord_ClearPresence();
            Discord_Shutdown();
            mInitialized = false;
            mConnected = false;
        }
        sCallbackTarget = nullptr;
    }

    void OnReady() {
        mConnected = true;
        mHasPublished = false;
        mRetryAfter = SteadyClock::time_point{};
        mLastError.clear();
    }

    void OnDisconnected(int errorCode, const char* message) {
        mConnected = false;
        RecordError(errorCode, message);
    }

    void OnError(int errorCode, const char* message) {
        mRetryAfter = SteadyClock::now() + kPublishInterval;
        RecordError(errorCode, message);
    }

  private:
    void InitializeRpc() {
        DiscordEventHandlers handlers{};
        handlers.ready = [](const DiscordUser*) {
            if (sCallbackTarget != nullptr) {
                sCallbackTarget->OnReady();
            }
        };
        handlers.disconnected = [](int errorCode, const char* message) {
            if (sCallbackTarget != nullptr) {
                sCallbackTarget->OnDisconnected(errorCode, message);
            }
        };
        handlers.errored = [](int errorCode, const char* message) {
            if (sCallbackTarget != nullptr) {
                sCallbackTarget->OnError(errorCode, message);
            }
        };

        mApplicationId = std::to_string(SOH_DISCORD_CONFIGURED_APPLICATION_ID);
        sCallbackTarget = this;
        Discord_Initialize(mApplicationId.c_str(), &handlers, 0, nullptr);
        mInitialized = true;
    }

    void RecordError(int errorCode, const char* message) {
        const std::string error = std::to_string(errorCode) + ":" + (message == nullptr ? "" : message);
        if (error != mLastError) {
            SPDLOG_WARN("Discord Rich Presence connection error: {}", error);
            mLastError = error;
        }
    }

    std::pair<std::string, std::string> BuildActivityText() const {
        switch (mScreen) {
            case Screen::Title:
                return { "At the title screen", "" };
            case Screen::FileSelect:
                return { "Selecting a save file", "" };
            case Screen::Gameplay: {
                const std::string age = gSaveContext.linkAge == LINK_AGE_CHILD ? "Child Link" : "Adult Link";
                if (!SohUtils::IsValidScene(gPlayState->sceneNum)) {
                    return { "Exploring Hyrule", age };
                }
                const auto& scene = SohUtils::GetSceneName(gPlayState->sceneNum);
                return { scene.empty() ? "Exploring Hyrule" : scene, age };
            }
        }
        return { "At the title screen", "" };
    }

    static std::string BoundRpcText(const std::string& text) {
        if (text.size() <= kRpcTextLimit) {
            return text;
        }
        std::size_t length = kRpcTextLimit;
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
            --length;
        }
        return text.substr(0, length);
    }

    bool mInitialized = false;
    bool mConnected = false;
    bool mHasPublished = false;
    bool mHooksRegistered = false;
    bool mEnabled = false;
    int64_t mSessionStart = 0;
    SteadyClock::time_point mLastPublish{};
    SteadyClock::time_point mRetryAfter{};
    Screen mScreen = Screen::Title;
    std::string mApplicationId;
    std::string mLastError;
};

PresenceManager sManager;
} // namespace

void Initialize() {
    sManager.Initialize();
}

void Tick() {
    sManager.Tick();
}

void Shutdown() {
    sManager.Shutdown();
}
} // namespace DiscordPresence
