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

#pragma once

#define VLINK_PROXY_ENABLE_FILTER 1

#define VLINK_PROXY_ENABLE_ZEROCOPY_DATA 1

#define VLINK_PROXY_SOCKET_BUF_STR "8388608"

#define VLINK_PROXY_SOCKET_MTU_STR "65500"

#define VLINK_PROXY_DATA_URL_CTX "://proxy/proxy_data/v3?qos=better&domain="
#define VLINK_VIEWER_DATA_URL_CTX "://proxy/viewer_data/v3?qos=better&domain="

#define VLINK_PROXY_DATA_RELIABLE_URL_CTX "://proxy/proxy_data/reliable/v3?qos=large&domain="
#define VLINK_VIEWER_DATA_RELIABLE_URL_CTX "://proxy/viewer_data/reliable/v3?qos=large&domain="

#define VLINK_PROXY_TIME_URL_CTX "://proxy/time/v3?qos=clock&domain="
#define VLINK_PROXY_INFOLIST_URL_CTX "://proxy/info_list/v3?qos=poor&domain="
#define VLINK_PROXY_CONTROL_URL_CTX "://proxy/control/v3?qos=best&domain="

#define VLINK_PROXY_DATA_SHM_URL_CTX "://proxy/proxy_data/v3?domain="
#define VLINK_VIEWER_DATA_SHM_URL_CTX "://proxy/viewer_data/v3?domain="
