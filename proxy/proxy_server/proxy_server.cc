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

#include <vlink/external/proxy_server.h>
//
#include <vlink/base/elapsed_timer.h>
#include <vlink/base/helpers.h>
#include <vlink/base/plugin.h>
#include <vlink/base/utils.h>
#include <vlink/base/uuid.h>
#include <vlink/extension/discovery_viewer.h>
#include <vlink/extension/runnable_plugin_interface.h>
#include <vlink/version.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/proxy_data.h>
//
#include <algorithm>
#include <deque>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../proxy_macros.h"

#ifdef _WIN32
#include <Windows.h>
#undef min
#undef max
#undef GetMessage
#endif

#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif

#if defined(__ANDROID__) && __has_include("./proxy/proxy.pb.h")
#include "./proxy/proxy.pb.h"
#else
#include "./proxy.pb.h"
#endif

namespace vlink {

[[maybe_unused]] static constexpr int kCounterCache{2};
[[maybe_unused]] static constexpr int kCounterWeight{2};
[[maybe_unused]] static constexpr int kCollectInterval{1000};

[[maybe_unused]] static constexpr size_t kMaxTaskSize{100000U};
[[maybe_unused]] static constexpr size_t kMaxTaskElapsed{10000U};

using RawPub = Publisher<Bytes>;
using RawSub = Subscriber<Bytes>;

#if VLINK_PROXY_ENABLE_ZEROCOPY_DATA
using DataPub = Publisher<zerocopy::ProxyData>;
using DataSub = Subscriber<zerocopy::ProxyData>;
#else
using DataPub = Publisher<pb::proxy::Data>;
using DataSub = Subscriber<pb::proxy::Data>;
#endif

using TimePub = SecurityPublisher<pb::proxy::Time>;
using InfoPub = SecurityPublisher<pb::proxy::InfoList>;
using ControlSub = SecuritySubscriber<pb::proxy::Control>;

#if VLINK_PROXY_ENABLE_HANDSHAKE
using HandshakeSrv = SecurityServer<pb::proxy::HandshakeReq, pb::proxy::HandshakeResp>;
#endif

struct ProxySubMeta final {
  std::string ser;
  SchemaType schema{SchemaType::kUnknown};
};

struct ProxySubEntry final {
  std::shared_ptr<RawSub> node;
  std::string ser;
  SchemaType schema{SchemaType::kUnknown};
};

// ProxyServerGlobal
struct ProxyServerGlobal final {
  std::atomic_bool has_init{false};
  std::atomic_bool has_quit{false};

  static ProxyServerGlobal& get() {
    static ProxyServerGlobal instance;
    return instance;
  }

 private:
  ProxyServerGlobal() = default;
};

// ProxyServer::Impl
struct ProxyServer::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
  std::atomic<uint32_t> control_id{0};
  std::atomic<pb::proxy::Mode> mode{pb::proxy::OFFLINE};

  std::string current_host_name;
  std::string current_machine_id;
  std::string token;

  size_t real_max_packet_size{0};

  ProxyServer::Config config;

  std::shared_ptr<DiscoveryViewer> discovery_viewer;
  std::shared_ptr<DataPub> data_pub;
  std::shared_ptr<DataSub> data_sub;
  std::shared_ptr<TimePub> time_pub;
  std::shared_ptr<InfoPub> info_pub;
  std::shared_ptr<ControlSub> control_sub;
#if VLINK_PROXY_ENABLE_HANDSHAKE
  std::shared_ptr<HandshakeSrv> handshake_srv;
#endif

  bool filter_by_process{false};
  std::vector<std::string> filter_list;
  uint32_t filter_type{0};

  std::unordered_map<std::string, std::shared_ptr<RawPub>> pub_ptr_map;
  std::unordered_map<std::string, ProxySubEntry> sub_ptr_map;
  std::unordered_map<std::string, std::atomic<int64_t>> sub_total_seq_map;
  std::unordered_map<std::string, std::atomic<int64_t>> sub_seq_map;
  std::unordered_map<std::string, std::atomic<size_t>> sub_size_map;
  std::unordered_map<std::string, std::atomic<double>> sub_lost_map;
  std::unordered_map<std::string, std::atomic<int64_t>> sub_lat_map;
  std::unordered_map<std::string, ElapsedTimer> sub_elapsed_map;
  std::unordered_map<std::string, std::deque<int64_t>> sub_seq_buffer_map;
  std::unordered_map<std::string, std::deque<size_t>> sub_size_buffer_map;
  std::unordered_map<std::string, std::deque<double>> sub_lost_buffer_map;
  std::unordered_map<std::string, std::deque<int64_t>> sub_lat_buffer_map;
  std::unordered_map<std::string, SampleLostInfo> sub_last_sample_map;
  std::unordered_set<std::string> sub_error_url_set;
  std::unordered_set<std::string> pub_error_url_set;
  std::unordered_set<std::string> sub_urls;
  std::unordered_map<std::string, ProxySubMeta> requested_sub_meta_map;
  std::shared_mutex control_mtx;
  std::shared_mutex pubs_mtx;
  std::mutex subs_mtx;
  ElapsedTimer boot_elapsed{ElapsedTimer::kMicro};
  ElapsedTimer main_elapsed{ElapsedTimer::kMicro};

  Timer time_timer;
  Timer info_timer;

  Plugin runnable_plugin;
  std::vector<std::shared_ptr<RunablePluginInterface>> runnable_interface_list;

  bool has_intra_bind{false};
};

// ProxyServer
ProxyServer::ProxyServer(const Config& config) : impl_(std::make_unique<Impl>()) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  set_name("ProxyServer");

  impl_->config = config;

  impl_->current_host_name = Utils::get_host_name();
  impl_->current_machine_id = Utils::get_machine_id();

  impl_->real_max_packet_size = impl_->config.max_packet_size * 1024L * 1024L;

  if VUNLIKELY (ProxyServerGlobal::get().has_init) {
    VLOG_F("ProxyServer: Already initialized.");
    return;
  }

#if VLINK_PROXY_ENABLE_HANDSHAKE
  impl_->token = Uuid::random_hex();
  // CLOG_I("ProxyServer: Auth token issued (prefix=%.8s..., length=%zu).", impl_->token.c_str(), impl_->token.size());
#endif

  // intra_bind
  static std::string intra_bind = Utils::get_env("VLINK_INTRA_BIND");

  if (!intra_bind.empty()) {
    impl_->has_intra_bind = true;
  }

  if (impl_->config.use_iox) {
    init_shm_roudi();
  }

  init_server();
  init_runnable();

  ProxyServerGlobal::get().has_init = true;
}

