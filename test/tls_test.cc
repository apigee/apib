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

#include "apib/apib_iothread.h"
#include "apib/apib_reporting.h"
#include "apib/apib_url.h"
#include "gtest/gtest.h"
#include "test/test_keygen.h"
#include "test/test_server.h"

using apib::BenchmarkResults;
using apib::IOThread;
using apib::RecordStart;
using apib::RecordStop;
using apib::ReportResults;
using apib::ThreadList;
using apib::URLInfo;

namespace {

static char KeyPath[512];
static char CertPath[512];
static int testServerPort;
static apib::TestServer testServer;

class TLSTest : public ::testing::Test {
 protected:
  TLSTest() {
    apib::RecordInit("", "");
    testServer.resetStats();
  }
  ~TLSTest() {
    // The "url_" family of functions use static data, so reset every time.
    URLInfo::Reset();
    apib::EndReporting();
  }

  ThreadList threads;
};

static void compareReporting() {
  apib::TestServerStats stats = testServer.stats();
  BenchmarkResults results = ReportResults();

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
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->sslCtx = setUpTLS();

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(TLSTest, NoKeepAlive) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->noKeepAlive = 1;
  t->httpVerb = "GET";
  t->sslCtx = setUpTLS();

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(TLSTest, Larger) {
  char url[128];
  sprintf(url, "https://localhost:%i/data?size=8000", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->sslCtx = setUpTLS();

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(TLSTest, VerifyPeerFailing) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->sslCtx = setUpTLS();
  SSL_CTX_set_verify(t->sslCtx, SSL_VERIFY_PEER, NULL);

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  BenchmarkResults results = ReportResults();
  EXPECT_EQ(0, results.successfulRequests);
  EXPECT_LT(0, results.socketErrors);
}

TEST_F(TLSTest, VerifyPeerSuccess) {
  char url[128];
  sprintf(url, "https://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->sslCtx = setUpTLS();
  SSL_CTX_set_verify(t->sslCtx, SSL_VERIFY_PEER, NULL);
  ASSERT_EQ(1, SSL_CTX_load_verify_locations(t->sslCtx, CertPath, NULL));

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
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

  RSA* key = apib::keygen_MakeRSAPrivateKey(2048);
  assert(key != NULL);
  X509* cert = apib::keygen_MakeServerCertificate(key, 1, 1);
  assert(cert != NULL);
  int err = apib::keygen_SignCertificate(key, cert);
  assert(err == 0);
  err = apib::keygen_WriteRSAPrivateKey(key, KeyPath);
  assert(err == 0);
  err = apib::keygen_WriteX509Certificate(cert, CertPath);
  assert(err == 0);
  RSA_free(key);
  X509_free(cert);

  err = testServer.start("127.0.0.1", 0, KeyPath, CertPath);
  if (err != 0) {
    fprintf(stderr, "Can't start test server: %i\n", err);
    return 2;
  }
  testServerPort = testServer.port();

  int r = RUN_ALL_TESTS();

  testServer.stop();

  return r;
}
