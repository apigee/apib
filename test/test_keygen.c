/*
Copyright 2019 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "test/test_keygen.h"

#include <assert.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <stdio.h>

#define DAY (60 * 60 * 24)

static void printError(const char* msg) {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, 256);
  fprintf(stderr, "%s: %s\n", msg, buf);
}

RSA* keygen_MakeRSAPrivateKey(int bits) {
  // This is apparently the right exponent for a 2048-bit key.
  BIGNUM* e = BN_new();
  BN_set_word(e, RSA_F4);

  RSA* key = RSA_new();
  int s = RSA_generate_key_ex(key, bits, e, NULL);
  if (s != 1) {
    printError("Error generating RSA key");
    return NULL;
  }

  BN_free(e);
  return key;
}

X509* keygen_MakeServerCertificate(RSA* key, int serial, int days) {
  EVP_PKEY* public = EVP_PKEY_new();
  int err = EVP_PKEY_set1_RSA(public, key);
  assert(err == 1);

  X509* cert = X509_new();
  // This means X509 V3
  err = X509_set_version(cert, 2);
  assert(err == 1);
  err = ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);
  assert(err == 1);
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), days * DAY);

  X509_NAME* subject = X509_get_subject_name(cert);
  err = X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC,
                                   (unsigned char*)"US", -1, -1, 0);
  assert(err == 1);
  err = X509_NAME_add_entry_by_txt(subject, "ST", MBSTRING_ASC,
                                   (unsigned char*)"CA", -1, -1, 0);
  assert(err == 1);
  err = X509_NAME_add_entry_by_txt(subject, "L", MBSTRING_ASC,
                                   (unsigned char*)"Sunnyvale", -1, -1, 0);
  assert(err == 1);
  err = X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC,
                                   (unsigned char*)"The Cloud", -1, -1, 0);
  assert(err == 1);
  err = X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                   (unsigned char*)"testserver", -1, -1, 0);
  assert(err == 1);

  // Issuer same as key for now
  err = X509_set_issuer_name(cert, subject);
  assert(err == 1);

  err = X509_set_pubkey(cert, public);
  assert(err == 1);
  EVP_PKEY_free(public);

  // X509_set_issuer_name
  // extensions... which ones!

  return cert;
}

int keygen_SignCertificate(RSA* key, X509* cert) {
    EVP_PKEY* public = EVP_PKEY_new();
    int err = EVP_PKEY_set1_RSA(public, key);
    assert(err == 1);

    err = X509_sign(cert, public, EVP_sha256());
    EVP_PKEY_free(public);
    if (err == 0) {
      printError("Error signing certificate");
      return -1;
    }
    return 0;
}

int keygen_WriteRSAPrivateKey(RSA* key, const char* fileName) {
  FILE* out = fopen(fileName, "w");
  if (out == NULL) {
    perror("Can't open output file");
    return -2;
  }

  int s = PEM_write_RSAPrivateKey(out, key, NULL, NULL, 0, NULL, NULL);
  fclose(out);
  if (s != 1) {
    printError("Error writing private key");
    return -2;
  }

  return 0;
}

int keygen_WriteX509Certificate(X509* cert, const char* fileName) {
  FILE* out = fopen(fileName, "w");
  if (out == NULL) {
    perror("Can't open output file");
    return -2;
  }

  int s = PEM_write_X509(out, cert);
  fclose(out);
  if (s != 1) {
    printError("Error writing private key");
    return -2;
  }

  return 0;
}