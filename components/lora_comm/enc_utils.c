#include "enc_utils.h"
#define AES_KEY_SIZE 32 // 256 bits
#define AES_IV_SIZE 12  // 96 bits
#define AES_TAG_SIZE 16
#define AES_KEY                                                                \
  "d5c72e5e8ef42e5317c412fa59cdd33818b3288414826130fdaa3603301a176a"
// Function to convert a hexadecimal string to a byte array
void hex_string_to_byte_array(const char *hex_str, unsigned char *byte_array,
                              size_t byte_array_size) {
  size_t len = strlen(hex_str);
  if (len % 2 != 0 || len / 2 > byte_array_size) {
    printf("Invalid hex string\n");
    return;
  }

  for (size_t i = 0; i < len / 2; ++i) {
    sscanf(hex_str + 2 * i, "%2hhx", &byte_array[i]);
  }
}

// Function to print bytes for debugging
void print_hex(const char *label, const unsigned char *data, size_t length) {
  if (label != NULL) {
    printf("%s: ", label);
  }
  for (size_t i = 0; i < length; i++) {
    printf("%02X ", data[i]);
  }
  printf("\n");
}

// Function to decrypt received data
int decrypt_data(unsigned char *input, size_t input_length,
                 unsigned char *output) {
  mbedtls_gcm_context gcm;
  unsigned char key[AES_KEY_SIZE];
  unsigned char nonce[AES_IV_SIZE];
  unsigned char tag[AES_TAG_SIZE];
  size_t data_length = input_length - AES_IV_SIZE - AES_TAG_SIZE;
  int ret;

  // Convert hex key string to byte array
  hex_string_to_byte_array(AES_KEY, key, sizeof(key));

  // Initialize GCM context
  mbedtls_gcm_init(&gcm);

  // Set up the key in the GCM context
  ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    printf("Failed in mbedtls_gcm_setkey: %d\n", ret);
    mbedtls_gcm_free(&gcm);
    return ret;
  }

  // Extract nonce and tag from input
  memcpy(nonce, input + data_length, AES_IV_SIZE);
  memcpy(tag, input + data_length + AES_IV_SIZE, AES_TAG_SIZE);

  // Decrypt the data
  ret = esp_aes_gcm_auth_decrypt(&gcm, data_length, nonce, AES_IV_SIZE, NULL, 0,
                                 tag, AES_TAG_SIZE, input, output);
  if (ret != 0) {
    printf("Failed in esp_aes_gcm_auth_decrypt: %d\n", ret);
    mbedtls_gcm_free(&gcm);
    return ret;
  }

  printf("Decryption success\n");
  mbedtls_gcm_free(&gcm);
  return 0;
}