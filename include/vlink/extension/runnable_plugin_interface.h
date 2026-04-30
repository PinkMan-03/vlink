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
 * @file runnable_plugin_interface.h
 * @brief Plugin interface for self-contained, event-loop-driven plugin components.
 *
 * @details
 * @c RunablePluginInterface (note: intentional spelling) combines a @c MessageLoop
 * with the @c Plugin system to allow dynamic plugins to carry their own event loop.
 *
 * A plugin that inherits this interface runs in its own @c MessageLoop thread, started by
 * @c async_run() after loading.  The host calls @c on_init() to initialise the plugin and
 * @c on_deinit() to clean up before unloading.
 *
 * @par Plugin implementation example
 * @code
 * class MyPlugin : public vlink::RunablePluginInterface {
 * public:
 *     void on_init() override {
 *         // set up subscriptions, timers, etc.
 *     }
 *     void on_deinit() override {
 *         // stop timers, unsubscribe
 *     }
 * };
 * VLINK_PLUGIN_DECLARE(MyPlugin)
 * @endcode
 *
 * @par Host usage
 * @code
 * vlink::Plugin plugin;
 * auto instance = plugin.load<vlink::RunablePluginInterface>("my_plugin.so");
 * instance->async_run();
 * instance->on_init();
 * // ... run ...
 * instance->on_deinit();
 * @endcode
 */

#pragma once

#include "../base/message_loop.h"
#include "../base/plugin.h"

namespace vlink {

/**
 * @class RunablePluginInterface
 * @brief Abstract plugin interface that provides its own @c MessageLoop event thread.
 *
 * @details
 * The plugin owns a @c MessageLoop and implements @c on_init() / @c on_deinit() for
 * lifecycle management.  The host is responsible for calling @c async_run() after loading.
 */
class RunablePluginInterface : public MessageLoop {
  VLINK_PLUGIN_REGISTER(RunablePluginInterface)

 protected:
  RunablePluginInterface() = default;

  ~RunablePluginInterface() override = default;

 public:
  /**
   * @brief Called by the host after the plugin's event loop has started.
   *
   * @details
   * Implement this method to create subscribers, timers, and other resources that
   * require the loop to be running.  This method is called on the caller's thread.
   */
  virtual void on_init() = 0;

  /**
   * @brief Called by the host before the plugin is unloaded.
   *
   * @details
   * Implement this method to release all resources, cancel timers, and unsubscribe.
   * After this call, the host will stop the event loop and unload the plugin.
   */
  virtual void on_deinit() = 0;

 private:
  VLINK_DISALLOW_COPY_AND_ASSIGN(RunablePluginInterface)
};

}  // namespace vlink
