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

#include <assert.h>
#include <openssl/ssl.h>

#include "gtest/gtest.h"
#include "src/apib_iothread.h"
#include "src/apib_message.h"
#include "src/apib_reporting.h"
#include "src/apib_url.h"
#include "test/test_keygen.h"
#include "test/test_server.h"

using apib::IOThread;

namespace {

static char KeyPath[512];
static char CertPath[512];
static int testServerPort;
static TestServer* testServer;

class TLSTest : public ::testing::Test {
 protected:
  TLSTest() {
    RecordInit(NULL, NULL);
    RecordStart(1);
    testserver_ResetStats(testServer);
  }
  ~TLSTest() {
    // The "url_" family of functions use static data, so reset every time.
    url_Reset();
    EndReporting();
  }
};

static void compareReporting() {
  TestServerStats stats;
  testserver_GetStats(testServer, &stats);
  BenchmarkResults results;
  ReportResults(&results);

  EXPECT_LT(0, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);

  EXPECT_EQ(results.successfulRequests, stats.successCount);
  EXPECT_EQ(results.unsuccessfulRequests, stats.errorCount);
  EXPECT_EQ(results.socketErrors, stats.socketErrorCount);
  EXPECT_EQ(results.connectionsOpened, stats.connectionCount);
}

static SSL_CTX* setUpTLS() {
  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  return ctx;
}

TEST_F(TLSTest, Basic) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  url_InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.sslCtx = setUpTLS();

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  SSL_CTX_free(t.sslCtx);
}

TEST_F(TLSTest, NoKeepAlive) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  url_InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.noKeepAlive = 1;
  t.httpVerb = "GET";
  t.sslCtx = setUpTLS();

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  SSL_CTX_free(t.sslCtx);
}

TEST_F(TLSTest, Larger) {
  char url[128];
  sprintf(url, "https://localhost:%i/data?size=8000", testServerPort);
  url_InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.sslCtx = setUpTLS();

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  SSL_CTX_free(t.sslCtx);
}

TEST_F(TLSTest, VerifyPeerFailing) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  url_InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.sslCtx = setUpTLS();
  SSL_CTX_set_verify(t.sslCtx, SSL_VERIFY_PEER, NULL);

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  BenchmarkResults results;
  ReportResults(&results);
  EXPECT_EQ(0, results.successfulRequests);
  EXPECT_LT(0, results.socketErrors);

  SSL_CTX_free(t.sslCtx);
}

TEST_F(TLSTest, VerifyPeerSuccess) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  url_InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.sslCtx = setUpTLS();
  SSL_CTX_set_verify(t.sslCtx, SSL_VERIFY_PEER, NULL);
  ASSERT_EQ(1, SSL_CTX_load_verify_locations(t.sslCtx, CertPath, NULL));

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  SSL_CTX_free(t.sslCtx);
}

// TODO ciphers?

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  char* keyDir = getenv("TEST_TMPDIR");
  if (keyDir == NULL) {
    keyDir = getenv("TEMPDIR");
  }
  if (keyDir == NULL) {
    keyDir = strdup("/tmp");
  }

  sprintf(KeyPath, "%s/key.pem", keyDir);
  sprintf(CertPath, "%s/cert.pem", keyDir);
  printf("Key:  %s\n", KeyPath);
  printf("Cert: %s\n", CertPath);

  RSA* key = keygen_MakeRSAPrivateKey(2048);
  assert(key != NULL);
  X509* cert = keygen_MakeServerCertificate(key, 1, 1);
  assert(cert != NULL);
  int err = keygen_SignCertificate(key, cert);
  assert(err == 0);
  err = keygen_WriteRSAPrivateKey(key, KeyPath);
  assert(err == 0);
  err = keygen_WriteX509Certificate(cert, CertPath);
  assert(err == 0);
  RSA_free(key);
  X509_free(cert);

  testServer = (TestServer*)malloc(sizeof(TestServer));
  memset(testServer, 0, sizeof(TestServer));

  err = testserver_Start(testServer, "127.0.0.1", 0, KeyPath, CertPath);
  if (err != 0) {
    fprintf(stderr, "Can't start test server: %i\n", err);
    return 2;
  }
  testServerPort = testserver_GetPort(testServer);

  int r = RUN_ALL_TESTS();

  testserver_Stop(testServer);
  // testserver_Join(&svr);
  free(testServer);

  return r;
}
