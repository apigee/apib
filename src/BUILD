package(default_visibility = ["//visibility:public"])

config_setting(
    name = "apple",
    constraint_values = [
        "@platforms//os:osx",
    ],
)

config_setting(
    name = "bsd",
    constraint_values = [
        "@platforms//os:freebsd",
    ],
)

cc_library(
    name = "common",
    srcs = [
        "apib_lines.cc",
        "apib_rand.cc",
        "apib_time.cc",
        "apib_url.cc",
        "apib_util.cc",
    ] + select({
        "apple": [
            "apib_cpu_generic.cc",
        ],
        "bsd": [
            "apib_cpu_bsd.cc",
        ],
        "//conditions:default": [
            "apib_cpu_proc.cc",
        ],
    }),
    hdrs = [
        "apib_cpu.h",
        "apib_lines.h",
        "apib_rand.h",
        "apib_reporting.h",
        "apib_time.h",
        "apib_url.h",
        "apib_util.h",
    ],
    linkopts = [
    	"-lm",
    ],
    deps = [
        "@absl//absl/strings",
        "@httpparser",
    ],
)

cc_library(
    name = "io",
    srcs = [
        "apib_commandqueue.cc",
        "apib_io_basic.cc",
        "apib_io_socket.cc",
        "apib_iothread.cc",
        "apib_oauth.cc",
        "apib_reporting.cc",
    ],
    hdrs = [
        "apib_commandqueue.h",
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

cc_library(
    name = "mon_lib",
    srcs = [
        "apib_mon.cc",
    ],
    hdrs = [
        "apib_mon.h",
    ],
    deps = [
        ":common",
        "@absl//absl/strings",
        "@libev",
    ],
    linkopts = [
        "-lpthread",
    ],
)

cc_binary(
    name = "apib",
    srcs = [
        "apib_main.cc",
    ],
    deps = [
        ":io",
        "//third_party:base64",
    ],
)

cc_binary(
    name = "apibmon",
    srcs = [
        "apib_mon_main.cc",
    ],
    deps = [
        ":mon_lib",
    ],
)