ProxyServer::~ProxyServer() {
  ProxyServerGlobal::get().has_quit = true;

  quit(true);
  wait_for_quit();

  impl_->runnable_interface_list.clear();

  impl_->time_timer.stop();
  impl_->info_timer.stop();

  impl_->discovery_viewer->quit(true);
  impl_->discovery_viewer->wait_for_quit();

  impl_->control_id = 0;
  impl_->mode = pb::proxy::OFFLINE;

  {
    std::lock_guard control_lock(impl_->control_mtx);
    impl_->sub_urls.clear();
    impl_->requested_sub_meta_map.clear();
  }

  {
    std::lock_guard pubs_lock(impl_->pubs_mtx);
    impl_->pub_ptr_map.clear();
  }

  {
    std::lock_guard subs_lock(impl_->subs_mtx);
    impl_->sub_ptr_map.clear();
  }

  impl_->sub_seq_map.clear();
  impl_->sub_size_map.clear();
  impl_->sub_elapsed_map.clear();
  impl_->sub_seq_buffer_map.clear();
  impl_->sub_size_buffer_map.clear();

#if VLINK_PROXY_ENABLE_HANDSHAKE
  impl_->handshake_srv.reset();
#endif

  impl_->data_sub.reset();
  impl_->data_pub.reset();
  impl_->time_pub.reset();
  impl_->info_pub.reset();
  impl_->control_sub.reset();

  impl_->discovery_viewer.reset();

  impl_->runnable_plugin.clear();
}

std::string ProxyServer::get_token() const { return impl_->token; }

size_t ProxyServer::get_max_task_count() const { return kMaxTaskSize; }

uint32_t ProxyServer::get_max_elapsed_time() const { return kMaxTaskElapsed; }

void ProxyServer::on_begin() {
  for (const auto& runnable : impl_->runnable_interface_list) {
    runnable->on_init();
    runnable->async_run();
  }
}

void ProxyServer::on_end() {
  for (const auto& runnable : impl_->runnable_interface_list) {
    runnable->on_deinit();
    runnable->quit();
    runnable->wait_for_quit();
  }
}

void ProxyServer::init_shm_roudi() {
#ifdef VLINK_SUPPORT_SHM

  if (impl_->config.use_iox) {
    // VLOG_I("IOX Strategy: ", iox_strategy);

    std::string shm_roudi_name = Utils::get_app_name() + "_" + Utils::get_pid_str();

    ShmConf::init_roudi(impl_->config.iox_config, impl_->config.iox_strategy, impl_->config.iox_monitoring);
    ShmConf::init_runtime(shm_roudi_name, true);
    ShmConf::global_init();
  }
#else
  VLOG_F("ProxyServer: RouDi for shm is not supported.");
#endif
}

