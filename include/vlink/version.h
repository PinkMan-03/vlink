/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/vlink
 *  _    __   __      _           __
 * | |  / /  / /     (_) ____    / /__
 * | | / /  / /     / / / __ \  / //_/
 * | |/ /  / /___  / / / / / / / ,<
 * |___/  /_____/ /_/ /_/ /_/ /_/|_|
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file version.h
 * @brief VLink library version constants, build-time feature flags, and version comparison macros.
 *
 * @details
 * This header exposes the VLink library version as three numeric constants
 * (@c VLINK_VERSION_MAJOR, @c VLINK_VERSION_MINOR, @c VLINK_VERSION_PATCH) and as a
 * dot-separated string (@c VLINK_VERSION).  It also defines compile-time feature
 * flags that control which optional subsystems are compiled into the library.
 *
 * When the CMake-generated @c version_config.h is present it takes precedence;
 * the fallback definitions below are used in header-only or out-of-tree builds.
 *
 * @par Version Comparison
 * Use @c VLINK_VERSION_CHECK together with @c VLINK_VERSION_VALUE to guard
 * version-dependent code at compile time:
 * @code
 * #if VLINK_VERSION_VALUE >= VLINK_VERSION_CHECK(2, 0, 0)
 *   // safe to use VLink 2.0.0 APIs
 * #endif
 * @endcode
 */

#pragma once

#if __has_include("./vlink/version_config.h")

#include "./vlink/version_config.h"

#else

#define VLINK_VERSION_MAJOR 2
#define VLINK_VERSION_MINOR 0
#define VLINK_VERSION_PATCH 0
#define VLINK_VERSION "2.0.0"
#define VLINK_VERSION_TIMESTAMP ""
#define VLINK_VERSION_TAG ""
#define VLINK_VERSION_COMMIT_ID ""

#ifndef VLINK_ENABLE_CXX_STD_20
#if __cplusplus >= 202002L
#define VLINK_ENABLE_CXX_STD_20
#endif
#endif

#ifndef VLINK_ENABLE_C_API
#define VLINK_ENABLE_C_API
#endif

#ifndef VLINK_ENABLE_SECURITY
#define VLINK_ENABLE_SECURITY
#endif

#ifndef VLINK_ENABLE_ZSTD
// #define VLINK_ENABLE_ZSTD
#endif

#ifndef VLINK_ENABLE_SQLITE
#define VLINK_ENABLE_SQLITE
#endif

#ifndef VLINK_ENABLE_CLI_INFO
#define VLINK_ENABLE_CLI_INFO
#endif

#ifndef VLINK_ENABLE_CLI_BAG
#define VLINK_ENABLE_CLI_BAG
#endif

#ifndef VLINK_ENABLE_CLI_BENCH
#define VLINK_ENABLE_CLI_BENCH
#endif

#ifndef VLINK_ENABLE_CLI_EPROTO
#define VLINK_ENABLE_CLI_EPROTO
#endif

#ifndef VLINK_ENABLE_CLI_EFBS
#define VLINK_ENABLE_CLI_EFBS
#endif

#ifndef VLINK_ENABLE_CLI_LIST
#define VLINK_ENABLE_CLI_LIST
#endif

#ifndef VLINK_ENABLE_CLI_MONITOR
#define VLINK_ENABLE_CLI_MONITOR
#endif

#ifndef VLINK_ENABLE_CLI_DUMP
#define VLINK_ENABLE_CLI_DUMP
#endif

#ifndef VLINK_ENABLE_CLI_CHECK
#define VLINK_ENABLE_CLI_CHECK
#endif

#ifndef VLINK_ENABLE_LOG_QUI
// #define VLINK_ENABLE_LOG_QUI
#endif

#ifndef VLINK_ENABLE_LOG_SPD
// #define VLINK_ENABLE_LOG_SPD
#endif

#ifndef VLINK_ENABLE_LOG_DLT
// #define VLINK_ENABLE_LOG_DLT
#endif

#ifndef VLINK_ENABLE_LOG_NAT
#define VLINK_ENABLE_LOG_NAT
#endif

#ifndef VLINK_ENABLE_PROXY
#define VLINK_ENABLE_PROXY
#endif

#ifndef VLINK_ENABLE_VIEWER
// #define VLINK_ENABLE_VIEWER
#endif

#ifndef VLINK_ENABLE_WEBVIZ
// #define VLINK_ENABLE_WEBVIZ
#endif

#ifndef VLINK_ENABLE_COMPLETIONS
// #define VLINK_ENABLE_COMPLETIONS
#endif

#ifndef VLINK_ENABLE_EXAMPLES
// #define VLINK_ENABLE_EXAMPLES
#endif

#ifndef VLINK_ENABLE_WHOLE_EXAMPLES
// #define VLINK_ENABLE_WHOLE_EXAMPLES
#endif

#ifndef VLINK_ENABLE_TEST
#define VLINK_ENABLE_TEST
#endif

#endif

/**
 * @def VLINK_VERSION_CHECK(major, minor, patch)
 * @brief Encodes a version triple into a single 24-bit integer for comparison.
 *
 * @details
 * Packs @p major into bits 23-16, @p minor into bits 15-8, and @p patch into
 * bits 7-0.  Use this macro together with @c VLINK_VERSION_VALUE to perform
 * compile-time version checks.
 *
 * @param major  Major version component (0-255).
 * @param minor  Minor version component (0-255).
 * @param patch  Patch version component (0-255).
 * @return       Encoded 24-bit integer representation of the version triple.
 */
#define VLINK_VERSION_CHECK(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))

/**
 * @def VLINK_VERSION_VALUE
 * @brief The packed 24-bit integer value for the current VLink library version.
 *
 * @details
 * Equivalent to @c VLINK_VERSION_CHECK(VLINK_VERSION_MAJOR, VLINK_VERSION_MINOR, VLINK_VERSION_PATCH).
 * Compare this against @c VLINK_VERSION_CHECK(x, y, z) to conditionally enable
 * code paths that depend on a minimum library version.
 */
#define VLINK_VERSION_VALUE VLINK_VERSION_CHECK(VLINK_VERSION_MAJOR, VLINK_VERSION_MINOR, VLINK_VERSION_PATCH)

/*
 *                    _ooOoo_
 *                   o8888888o
 *                   88" . "88
 *                   (| -_- |)
 *                    O\ = /O
 *                ____/`---'\____
 *              .   ' \\| |// `.
 *               / \\||| : |||// \
 *             / _||||| -:- |||||- \
 *               | | \\\ - /// | |
 *             | \_| ''\---/'' | |
 *              \ .-\__ `-` ___/-. /
 *           ___`. .' /--.--\ `. . __
 *        ."" '< `.___\_<|>_/___.' >'"".
 *       | | : `- \`.;`\ _ /`;.`/ - ` : | |
 *         \ \ `-. \_ __\ /__ _/ .-` / /
 * ======`-.____`-.___\_____/___.-`____.-'======
 *                    `=---='
 */
