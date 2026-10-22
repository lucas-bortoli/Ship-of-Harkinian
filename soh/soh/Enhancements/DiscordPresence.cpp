#include "DiscordPresence.h"

#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>

#include "DiscordPresenceConfig.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/util.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <chrono>
#include <ctime>
#include <memory>
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
using Clock = std::chrono::system_clock;
using SteadyClock = std::chrono::steady_clock;
constexpr auto kPublishInterval = std::chrono::seconds(15);

enum class Screen {
    Title,
    FileSelect,
    Gameplay,
};

class PresenceManager {
  public:
    void Initialize() {
        if (mHooksRegistered) {
            return;
        }
        mHooksRegistered = true;
        mSessionStart = Clock::to_time_t(Clock::now());

        auto* interactor = GameInteractor::Instance;
        interactor->RegisterGameHook<GameInteractor::OnZTitleInit>([this](void*) { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnPresentFileSelect>([this]() { mScreen = Screen::FileSelect; });
        interactor->RegisterGameHook<GameInteractor::OnLoadGame>([this](int32_t) { mScreen = Screen::Gameplay; });
        interactor->RegisterGameHook<GameInteractor::OnExitGame>([this](int32_t) { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnSceneInit>([this](int16_t) { mScreen = Screen::Gameplay; });
        interactor->RegisterGameHook<GameInteractor::OnPlayDestroy>([this]() { mScreen = Screen::Title; });
        interactor->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([this]() { Tick(); });
    }

    void Tick() {
        const bool enabled = CVarGetInteger(CVAR_SETTING("DiscordRichPresence"), 1) != 0;
        if (!enabled) {
            if (mEnabled) {
                ClearAndRelease();
            }
            mEnabled = false;
            return;
        }

        if (!mEnabled) {
            mEnabled = true;
            mHasPublished = false;
        }

        if (mClient) {
            discordpp::RunCallbacks();
        } else {
            mClient = std::make_shared<discordpp::Client>();
            mClient->SetApplicationId(SOH_DISCORD_CONFIGURED_APPLICATION_ID);
            mClient->SetStatusChangedCallback(
                [this](discordpp::Client::Status status, discordpp::Client::Error, int32_t) {
                    mClientReady = status == discordpp::Client::Status::Ready;
                    if (mClientReady) {
                        mHasPublished = false;
                    }
                });
            mClient->Connect();
            mHasPublished = false;
        }

        if (!mClientReady) {
            return;
        }

        // A load hook can run before PlayState is fully constructed. Do not read game state until it exists.
        if (mScreen == Screen::Gameplay && gPlayState == nullptr) {
            return;
        }

        const auto [details, state] = BuildActivityText();
        const auto now = SteadyClock::now();
        if (mHasPublished && now - mLastPublish < kPublishInterval) {
            return;
        }

        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Playing);
        activity.SetDetails(details);
        if (!state.empty()) {
            activity.SetState(state);
        }
        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(static_cast<uint64_t>(mSessionStart) * 1000);
        activity.SetTimestamps(timestamps);

        mLastPublish = now;
        mHasPublished = true;
        mClient->UpdateRichPresence(activity, [this](const discordpp::ClientResult& result) {
            if (!result.Successful() && !mLoggedFailure) {
                SPDLOG_WARN("Discord Rich Presence update failed: {}", result.Error());
                mLoggedFailure = true;
            } else if (result.Successful()) {
                mLoggedFailure = false;
            }
        });
    }

    void Shutdown() {
        if (mClient) {
            if (mClientReady) {
                mClient->ClearRichPresence();
                mClient->Disconnect();
            }
            discordpp::RunCallbacks();
            mClient.reset();
            mClientReady = false;
        }
    }

    void ClearAndRelease() {
        if (mClient) {
            if (mClientReady) {
                mClient->ClearRichPresence();
                mClient->Disconnect();
            }
            discordpp::RunCallbacks();
            mClient.reset();
            mClientReady = false;
        }
        mHasPublished = false;
    }

  private:
    std::pair<std::string, std::string> BuildActivityText() const {
        switch (mScreen) {
            case Screen::Title:
                return { "At the title screen", "" };
            case Screen::FileSelect:
                return { "Selecting a save file", "" };
            case Screen::Gameplay: {
                if (!SohUtils::IsValidScene(gPlayState->sceneNum)) {
                    const std::string age = gSaveContext.linkAge == LINK_AGE_CHILD ? "Child Link" : "Adult Link";
                    return { "Exploring Hyrule", age };
                }
                const auto& scene = SohUtils::GetSceneName(gPlayState->sceneNum);
                const std::string details = scene.empty() ? "Exploring Hyrule" : scene;
                const std::string age = gSaveContext.linkAge == LINK_AGE_CHILD ? "Child Link" : "Adult Link";
                return { details, age };
            }
        }
        return { "At the title screen", "" };
    }

    std::shared_ptr<discordpp::Client> mClient;
    std::time_t mSessionStart = 0;
    SteadyClock::time_point mLastPublish{};
    Screen mScreen = Screen::Title;
    bool mHasPublished = false;
    bool mHooksRegistered = false;
    bool mEnabled = false;
    bool mLoggedFailure = false;
    bool mClientReady = false;
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