void ProxyServer::init_server() {
  std::string domain_id_str = std::to_string(impl_->config.domain_id);

  DiscoveryViewer::FilterType filter_type = DiscoveryViewer::kFilterAvailable;

  if (impl_->config.native_mode) {
    filter_type = DiscoveryViewer::kFilterNative;
  }

  impl_->discovery_viewer = std::make_shared<DiscoveryViewer>(filter_type);

  if (impl_->config.direct) {
    impl_->data_pub = std::make_shared<DataPub>(std::string("shm") + VLINK_PROXY_DATA_SHM_URL_CTX + domain_id_str,
                                                InitType::kWithoutInit);
    impl_->data_sub = std::make_shared<DataSub>(std::string("shm") + VLINK_VIEWER_DATA_SHM_URL_CTX + domain_id_str,
                                                InitType::kWithoutInit);
  } else {
    if (impl_->config.reliable) {
      impl_->data_pub = std::make_shared<DataPub>(
          impl_->config.dds_impl + VLINK_PROXY_DATA_RELIABLE_URL_CTX + domain_id_str, InitType::kWithoutInit);
      impl_->data_sub = std::make_shared<DataSub>(
          impl_->config.dds_impl + VLINK_VIEWER_DATA_RELIABLE_URL_CTX + domain_id_str, InitType::kWithoutInit);
    } else {
      impl_->data_pub = std::make_shared<DataPub>(impl_->config.dds_impl + VLINK_PROXY_DATA_URL_CTX + domain_id_str,
                                                  InitType::kWithoutInit);
      impl_->data_sub = std::make_shared<DataSub>(impl_->config.dds_impl + VLINK_VIEWER_DATA_URL_CTX + domain_id_str,
                                                  InitType::kWithoutInit);
    }
  }

  Security::Config sec_cfg;

  if (!impl_->config.security_key.empty()) {
    sec_cfg.key = impl_->config.security_key;
  }

  impl_->time_pub = std::make_shared<TimePub>(impl_->config.dds_impl + VLINK_PROXY_TIME_URL_CTX + domain_id_str,
                                              sec_cfg, InitType::kWithoutInit);
  impl_->info_pub = std::make_shared<InfoPub>(impl_->config.dds_impl + VLINK_PROXY_INFOLIST_URL_CTX + domain_id_str,
                                              sec_cfg, InitType::kWithoutInit);
  impl_->control_sub = std::make_shared<ControlSub>(
      impl_->config.dds_impl + VLINK_PROXY_CONTROL_URL_CTX + domain_id_str, sec_cfg, InitType::kWithoutInit);

#if VLINK_PROXY_ENABLE_HANDSHAKE
  impl_->handshake_srv = std::make_shared<HandshakeSrv>(
      impl_->config.dds_impl + VLINK_PROXY_HANDSHAKE_URL_CTX + domain_id_str, sec_cfg, InitType::kWithoutInit);
#endif

  impl_->data_pub->set_discovery_enabled(false);
  impl_->data_sub->set_discovery_enabled(false);
  impl_->time_pub->set_discovery_enabled(false);
  impl_->info_pub->set_discovery_enabled(false);
  impl_->control_sub->set_discovery_enabled(false);
#if VLINK_PROXY_ENABLE_HANDSHAKE
  impl_->handshake_srv->set_discovery_enabled(false);
#endif

  if (!impl_->config.bind_ip.empty()) {
    impl_->data_pub->set_property("dds.ip", impl_->config.bind_ip);
    impl_->data_sub->set_property("dds.ip", impl_->config.bind_ip);
    impl_->time_pub->set_property("dds.ip", impl_->config.bind_ip);
    impl_->info_pub->set_property("dds.ip", impl_->config.bind_ip);
    impl_->control_sub->set_property("dds.ip", impl_->config.bind_ip);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.ip", impl_->config.bind_ip);
#endif
  }

  if (!impl_->config.peer_ip.empty()) {
    impl_->data_pub->set_property("dds.peer", impl_->config.peer_ip);
    impl_->data_sub->set_property("dds.peer", impl_->config.peer_ip);
    impl_->time_pub->set_property("dds.peer", impl_->config.peer_ip);
    impl_->info_pub->set_property("dds.peer", impl_->config.peer_ip);
    impl_->control_sub->set_property("dds.peer", impl_->config.peer_ip);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.peer", impl_->config.peer_ip);
#endif
  }

  if (impl_->config.buf_size > 0) {
    std::string buf_str = std::to_string(impl_->config.buf_size);
    impl_->data_pub->set_property("dds.buf", buf_str);
    impl_->data_sub->set_property("dds.buf", buf_str);
    impl_->time_pub->set_property("dds.buf", buf_str);
    impl_->info_pub->set_property("dds.buf", buf_str);
    impl_->control_sub->set_property("dds.buf", buf_str);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.buf", buf_str);
#endif
  } else {
    impl_->data_pub->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
    impl_->data_sub->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
    impl_->time_pub->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
    impl_->info_pub->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
    impl_->control_sub->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.buf", VLINK_PROXY_SOCKET_BUF_STR);
#endif
  }

  if (impl_->config.mtu_size > 0) {
    std::string mtu_str = std::to_string(impl_->config.mtu_size);
    impl_->data_pub->set_property("dds.mtu", mtu_str);
    impl_->data_sub->set_property("dds.mtu", mtu_str);
    impl_->time_pub->set_property("dds.mtu", mtu_str);
    impl_->info_pub->set_property("dds.mtu", mtu_str);
    impl_->control_sub->set_property("dds.mtu", mtu_str);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.mtu", mtu_str);
#endif
  } else {
    impl_->data_pub->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
    impl_->data_sub->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
    impl_->time_pub->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
    impl_->info_pub->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
    impl_->control_sub->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.mtu", VLINK_PROXY_SOCKET_MTU_STR);
#endif
  }

  if (impl_->config.enable_tcp) {
    impl_->data_pub->set_property("dds.tcp", "1");
    impl_->data_sub->set_property("dds.tcp", "1");
  } else {
    impl_->data_pub->set_property("dds.tcp", "0");
    impl_->data_sub->set_property("dds.tcp", "0");
  }

  if (impl_->config.native_mode) {
    impl_->data_pub->set_property("dds.ip", "127.0.0.1");
    impl_->data_sub->set_property("dds.ip", "127.0.0.1");
    impl_->time_pub->set_property("dds.ip", "127.0.0.1");
    impl_->info_pub->set_property("dds.ip", "127.0.0.1");
    impl_->control_sub->set_property("dds.ip", "127.0.0.1");
#if VLINK_PROXY_ENABLE_HANDSHAKE
    impl_->handshake_srv->set_property("dds.ip", "127.0.0.1");
#endif
  }

#if VLINK_PROXY_ENABLE_HANDSHAKE
  impl_->handshake_srv->init();

  impl_->handshake_srv->listen([this](const pb::proxy::HandshakeReq& req, pb::proxy::HandshakeResp& resp) {
    resp.set_hostname(impl_->current_host_name);
    resp.set_machine_id(impl_->current_machine_id);
    resp.set_version(VLINK_VERSION);

    if VUNLIKELY (!req.version().empty() && req.version() != VLINK_VERSION) {
      CLOG_E("ProxyServer: Reject handshake from %s due to version mismatch (peer=%s, self=%s).",
             req.hostname().c_str(), req.version().c_str(), VLINK_VERSION);
      resp.set_result(pb::proxy::HANDSHAKE_VERSION_MISMATCH);
      return;
    }

    resp.set_result(pb::proxy::HANDSHAKE_OK);
    resp.set_token(impl_->token);
  });
#endif

  if (!impl_->config.direct) {
    impl_->data_pub->init();
    impl_->data_sub->init();
  }

  impl_->time_pub->init();
  impl_->info_pub->init();
  impl_->control_sub->init();

  impl_->boot_elapsed.start();
  impl_->main_elapsed.start();

  impl_->time_timer.set_interval(kCollectInterval);
  impl_->time_timer.set_loop_count(Timer::kInfinite);
  impl_->time_timer.attach(impl_->discovery_viewer.get());
  impl_->time_timer.set_callback([this]() { send_time(); });
  impl_->time_timer.start();

  impl_->info_timer.set_interval(kCollectInterval);
  impl_->info_timer.set_loop_count(Timer::kInfinite);
  impl_->info_timer.attach(impl_->discovery_viewer.get());
  impl_->info_timer.set_callback([this]() { update_all(); });
  impl_->info_timer.start();

  impl_->info_pub->detect_subscribers([this](bool connected) {
    if VUNLIKELY (impl_->mode != pb::proxy::OFFLINE && !connected && !impl_->info_pub->has_subscribers()) {
      impl_->discovery_viewer->post_task([this]() {
        pb::proxy::Control control;
        control.set_control_id(impl_->control_id);
        control.set_mode(pb::proxy::OFFLINE);

        send_control(&control);
      });
    }
  });

  if (impl_->data_sub->has_inited()) {
#if VLINK_PROXY_ENABLE_ZEROCOPY_DATA
    impl_->data_sub->listen([this](const zerocopy::ProxyData& t_data) {
      if VUNLIKELY (ProxyServerGlobal::get().has_quit) {
        return;
      }

      if VUNLIKELY (t_data.control_id() != impl_->control_id) {
        return;
      }

      if VUNLIKELY (impl_->mode != pb::proxy::PLAY && impl_->mode != pb::proxy::EDIT &&
                    impl_->mode != pb::proxy::AUTO && impl_->mode != pb::proxy::AUTO_AND_OBSERVE_ALL) {
        return;
      }

      const auto schema = SchemaData::is_valid_type(static_cast<SchemaType>(t_data.schema()))
                              ? static_cast<SchemaType>(t_data.schema())
                              : SchemaType::kUnknown;
      const auto ser = std::string(t_data.ser());

      if VUNLIKELY (ser.empty() || schema == SchemaType::kUnknown) {
        return;
      }

      if (!impl_->config.direct) {
        if (impl_->config.async) {
          post_task([this, t_data, ser, schema]() {
            std::shared_lock lock(impl_->pubs_mtx);
            auto iter = impl_->pub_ptr_map.find(std::string(t_data.url()));

            if VUNLIKELY (iter == impl_->pub_ptr_map.end()) {
              return;
            }

            if VUNLIKELY (!iter->second || iter->second->get_ser_type() != ser ||
                          iter->second->get_schema_type() != schema) {
              return;
            }

            if VLIKELY (iter->second->has_subscribers()) {
              iter->second->publish(t_data.raw(), true);
            }
          });
        } else {
          std::shared_lock lock(impl_->pubs_mtx);
          auto iter = impl_->pub_ptr_map.find(std::string(t_data.url()));

          if VUNLIKELY (iter == impl_->pub_ptr_map.end()) {
            return;
          }

          if VUNLIKELY (!iter->second || iter->second->get_ser_type() != ser ||
                        iter->second->get_schema_type() != schema) {
            return;
          }

          if VLIKELY (iter->second->has_subscribers()) {
            iter->second->publish(t_data.raw(), true);
          }
        }
      }
    });
#else
    impl_->data_sub->listen([this](const pb::proxy::Data& data) {
      if VUNLIKELY (ProxyServerGlobal::get().has_quit) {
        return;
      }

      if VUNLIKELY (data.control_id() != impl_->control_id) {
        return;
      }

      if VUNLIKELY (impl_->mode != pb::proxy::PLAY && impl_->mode != pb::proxy::EDIT &&
                    impl_->mode != pb::proxy::AUTO && impl_->mode != pb::proxy::AUTO_AND_OBSERVE_ALL) {
        return;
      }

      const auto schema = SchemaData::is_valid_type(static_cast<SchemaType>(data.schema()))
                              ? static_cast<SchemaType>(data.schema())
                              : SchemaType::kUnknown;

      if VUNLIKELY (data.ser().empty() || schema == SchemaType::kUnknown) {
        return;
      }

      if (!impl_->config.direct) {
        if (impl_->config.async) {
          post_task([this, data, schema]() {
            std::shared_lock lock(impl_->pubs_mtx);
            auto iter = impl_->pub_ptr_map.find(data.url());

            if VUNLIKELY (iter == impl_->pub_ptr_map.end()) {
              return;
            }

            if VUNLIKELY (!iter->second || iter->second->get_ser_type() != data.ser() ||
                          iter->second->get_schema_type() != schema) {
              return;
            }

            if VLIKELY (iter->second->has_subscribers()) {
              iter->second->publish(Bytes::from_string(data.raw()), true);
            }
          });
        } else {
          std::shared_lock lock(impl_->pubs_mtx);
          auto iter = impl_->pub_ptr_map.find(data.url());

          if VUNLIKELY (iter == impl_->pub_ptr_map.end()) {
            return;
          }

          if VUNLIKELY (!iter->second || iter->second->get_ser_type() != data.ser() ||
                        iter->second->get_schema_type() != schema) {
            return;
          }

          if VLIKELY (iter->second->has_subscribers()) {
            iter->second->publish(Bytes::from_string(data.raw()), true);
          }
        }
      }
    });
#endif
  }

  impl_->control_sub->listen([this](const pb::proxy::Control& control) {
    if VUNLIKELY (ProxyServerGlobal::get().has_quit) {
      return;
    }

#if VLINK_PROXY_ENABLE_HANDSHAKE
    if VUNLIKELY (control.token() != impl_->token) {
      CLOG_E("ProxyServer: Reject control with mismatched token (control_id=%u).",
             static_cast<uint32_t>(control.control_id()));
      return;
    }
#endif

    impl_->discovery_viewer->post_task([this, control]() { send_control(&control); });
  });

  impl_->discovery_viewer->async_run();
}

