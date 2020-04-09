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
        "addresses.cc",
        "apib_lines.cc",
        "apib_rand.cc",
        "apib_time.cc",
        "apib_url.cc",
        "apib_util.cc",
        "status.cc",
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
        "addresses.h",
        "apib_cpu.h",
        "apib_lines.h",
        "apib_rand.h",
        "apib_time.h",
        "apib_url.h",
        "apib_util.h",
        "status.h",
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
        "apib_iothread.cc",
        "apib_oauth.cc",
        "apib_reporting.cc",
        "socket.cc",
        "tlssocket.cc",
    ],
    hdrs = [
        "apib_commandqueue.h",
        "apib_iothread.h",
        "apib_oauth.h",
        "apib_reporting.h",
        "socket.h",
        "tlssocket.h",
    ],
    linkopts = [
        "-lpthread",
    ],
    deps = [
        ":common",
        "//third_party:base64",
        "//third_party/libev",
        "@absl//absl/strings:str_format",
        "@boringssl//:crypto",
        "@boringssl//:ssl",
        "@httpparser",
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
        "//third_party/libev",
        "@absl//absl/strings",
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
