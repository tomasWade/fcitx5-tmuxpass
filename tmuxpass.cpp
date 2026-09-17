/*
 * fcitx5-tmuxpass: perform a configurable action right after a prefix key,
 * so prefix-key tools (tmux, screen, zellij, ...) work without manually
 * toggling the input method. Two actions are available:
 *
 *   - SwitchIM: switch the current input context to the first input method
 *     (usually the plain keyboard layout). Takes effect synchronously
 *     within the fcitx5 process, so there is no timing race with
 *     subsequent keys. Switch back with your normal IM toggle key.
 *
 *   - PassThrough: skip the input method engine for keys pressed within a
 *     short time window after the prefix key, and pass them through to the
 *     application unchanged (the original v0.1 behaviour, kept as an
 *     optional mode).
 *
 * Both modes are gated by the Programs list (wayland appId / input context
 * program name); an empty list means "all programs".
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "fcitx-config/configuration.h"
#include "fcitx-config/enum.h"
#include "fcitx-config/iniparser.h"
#include "fcitx-config/option.h"
#include "fcitx-utils/handlertable.h"
#include "fcitx-utils/i18n.h"
#include "fcitx-utils/key.h"
#include "fcitx/addonfactory.h"
#include "fcitx/addoninstance.h"
#include "fcitx/addonmanager.h"
#include "fcitx/event.h"
#include "fcitx/inputcontext.h"
#include "fcitx/instance.h"

namespace fcitx {

FCITX_CONFIG_ENUM(PrefixAction, SwitchIM, PassThrough);

FCITX_CONFIGURATION(
    TmuxPassConfig,
    KeyListOption prefixKeys{this,
                             "PrefixKeys",
                             _("Prefix keys"),
                             {Key("Control+q")},
                             KeyListConstrain()};
    Option<PrefixAction, NoConstrain<PrefixAction>,
           DefaultMarshaller<PrefixAction>, EnumAnnotation>
        action{this,
               "Action",
               _("Action after prefix key"),
               PrefixAction::SwitchIM};
    Option<std::vector<std::string>> programs{
        this,
        "Programs",
        _("Only react in these programs (appId; empty = all)"),
        {"foot"}};
    Option<int, IntConstrain> windowMs{
        this,
        "WindowMs",
        _("Pass-through window in milliseconds"),
        400,
        IntConstrain(100, 5000)};);

class TmuxPass final : public AddonInstance {
public:
    explicit TmuxPass(Instance *instance) : instance_(instance) {
        reloadConfig();
        handler_ = instance_->watchEvent(
            EventType::InputContextKeyEvent,
            EventWatcherPhase::PreInputMethod,
            [this](Event &e) { handleKey(static_cast<KeyEvent &>(e)); });
    }

    void reloadConfig() override {
        readAsIni(config_, "conf/tmuxpass.conf");
        window_ = std::chrono::milliseconds(*config_.windowMs);
    }

    const Configuration *getConfig() const override { return &config_; }

private:
    void handleKey(KeyEvent &event) {
        const auto &key = event.key();
        const bool press = !event.isRelease();

        if (press && matchesPrefix(key)) {
            if (allowedProgram(event.inputContext())) {
                if (*config_.action == PrefixAction::SwitchIM) {
                    // Switch to the first input method (keyboard layout).
                    // This acts on the last focused input context, which is
                    // always where the key event came from -- the same path
                    // as manually toggling to English. Later tmux command
                    // keys then flow through the plain keyboard naturally.
                    instance_->deactivate();
                } else {
                    armed_ = true;
                    lastKey_ = std::chrono::steady_clock::now();
                }
            }
            return;
        }

        if (*config_.action != PrefixAction::PassThrough) {
            return;
        }
        if (!armed_) {
            return;
        }
        // Inside the window: key releases and bare modifier keys do not
        // consume the window; hand them back to the normal pipeline.
        if (!press || key.isModifier()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - lastKey_ > window_) {
            armed_ = false;
            return;
        }
        lastKey_ = now;
        // Filter without accept: the IM engine never sees this key, and
        // the frontend sends the original key back to the application
        // (waylandim sendKeyToVK / dbus returns "not handled"). This is
        // real pass-through, not simulated input.
        event.filter();
    }

    // When Programs is non-empty, only react if the input context's
    // program name (wayland appId, e.g. foot/kitty) is in the list.
    // An empty list means "react in all programs".
    bool allowedProgram(InputContext *ic) const {
        const auto &programs = config_.programs.value();
        if (programs.empty()) {
            return true;
        }
        if (!ic) {
            return false;
        }
        const auto &program = ic->program();
        return std::find(programs.begin(), programs.end(), program) !=
               programs.end();
    }

    bool matchesPrefix(const Key &key) const {
        for (const auto &cfg : config_.prefixKeys.value()) {
            if (normalizeSym(key.sym()) == normalizeSym(cfg.sym()) &&
                sameModifiers(key.states(), cfg.states())) {
                return true;
            }
        }
        return false;
    }

    // xkb may report an uppercase keysym while Ctrl is held (e.g.
    // Control+Q instead of Control+q); normalize to lowercase before
    // matching.
    static KeySym normalizeSym(KeySym sym) {
        if (sym >= FcitxKey_A && sym <= FcitxKey_Z) {
            return static_cast<KeySym>(sym - FcitxKey_A + FcitxKey_a);
        }
        return sym;
    }

    // Compare only the primary modifiers; ignore NumLock/CapsLock/Repeat
    // and other state bits.
    static bool sameModifiers(KeyStates actual, KeyStates wanted) {
        constexpr auto mods = KeyStates{KeyState::Ctrl, KeyState::Alt,
                                        KeyState::Shift, KeyState::Super};
        return ((actual & mods) == (wanted & mods));
    }

    Instance *instance_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> handler_;
    TmuxPassConfig config_;
    std::chrono::milliseconds window_{600};
    bool armed_ = false;
    std::chrono::steady_clock::time_point lastKey_{};
};

class TmuxPassFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new TmuxPass(manager->instance());
    }
};
} // namespace fcitx

FCITX_ADDON_FACTORY_V2(tmuxpass, fcitx::TmuxPassFactory);