void ProxyServer::init_runnable() {
  for (const auto& runnable_name : impl_->config.runnable_list) {
    CLOG_I("ProxyServer: Load runnable plugin [%s].", runnable_name.c_str());
    auto runnable = impl_->runnable_plugin.load<RunablePluginInterface>(impl_->config.runnable_prefix + runnable_name,
                                                                        impl_->config.runnable_version_major,
                                                                        impl_->config.runnable_version_minor);

    if (runnable) {
      auto plugin_complex_id = impl_->runnable_plugin.get_plugin_complex_id<RunablePluginInterface>(runnable_name);
      runnable->set_name(plugin_complex_id);
      impl_->runnable_interface_list.emplace_back(std::move(runnable));
    }
  }
}

void ProxyServer::send_time() {
  if VUNLIKELY (!impl_->time_pub->has_subscribers()) {
    return;
  }

  pb::proxy::Time time;
  time.set_control_id(impl_->control_id);
  time.set_mode(impl_->mode);

  time.set_reliable_mode(impl_->config.reliable);
  time.set_tcp_mode(impl_->config.enable_tcp);
  time.set_direct_mode(impl_->config.direct);
  time.set_version(VLINK_VERSION);
  time.set_hostname(impl_->current_host_name);
  time.set_machine_id(impl_->current_machine_id);
#if VLINK_PROXY_ENABLE_HANDSHAKE
  time.set_token(impl_->token);
#endif

  time.set_cpu_usage(Utils::get_cpu_usage());
  time.set_memory_usage(Utils::get_memory_usage());

  time.set_sys_time(ElapsedTimer::get_sys_timestamp(ElapsedTimer::kMicro, false));
  time.set_boot_time(static_cast<uint64_t>(impl_->boot_elapsed.get()));

  impl_->time_pub->publish(time, true);
}

