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

// NOLINTBEGIN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlink/external/c_api.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

void custom_sleep(int ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

int test_schema_type_mapping(void);

// event

void on_subscriber_msg(const uint8_t* data, const size_t size, void* user_data) {
  if (!data || size == 0) {
    return;
  }

  if (data != NULL && size == strlen("event") && memcmp(data, "event", strlen("event")) == 0) {
    if (user_data) {
      *(int*)user_data = 0;
    }
  }
}

int test_pub_sub(void) {
  printf("test_pub_sub...\n");
  fflush(stdout);

  int ret = 1;
  int ret2 = 1;
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  vlink_subscriber_handle_t sub_handle = {0};

  ret = vlink_create_subscriber("dds://c_interface/event", &schema, &sub_handle, on_subscriber_msg, &ret2);

  vlink_publisher_handle_t pub_handle = {0};

  ret += vlink_create_publisher("dds://c_interface/event", &schema, &pub_handle);

  ret += vlink_wait_for_subscribers(pub_handle, 5000);

  ret += vlink_has_subscribers(pub_handle);

  char* msg = "event";

  ret += vlink_publish(pub_handle, (uint8_t*)msg, strlen(msg));

  custom_sleep(100);

  ret += vlink_destroy_subscriber(&sub_handle);

  ret += vlink_destroy_publisher(&pub_handle);

  return ret + ret2;
}

// method

void on_server_msg(const uint8_t* req_data, const size_t req_size, void* user_data) {
  if (!req_data || req_size == 0) {
    return;
  }

  if (req_data != NULL && req_size == strlen("method_req") &&
      memcmp(req_data, "method_req", strlen("method_req")) == 0) {
    vlink_server_handle_t* handle = (vlink_server_handle_t*)user_data;

    char* msg = "method_resp";

    vlink_reply(handle, (uint8_t*)msg, strlen(msg));
  }
}

void on_client_msg(const uint8_t* data, const size_t size, void* user_data) {
  if (!data || size == 0) {
    return;
  }

  if (data != NULL && size == strlen("method_resp") && memcmp(data, "method_resp", strlen("method_resp")) == 0) {
    if (user_data) {
      *(int*)user_data = 0;
    }
  }
}

int test_server_client(void) {
  printf("test_server_client...\n");
  fflush(stdout);

  int ret = 1;
  int ret2 = 1;
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  vlink_server_handle_t server_handle = {0};

  ret = vlink_create_server("dds://c_interface/method", &schema, &server_handle, on_server_msg, &server_handle);

  vlink_client_handle_t client_handle = {0};

  ret += vlink_create_client("dds://c_interface/method", &schema, &client_handle);

  ret += vlink_wait_for_server(client_handle, 5000);

  ret += vlink_has_server(client_handle);

  char* msg = "method_req";

  ret += vlink_invoke(client_handle, (uint8_t*)msg, strlen(msg), on_client_msg, &ret2);

  custom_sleep(100);

  ret += vlink_destroy_server(&server_handle);

  ret += vlink_destroy_client(&client_handle);

  return ret + ret2;
}

// field

void on_getter_msg(const uint8_t* data, const size_t size, void* user_data) {
  if (data != NULL && size == strlen("field") && memcmp(data, "field", strlen("field")) == 0) {
    *(int*)user_data = 0;
  }
}

int test_setter_getter(void) {
  printf("test_setter_getter...\n");
  fflush(stdout);

  int ret = 1;
  int ret2 = 1;
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  vlink_setter_handle_t setter_handle = {0};

  ret = vlink_create_setter("dds://c_interface/field", &schema, &setter_handle);

  vlink_getter_handle_t getter_handle = {0};

  ret += vlink_create_getter("dds://c_interface/field", &schema, &getter_handle, on_getter_msg, &ret2);

  char* msg = "field";

  ret += vlink_set(setter_handle, (uint8_t*)msg, strlen(msg));

  custom_sleep(100);

  uint8_t data[100];

  size_t size = sizeof(data);

  ret += vlink_get(getter_handle, data, &size);

  ret += !(size == strlen("field") && memcmp(data, "field", strlen("field")) == 0);

  ret += vlink_destroy_setter(&setter_handle);

  ret += vlink_destroy_getter(&getter_handle);

  return ret + ret2;
}

int test_schema_info_validation(void) {
  printf("test_schema_info_validation...\n");
  fflush(stdout);

  int ret = 0;

  vlink_publisher_handle_t pub_handle = {0};

  ret += vlink_create_publisher("dds://c_interface/schema_null", NULL, &pub_handle);
  ret += vlink_destroy_publisher(&pub_handle);

  const vlink_schema_info_t empty_schema = {"", VLINK_SCHEMA_UNKNOWN};
  ret += vlink_create_publisher("dds://c_interface/schema_empty", &empty_schema, &pub_handle);
  ret += vlink_destroy_publisher(&pub_handle);

  const vlink_schema_info_t invalid_schema = {"text", (vlink_schema_t)99};
  ret += vlink_create_publisher("dds://c_interface/schema_invalid", &invalid_schema, &pub_handle) !=
         VLINK_RET_INVALID_ERROR;

  const vlink_schema_info_t missing_schema = {"text", VLINK_SCHEMA_UNKNOWN};
  ret += vlink_create_publisher("dds://c_interface/schema_missing_schema", &missing_schema, &pub_handle) !=
         VLINK_RET_INVALID_ERROR;

  const vlink_schema_info_t missing_ser = {NULL, VLINK_SCHEMA_RAW};
  ret += vlink_create_publisher("dds://c_interface/schema_missing_ser", &missing_ser, &pub_handle) !=
         VLINK_RET_INVALID_ERROR;

  const vlink_schema_info_t empty_ser = {"", VLINK_SCHEMA_RAW};
  ret += vlink_create_publisher("dds://c_interface/schema_empty_ser", &empty_ser, &pub_handle) !=
         VLINK_RET_INVALID_ERROR;

  {
    const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};
    vlink_publisher_handle_t valid_pub_handle = {0};

    ret += vlink_create_publisher("dds://c_interface/schema_detect_null_cb", &schema, &valid_pub_handle);
    ret += vlink_detect_subscribers(valid_pub_handle, NULL, NULL) != VLINK_RET_INVALID_ERROR;
    ret += vlink_destroy_publisher(&valid_pub_handle);
  }

  {
    const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};
    vlink_client_handle_t client_handle = {0};

    ret += vlink_create_client("dds://c_interface/schema_detect", &schema, &client_handle);
    ret += vlink_detect_server(client_handle, NULL, NULL) != VLINK_RET_INVALID_ERROR;
    ret += vlink_destroy_client(&client_handle);
  }

  return ret;
}

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  int ret = 1;

  ret = test_pub_sub();

  ret += test_server_client();

  ret += test_setter_getter();

  ret += test_schema_info_validation();

  ret += test_schema_type_mapping();

  return ret;
}

// NOLINTEND
