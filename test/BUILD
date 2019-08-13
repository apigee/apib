cc_library(
    name = "testserver_lib",
    srcs = ["test_server.c"],
    hdrs = ["test_server.h"],
    linkopts = ["-lpthread"],
    deps = [
        "//src:common",
    ],
)

cc_binary(
    name = "testserver",
    srcs = ["test_server_main.c"],
    deps = [
        ":testserver_lib",
    ],
)

cc_test(
    name = "priorityq",
    srcs = ["priorityq_test.cc"],
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "lines",
    srcs = ["lines_test.cc"],
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "urls",
    srcs = ["url_test.cc"],
    data = [
        ":data/urls.txt",
    ],
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "message",
    srcs = ["message_test.cc"],
    data = [
        ":data/bodyrequest.txt",
        ":data/chunkedresponse.txt",
        ":data/getrequest.txt",
        ":data/response.txt",
    ],
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "testserver_test",
    srcs = ["test_server_test.cc"],
    deps = [
        ":testserver_lib",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "iotest",
    srcs = ["io_test.cc"],
    deps = [
        ":testserver_lib",
        "//src:io",
        "@gtest",
    ]
)