void ProxyServer::send_control(const void* control_data) {
  auto& control = *static_cast<pb::proxy::Control*>(const_cast<void*>(control_data));
  const auto next_mode = control.mode();

  switch (next_mode) {
    case pb::proxy::OFFLINE:
    case pb::proxy::OBSERVE_ONE:
    case pb::proxy::OBSERVE_ALL:
    case pb::proxy::RECORD:
    case pb::proxy::PLAY:
    case pb::proxy::EDIT:
    case pb::proxy::AUTO:
    case pb::proxy::AUTO_AND_OBSERVE_ALL:
      break;
    default:
      CLOG_E("ProxyServer: Unsupported control mode %d.", static_cast<int>(next_mode));
      return;
  }

  std::unordered_set<std::string> next_sub_urls;
  std::unordered_map<std::string, ProxySubMeta> next_sub_meta_map;
  std::unordered_map<std::string, ProxySubMeta> next_pub_meta_map;

  if VUNLIKELY (next_mode != pb::proxy::OFFLINE) {
    next_sub_urls.reserve(control.sub_url_list().size());
    next_sub_meta_map.reserve(control.sub_url_list().size());
    next_pub_meta_map.reserve(control.pub_url_list().size());

    for (int i = 0; i < control.sub_url_list().size(); ++i) {
      const auto& url = control.sub_url_list()[i];

      if (url.empty()) {
        continue;
      }

      next_sub_urls.emplace(url);

      if (i >= control.sub_ser_list().size() || i >= control.sub_schema_list().size()) {
        continue;
      }

      const auto& ser = control.sub_ser_list()[i];
      const auto schema = SchemaData::is_valid_type(static_cast<SchemaType>(control.sub_schema_list()[i]))
                              ? static_cast<SchemaType>(control.sub_schema_list()[i])
                              : SchemaType::kUnknown;

      if (ser.empty() || schema == SchemaType::kUnknown) {
        continue;
      }

      next_sub_meta_map.try_emplace(url, ProxySubMeta{ser, schema});
    }

    for (int i = 0; i < control.pub_url_list().size(); ++i) {
      const auto& url = control.pub_url_list()[i];

      if (url.empty()) {
        continue;
      }

      if (i >= control.pub_ser_list().size() || i >= control.pub_schema_list().size()) {
        continue;
      }

      const auto& ser = control.pub_ser_list()[i];
      const auto schema = SchemaData::is_valid_type(static_cast<SchemaType>(control.pub_schema_list()[i]))
                              ? static_cast<SchemaType>(control.pub_schema_list()[i])
                              : SchemaType::kUnknown;

      if (ser.empty() || schema == SchemaType::kUnknown) {
        continue;
      }

      next_pub_meta_map.try_emplace(url, ProxySubMeta{ser, schema});
    }
  }

  pb::proxy::Mode last_mode = impl_->mode;
  std::unordered_set<std::string> last_sub_urls;
  std::unordered_map<std::string, ProxySubMeta> last_sub_meta_map;

  {
    std::shared_lock control_lock(impl_->control_mtx);
    last_sub_urls = impl_->sub_urls;
    last_sub_meta_map = impl_->requested_sub_meta_map;
  }

  impl_->control_id = control.control_id();
  impl_->mode = next_mode;

  bool to_update = next_mode != last_mode || next_mode == pb::proxy::RECORD;

  bool sub_meta_changed = next_sub_meta_map.size() != last_sub_meta_map.size();

  if (!sub_meta_changed) {
    for (const auto& [url, meta] : next_sub_meta_map) {
      auto meta_iter = last_sub_meta_map.find(url);

      if (meta_iter == last_sub_meta_map.end() || meta_iter->second.ser != meta.ser ||
          meta_iter->second.schema != meta.schema) {
        sub_meta_changed = true;
        break;
      }
    }
  }

  if VUNLIKELY (next_mode == pb::proxy::OFFLINE) {
    impl_->control_id = 0;

    {
      std::lock_guard control_lock(impl_->control_mtx);
      impl_->sub_urls.clear();
      impl_->requested_sub_meta_map.clear();
    }

    {
      std::lock_guard pubs_lock(impl_->pubs_mtx);
      impl_->pub_ptr_map.clear();
    }

    {
      std::lock_guard subs_lock(impl_->subs_mtx);
      impl_->sub_ptr_map.clear();
    }

    return;
  }

  impl_->discovery_viewer->post_task([this]() { send_time(); });
  impl_->time_timer.restart();

  {
    std::lock_guard control_lock(impl_->control_mtx);
    impl_->sub_urls = next_sub_urls;
    impl_->requested_sub_meta_map = next_sub_meta_map;
    impl_->filter_by_process = control.filter_by_process();
    impl_->filter_list = Helpers::get_split_string(control.filter_str(), ' ');
    impl_->filter_type = control.filter_type();
  }

  if (next_mode == pb::proxy::RECORD || next_mode == pb::proxy::OBSERVE_ONE || next_mode == pb::proxy::OBSERVE_ALL) {
    {
      std::lock_guard lock(impl_->pubs_mtx);
      impl_->pub_ptr_map.clear();
    }

    if (next_mode == pb::proxy::OBSERVE_ONE) {
      if (last_mode == pb::proxy::OBSERVE_ONE) {
        if (next_sub_urls.empty() || last_sub_urls.empty()) {
          to_update = true;
        } else if (last_sub_urls.size() == 1 && next_sub_urls.size() == 1) {
          if (*last_sub_urls.begin() != *next_sub_urls.begin()) {
            to_update = true;
          }
        } else {
          to_update = true;
        }
      }
    }

    if (sub_meta_changed) {
      to_update = true;
    }
  } else if (next_mode == pb::proxy::PLAY || next_mode == pb::proxy::EDIT || next_mode == pb::proxy::AUTO ||
             next_mode == pb::proxy::AUTO_AND_OBSERVE_ALL) {
    if (!impl_->config.direct) {
      {
        std::lock_guard lock(impl_->pubs_mtx);
        for (auto pub_iter = impl_->pub_ptr_map.begin(); pub_iter != impl_->pub_ptr_map.end();) {
          if (next_pub_meta_map.find(pub_iter->first) == next_pub_meta_map.end()) {
            pub_iter = impl_->pub_ptr_map.erase(pub_iter);
          } else {
            ++pub_iter;
          }
        }

        for (const auto& [url, meta] : next_pub_meta_map) {
          if (impl_->pub_error_url_set.count(url) != 0) {
            continue;
          }

          auto pub_iter = impl_->pub_ptr_map.find(url);

          if (pub_iter != impl_->pub_ptr_map.end()) {
            auto* pub = pub_iter->second.get();

            if (pub && pub->get_ser_type() == meta.ser && pub->get_schema_type() == meta.schema) {
              continue;
            }

            impl_->pub_ptr_map.erase(pub_iter);
          }

          try {
            auto pub = std::make_shared<RawPub>(url, InitType::kWithoutInit);

            if (impl_->config.native_mode) {
              pub->set_property("dds.ip", "127.0.0.1");
            }

            pub->set_ser_type(meta.ser, meta.schema);
            pub->set_discovery_enabled(true);
            pub->init();

            impl_->pub_ptr_map.emplace(url, std::move(pub));
          } catch (Exception::RuntimeError&) {
            impl_->pub_error_url_set.emplace(url);
            continue;
          }
        }
      }

      if (next_mode == pb::proxy::PLAY || next_mode == pb::proxy::EDIT) {
        std::lock_guard subs_lock(impl_->subs_mtx);
        impl_->sub_ptr_map.clear();
      }
    }
  }

  if (to_update) {
    {
      std::lock_guard subs_lock(impl_->subs_mtx);
      impl_->sub_ptr_map.clear();
    }

    impl_->main_elapsed.restart();
    impl_->discovery_viewer->post_task([this]() { update_all(); });
    impl_->info_timer.restart();
  }
}

