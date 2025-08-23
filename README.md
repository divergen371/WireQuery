# WireQuery - DNS Resolver & Raw DNS Query Tool (C++23)

軽量な DNS 解析・計測ツールです。`getaddrinfo()` による順引き、任意で
`getnameinfo()` による PTR 逆引き、複数回トライの計測、並列化、結果の重複排除、JSON
集約出力、NDJSON ストリーミング出力、パーセンタイル統計をサポートします。

- 主要ソース: `main.cpp`
- ビルド設定・テスト: `CMakeLists.txt`
- テスト実行: CTest（`ctest`）

## 主な機能

- 複数回トライと計測（最小/平均/最大）
- サブミリ秒精度の計測（単位: ms、小数3桁で出力）
- 並列解決（`--concurrency` / `--parallel`）
- アドレスファミリ/ソケット種別/プロトコル/サービス指定
- オプションフラグ（`AI_ADDRCONFIG`/`AI_CANONNAME`/`AI_ALL`/`AI_V4MAPPED`/
  `AI_NUMERICHOST`）
- 逆引き（PTR）と `NI_NAMEREQD` 指定
- 結果の重複排除（同一 af/ip/socktype/protocol/port）
- JSON 集約出力（1ドキュメント）
- NDJSON ストリーミング出力（試行ごと 1 行）
- パーセンタイル統計（例: p50/p90/p99）
- 協調的キャンセル（`--cancel-on-error` / `Options::stop_on_error`）
- Raw DNS クエリ（`--type RR`）

## 必要環境

- macOS（他 Unix 系でも移植容易）
- C++23
- 推奨コンパイラ: Homebrew LLVM clang++（`std::print` 利用のため）
- 任意: ldns 1.8+ と OpenSSL（Raw DNS 機能を使う場合）

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
  --cancel-on-error  Short-circuit remaining tries when a try fails
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
  --ptr              Alias of --reverse
  --ni-namereqd      Use NI_NAMEREQD for reverse (require name)
  --json             Output results in JSON format
  --ndjson           Output each attempt as a single JSON line (NDJSON)
  --pctl LIST        Comma-separated percentiles for summary (e.g., 50,90,99)
  --dedup            Fold duplicate results per attempt
  --type RR          Raw DNS mode (ldns): A,AAAA,CNAME,NS,MX,TXT,SOA,CAA,SRV,DS,DNSKEY,PTR
  --ns SERVER        DNS server to query (IP or host)
  --rd on|off        Recursion Desired flag (default: on)
  --do on|off        DNSSEC DO flag (default: off)
  --timeout MS       Query timeout in milliseconds (default: 2000)
  --tcp              Force TCP transport (default: UDP with TCP fallback)
  -h, --help         Show this help
```

### 補足: オプションの指定方法

- __ロングオプションの指定__: `--opt value` と `--opt=value` の両方に対応しています（例: `--family inet6` または `--family=inet6`）。
- __ブール値の指定__: `--rd` / `--do` は `on|off` に加えて `true|false` / `1|0` も受け付けます。
- __境界値__: `--tries` と `--concurrency` は 1 未満が指定された場合に 1 に丸められます。
- __Raw DNS の名前解決サーバ__: `--ns` を省略した場合、システムの名前解決設定（例: `/etc/resolv.conf`）に基づくネームサーバを使用します。

## 出力フォーマット

### テキスト

- 各試行のアドレス一覧、PTR 結果、`try N: X.XXX ms - M address(es)`（ms は小数3桁）
- 終了時に `summary: min=.. . . ms, avg=.. . . ms, max=.. . . ms (N tries)`（3桁固定）
- `--pctl` 指定時は `percentiles: p50=.. . . , p90=.. . . , ...` を追加
- Raw DNS モード（`--type` 指定）では、各試行で `raw DNS rcode=<code> aa=<..> tc=<..> rd=<..> ra=<..> ad=<..> cd=<..> an=<count>` を表示します。

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
    "min_ms": 0.395,
    "avg_ms": 1.234,
    "max_ms": 3.210,
    "count": 3
  },
  "percentiles": {
    "p50": 0.924,
    "p90": 2.718
  },
  // --pctl 指定時のみ
  "attempts": [
    {
      "try": 1,
      "ms": 0.395,
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

- 注意: Raw DNS の詳細フィールド（`rcode` や `answers` など）は集約 JSON には含まれません。Raw DNS の詳細は NDJSON モードで提供されます。

### NDJSON（ストリーミング）

- 試行ごとに 1 行の JSON を即時出力します（並列でも整形出力をミューテックスで保護）。
- 成功例:

```json
{
  "try": 1,
  "ms": 12.345,
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
  "ms": 5.000,
  "rc": -2,
  "error": "Name or service not known"
}
```

- NDJSON モードでは、ヘッダ表示や集約 JSON/テキストのサマリ出力は行いません。
- ms は常に小数3桁で出力されます（例: 0.340, 12.000）。

#### Raw DNS（NDJSON 抜粋）

`--type RR` を指定すると Raw DNS 経路になり、NDJSON に `raw_dns` フィールドが追加されます（抜粋）。

```json
{
  "try": 1,
  "ms": 0.732,
  "rc": 0,
  "raw_dns": {
    "type": "A",
    "rcode": 0,
    "flags": {"aa": false, "tc": false, "rd": true, "ra": true, "ad": false, "cd": false}
  },
  "counts": {"answer": 4, "authority": 0, "additional": 0},
  "answers": ["example.com.  300 IN A 93.184.216.34", "..."]
}
```

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

# Raw DNS（A レコードを 8.8.8.8 に問い合わせ）
./wireq --type A --ns 8.8.8.8 example.com

# Raw DNS（TCP 強制 + DNSSEC DO + タイムアウト 1s）
./wireq --type AAAA --tcp --do on --timeout 1000 --ns 1.1.1.1 example.com

# 失敗時に残りトライを短絡キャンセル
./wireq --cancel-on-error --tries 5 --concurrency 4 example.com
```

## キャンセルと例外伝播

- __`--cancel-on-error` / `Options::stop_on_error`__
  - 有効時、各試行のコールバック（`on_try`）で例外が発生すると、その例外を最初の 1 件だけ伝播し、以降の試行を協調的にキャンセルします（"first exception wins"）。
  - 無効時（デフォルト）、`on_try` 内の例外はランナー側で捕捉・抑止し、すべての試行を継続します。
- __CLI の挙動__
  - 標準の CLI 実装では `on_try` は例外を投げない設計のため、通常は `--cancel-on-error` を付けても出力の見た目は変わりません（エラーは rc とメッセージとして扱われます）。
  - ライブラリとして組み込む場合や拡張で `on_try` が例外を投げうるときに、このフラグで短絡/継続の方針を切り替えられます。

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
