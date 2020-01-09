cc_library(
    name = "testserver_lib",
    srcs = ["test_server.cc"],
    hdrs = ["test_server.h"],
    deps = [
        "//apib:common",
        "@boringssl//:ssl",
        "@httpparser",
        "@libev",
    ],
)

cc_binary(
    name = "testserver",
    srcs = ["test_server_main.cc"],
    deps = [
        ":testserver_lib",
    ],
)

cc_library(
    name = "keygen_lib",
    srcs = [
        "test_keygen.cc",
    ],
    hdrs = [
        "test_keygen.h",
    ],
    deps = [
        "//apib:common",
        "@boringssl//:ssl",
    ],
)

cc_binary(
    name = "keygen",
    srcs = [
        "test_keygen_main.cc",
    ],
    deps = [
        ":keygen_lib",
    ],
)

cc_test(
    name = "commandqueue",
    srcs = ["commandqueue_test.cc"],
    deps = [
        "//apib:io",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "lines",
    srcs = ["lines_test.cc"],
    deps = [
        "//apib:common",
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
        "//apib:common",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "cpu",
    srcs = ["cpu_test.cc"],
    deps = [
        "//apib:common",
        "@gtest",
    ],
)

cc_test(
    name = "reporting",
    srcs = ["reporting_test.cc"],
    deps = [
        "//apib:io",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "oauth",
    srcs = ["oauth_test.cc"],
    deps = [
        "//apib:io",
        "@gtest",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "iotest",
    srcs = ["io_test.cc"],
    deps = [
        ":testserver_lib",
        "//apib:io",
        "@gtest",
    ],
)

cc_test(
    name = "tlstest",
    srcs = ["tls_test.cc"],
    deps = [
        ":keygen_lib",
        ":testserver_lib",
        "//apib:io",
        "@gtest",
    ],
)

cc_test(
    name = "montest",
    srcs = ["mon_test.cc"],
    deps = [
        "//apib:mon_lib",
        "@gtest",
    ],
)

cc_test(
    name = "util",
    srcs = ["util_test.cc"],
    deps = [
        "//apib:common",
        "@gtest",
        "@gtest//:gtest_main",
    ]
)