cc_library(
    name = "testserver_lib",
    srcs = ["test_server.c"],
    hdrs = ["test_server.h"],
    deps = [
        "//src:common",
        "@boringssl//:ssl",
        "@httpparser",
    ],
)

cc_binary(
    name = "testserver",
    srcs = ["test_server_main.c"],
    deps = [
        ":testserver_lib",
    ],
)

cc_library(
    name = "keygen_lib",
    srcs = [
        "test_keygen.c",
    ],
    hdrs = [
        "test_keygen.h",
    ],
    deps = [
        "@boringssl//:ssl",
    ]
)

cc_binary(
    name = "keygen",
    srcs = [
        "test_keygen_main.c",
    ],
    deps = [
        ":keygen_lib"
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
    name = "cpu",
    srcs = ["cpu_test.cc"],
    deps = [
        "//src:common",
        "@gtest",
    ]
)

cc_test(
    name = "reporting",
    srcs = ["reporting_test.cc"],
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main",
    ]
)

cc_test(
    name = "oauth",
    srcs = ["oauth_test.cc"],
    deps = [
        "//src:io",
        "@gtest",
        "@gtest//:gtest_main",
    ]
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

cc_test(
    name = "tlstest",
    srcs = ["tls_test.cc"],
    deps = [
        ":keygen_lib",
        ":testserver_lib",
        "//src:io",
        "@gtest",
    ]
)