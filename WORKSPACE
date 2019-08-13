load(
	"@bazel_tools//tools/build_defs/repo:http.bzl",
	"http_archive"
)

http_archive(
	name = "gtest",
	urls = ["https://github.com/google/googletest/archive/release-1.8.1.tar.gz"],
	sha256 = "9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c",
	strip_prefix = "googletest-release-1.8.1"
)

http_archive(
	name = "libev",
	urls = ["http://dist.schmorp.de/libev/libev-4.27.tar.gz"],
	sha256 = "2d5526fc8da4f072dd5c73e18fbb1666f5ef8ed78b73bba12e195cfdd810344e",
	strip_prefix = "libev-4.27",
	build_file = "@//:libev.build"
)