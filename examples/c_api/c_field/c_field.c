/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 */

/* Pure C Setter / Getter Field Example
 * Demonstrates the VLink C API for field-model communication.
 */

#include <stdio.h>
#include <string.h>
#include <vlink/external/c_api.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)  // NOLINT(readability-identifier-naming)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)  // NOLINT(readability-identifier-naming)
#endif

static int g_change_count = 0;

/* Getter callback: invoked when the field value changes (push mode) */
static void on_value_changed(const uint8_t* data, const size_t size, void* user_data) {
  (void)user_data;
  printf("[Getter/Push] Value changed: %.*s (%zu bytes)\n", (int)size, (const char*)data, size);
  g_change_count++;
}

int main(void) {
  int ret;
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  printf("=== VLink C API Field Example ===\n\n");

  /* ======== Create Setter ======== */
  printf("[1] Creating Setter...\n");
  vlink_setter_handle_t setter;
  ret = vlink_create_setter("intra://c_api/field", &schema, &setter);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create setter: %d\n", ret);
    return 1;
  }
  printf("  Setter created.\n");

  /* ======== Set Initial Value ======== */
  printf("\n[2] Setting initial value...\n");
  {
    const char* initial = "temperature=25";
    ret = vlink_set(setter, (const uint8_t*)initial, strlen(initial));
    printf("  vlink_set(\"%s\") ret=%d\n", initial, ret);
  }

  sleep_ms(50);

  /* ======== Create Getter (Push Mode) ======== */
  printf("\n[3] Creating Getter (push mode with callback)...\n");
  vlink_getter_handle_t getter_push;
  ret = vlink_create_getter("intra://c_api/field", &schema, &getter_push, on_value_changed, NULL);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create push getter: %d\n", ret);
    vlink_destroy_setter(&setter);
    return 1;
  }
  printf("  Push getter created.\n");

  /* ======== Create Getter (Poll Mode) ======== */
  printf("\n[4] Creating Getter (poll mode, no callback)...\n");
  vlink_getter_handle_t getter_poll;
  ret = vlink_create_getter("intra://c_api/field", &schema, &getter_poll, NULL, NULL);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create poll getter: %d\n", ret);
    vlink_destroy_getter(&getter_push);
    vlink_destroy_setter(&setter);
    return 1;
  }
  printf("  Poll getter created.\n");

  sleep_ms(100);

  /* ======== Update Field Values ======== */
  printf("\n[5] Updating field values...\n");
  {
    const char* values[] = {
        "temperature=28",
        "temperature=30",
        "temperature=27",
        "temperature=32",
    };
    int count = (int)(sizeof(values) / sizeof(values[0]));

    for (int i = 0; i < count; ++i) {
      ret = vlink_set(setter, (const uint8_t*)values[i], strlen(values[i]));
      printf("  Set: \"%s\" (ret=%d)\n", values[i], ret);
      sleep_ms(50);
    }
  }

  sleep_ms(200);

  /* ======== Poll Current Value ======== */
  printf("\n[6] Polling current value (vlink_get)...\n");
  {
    uint8_t buf[256];
    size_t buf_size = sizeof(buf);

    ret = vlink_get(getter_poll, buf, &buf_size);
    if (ret == VLINK_RET_NO_ERROR) {
      printf("  Current value: %.*s (%zu bytes)\n", (int)buf_size, (const char*)buf, buf_size);
    } else if (ret == VLINK_RET_TRANSFER_ERROR) {
      printf("  No value available yet (ret=%d)\n", ret);
    } else if (ret == VLINK_RET_MEMORY_ERROR) {
      printf("  Buffer too small (ret=%d)\n", ret);
    } else {
      printf("  vlink_get failed: ret=%d\n", ret);
    }
  }

  printf("\n[7] Push callback invocations: %d\n", g_change_count);

  /* ======== Cleanup ======== */
  printf("\n[8] Destroying nodes...\n");
  vlink_destroy_getter(&getter_poll);
  vlink_destroy_getter(&getter_push);
  vlink_destroy_setter(&setter);
  printf("  Cleanup complete.\n");

  /* ======== API Summary ======== */
  printf("\n[9] Field API Summary:\n");
  printf("  vlink_create_setter(url, &schema, &handle)              Create writer\n");
  printf("  vlink_set(handle, data, size)                           Write value\n");
  printf("  vlink_create_getter(url, &schema, &handle, cb, ud)      Create reader\n");
  printf("  vlink_get(handle, buf, &size)                           Poll value\n");
  printf("  vlink_destroy_setter(&handle)                           Release\n");
  printf("  vlink_destroy_getter(&handle)                           Release\n");

  printf("\n=== Example complete ===\n");
  return 0;
}
