add_library(
  common OBJECT
  addresses.cc
  apib_lines.cc
  apib_rand.cc
  apib_time.cc
  apib_url.cc
  apib_util.cc
  status.cc
  addresses.h
  apib_cpu.h
  apib_lines.h
  apib_rand.h
  apib_time.h
  apib_url.h
  apib_util.h
  status.h
)
target_link_libraries(common PUBLIC -lhttp_parser
 -labsl_strings -labsl_str_format_internal -labsl_strings_internal 
 -labsl_int128 -labsl_throw_delegate -labsl_raw_logging_internal -labsl_base)

add_library(
  cpu OBJECT
  apib_cpu_generic.cc
)
target_link_libraries(cpu PUBLIC common)

# TODO apib_cpu_bsd for bsd, apib_cpu_proc for linux

add_library(
  io OBJECT
  apib_commandqueue.cc
  apib_io_basic.cc
  apib_iothread.cc
  apib_oauth.cc
  apib_reporting.cc
  socket.cc
  tlssocket.cc
  apib_commandqueue.h
  apib_iothread.h
  apib_oauth.h
  apib_reporting.h
  socket.h
  tlssocket.h
)
target_link_libraries(io PUBLIC common base64 -lev -lssl.1.1 -lcrypto.1.1)

add_library(
  mon_lib OBJECT
  apib_mon.cc
  apib_mon.h
)
target_link_libraries(mon_lib PUBLIC io)

add_executable(
  apib
  apib_main.cc
)
target_link_libraries(apib io cpu common base64)

add_executable(
  apibmon
  apib_mon_main.cc
)
target_link_libraries(apibmon mon_lib io cpu common base64)

