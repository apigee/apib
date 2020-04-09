load(
    "@bazel_tools//tools/build_defs/repo:http.bzl",
    "http_archive",
)
load(
    "@bazel_tools//tools/build_defs/repo:git.bzl",
    "git_repository",
    "new_git_repository",
)

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
    sha256 = "5199500e352584852c95c13423edc5f0cb329297c81dd69c3c8f52a75496da08",
    strip_prefix = "http-parser-2.9.2",
    urls = ["https://github.com/nodejs/http-parser/archive/v2.9.2.tar.gz"],
)

git_repository(
    name = "boringssl",
    commit = "e0c35d6c06fd800de1092f0b4d4326570ca2617a",
    remote = "https://boringssl.googlesource.com/boringssl",
    shallow_since = "1566966435 +0000",
)

http_archive(
    name = "absl",
    sha256 = "8100085dada279bf3ee00cd064d43b5f55e5d913be0dfe2906f06f8f28d5b37e",
    strip_prefix = "abseil-cpp-20190808",
    urls = ["https://github.com/abseil/abseil-cpp/archive/20190808.tar.gz"],
)