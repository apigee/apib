package(default_visibility = ["//visibility:public"])

load("@rules_foreign_cc//foreign_cc:defs.bzl", "configure_make")

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
  env = {
    "AR": "",
  },
  # Bazel likes to use the shared libraries for tests
  out_static_libs = ["libev.a"],
)
