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
 * @file protobuf_registry.h
 * @brief Protobuf runtime include wrapper for schema-plugin support.
 *
 * @details
 * Protobuf does not need a custom VLink-side registry implementation because
 * generated message descriptors are already exposed through
 * @c google::protobuf::DescriptorPool::generated_pool().
 *
 * This header only centralizes the protobuf runtime includes and the feature
 * macro used by schema-plugin related code:
 * - @c VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF is defined when the required protobuf
 *   reflection headers are available in the current build environment.
 *
 * Keeping this probe in a dedicated header makes @c schema_plugin_base.h and
 * other schema-plugin code easier to read and keeps protobuf availability
 * checks consistent across the codebase.
 */

#pragma once

/**
 * @brief Enables protobuf-backed schema-plugin code when protobuf reflection is available.
 */
#if __has_include(<google/protobuf/dynamic_message.h>)
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>
#define VLINK_HAS_SCHEMA_PLUGIN_PROTOBUF
#endif
