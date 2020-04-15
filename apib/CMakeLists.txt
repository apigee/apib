add_library(
  common
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
target_link_libraries(common http_parser absl::strings absl::str_format)

add_library(
  cpu 
  apib_cpu_generic.cc
)
target_link_libraries(cpu common)

# TODO apib_cpu_bsd for bsd, apib_cpu_proc for linux

add_library(
  io 
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
target_link_libraries(io cpu common base64 -lev -lssl -lcrypto)

add_library(
  mon_lib 
  apib_mon.cc
  apib_mon.h
)
target_link_libraries(mon_lib io)

add_executable(
  apib
  apib_main.cc
)
target_link_libraries(apib io)

add_executable(
  apibmon
  apib_mon_main.cc
)
target_link_libraries(apibmon mon_lib io)

