/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dev_overlays_screen.h"

#include "apple_screen.h"
#include "theme.h"
#include "theme_engine.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

namespace screen {
namespace {

struct ToastSample {
    const char* prefix;
    const char* highlighted;
    const char* suffix;
    view::widgets::ToastTone tone;
};

// Messages like the app's own, one per tone, from the top of the page down.
// The Default one is the message shown after a theme switch; it names the
// active theme.
constexpr ToastSample kToastSamples[] = {
    {"Saved to ", "screenshot-0001.png", "", view::widgets::ToastTone::Success},
    {"Hold ", "ESC", " to exit", view::widgets::ToastTone::Warning},
    {"Theme: ", nullptr, "", view::widgets::ToastTone::Default},
};

// Distance of each toast's bottom edge from the bottom of the screen. Toasts
// are at least 36 px tall, so these leave a gap of about 14 px between them.
constexpr int32_t kToastBottomOffsets[] = {116, 66, 16};

static_assert(std::size(kToastSamples) == std::size(kToastBottomOffsets),
              "one bottom offset per toast sample");
static_assert(std::size(kToastSamples) == DevOverlaysScreen::kToastCount, "one toast per sample");

} // namespace

DevOverlaysScreen::DevOverlaysScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     view::ThemeEngine& theme_engine,
                                     model::AppPage page)
    : BaseScreen(view_model, assets, theme_engine), page_(page) {
    // build_content() runs inside init() and needs page_, so init() is
    // called here rather than in the base class.
    init(Layout::FullScreen);
}

DevOverlaysScreen::~DevOverlaysScreen() {
    help_popup_.reset();
    for (auto& toast : toasts_) {
        toast.reset();
    }
}

bool DevOverlaysScreen::handles(model::AppPage page) {
    return page == model::AppPage::DevHelp || page == model::AppPage::DevToast;
}

void DevOverlaysScreen::build_content(lv_obj_t* content) {
    if (page_ == model::AppPage::DevToast) {
        build_toasts(content);
        return;
    }
    build_help(content);
}

void DevOverlaysScreen::build_help(lv_obj_t* content) {
    help_popup_ = std::make_unique<view::widgets::HelpPopup>(content, theme_engine());
    help_popup_->build();
    help_popup_->show(AppleScreen::help_content());
}

void DevOverlaysScreen::build_toasts(lv_obj_t* content) {
    for (std::size_t index = 0; index < toasts_.size(); ++index) {
        const auto& sample = kToastSamples[index];
        view::widgets::ToastConfig config;
        config.bottom_offset = kToastBottomOffsets[index];
        config.duration_ms = 0;

        auto toast = std::make_unique<view::widgets::Toast>(content, theme_engine(), config);
        toast->build();
        const char* highlighted = sample.highlighted ? sample.highlighted : theme().display_name;
        toast->show_persistent_highlighted(sample.prefix, highlighted, sample.suffix, sample.tone);
        toasts_[index] = std::move(toast);
    }
}

} // namespace screen
