// The Ada runtime that every translated program uses.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_RUNTIME_H
#define MPDCC_RUNTIME_H

#include <string_view>

// Return the spec of Mpd_Runtime, the Ada package each translated program uses.
std::string_view runtime_spec();

// Return the body of Mpd_Runtime.
std::string_view runtime_body();

#endif
