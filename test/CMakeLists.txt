add_library(
  testserver_lib
  test_server.cc
  test_server.h
)
target_link_libraries(testserver_lib io)

add_executable(
  testserver
  test_server_main.cc
)
target_link_libraries(testserver testserver_lib)

add_library(
  keygen_lib
  test_keygen.cc
  test_keygen.h
)
target_link_libraries(keygen_lib common -lcrypto)

add_executable(
  keygen_test
  test_keygen_main.cc
)
target_link_libraries(keygen_test keygen_lib gtest)
add_test(keygen_test keygen_test)

add_executable(
  commandqueue_test
  commandqueue_test.cc
)
target_link_libraries(commandqueue_test io gtest gtest_main)
add_test(commandqueue_test commandqueue_test)

add_executable(
  lines_test
  lines_test.cc
)
target_link_libraries(lines_test common gtest gtest_main)
add_test(lines_test lines_test)

add_executable(
  url_test
  url_test.cc
)
target_link_libraries(url_test common gtest gtest_main)
add_test(url_test url_test)

add_executable(
  cpu_test
  cpu_test.cc
)
target_link_libraries(cpu_test common cpu gtest)
add_test(cpu_test cpu_test)

add_executable(
  reporting_test
  reporting_test.cc
)
target_link_libraries(reporting_test io gtest gtest_main)
add_test(reporting_test reporting_test)

add_executable(
  oauth_test
  oauth_test.cc
)
target_link_libraries(oauth_test io gtest gtest_main)
add_test(oauth_test oauth_test)

add_executable(
  util_test
  util_test.cc
)
target_link_libraries(util_test common gtest gtest_main)
add_test(util_test util_test)

add_executable(
  mon_test
  mon_test.cc
)
target_link_libraries(mon_test mon_lib gtest)
add_test(mon_test mon_test)

add_executable(
  iotest
  io_test.cc
)
target_link_libraries(iotest testserver_lib gtest)
add_test(iotest iotest)

add_executable(
  tlstest
  tls_test.cc
)
target_link_libraries(tlstest testserver_lib keygen_lib gtest)
add_test(tlstest tlstest)
