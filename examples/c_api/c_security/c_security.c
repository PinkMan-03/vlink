/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 */

/* Pure C Security Example
 * Demonstrates the VLink C Security API:
 *   1. Standalone vlink_security_create() + encrypt/decrypt roundtrip
 *      (raw symmetric key, AES-128-GCM AEAD)
 *   2. PBKDF2 passphrase-derived symmetric key
 *   3. Atomic create + Security install for a Publisher/Subscriber pair via
 *      vlink_create_secure_publisher() / vlink_create_secure_subscriber().
 *      Security configuration is one-shot at creation; there is no separate
 *      enable_security() entry point.
 *
 * The C API secure node wrappers encrypt/decrypt Bytes payloads in the wrapper
 * layer.  For cross-language interop, use the same URL, schema metadata, and
 * Security config on the C, C++, and Python endpoints.
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

static int g_received_count = 0;

/* Subscriber callback: invoked for each decrypted message */
static void on_message(const uint8_t* data, const size_t size, void* user_data) {
  (void)user_data;
  printf("[Subscriber] Decrypted %zu bytes: %.*s\n", size, (int)size, (const char*)data);
  g_received_count++;
}

/* Print a buffer as a short hex preview (first/last bytes). */
static void print_hex_preview(const char* label, const uint8_t* data, size_t size) {
  printf("  %s size=%zu", label, size);
  if (size > 0) {
    printf(" first=0x%02x last=0x%02x", data[0], data[size - 1]);
  }
  printf("\n");
}

