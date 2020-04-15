add_library(
  testserver_lib OBJECT
  test_server.cc
  test_server.h
)
target_link_libraries(testserver_lib PUBLIC common -lev -lssl.1.1 -lcrypto.1.1 -lhttp_parser)

add_library(
  keygen_lib OBJECT
  test_keygen.cc
  test_keygen.h
)
target_link_libraries(keygen_lib PUBLIC common -lssl.1.1 -lcrypto.1.1)

add_executable(
  testserver
  test_server_main.cc
)
target_link_libraries(testserver testserver_lib common)

add_executable(
  iotest
  io_test.cc
)
add_dependencies(iotest gtest)
target_link_libraries(iotest testserver_lib io cpu common base64 -lgtest)
add_test(iotest iotest)

add_executable(
  tlstest
  tls_test.cc
)
add_dependencies(tlstest gtest)
target_link_libraries(tlstest testserver_lib io cpu common keygen_lib base64 -lgtest)
add_test(tlstest tlstest)
