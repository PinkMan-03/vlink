/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 */

/* Pure C Server / Client RPC Example
 * Demonstrates the VLink C API for method-model communication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlink/external/c_api.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)  // NOLINT(readability-identifier-naming)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)  // NOLINT(readability-identifier-naming)
#endif

static int g_resp_received = 0;

/* Server request callback:
 * MUST call vlink_reply() inside this callback before returning. */
static void on_request(const uint8_t* data, const size_t size, void* user_data) {
  vlink_server_handle_t* server = (vlink_server_handle_t*)user_data;

  printf("[Server] Received request: %.*s (%zu bytes)\n", (int)size, (const char*)data, size);

  /* Build response */
  char resp[128];

  // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
  int resp_len = snprintf(resp, sizeof(resp), "REPLY: %.*s", (int)size, (const char*)data);

  /* vlink_reply() MUST be called within this callback */
  int ret = vlink_reply(server, (const uint8_t*)resp, (size_t)resp_len);
  printf("[Server] Reply sent (ret=%d): %s\n", ret, resp);
}

/* Client response callback */
static void on_response(const uint8_t* data, const size_t size, void* user_data) {
  (void)user_data;
  if (data != NULL && size > 0) {
    printf("[Client] Response: %.*s (%zu bytes)\n", (int)size, (const char*)data, size);
  } else {
    printf("[Client] Empty response\n");
  }
  g_resp_received++;
}

int main(void) {
  int ret;
  const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

  printf("=== VLink C API RPC Example ===\n\n");

  /* ======== Create Server ======== */
  printf("[1] Creating Server...\n");
  vlink_server_handle_t server;
  ret = vlink_create_server("intra://c_api/rpc", &schema, &server, on_request, &server);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create server: %d\n", ret);
    return 1;
  }
  printf("  Server created.\n");

  /* ======== Create Client ======== */
  printf("\n[2] Creating Client...\n");
  vlink_client_handle_t client;
  ret = vlink_create_client("intra://c_api/rpc", &schema, &client);
  if (ret != VLINK_RET_NO_ERROR) {
    printf("  Failed to create client: %d\n", ret);
    vlink_destroy_server(&server);
    return 1;
  }
  printf("  Client created.\n");

  /* ======== Wait for Server ======== */
  printf("\n[3] Waiting for server...\n");
  ret = vlink_wait_for_server(client, 2000);
  printf("  wait_for_server: %s\n", ret == VLINK_RET_NO_ERROR ? "connected" : "timeout");

  /* ======== Check Server Presence ======== */
  ret = vlink_has_server(client);
  printf("\n[4] has_server: %s\n", ret == VLINK_RET_NO_ERROR ? "yes" : "no");

  /* ======== Invoke RPC Calls ======== */
  printf("\n[5] Invoking RPC calls...\n");
  for (int i = 1; i <= 3; ++i) {
    char req[64];

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    int req_len = snprintf(req, sizeof(req), "request_%d", i);

    ret = vlink_invoke(client, (const uint8_t*)req, (size_t)req_len, on_response, NULL);
    printf("  invoke(\"%s\") ret=%d\n", req, ret);
    sleep_ms(100);
  }

  sleep_ms(200);

  printf("\n[6] Responses received: %d\n", g_resp_received);

  /* ======== Cleanup ======== */
  printf("\n[7] Destroying nodes...\n");
  vlink_destroy_client(&client);
  vlink_destroy_server(&server);
  printf("  Cleanup complete.\n");

  printf("\n=== Example complete ===\n");
  return 0;
}
