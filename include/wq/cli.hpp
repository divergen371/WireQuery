#pragma once

#include "wq/options.hpp"

namespace wq
{
void print_usage(const char *prog);

bool parse_args(int argc, char **argv, Options &opt);
} // namespace wq
