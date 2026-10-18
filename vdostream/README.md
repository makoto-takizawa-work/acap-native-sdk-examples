*Copyright (C) 2021, Axis Communications AB, Lund, Sweden. All Rights Reserved.*

# vdostream — VDO によるフレーム取得と larod 前処理（クロップ・色変換）

## 概要

`vdoencodeclient` は [Video capture API (VDO)](https://developer.axis.com/acap/api/native-sdk-api/#video-capture-api-vdo)
でカメラからフレームを取得し、ファイル（または標準出力）へ書き出す ACAP アプリケーションです。

- VDO から av1 / avif / h264 / h265 / jpeg / nv12 / rgb / y800 の各形式でフレームを取得できます。
- ストリームの解像度を引数（`--width` / `--height`）で指定できます。
- `nv12` / `rgb` のフレームは、[larod](https://developer.axis.com/acap/api/native-sdk-api/#machine-learning-api-larod)
  の前処理（pre-processing）ジョブを通して、**指定領域のクロップ** と **出力形式の変換** ができます。
  前処理デバイスは `cpu-proc`（libyuv）と `a9-gpu-proc`（OpenCL）から選べます。

前処理は `--crop` または `--output-format` を指定したときだけ有効になります。どちらも指定しなければ、
VDO から受け取ったフレームをそのまま書き出す従来の動作です。

処理の流れ（前処理あり）:

```text
VDO (nv12 / rgb, W×H) ──► larod 前処理ジョブ ──► 出力ファイル
                           image.input.crop = X,Y,CW,CH
                           出力 = CW×CH, --output-format
```

## ファイル構成

```text
vdostream
├── app
│   ├── LICENSE
│   ├── Makefile             アプリのコンパイル（vdostream, liblarod をリンク）
│   ├── manifest.json.<fmt>  起動時の既定フォーマットごとの manifest（av1, avif, h264, h265, jpeg, nv12, rgb, y800）
│   ├── panic.c / panic.h    エラー時に syslog へ出力して終了
│   ├── preprocess.c / .h    larod 前処理（クロップ・色変換・処理時間計測）
│   └── vdoencodeclient.c    main（引数解析、VDO ストリーム、フレーム書き出し）
├── Dockerfile               マルチステージビルド（builder → export）
├── Makefile                 Docker ビルドを実行し build/ に .eap を出力
└── README.md
```

## 使用方法

### ビルド

```sh
make build                                  # aarch64, manifest.json.h264
make VDO_FORMAT=rgb build                   # manifest.json.rgb を使用
make ARCH=armv7hf VDO_FORMAT=nv12 build     # armv7hf 向け
make clean                                  # build/ を削除
```

| 変数 | 既定値 | 内容 |
|---|---|---|
| `ARCH` | `aarch64` | `aarch64` または `armv7hf` |
| `VERSION` | `12.11.0` | ACAP Native SDK のバージョン |
| `VDO_FORMAT` | `h264` | 使用する `app/manifest.json.<VDO_FORMAT>`（Web UI から起動したときの `runOptions` が変わる） |

成果物は `build/vdoencodeclient_<VDO_FORMAT>_1_0_0_<ARCH>.eap` です。どの manifest でもバイナリは同じで、
引数を変えればすべての機能を使えます。

> [!NOTE]
> Apple Silicon の Mac では `InvalidBaseImagePlatform` の警告が出ますが、SDK イメージが amd64 専用で
> エミュレーション実行されるためで、生成されるバイナリは正しい aarch64 です。

### インストールと起動

1. デバイスの Web UI（`http://<AXIS_DEVICE_IP>/index.html#apps`）で **Allow unsigned apps** を有効にし、
   `.eap` をアップロードしてインストールします。
2. manifest の `runMode` は `never` なので、自動では起動しません。Web UI の **Start** で、manifest の
   `runOptions`（例: `--format rgb --frames 25`）で一度だけ実行されます。
3. 任意の引数で実行する場合は、デバイスに SSH でログインしてインストール先のバイナリを直接実行します。

```sh
/usr/local/packages/vdoencodeclient/vdoencodeclient --format nv12 --frames 10 \
    -W 1280 -H 720 --crop 400,200,320,240 --output /tmp/crop.rgb
```

ログは `http://<AXIS_DEVICE_IP>/axis-cgi/admin/systemlog.cgi?appname=vdoencodeclient` で確認できます。

### 引数

| 引数 | 短縮形 | 既定値 | 内容 |
|---|---|---|---|
| `--format` | `-t` | `h264` | VDO の形式: av1, avif, h264, h265, jpeg, nv12, rgb, y800 |
| `--frames` | `-n` | 無制限 | 取得するフレーム数（Ctrl-C で中断） |
| `--output` | `-o` | `/dev/null` | 出力ファイル。`/dev/stdout` も可 |
| `--width` | `-W` | `640` | ストリームの幅 |
| `--height` | `-H` | `360` | ストリームの高さ |
| `--crop` | `-c` | なし | クロップ領域 `X,Y,W,H`（前処理を有効化） |
| `--output-format` | `-f` | `rgb-interleaved` | 前処理の出力形式: rgb-interleaved, rgb-planar, nv12（前処理を有効化） |
| `--pp-device` | `-d` | `cpu-proc` | 前処理デバイス: cpu-proc, a9-gpu-proc |

前処理に関する決まり:

- `--format` は `nv12` か `rgb` である必要があります（それ以外はエラー終了）。
- 出力サイズはクロップサイズ（`--crop` がなければストリームサイズ）で、スケーリングはしません。
- クロップ領域はフレーム内に収まり、4 値とも偶数である必要があります（libyuv と nv12 の制約）。
- 前処理が有効なときは `--frames 1` でもスナップショット API ではなくストリームから取得します。
- 終了時に、前処理ジョブ（`larodRunJob()`）の処理時間の統計をログに出力します。

```text
Preprocessing on cpu-proc: nv12 1280x720 -> crop 400,200,320,240 -> rgb-interleaved 320x240 (230400 bytes)
...
Preprocessing time on cpu-proc: first 7.95 ms, then avg 5.45 ms, min 5.35 ms, max 5.83 ms over 99 jobs
```

### デバイスごとの対応組み合わせ

AXIS Q1728（ARTPEC-9）で実際に動かして確認した結果です。

| 入力 → 出力 | cpu-proc | a9-gpu-proc |
|---|---|---|
| nv12 → rgb-interleaved（クロップあり・なし） | ✔ | ✔ |
| nv12 → nv12（クロップ） | ✔ | ✔ |
| nv12 → rgb-planar（クロップ） | ✔ | ✘ `This backend only supports color conversion from NV12 to RGB interleaved` |
| rgb → rgb-interleaved（クロップ） | ✔ | ✘ `Crop-scale for the specified format is not supported by OpenCL backend` |

非対応の組み合わせは最初のジョブ実行時に larod がエラーを返し、メッセージを syslog に出して終了します。

### 出力の確認

出力はヘッダーのない raw 画像の連続です。ホストで `ffmpeg` を使って画像に変換できます（例: 320x240）。

```sh
ffmpeg -f rawvideo -pix_fmt rgb24 -s 320x240 -i crop.rgb -frames:v 1 crop.png   # rgb-interleaved
ffmpeg -f rawvideo -pix_fmt nv12  -s 320x240 -i crop.nv12 -frames:v 1 crop.png   # nv12
```

`rgb-planar` は R, G, B の順のプレーンです。`ffmpeg` の `gbrp` は G, B, R の順なので、プレーンを並べ替えてから
変換してください。

## cpu-proc と a9-gpu-proc の処理速度比較

### 測定条件

- デバイス: AXIS Q1728（ARTPEC-9）、ACAP Native SDK 12.11.0（aarch64）
- 入力: VDO nv12 ストリーム（30 fps）
- 測定対象: 1 フレームあたりの `larodRunJob()`（ブロッキング）の所要時間。テンソル登録などの初回コストを
  含む 1 ジョブ目は除き、続く 99 ジョブの平均を求めました。
- 各条件 3 回実行した平均の平均です（条件 A の cpu-proc は SSH 接続エラーの 1 回を除いた 2 回）。
  1 回ごとの平均のばらつきは ±0.3 ms 以内でした。
- 出力は `/dev/null`（ファイル書き出しのコストは含みません）。

### 結果

| # | ストリーム | 処理 | 出力 | cpu-proc 平均 [ms] | a9-gpu-proc 平均 [ms] | GPU / CPU |
|---|---|---|---|---:|---:|---:|
| B | 1920x1080 | クロップ 320x240 | rgb-interleaved | **1.48** | 3.51 | 2.4 倍 |
| F | 1920x1080 | クロップ 640x640 | nv12 | **3.03** | 11.07 | 3.7 倍 |
| A | 1920x1080 | クロップ 640x640 | rgb-interleaved | **5.45** | 7.95 | 1.5 倍 |
| E | 3840x2160 | クロップ 640x640 | rgb-interleaved | **5.98** | 12.78 | 2.1 倍 |
| G | 1280x720 | 全体を変換 | rgb-interleaved | **3.77** | 4.32 | 1.1 倍 |
| C | 1920x1080 | 全体を変換 | rgb-interleaved | **7.84** | 9.19 | 1.2 倍 |
| D | 3840x2160 | 全体を変換 | rgb-interleaved | **28.74** | 46.51 | 1.6 倍 |

初回ジョブ（参考）は、cpu-proc が 2〜72 ms、a9-gpu-proc が 4〜98 ms でした（条件 D が最大）。

### 考察

- **すべての条件で cpu-proc のほうが速い** 結果でした。全体を変換する条件（C, G）では差が 1.1〜1.2 倍と小さく、
  小さな領域をクロップする条件（B, E, F）では 2〜3.7 倍の差がつきました。
- cpu-proc のクロップの所要時間はほぼ **出力サイズ** で決まります（A: 1080p から 5.45 ms、E: 4K から 5.98 ms）。
  a9-gpu-proc は **入力サイズ** にも比例して増えます（A: 7.95 ms → E: 12.78 ms）。GPU 側はクロップ領域だけでなく
  入力バッファ全体を扱うコストがかかっていると考えられます。
- a9-gpu-proc の nv12 → nv12 クロップ（F）は、rgb への変換を含む A よりも遅くなりました。
- 今回測ったのは 1 ジョブの所要時間（レイテンシ）だけで、CPU 使用率は測っていません。a9-gpu-proc には、変換処理を
  CPU から逃がせる利点があります。推論など他の CPU 負荷と並行して動かす場合は、システム全体の負荷も含めて
  評価してください。

### 再現方法

デバイスで以下を実行し、ログの `Preprocessing time on ...` 行を確認します。

```sh
cd /usr/local/packages/vdoencodeclient
./vdoencodeclient --format nv12 -W 1920 -H 1080 --crop 640,220,640,640 \
    --frames 100 --pp-device cpu-proc --output /dev/null
./vdoencodeclient --format nv12 -W 1920 -H 1080 --crop 640,220,640,640 \
    --frames 100 --pp-device a9-gpu-proc --output /dev/null
```

## License

**[Apache License 2.0](../LICENSE)**
