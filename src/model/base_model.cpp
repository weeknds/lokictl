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

const char* BaseModel::greeting() const {
    return "Hello World";
}

AppPage BaseModel::current_page() const {
    return current_page_;
}


void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

void BaseModel::toggle_page() {
    current_page_ = current_page_ == AppPage::Apple ? AppPage::Butter : AppPage::Apple;
}

} // namespace model
