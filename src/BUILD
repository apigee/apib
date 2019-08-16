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
        "apib_cpu_generic.c",
        "apib_lines.c",
        "apib_message.c",
        "apib_priorityq.c",
        "apib_time.c",
        "apib_url.c",
    ] + select({
        "apple": ["apib_rand.c"],
        "//conditions:default": ["apib_rand_48.c"],
    }),
    hdrs = [
        "apib_cpu.h",
        "apib_lines.h",
        "apib_message.h",
        "apib_priorityq.h",
        "apib_rand.h",
        "apib_time.h",
        "apib_url.h",
    ],
)

cc_library(
    name = "io",
    srcs = [
        "apib_io_clear.c",
        "apib_iothread.c",
        "apib_reporting.c",
    ],
    hdrs = [
        "apib_iothread.h",
        "apib_reporting.h",
    ],
    linkopts = [
        "-lpthread",
    ],
    deps = [
        ":common",
        "@httpparser",
        "@libev",
    ],
)
