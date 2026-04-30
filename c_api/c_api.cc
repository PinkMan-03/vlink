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

#include "./external/c_api.h"

#include <mutex>

#include "./vlink.h"

template <typename NodeT>
static bool apply_schema_info(NodeT& node, const vlink_schema_info_t* schema_info) {
  if (!schema_info) {
    return true;
  }

  if VUNLIKELY (!vlink::SchemaData::is_valid_type(static_cast<vlink::SchemaType>(schema_info->schema))) {
    return false;
  }

  const auto schema_type = static_cast<vlink::SchemaType>(schema_info->schema);

  const bool has_ser = schema_info->ser && schema_info->ser[0] != '\0';
  const bool has_schema = schema_type != vlink::SchemaType::kUnknown;

  if (!has_ser && !has_schema) {
    return true;
  }

  if VUNLIKELY (has_ser != has_schema) {
    return false;
  }

  node.set_ser_type(schema_info->ser, schema_type);
  return true;
}

////////////////////////////////////////////////////////////////
/// Publisher
////////////////////////////////////////////////////////////////

int vlink_create_publisher(const char* url, const vlink_schema_info_t* schema_info, vlink_publisher_handle_t* handle) {
  if VUNLIKELY (!url || !handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Publisher<vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_publisher(vlink_publisher_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;
  handle->native_handle = nullptr;

  return VLINK_RET_NO_ERROR;
}

int vlink_has_subscribers(const vlink_publisher_handle_t handle) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->has_subscribers()) {
    return VLINK_RET_UNEXPECTED_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

int vlink_wait_for_subscribers(const vlink_publisher_handle_t handle, const int timeout) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->wait_for_subscribers(std::chrono::milliseconds(timeout))) {
    return VLINK_RET_UNEXPECTED_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

int vlink_detect_subscribers(const vlink_publisher_handle_t handle, const vlink_connect_callback_t connect_callback,
                             void* user_data) {
  if VUNLIKELY (!handle.native_handle || !connect_callback) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  ptr->detect_subscribers([connect_callback, user_data](bool connected) { connect_callback(connected, user_data); });

  return VLINK_RET_NO_ERROR;
}

int vlink_publish(const vlink_publisher_handle_t handle, const uint8_t* data, const size_t size) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->publish(vlink::Bytes::shallow_copy(data, size))) {
    return VLINK_RET_TRANSFER_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

int vlink_publish_by_force(const vlink_publisher_handle_t handle, const uint8_t* data, const size_t size) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Publisher<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->publish(vlink::Bytes::shallow_copy(data, size), true)) {
    return VLINK_RET_TRANSFER_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

////////////////////////////////////////////////////////////////
/// Subscriber
////////////////////////////////////////////////////////////////

int vlink_create_subscriber(const char* url, const vlink_schema_info_t* schema_info, vlink_subscriber_handle_t* handle,
                            const vlink_msg_callback_t msg_callback, void* user_data) {
  if VUNLIKELY (!url || !handle || !msg_callback) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Subscriber<vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

    bool ret = ptr->listen(
        [msg_callback, user_data](const vlink::Bytes& data) { msg_callback(data.data(), data.size(), user_data); });

    if VUNLIKELY (!ret) {
      delete ptr;
      handle->native_handle = nullptr;
      return VLINK_RET_TRANSFER_ERROR;
    }

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_subscriber(vlink_subscriber_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Subscriber<vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;
  handle->native_handle = nullptr;

  return VLINK_RET_NO_ERROR;
}

////////////////////////////////////////////////////////////////
/// Server
////////////////////////////////////////////////////////////////

int vlink_create_server(const char* url, const vlink_schema_info_t* schema_info, vlink_server_handle_t* handle,
                        const vlink_req_callback_t req_callback, void* user_data) {
  if VUNLIKELY (!url || !handle || !req_callback) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Server<vlink::Bytes, vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    handle->reserved[0] = new std::mutex;
    handle->reserved[1] = nullptr;
    handle->reserved[2] = nullptr;
    handle->reserved[3] = nullptr;

    bool ret = ptr->listen([handle, req_callback, user_data](const vlink::Bytes& req_data, vlink::Bytes& resp_data) {
      std::unique_lock lock(*static_cast<std::mutex*>(handle->reserved[0]));

      handle->reserved[3] = reinterpret_cast<void*>(1);

      req_callback(req_data.data(), req_data.size(), user_data);

      if (!handle->reserved[1] || !handle->reserved[2] || handle->reserved[3]) {
        return;
      }

      resp_data = vlink::Bytes::shallow_copy(static_cast<uint8_t*>(handle->reserved[1]),
                                             reinterpret_cast<size_t>(handle->reserved[2]));
    });

    if VUNLIKELY (!ret) {
      delete static_cast<std::mutex*>(handle->reserved[0]);
      delete ptr;
      handle->native_handle = nullptr;
      std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));
      return VLINK_RET_TRANSFER_ERROR;
    }

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_server(vlink_server_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Server<vlink::Bytes, vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;

  handle->native_handle = nullptr;

  if (handle->reserved[0]) {
    delete static_cast<std::mutex*>(handle->reserved[0]);
  }

  if (handle->reserved[1]) {
    delete[] static_cast<uint8_t*>(handle->reserved[1]);
  }

  if (handle->reserved[2]) {
    handle->reserved[2] = nullptr;
  }

  std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

  return VLINK_RET_NO_ERROR;
}

int vlink_reply(vlink_server_handle_t* handle, const uint8_t* data, const size_t size) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Server<vlink::Bytes, vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* mtx = static_cast<std::mutex*>(handle->reserved[0]);
  if VUNLIKELY (!mtx) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!handle->reserved[3]) {
    return VLINK_RET_RUNTIME_ERROR;
  }

  if (handle->reserved[1]) {
    delete[] static_cast<uint8_t*>(handle->reserved[1]);
  }
  handle->reserved[1] = new uint8_t[size]();
  handle->reserved[2] = reinterpret_cast<void*>(size);  // NOLINT(performance-no-int-to-ptr)
  handle->reserved[3] = nullptr;

  std::memcpy(handle->reserved[1], data, size);

  return VLINK_RET_NO_ERROR;
}

