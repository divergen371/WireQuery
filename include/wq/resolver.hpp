#pragma once

#include "wq/options.hpp"
#include "wq/model.hpp"

namespace wq
{
// POSIX getaddrinfo ベースの1回分解決を実行し、測定値と結果を返す
// - opt.dedup / opt.reverse / opt.ni_namereqd などの挙動を内部で反映
// - rc != 0 の場合、error に gai_strerror(rc) を格納
AttemptResult resolve_posix_once(const Options &opt);
} // namespace wq
