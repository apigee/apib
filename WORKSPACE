load(
    "@bazel_tools//tools/build_defs/repo:http.bzl",
    "http_archive",
)
load(
    "@bazel_tools//tools/build_defs/repo:git.bzl",
    "git_repository",
    "new_git_repository",
)

# This version contains a patch that fixes things on FreeBSD.
git_repository(
   name = "rules_foreign_cc",
   commit = "38358597f9380e9098eb5642169ad23c169df98e",
   remote = "https://github.com/gbrail/rules_foreign_cc.git",
   shallow_since = "1586453104 -0700"
)
load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies([])

# Group the sources of the library so that make rule have access to it
all_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

http_archive(
    name = "gtest",
    sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
    strip_prefix = "googletest-release-1.10.0",
    urls = ["https://github.com/google/googletest/archive/release-1.10.0.tar.gz"],
)

http_archive(
    name = "libev",
    sha256 = "507eb7b8d1015fbec5b935f34ebed15bf346bed04a11ab82b8eee848c4205aea",
    strip_prefix = "libev-4.33",
    build_file_content = all_content,
    urls = ["http://dist.schmorp.de/libev/Attic/libev-4.33.tar.gz"],
)

http_archive(
    name = "httpparser",
    build_file = "@//:httpparser.build",
    sha256 = "467b9e30fd0979ee301065e70f637d525c28193449e1b13fbcb1b1fab3ad224f",
    strip_prefix = "http-parser-2.9.4",
    urls = ["https://github.com/nodejs/http-parser/archive/v2.9.4.tar.gz"],
)

# Take a commit from the "build-with-bazel" branch
git_repository(
    name = "boringssl",
    commit = "24193678fd35f7f4f8b9be216cc4e7a76f056081",
    remote = "https://boringssl.googlesource.com/boringssl",
    shallow_since = "1586447192 +0000"
)

http_archive(
    name = "absl",
    sha256 = "0db0d26f43ba6806a8a3338da3e646bb581f0ca5359b3a201d8fb8e4752fd5f8",
    strip_prefix = "abseil-cpp-20200225.1",
    urls = ["https://github.com/abseil/abseil-cpp/archive/20200225.1.tar.gz"],
)