////////////////////////////////////////////////////////////////
/// Client
////////////////////////////////////////////////////////////////

int vlink_create_client(const char* url, const vlink_schema_info_t* schema_info, vlink_client_handle_t* handle) {
  if VUNLIKELY (!url || !handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Client<vlink::Bytes, vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_client(vlink_client_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Client<vlink::Bytes, vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;
  handle->native_handle = nullptr;

  return VLINK_RET_NO_ERROR;
}

int vlink_has_server(const vlink_client_handle_t handle) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Client<vlink::Bytes, vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->is_connected()) {
    return VLINK_RET_UNEXPECTED_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

int vlink_wait_for_server(const vlink_client_handle_t handle, const int timeout) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Client<vlink::Bytes, vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!ptr->wait_for_connected(std::chrono::milliseconds(timeout))) {
    return VLINK_RET_UNEXPECTED_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

int vlink_detect_server(const vlink_client_handle_t handle, const vlink_connect_callback_t connect_callback,
                        void* user_data) {
  if VUNLIKELY (!handle.native_handle || !connect_callback) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Client<vlink::Bytes, vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  ptr->detect_connected([connect_callback, user_data](bool connected) { connect_callback(connected, user_data); });

  return VLINK_RET_NO_ERROR;
}

int vlink_invoke(const vlink_client_handle_t handle, const uint8_t* data, const size_t size,
                 const vlink_resp_callback_t resp_callback, void* user_data) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Client<vlink::Bytes, vlink::Bytes>*>(handle.native_handle);
  const bool ret =
      ptr->invoke(vlink::Bytes::shallow_copy(data, size), [resp_callback, user_data](const vlink::Bytes& resp_data) {
        if (resp_callback) {
          resp_callback(resp_data.data(), resp_data.size(), user_data);
        }
      });

  if VUNLIKELY (!ret) {
    return VLINK_RET_TRANSFER_ERROR;
  }

  return VLINK_RET_NO_ERROR;
}

////////////////////////////////////////////////////////////////
/// Setter
////////////////////////////////////////////////////////////////

int vlink_create_setter(const char* url, const vlink_schema_info_t* schema_info, vlink_setter_handle_t* handle) {
  if VUNLIKELY (!url || !handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Setter<vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_setter(vlink_setter_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Setter<vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;
  handle->native_handle = nullptr;

  return VLINK_RET_NO_ERROR;
}

int vlink_set(const vlink_setter_handle_t handle, const uint8_t* data, const size_t size) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Setter<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  ptr->set(vlink::Bytes::shallow_copy(data, size));

  return VLINK_RET_NO_ERROR;
}

////////////////////////////////////////////////////////////////
/// Getter
////////////////////////////////////////////////////////////////

int vlink_create_getter(const char* url, const vlink_schema_info_t* schema_info, vlink_getter_handle_t* handle,
                        const vlink_msg_callback_t msg_callback, void* user_data) {
  if VUNLIKELY (!url || !handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  try {
    auto* ptr = new vlink::Getter<vlink::Bytes>(url, vlink::InitType::kWithoutInit);

    if VUNLIKELY (!apply_schema_info(*ptr, schema_info)) {
      delete ptr;
      return VLINK_RET_INVALID_ERROR;
    }

    ptr->init();

    handle->native_handle = ptr;
    std::memset(static_cast<void*>(handle->reserved), 0, sizeof(handle->reserved));

    if (msg_callback) {
      bool ret = ptr->listen(
          [msg_callback, user_data](const vlink::Bytes& data) { msg_callback(data.data(), data.size(), user_data); });

      if VUNLIKELY (!ret) {
        delete ptr;
        handle->native_handle = nullptr;
        return VLINK_RET_TRANSFER_ERROR;
      }
    }

    return VLINK_RET_NO_ERROR;
  } catch (std::exception&) {
    return VLINK_RET_RUNTIME_ERROR;
  }
}

int vlink_destroy_getter(vlink_getter_handle_t* handle) {
  if VUNLIKELY (!handle || !handle->native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Getter<vlink::Bytes>*>(handle->native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  delete ptr;
  handle->native_handle = nullptr;

  return VLINK_RET_NO_ERROR;
}

int vlink_get(const vlink_getter_handle_t handle, uint8_t* data, size_t* size) {
  if VUNLIKELY (!handle.native_handle) {
    return VLINK_RET_INVALID_ERROR;
  }

  if VUNLIKELY (!data || !size) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto* ptr = static_cast<vlink::Getter<vlink::Bytes>*>(handle.native_handle);

  if VUNLIKELY (!ptr) {
    return VLINK_RET_INVALID_ERROR;
  }

  auto result = ptr->get();

  if VUNLIKELY (!result.has_value()) {
    return VLINK_RET_TRANSFER_ERROR;
  }

  const auto& val = result.value();

  if VUNLIKELY (val.size() > *size) {
    *size = val.size();
    return VLINK_RET_MEMORY_ERROR;
  }

  std::memcpy(data, val.data(), val.size());

  *size = val.size();

  return VLINK_RET_NO_ERROR;
}