int main(void) {
  int ret;

  printf("=== VLink C API Security Example ===\n\n");

  /* ======== Section 1: Standalone Security Roundtrip (Raw Key) ======== */
  printf("[1] Standalone Security (AES-128-GCM, raw key)\n");
  {
    vlink_security_config_t cfg;
    vlink_security_config_init(&cfg);
    cfg.key = "my-secret-key-16";
    cfg.advanced.aad_context = "c_api/security/raw";

    vlink_security_handle_t sec = vlink_security_create(&cfg);
    if (sec == NULL) {
      printf("  vlink_security_create failed.\n");
      return 1;
    }

    const char* plain = "Hello with AES-128-GCM via C API!";
    const size_t plain_size = strlen(plain);

    uint8_t* cipher = NULL;
    size_t cipher_size = 0;
    ret = vlink_security_encrypt(sec, (const uint8_t*)plain, plain_size, &cipher, &cipher_size);
    printf("  encrypt ret=%d\n", ret);
    print_hex_preview("cipher", cipher, cipher_size);

    uint8_t* recovered = NULL;
    size_t recovered_size = 0;
    ret = vlink_security_decrypt(sec, cipher, cipher_size, &recovered, &recovered_size);
    printf("  decrypt ret=%d\n", ret);

    if (ret == VLINK_RET_NO_ERROR && recovered_size == plain_size && memcmp(recovered, plain, plain_size) == 0) {
      printf("  Roundtrip: PASS (recovered \"%.*s\")\n", (int)recovered_size, (const char*)recovered);
    } else {
      printf("  Roundtrip: FAIL\n");
    }

    uint8_t* replay_plain = NULL;
    size_t replay_plain_size = 0;
    fflush(stdout);
    ret = vlink_security_decrypt(sec, cipher, cipher_size, &replay_plain, &replay_plain_size);
    printf("  replay decrypt ret=%d (expected %d)\n", ret, VLINK_RET_TRANSFER_ERROR);
    vlink_security_free_buffer(replay_plain);

    vlink_security_free_buffer(cipher);
    vlink_security_free_buffer(recovered);
    vlink_security_destroy(sec);
  }

  /* ======== Section 2: Standalone Security with PBKDF2 Passphrase ======== */
  printf("\n[2] Standalone Security (PBKDF2-HMAC-SHA256 + AES-128-GCM)\n");
  {
    /* The salt must be >= 16 bytes and shared out-of-band with the peer. */
    static uint8_t salt[] = "vlink-c-example-salt-v1";

    vlink_security_config_t cfg;
    vlink_security_config_init(&cfg);
    cfg.passphrase = "correct horse battery staple";
    cfg.pbkdf2_salt = salt;
    cfg.pbkdf2_salt_size = sizeof(salt) - 1;
    cfg.pbkdf2_iterations = 200000U;

    vlink_security_handle_t sec = vlink_security_create(&cfg);
    if (sec == NULL) {
      printf("  vlink_security_create failed.\n");
      return 1;
    }

    const char* plain = "Passphrase-derived AES key";
    const size_t plain_size = strlen(plain);

    uint8_t* cipher = NULL;
    size_t cipher_size = 0;
    ret = vlink_security_encrypt(sec, (const uint8_t*)plain, plain_size, &cipher, &cipher_size);
    printf("  encrypt ret=%d cipher_size=%zu\n", ret, cipher_size);

    uint8_t* recovered = NULL;
    size_t recovered_size = 0;
    ret = vlink_security_decrypt(sec, cipher, cipher_size, &recovered, &recovered_size);
    printf("  decrypt ret=%d recovered_size=%zu\n", ret, recovered_size);

    if (ret == VLINK_RET_NO_ERROR && recovered_size == plain_size && memcmp(recovered, plain, plain_size) == 0) {
      printf("  Roundtrip: PASS\n");
    } else {
      printf("  Roundtrip: FAIL\n");
    }

    vlink_security_free_buffer(cipher);
    vlink_security_free_buffer(recovered);
    vlink_security_destroy(sec);
  }

  /* ======== Section 3: Publisher/Subscriber with Encryption ======== */
  /* C API secure wrappers encrypt Bytes payloads in the wrapper layer.  This
   * shm:// section is illustrative and requires the Iceoryx RouDi daemon. */
  printf("\n[3] Publisher/Subscriber with shm:// + Security\n");
  printf("    (requires `iox-roudi`; set VLINK_C_SECURITY_RUN_SHM=1 to run this section)\n");
  if (getenv("VLINK_C_SECURITY_RUN_SHM") == NULL) {
    printf("  Skipping: shm transport section is opt-in to avoid Iceoryx fatal aborts without RouDi.\n");
    goto section3_end;
  }
  {
    const vlink_schema_info_t schema = {"text", VLINK_SCHEMA_RAW};

    vlink_security_config_t sub_cfg;
    vlink_security_config_init(&sub_cfg);
    sub_cfg.key = "shm-shared-key-1";

    vlink_subscriber_handle_t sub;
    ret = vlink_create_secure_subscriber("shm://c_api/security", &schema, &sub, on_message, NULL, &sub_cfg);
    if (ret != VLINK_RET_NO_ERROR) {
      printf("  Skipping: vlink_create_secure_subscriber returned %d (RouDi not running?)\n", ret);
      goto section3_end;
    }

    vlink_security_config_t pub_cfg;
    vlink_security_config_init(&pub_cfg);
    pub_cfg.key = "shm-shared-key-1";

    vlink_publisher_handle_t pub;
    ret = vlink_create_secure_publisher("shm://c_api/security", &schema, &pub, &pub_cfg);
    if (ret != VLINK_RET_NO_ERROR) {
      printf("  Skipping: vlink_create_secure_publisher returned %d\n", ret);
      vlink_destroy_subscriber(&sub);
      goto section3_end;
    }

    ret = vlink_wait_for_subscribers(pub, 2000);
    printf("  wait_for_subscribers ret=%d\n", ret);

    const char* messages[] = {"encrypted msg 1", "encrypted msg 2", "encrypted msg 3"};
    for (int i = 0; i < 3; ++i) {
      const char* m = messages[i];
      ret = vlink_publish(pub, (const uint8_t*)m, strlen(m));
      printf("  publish \"%s\" ret=%d\n", m, ret);
      sleep_ms(50);
    }

    sleep_ms(200);
    printf("  Total decrypted messages received: %d\n", g_received_count);

    vlink_destroy_publisher(&pub);
    vlink_destroy_subscriber(&sub);
  }
section3_end:

  /* ======== Section 4: Reference Notes ======== */
  printf("\n[4] Reference\n");
  printf("  Construction:   vlink_security_config_init(&cfg) + populate fields\n");
  printf("  Standalone:     vlink_security_create / encrypt / decrypt / destroy\n");
  printf("  Atomic create-with-security:\n");
  printf("                  vlink_create_secure_publisher  / vlink_create_secure_subscriber\n");
  printf("                  vlink_create_secure_server     / vlink_create_secure_client\n");
  printf("                  vlink_create_secure_setter     / vlink_create_secure_getter\n");
  printf("  Allocations:    encrypt/decrypt allocate; release with vlink_security_free_buffer.\n");
  printf("  Interop:        C/C++/Python endpoints must share URL, schema metadata, and security config.\n");

  printf("\n=== Example complete ===\n");
  return 0;
}
