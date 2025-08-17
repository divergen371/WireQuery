# WireQuery - DNS Resolver & Raw DNS Query Tool (C++23)

軽量な DNS 解析・計測ツールです。`getaddrinfo()` による順引き、任意で
`getnameinfo()` による PTR 逆引き、複数回トライの計測、並列化、結果の重複排除、JSON
集約出力、NDJSON ストリーミング出力、パーセンタイル統計をサポートします。

- 主要ソース: `main.cpp`
- ビルド設定・テスト: `CMakeLists.txt`
- テスト実行: CTest（`ctest`）

## 主な機能

- 複数回トライと計測（最小/平均/最大）
- 並列解決（`--concurrency` / `--parallel`）
- アドレスファミリ/ソケット種別/プロトコル/サービス指定
- オプションフラグ（`AI_ADDRCONFIG`/`AI_CANONNAME`/`AI_ALL`/`AI_V4MAPPED`/
  `AI_NUMERICHOST`）
- 逆引き（PTR）と `NI_NAMEREQD` 指定
- 結果の重複排除（同一 af/ip/socktype/protocol/port）
- JSON 集約出力（1ドキュメント）
- NDJSON ストリーミング出力（試行ごと 1 行）
- パーセンタイル統計（例: p50/p90/p99）

## 必要環境

- macOS（他 Unix 系でも移植容易）
- C++23
- 推奨コンパイラ: Homebrew LLVM clang++（`std::print` 利用のため）

## ビルド

### CMake

```bash
cmake -S . -B build
cmake --build build -j 4
```

> 環境により `CMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++`
> を指定することを推奨します。

### 直接コンパイル（参考）

```bash
/opt/homebrew/opt/llvm/bin/clang++ -std=c++23 -stdlib=libc++ main.cpp -o main
```

## 使い方

```
DNS resolver / timing tool
Usage: ./wireq [options] <hostname>
Options:
  --tries N          Number of resolution attempts (default: 3)
  --concurrency K    Number of parallel lookups (default: 1)
  --parallel K       Alias of --concurrency
  --family F         Address family: any|inet|inet6 (default: any)
  -4                 Shortcut for --family inet
  -6                 Shortcut for --family inet6
  --service S        Service name or port (e.g., 80, http)
  --socktype T       stream|dgram|raw (default: any)
  --protocol P       tcp|udp (default: any)
  --[no-]addrconfig  Toggle AI_ADDRCONFIG (default: on)
  --[no-]canonname   Toggle AI_CANONNAME (default: on)
  --all              AI_ALL (IPv6 + V4MAPPED only)
  --v4mapped         AI_V4MAPPED
  --numeric-host     AI_NUMERICHOST (no DNS query)
  --reverse          Do reverse (PTR) lookups for results
  --ni-namereqd      Use NI_NAMEREQD for reverse (require name)
  --json             Output results in JSON format
  --ndjson           Output each attempt as a single JSON line (NDJSON)
  --pctl LIST        Comma-separated percentiles (e.g., 50,90,99)
  --dedup            Fold duplicate results per attempt
  -h, --help         Show this help
```

## 出力フォーマット

### テキスト

- 各試行のアドレス一覧、PTR 結果、`try N: X ms - M address(es)`
- 終了時に `summary: min=.. ms, avg=.. ms, max=.. ms (N tries)`
- `--pctl` 指定時は `percentiles: p50=.., p90=.., ...` を追加

### JSON（集約）

- 実行終了時に 1 つの JSON ドキュメントを出力
- 構造（抜粋）:

```json
{
  "host": "localhost",
  "family": "any|inet|inet6",
  "tries": 3,
  "service": "",
  "socktype": "any|stream|dgram|raw",
  "protocol": "any|tcp|udp",
  "flags": {
    "addrconfig": true,
    "canonname": true,
    "all": false,
    "v4mapped": false,
    "numeric_host": false
  },
  "reverse": false,
  "ni_namereqd": false,
  "concurrency": 1,
  "dedup": false,
  "summary": {
    "min_ms": 0,
    "avg_ms": 0.0,
    "max_ms": 0,
    "count": 3
  },
  "percentiles": {
    "p50": 0,
    "p90": 1
  },
  // --pctl 指定時のみ
  "attempts": [
    {
      "try": 1,
      "ms": 0,
      "rc": 0,
      "canon": "localhost",
      // あれば
      "addresses": [
        {
          "family": "inet",
          "ip": "127.0.0.1",
          "socktype": "stream",
          "protocol": "tcp",
          "port": 0
        }
      ],
      "ptr": [
        // --reverse 時のみ
        {
          "family": "inet",
          "ip": "127.0.0.1",
          "rc": 0,
          "name": "localhost"
        }
      ]
    }
  ]
}
```

### NDJSON（ストリーミング）

- 試行ごとに 1 行の JSON を即時出力します（並列でも整形出力をミューテックスで保護）。
- 成功例:

```json
{
  "try": 1,
  "ms": 12,
  "rc": 0,
  "canon": "localhost",
  "addresses": [
    {
      "family": "inet",
      "ip": "127.0.0.1",
      "socktype": "stream",
      "protocol": "tcp",
      "port": 0
    }
  ]
}
```

- エラー例:

```json
{
  "try": 2,
  "ms": 5,
  "rc": -2,
  "error": "Name or service not known"
}
```

- NDJSON モードでは、ヘッダ表示や集約 JSON/テキストのサマリ出力は行いません。

## 例

```bash
# 基本
./wireq example.com

# JSON 集約
./wireq --json --tries 3 example.com

# NDJSON ストリーミング
./wireq --ndjson --tries 3 example.com

# パーセンタイル（テキスト/JSON 集約に反映）
./wireq --pctl 50,90,99 --tries 7 example.com

# IPv4 強制 + 逆引き + 重複排除
./wireq -4 --reverse --dedup --tries 2 localhost

# サービス/ポート指定（80/TCP）
./wireq --service 80 --protocol tcp --tries 1 127.0.0.1
```

## パーセンタイルの定義

- 近傍順位（Nearest-rank）法を使用: `rank = ceil(p/100 * n)`、`p∈[0,100]`、`n` は試行数。
- 値は昇順ソート後の `rank` 番目の値（1 始まり）。

## 終了コード

- 0: 正常終了（試行内エラーがあってもサマリ出力まで到達すれば 0）
- 1: 使い方エラー（引数不正/ホスト未指定 など）

## テスト

- CTest による自動テストを同梱しています。

```bash
cmake -S . -B build-tests && cmake --build build-tests -j 4
ctest --test-dir build-tests -j 4 --output-on-failure
```

## ライセンス

- 未設定
