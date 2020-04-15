add_library(
  http_parser
  http_parser.c
  http_parser.h
)
target_compile_definitions(
  http_parser PUBLIC
  HTTP_PARSER_STRICT=0
)