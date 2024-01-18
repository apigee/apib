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
   commit = "c2e097455d2bbf92b2ae71611d1261ba79eb8aa8",
   remote = "https://github.com/bazelbuild/rules_foreign_cc",
)
load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

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

# Take a commit from the "build-with-bazel" branch
git_repository(
    name = "boringssl",
    commit = "02802f26d75830f8e3041aa210c3a9cd27cc94d4",
    remote = "https://boringssl.googlesource.com/boringssl",
)

git_repository(
    name = "absl",
    commit = "fb3621f4f897824c0dbe0615fa94543df6192f30",
    remote = "https://github.com/abseil/abseil-cpp",
)