void ProxyServer::update_all() {
  if VUNLIKELY (!impl_->info_pub->has_subscribers()) {
    // control_id = 0;

    // mode = pb::proxy::OFFLINE;

    {
      std::lock_guard pubs_lock(impl_->pubs_mtx);
      impl_->pub_ptr_map.clear();
    }

    {
      std::lock_guard subs_lock(impl_->subs_mtx);
      impl_->sub_ptr_map.clear();
    }

    {
      std::lock_guard control_lock(impl_->control_mtx);
      impl_->sub_urls.clear();
      impl_->requested_sub_meta_map.clear();
    }

    return;
  }

  if (impl_->mode != pb::proxy::OBSERVE_ONE && impl_->mode != pb::proxy::OBSERVE_ALL &&
      impl_->mode != pb::proxy::RECORD && impl_->mode != pb::proxy::AUTO &&
      impl_->mode != pb::proxy::AUTO_AND_OBSERVE_ALL) {
    return;
  }

  pb::proxy::InfoList list;

  const auto& info_list = impl_->discovery_viewer->get_info_list();

  {
    std::unordered_set<std::string> current_urls;

    current_urls.reserve(info_list.size());

    for (const auto& info : info_list) {
      current_urls.emplace(info.url);
    }

    std::lock_guard subs_lock(impl_->subs_mtx);

    for (auto iter = impl_->sub_seq_buffer_map.begin(); iter != impl_->sub_seq_buffer_map.end();) {
      if (current_urls.count(iter->first) == 0) {
        std::atomic<int64_t>& seq = impl_->sub_seq_map[iter->first];
        std::atomic<size_t>& size = impl_->sub_size_map[iter->first];
        std::atomic<double>& lost = impl_->sub_lost_map[iter->first];
        std::atomic<int64_t>& lat = impl_->sub_lat_map[iter->first];
        ElapsedTimer& elapsed = impl_->sub_elapsed_map[iter->first];

        seq = 0;
        size = 0;
        lost = 0;
        lat = 0;

        elapsed.stop();

        impl_->sub_size_buffer_map.erase(iter->first);
        impl_->sub_lost_buffer_map.erase(iter->first);
        impl_->sub_lat_buffer_map.erase(iter->first);

        impl_->sub_ptr_map.erase(iter->first);

        iter = impl_->sub_seq_buffer_map.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  for (const auto& info : info_list) {
    const auto discovered_schema =
        SchemaData::is_valid_type(info.schema_type) ? info.schema_type : SchemaType::kUnknown;
    ProxySubMeta stream_meta;
    bool has_stream_meta = false;

#if VLINK_PROXY_ENABLE_FILTER

    if (impl_->mode == pb::proxy::OBSERVE_ONE || impl_->mode == pb::proxy::OBSERVE_ALL ||
        impl_->mode == pb::proxy::AUTO || impl_->mode == pb::proxy::AUTO_AND_OBSERVE_ALL) {
      if (!impl_->filter_list.empty()) {
        bool contains = false;

        if (impl_->filter_by_process) {
          for (const auto& process : info.process_list) {
            std::string left_str = process.name;
            std::transform(left_str.begin(), left_str.end(), left_str.begin(), [](char& c) { return std::tolower(c); });
            for (const auto& filter : impl_->filter_list) {
              if (filter.empty()) {
                continue;
              }

              std::string right_str = filter;
              std::transform(right_str.begin(), right_str.end(), right_str.begin(),
                             [](char& c) { return std::tolower(c); });

              if (left_str.find(right_str) != std::string::npos) {
                contains = true;
                break;
              }
            }

            if (contains) {
              break;
            }
          }

        } else {
          std::string left_str = info.url;
          std::transform(left_str.begin(), left_str.end(), left_str.begin(), [](char& c) { return std::tolower(c); });

          for (const auto& filter : impl_->filter_list) {
            if (filter.empty()) {
              continue;
            }

            std::string right_str = filter;
            std::transform(right_str.begin(), right_str.end(), right_str.begin(),
                           [](char& c) { return std::tolower(c); });

            if (left_str.find(right_str) != std::string::npos) {
              contains = true;
              break;
            }
          }
        }

        if (!contains) {
          std::lock_guard subs_lock(impl_->subs_mtx);
          impl_->sub_ptr_map.erase(info.url);
          continue;
        }
      }

      switch (impl_->filter_type) {
        case 0:
          break;
        case 1:
          if (!(info.type & kPublisher && info.type & kSubscriber)) {
            continue;
          }

          break;
        case 2:
          if (!(info.type & kServer && info.type & kClient)) {
            continue;
          }

          break;
        case 3:
          if (!(info.type & kSetter && info.type & kGetter)) {
            continue;
          }

          break;
        case 4:
          if (!((info.type & kPublisher) || (info.type & kSubscriber))) {
            continue;
          }

          break;
        case 5:
          if (!((info.type & kServer) || (info.type & kClient))) {
            continue;
          }

          break;
        case 6:
          if (!((info.type & kSetter) || (info.type & kGetter))) {
            continue;
          }

          break;
        case 7:
          if (!(info.type & kPublisher)) {
            continue;
          }

          break;
        case 8:
          if (!(info.type & kSubscriber)) {
            continue;
          }

          break;
        case 9:
          if (!(info.type & kServer)) {
            continue;
          }

          break;
        case 10:
          if (!(info.type & kClient)) {
            continue;
          }

          break;
        case 11:
          if (!(info.type & kSetter)) {
            continue;
          }

          break;
        case 12:
          if (!(info.type & kGetter)) {
            continue;
          }

          break;
        default:
          break;
      }
    }
#endif

    auto* ptr = list.add_info_list();
    ptr->set_type(info.type);
    ptr->set_url(info.url);
    ptr->set_ser(info.ser_type);
    ptr->set_schema(static_cast<uint32_t>(discovered_schema));
    ptr->set_status(pb::proxy::Status::INVALID);
    ptr->set_freq(0);
    ptr->set_rate(0);

    for (const auto& process : info.process_list) {
      auto* process_ptr = ptr->add_process_list();
      process_ptr->set_type(process.type);
      process_ptr->set_host(process.host);
      process_ptr->set_pid(process.pid);
      process_ptr->set_name(process.name);
      process_ptr->set_ip(process.ip);
    }

    if ((!(info.type & kPublisher) && !(info.type & kSetter)) ||
        (!impl_->has_intra_bind && impl_->runnable_interface_list.empty() && Url::is_intra_type(info.url))) {
      ptr->set_status(pb::proxy::Status::INVALID);
      ptr->set_freq(0);
      ptr->set_rate(0);
      continue;
    }

    std::atomic<int64_t>& total_seq = impl_->sub_total_seq_map[info.url];
    std::atomic<int64_t>& seq = impl_->sub_seq_map[info.url];
    std::atomic<size_t>& size = impl_->sub_size_map[info.url];
    std::atomic<double>& lost = impl_->sub_lost_map[info.url];
    std::atomic<int64_t>& lat = impl_->sub_lat_map[info.url];
    ElapsedTimer& elapsed = impl_->sub_elapsed_map[info.url];
    std::deque<int64_t>& seq_buffer = impl_->sub_seq_buffer_map[info.url];
    std::deque<size_t>& size_buffer = impl_->sub_size_buffer_map[info.url];
    std::deque<double>& lost_buffer = impl_->sub_lost_buffer_map[info.url];
    std::deque<int64_t>& lat_buffer = impl_->sub_lat_buffer_map[info.url];

    if VUNLIKELY (!elapsed.is_active()) {
      elapsed.start();
    }

    if (seq > 0 && seq_buffer.size() >= kCounterCache && size_buffer.size() >= kCounterCache) {
      ptr->set_status(pb::proxy::Status::ACTIVE);
    } else {
      if (elapsed.get() >= kCollectInterval * kCounterCache) {
        seq = 0;
        size = 0;
        lost = 0;
        lat = 0;
        seq_buffer.clear();
        size_buffer.clear();
        lost_buffer.clear();
        lat_buffer.clear();
        ptr->set_status(pb::proxy::Status::INACTIVE);
      } else {
        ptr->set_status(pb::proxy::Status::PENDING);
      }
    }

    std::unique_lock subs_lock(impl_->subs_mtx);

    if VUNLIKELY (impl_->sub_error_url_set.count(info.url) != 0) {
      continue;
    }

    auto ptr_iter = impl_->sub_ptr_map.find(info.url);

    bool create = false;

    if (impl_->mode == pb::proxy::OBSERVE_ALL || impl_->mode == pb::proxy::AUTO_AND_OBSERVE_ALL) {
      create = (ptr_iter == impl_->sub_ptr_map.end());
      stream_meta = ProxySubMeta{info.ser_type, discovered_schema};
      has_stream_meta = !stream_meta.ser.empty() && stream_meta.schema != SchemaType::kUnknown;
    } else if (impl_->mode == pb::proxy::OBSERVE_ONE || impl_->mode == pb::proxy::RECORD ||
               impl_->mode == pb::proxy::AUTO) {
      std::shared_lock control_lock(impl_->control_mtx);
      if (impl_->sub_urls.count(info.url) == 0) {
        create = false;
        seq = 0;
        lost = 0;
        size = 0;
        lat = 0;
        seq_buffer.clear();
        size_buffer.clear();
        lost_buffer.clear();
        lat_buffer.clear();
      } else {
        create = ptr_iter == impl_->sub_ptr_map.end();
        auto meta_iter = impl_->requested_sub_meta_map.find(info.url);

        if (meta_iter != impl_->requested_sub_meta_map.end()) {
          stream_meta = meta_iter->second;
          has_stream_meta = !stream_meta.ser.empty() && stream_meta.schema != SchemaType::kUnknown &&
                            !info.ser_type.empty() && discovered_schema != SchemaType::kUnknown &&
                            info.ser_type == stream_meta.ser && discovered_schema == stream_meta.schema;
        } else {
          has_stream_meta = false;
        }
      }
    }

    if VUNLIKELY (!has_stream_meta) {
      if (ptr_iter != impl_->sub_ptr_map.end()) {
        impl_->sub_ptr_map.erase(ptr_iter);
      }

      seq = 0;
      lost = 0;
      size = 0;
      lat = 0;
      seq_buffer.clear();
      size_buffer.clear();
      lost_buffer.clear();
      lat_buffer.clear();
      continue;
    }

    if (!create && ptr_iter != impl_->sub_ptr_map.end() &&
        (ptr_iter->second.ser != stream_meta.ser || ptr_iter->second.schema != stream_meta.schema)) {
      impl_->sub_ptr_map.erase(ptr_iter);
      ptr_iter = impl_->sub_ptr_map.end();
      create = true;
    }

    if (create) {
      subs_lock.unlock();

      std::shared_ptr<RawSub> sub;
      const auto& current_meta = stream_meta;

      try {
        sub = std::make_shared<RawSub>(info.url, InitType::kWithoutInit);

        // A setter is the field data source; the proxy must observe it as a getter.

        if (info.type & kSetter) {
          sub->mark_as_getter();
        }

        sub->set_latency_and_lost_enabled(true);

        if (impl_->config.native_mode) {
          sub->set_property("dds.ip", "127.0.0.1");
        }

        sub->set_discovery_enabled(false);
        sub->set_ser_type(current_meta.ser, current_meta.schema);
        sub->init();

        total_seq = 0;

        sub->listen([this, sub_ptr = sub.get(), url = info.url, ser = current_meta.ser, schema = current_meta.schema,
                     &total_seq, &seq, &size, &lat, &elapsed](const Bytes& bytes) {
          if VUNLIKELY (ProxyServerGlobal::get().has_quit || impl_->discovery_viewer->is_ready_to_quit()) {
            return;
          }

          ++seq;
          size += bytes.size();
          lat += sub_ptr->get_latency();
          elapsed.restart();

          if (!impl_->config.direct) {
            if VUNLIKELY (bytes.size() > impl_->real_max_packet_size) {  // LIMIT SIZE
              return;
            }

            {
              std::shared_lock control_lock(impl_->control_mtx);

              if VUNLIKELY (!impl_->data_pub->has_subscribers()) {
                return;
              }

              if (impl_->mode != pb::proxy::AUTO_AND_OBSERVE_ALL && impl_->sub_urls.count(url) == 0) {
                return;
              }
            }

            const auto current_seq = total_seq.load();

            if (impl_->config.async) {
              Bytes queued_bytes = bytes;

              // NOLINTNEXTLINE(readability-container-size-empty)
              if VUNLIKELY (queued_bytes.size() != bytes.size() || (queued_bytes.size() > 0 && !queued_bytes.data())) {
                VLOG_E("ProxyServer: Failed to create an owned copy for async forwarding.");
                return;
              }

              auto forward_task = [this, control_id = impl_->control_id.load(), mode = impl_->mode.load(),
                                   elapsed = impl_->main_elapsed.get(), seq = current_seq, url, ser, schema,
                                   queued_bytes = std::move(queued_bytes)]() {
#if VLINK_PROXY_ENABLE_ZEROCOPY_DATA
                zerocopy::ProxyData t_data;
                t_data.set_control_id(control_id);
                t_data.set_mode(mode);
                t_data.set_timestamp(elapsed);
                t_data.set_seq(seq);

                t_data.create(queued_bytes, url, ser, static_cast<uint32_t>(schema), impl_->current_host_name);

                impl_->data_pub->publish(t_data, true);
#else
                pb::proxy::Data data;
                data.set_control_id(control_id);
                data.set_mode(mode);
                data.set_timestamp(elapsed);
                data.set_url(url);
                data.set_ser(ser);
                data.set_schema(static_cast<uint32_t>(schema));
                data.set_raw(queued_bytes.to_string());
                data.set_seq(seq);
                data.set_hostname(impl_->current_host_name);

                impl_->data_pub->publish(data, true);
#endif
              };

              if VUNLIKELY (!post_task(std::move(forward_task))) {
                VLOG_E("ProxyServer: Failed to post async forwarding task.");
                return;
              }
            } else {
#if VLINK_PROXY_ENABLE_ZEROCOPY_DATA
              zerocopy::ProxyData t_data;
              t_data.set_control_id(impl_->control_id);
              t_data.set_mode(impl_->mode);
              t_data.set_timestamp(impl_->main_elapsed.get());
              t_data.set_seq(current_seq);

              t_data.create(bytes, url, ser, static_cast<uint32_t>(schema), impl_->current_host_name);

              impl_->data_pub->publish(t_data, true);
#else
              pb::proxy::Data data;
              data.set_control_id(impl_->control_id);
              data.set_mode(impl_->mode);
              data.set_timestamp(impl_->main_elapsed.get());
              data.set_url(url);
              data.set_ser(ser);
              data.set_schema(static_cast<uint32_t>(schema));
              data.set_raw(bytes.to_string());
              data.set_seq(current_seq);
              data.set_hostname(impl_->current_host_name);

              impl_->data_pub->publish(data, true);
#endif
            }
          }

          ++total_seq;
        });

        subs_lock.lock();

        impl_->sub_ptr_map.emplace(info.url, ProxySubEntry{std::move(sub), current_meta.ser, current_meta.schema});
      } catch (Exception::RuntimeError&) {
        impl_->sub_error_url_set.emplace(info.url);
        seq = 0;
        size = 0;
        lost = 0;
        lat = 0;
        seq_buffer.clear();
        size_buffer.clear();
        lost_buffer.clear();
        lat_buffer.clear();
        continue;
      }
    } else if (ptr_iter != impl_->sub_ptr_map.end()) {
      auto& last_sample = impl_->sub_last_sample_map[info.url];

      const auto& sample_info = ptr_iter->second.node->get_lost();

      int64_t total_sample = sample_info.total - last_sample.total;
      int64_t lost_sample = sample_info.lost - last_sample.lost;

      if (total_sample > 0 && lost_sample > 0) {
        lost = static_cast<double>(lost_sample) / total_sample;
      } else {
        lost = 0;
      }

      last_sample = sample_info;
    }

    subs_lock.unlock();

    if VUNLIKELY (impl_->main_elapsed.get() < 10'000) {
      ptr->set_status(pb::proxy::Status::PENDING);
      seq = 0;
      lost = 0;
      size = 0;
      lat = 0;
      seq_buffer.clear();
      size_buffer.clear();
      lost_buffer.clear();
      lat_buffer.clear();
    }

    double freq = 0;
    double rate = 0;
    double loss = 0;
    double latency = 0;
    int weight = 1;
    int total_weight = 0;

    seq_buffer.emplace_back(seq);
    while (seq_buffer.size() > kCounterCache) {
      seq_buffer.pop_front();
    }

    size_buffer.emplace_back(size);
    while (size_buffer.size() > kCounterCache) {
      size_buffer.pop_front();
    }

    lost_buffer.emplace_back(lost);
    while (lost_buffer.size() > kCounterCache) {
      lost_buffer.pop_front();
    }

    if VUNLIKELY (seq <= 0) {
      lat_buffer.emplace_back(lat);
    } else {
      lat_buffer.emplace_back(static_cast<double>(lat) / seq);
    }

    while (lat_buffer.size() > kCounterCache) {
      lat_buffer.pop_front();
    }

    if VLIKELY (seq_buffer.size() == size_buffer.size()) {
      for (size_t i = 0; i < seq_buffer.size(); ++i) {
        freq += seq_buffer[i] * weight;
        rate += size_buffer[i] * weight;
        loss += lost_buffer[i] * weight;
        latency += lat_buffer[i] * weight;
        total_weight += weight;
        weight *= kCounterWeight;
      }
    }

    if VLIKELY (total_weight > 0) {
      freq = freq / total_weight;
      rate = rate / total_weight;
      loss = loss / total_weight;
      latency = latency / total_weight;
    } else {
      freq = 0;
      rate = 0;
      loss = 0;
      latency = 0;
    }

    ptr->set_freq(freq);
    ptr->set_rate(rate);
    ptr->set_loss(loss);

    if (seq == 0) {
      ptr->set_latency(-1);
    } else if (latency > 5000'000'000 || latency < -500'000) {
      ptr->set_latency(-2);
    } else if (latency < 0) {
      ptr->set_latency(0);
    } else {
      ptr->set_latency(latency / 1000'000);
    }

    seq = 0;
    size = 0;
    lat = 0;
  }

  list.set_control_id(impl_->control_id);
  list.set_hostname(impl_->current_host_name);
  impl_->info_pub->publish(list, true);
}

}  // namespace vlink
