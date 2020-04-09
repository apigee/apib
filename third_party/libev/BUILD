package(default_visibility = ["//visibility:public"])

load("@rules_foreign_cc//tools/build_defs:configure.bzl", "configure_make")

configure_make(
  name = "libev",
  lib_source = "@libev//:all",
  configure_options = [
    "--disable-dependency-tracking",
    "--enable-static",
    "--disable-shared",
  ],
  # Without this for some reason Bazel tries to send
  # ar commands to libtool!
  configure_env_vars = {
    "AR": "",
  },
  # Bazel likes to use the shared libraries for tests
  static_libraries = ["libev.a"],
)
