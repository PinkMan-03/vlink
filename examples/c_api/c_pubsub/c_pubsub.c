/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 */

/* Pure C Publisher / Subscriber Example
 * Demonstrates the VLink C API for event-model communication.
 */

#include <stdio.h>
#include <string.h>
#include <vlink/external/c_api.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)  // NOLINT(readability-identifier-naming)
#endif

static int g_received_count = 0;

/* Subscriber callback: invoked for each received message */
static void on_message(const uint8_t* data, const size_t size, void* user_data) {
  (void)user_data;
  printf("[Subscriber] Received %zu bytes: %.*s\n", size, (int)size, (const char*)data);
  g_received_count++;
}

int main(void) {
  int ret;
  /* Apply ser + schema atomically during node creation. */
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  printf("=== VLink C API PubSub Example ===\n\n");

  /* ======== Create Subscriber ======== */
  printf("[1] Creating Subscriber...\n");
  vlink_subscriber_handle_t sub;
  ret = vlink_create_subscriber("intra://c_api/pubsub", &schema, &sub, on_message, NULL);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create subscriber: %d\n", ret);
    return 1;
  }
  printf("  Subscriber created successfully.\n");

  /* ======== Create Publisher ======== */
  printf("\n[2] Creating Publisher...\n");
  vlink_publisher_handle_t pub;
  ret = vlink_create_publisher("intra://c_api/pubsub", &schema, &pub);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create publisher: %d\n", ret);
    vlink_destroy_subscriber(&sub);
    return 1;
  }
  printf("  Publisher created successfully.\n");

  /* ======== Wait for Subscribers ======== */
  printf("\n[3] Waiting for subscriber match...\n");
  ret = vlink_wait_for_subscribers(pub, 2000);
  if (ret == VLINK_RET_NO_ERROR) {
    printf("  Subscriber matched!\n");
  } else {
    printf("  Timeout waiting for subscribers: %d\n", ret);
  }

  /* ======== Check Subscriber Presence ======== */
  ret = vlink_has_subscribers(pub);
  printf("\n[4] has_subscribers: %s\n", ret == VLINK_RET_NO_ERROR ? "yes" : "no");

  /* ======== Publish Messages ======== */
  printf("\n[5] Publishing messages...\n");
  for (int i = 1; i <= 5; ++i) {
    char msg[64];

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    int len = snprintf(msg, sizeof(msg), "Hello from C API #%d", i);

    ret = vlink_publish(pub, (const uint8_t*)msg, (size_t)len);
    printf("  Published: \"%s\" (ret=%d)\n", msg, ret);
    sleep_ms(50);
  }

  /* ======== Publish by Force ======== */
  printf("\n[6] Publish by force (even without subscribers)...\n");
  {
    const char* force_msg = "forced message";
    ret = vlink_publish_by_force(pub, (const uint8_t*)force_msg, strlen(force_msg));
    printf("  publish_by_force: ret=%d\n", ret);
  }

  /* Allow time for message delivery */
  sleep_ms(200);

  printf("\n[7] Total messages received: %d\n", g_received_count);

  /* ======== Cleanup ======== */
  printf("\n[8] Destroying nodes...\n");
  vlink_destroy_publisher(&pub);
  vlink_destroy_subscriber(&sub);
  printf("  Cleanup complete.\n");

  /* ======== Return Code Reference ======== */
  printf("\n[9] Return Code Reference:\n");
  printf("  VLINK_RET_NO_ERROR(0)         Success\n");
  printf("  VLINK_RET_UNEXPECTED_ERROR(1) Condition not met\n");
  printf("  VLINK_RET_INVALID_ERROR(2)    Bad handle or NULL\n");
  printf("  VLINK_RET_MEMORY_ERROR(3)     Buffer too small\n");
  printf("  VLINK_RET_RUNTIME_ERROR(4)    C++ exception\n");
  printf("  VLINK_RET_TRANSFER_ERROR(5)   Operation failed\n");

  printf("\n=== Example complete ===\n");
  return 0;
}
