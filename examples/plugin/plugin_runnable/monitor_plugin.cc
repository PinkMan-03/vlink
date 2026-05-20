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

/// @file monitor_plugin.cc
/// @brief RunablePluginInterface plugin that periodically prints system info.
///
/// Build output: libmonitor_plugin.so
/// The host loads this .so and manages its lifecycle via:
///   async_run() -> on_init() -> ... -> on_deinit() -> quit() -> wait_for_quit()

#include <vlink/base/elapsed_timer.h>
#include <vlink/base/logger.h>
#include <vlink/base/timer.h>
#include <vlink/extension/runnable_plugin_interface.h>

#include <atomic>
#include <memory>

/// MonitorPlugin: a self-contained plugin with its own event loop.
///
/// on_init() creates a periodic Timer that fires every 500 ms on the plugin's
/// own MessageLoop thread.  on_deinit() stops and destroys the timer.
class MonitorPlugin : public vlink::RunablePluginInterface {
  VLINK_PLUGIN_REGISTER(vlink::RunablePluginInterface)

 public:
  void on_init() override {
    VLOG_I("[MonitorPlugin] on_init -- creating 500ms timer");
    tick_count_.store(0);

    // Create a Timer attached to *this* (the plugin's own MessageLoop).
    // It fires every 500 ms, indefinitely, until stopped.
    timer_ = std::make_unique<vlink::Timer>(this, 500, vlink::Timer::kInfinite, [this]() {
      int count = tick_count_.fetch_add(1) + 1;
      int64_t elapsed_us = uptime_.get();
      VLOG_I("[MonitorPlugin] tick #", count, "  uptime=", elapsed_us / 1000, " ms");
    });

    uptime_.start();
    timer_->start();
  }

  void on_deinit() override {
    VLOG_I("[MonitorPlugin] on_deinit -- stopping timer");

    if (timer_) {
      timer_->stop();
      timer_.reset();
    }

    int64_t total_us = uptime_.get();
    uptime_.stop();
    VLOG_I("[MonitorPlugin] total ticks=", tick_count_.load(), "  total uptime=", total_us / 1000, " ms");
  }

 private:
  std::unique_ptr<vlink::Timer> timer_;
  std::atomic<int> tick_count_{0};
  vlink::ElapsedTimer uptime_{vlink::ElapsedTimer::kCpuTimestamp, vlink::ElapsedTimer::kMicro};
};

VLINK_PLUGIN_DECLARE(MonitorPlugin, 1, 0)
