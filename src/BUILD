package(default_visibility = ["//visibility:public"])

config_setting(
    name = "apple",
    constraint_values = [
        "@platforms//os:osx",
    ],
)

cc_library(
    name = "common",
    srcs = [
        "apib_lines.c",
        "apib_message.c",
        "apib_priorityq.c",
        "apib_reporting.c",
        "apib_time.c",
        "apib_url.c",
    ] + select({
        "apple": [
            "apib_cpu_generic.c",
            "apib_rand.c",
        ],
        "//conditions:default": [
            "apib_cpu_proc.c",
            "apib_rand_48.c",
        ],
    }),
    hdrs = [
        "apib_cpu.h",
        "apib_lines.h",
        "apib_message.h",
        "apib_priorityq.h",
        "apib_rand.h",
        "apib_reporting.h",
        "apib_time.h",
        "apib_url.h",
    ],
    copts = [
        "-std=gnu99",
    ],
    deps = [
        "@httpparser",
    ],
)

cc_library(
    name = "io",
    srcs = [
        "apib_io_basic.c",
        "apib_io_socket.c",
        "apib_iothread.c",
        "apib_oauth.c",
    ],
    hdrs = [
        "apib_iothread.h",
        "apib_oauth.h",
    ],
    linkopts = [
        "-lpthread",
    ],
    deps = [
        ":common",
        "//third_party:base64",
        "@boringssl//:crypto",
        "@boringssl//:ssl",
        "@httpparser",
        "@libev",
    ],
)

cc_binary(
    name = "apib",
    srcs = [
        "apib_main.c",
    ],
    deps = [
        ":io",
        "//third_party:base64",
    ],
)
