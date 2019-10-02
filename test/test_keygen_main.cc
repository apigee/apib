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

#include <stdio.h>

#include "test/test_keygen.h"

int main(int argc, char** argv) {
  RSA* key = apib::keygen_MakeRSAPrivateKey(2048);
  if (key == NULL) {
    printf("Error making private key\n");
    return 2;
  }

  int err = apib::keygen_WriteRSAPrivateKey(key, "/tmp/key.pem");
  if (err != 0) {
    printf("Error generating private key: %i\n", err);
    return 2;
  }

  X509* cert = apib::keygen_MakeServerCertificate(key, 1, 100);
  if (cert == NULL) {
    printf("Error making certificate\n");
    return 3;
  }

  err = apib::keygen_SignCertificate(key, cert);
  if (err != 0) {
    printf("Error signing certificate: %i\n", err);
    return 4;
  }

  err = apib::keygen_WriteX509Certificate(cert, "/tmp/cert.pem");
  X509_free(cert);
  RSA_free(key);
  if (err != 0) {
    printf("Error writing certificate: %i\n", err);
    return 4;
  }
  return 0;
}