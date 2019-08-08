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
    deps = [
        "//src:common",
        "@gtest",
        "@gtest//:gtest_main"
    ],
    data = [
        ":data/urls.txt"
    ]
)