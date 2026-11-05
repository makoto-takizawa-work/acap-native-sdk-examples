# ACAP アプリのアップデート時に受信するシグナルの調査結果

## 目的

ACAP アプリの停止・アップデート時に、アプリがどのシグナルを受信するかを確認する。

## 方法

`hello-world` をベースにしたアプリ `signal_logger` を作成した。

- 捕捉可能なシグナル（SIGKILL / SIGSTOP と、SIGSEGV などの同期フォルト系を除く）をすべてブロックし、
  `sigwaitinfo()` で受信する（`app/signal_logger.c`）。
- 受信ごとに、シグナル番号・名前・`si_code`・送信元の pid / uid / プロセス名（`/proc/<pid>/comm`）を
  syslog に出力する。
- SIGTERM / SIGINT / SIGQUIT を受信したら終了し、それ以外は記録だけして待ち続ける。
- `runMode` は `respawn`。

## 環境

| 項目 | 値 |
|---|---|
| デバイス | AXIS Q1728 |
| アーキテクチャ | aarch64 |
| ACAP Native SDK | 12.11.0 |
| 実施日 | 2026-11-05 |

## 手順

1. `signal_logger` 1.0.0 をビルド・デプロイし、起動する。
2. `manifest.json` のバージョンを 1.0.1 に上げてビルドし、起動中のアプリへ上書きデプロイする。
3. `bash scripts/view_log.sh signal_logger` でログを確認する。

## 結果（アップデート: 1.0.0 → 1.0.1）

```
2026-11-05T15:07:21.275+09:00 axis-q1728 [ INFO    ] signal_logger[2075547]: Hello World
2026-11-05T15:07:21.277+09:00 axis-q1728 [ INFO    ] signal_logger[2075547]: Started: pid=2075547 ppid=1, waiting for signals
2026-11-05T15:07:35.051+09:00 axis-q1728 [ INFO    ] signal_logger[2075547]: Received signal 15 (SIGTERM: Terminated) code=0 [SI_USER (kill)] sender_pid=1 sender_uid=0 sender_comm=systemd
2026-11-05T15:07:35.052+09:00 axis-q1728 [ INFO    ] signal_logger[2075547]: Exiting on signal 15
2026-11-05T15:07:50.876+09:00 axis-q1728 [ INFO    ] signal_logger[2076462]: Hello World
2026-11-05T15:07:50.877+09:00 axis-q1728 [ INFO    ] signal_logger[2076462]: Started: pid=2076462 ppid=1, waiting for signals
```

- アップデート時に受信したシグナルは **SIGTERM（15）の 1 回のみ**。
- 送信元は **systemd（pid 1、uid 0）** で、`kill()` による送信（`si_code = SI_USER`）。
- SIGHUP などのほかのシグナルは届かなかった。
- SIGTERM を受けて即座に終了したため、SIGKILL は送られていない（SIGKILL は捕捉できないので、送られてもログには出ない）。
- 旧プロセスの終了から新バージョンの起動まで約 15 秒。新バージョンは `start` を実行しなくても自動で起動した。

## 補足

- 同じ pid で「Hello World」が 2 回出力されることがある。syslog と `printf` の両方で出力しており、
  stdout も syslog に取り込まれるためで、不具合ではない。
- アプリの停止（`control.sh stop`）と再起動（`control.sh restart`）は未確認。
