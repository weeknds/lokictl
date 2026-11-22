/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

namespace model {

const char* BaseModel::app_title() const {
    return APP_DISPLAY_NAME;
}

const HomeData& BaseModel::home() const {
    return home_;
}

void BaseModel::set_home(const HomeData& data) {
    home_ = data;
}

AppPage BaseModel::current_page() const {
    return pages_[depth_ - 1];
}

std::size_t BaseModel::page_depth() const {
    return depth_;
}

void BaseModel::reset_pages(AppPage first) {
    pages_[0] = first;
    depth_ = 1;
}

bool BaseModel::open_page(AppPage page) {
    if (page == current_page()) {
        return false;
    }

    // Already further down: going "forward" to it is the same as going back
    // to it, so the path never holds a page twice.
    for (std::size_t index = 0; index + 1 < depth_; ++index) {
        if (pages_[index] == page) {
            depth_ = index + 1;
            return true;
        }
    }

    if (depth_ == kMaxPageDepth) {
        return false;
    }

    pages_[depth_] = page;
    ++depth_;
    return true;
}

bool BaseModel::go_back() {
    if (depth_ <= 1) {
        return false;
    }

    --depth_;
    return true;
}

} // namespace model
