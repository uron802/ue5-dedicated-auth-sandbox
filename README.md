# ue5-dedicated-auth-sandbox

Unreal Engine 5.8 の検証用プロジェクト。PlayFab 認証と DTLS 暗号化を備えた Dedicated Server の入場ゲートを試す。

- `Plugins/GameAuth`: 認証用プラグイン(PlayFab 認証、サーバー側の入場ゲート、DTLS 通信暗号化)
- `Content/`: Blueprint とマップ(既定マップは `DemoMap`)
- `Docs/`: 構成、手順、検証結果([AccountAuthentication.md](Docs/AccountAuthentication.md))、部屋機能の設計方針([RoomDesign.md](Docs/RoomDesign.md)、未実装)

## 開発

- エンジン: UE 5.8
- アセット(`.uasset` / `.umap`)は Git LFS で管理。clone 前に `git lfs install` を実行する
- PlayFab のシークレットキーはリポジトリに含めない。詳細は Docs を参照
- Unreal Editor は MCP 経由でも操作できる(`CLAUDE.md` / `AGENTS.md` 参照)

## 状態

エディタ内 PIE での認証・暗号化は検証済み。別プロセス、パッケージ版、公開ネットワークは未検証。
