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

#ifndef TEST_KEYGEN_H
#define TEST_KEYGEN_H

#include <openssl/rsa.h>
#include <openssl/x509.h>

#ifdef __cplusplus
extern "C" {
#endif

// Make a new RSA private key. Must be freed using RSA_free.
// Return NULL in the event of failure.
extern RSA* keygen_MakeRSAPrivateKey(int bits);

// Make a certificate for a web server
extern X509* keygen_MakeServerCertificate(RSA* key, int serial, int days);

// Sign it
extern int keygen_SignCertificate(RSA* key, X509* cert);

// Write it to the specified file name
extern int keygen_WriteRSAPrivateKey(RSA* k, const char* fileName);
extern int keygen_WriteX509Certificate(X509* c, const char* fileName);

#ifdef __cplusplus
}
#endif

#endif  // TEST_KEYGEN_H
