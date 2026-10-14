# GameProject20260916

Unreal Engine 5.8 のプロジェクト。Unreal Editor を MCP 経由で直接操作して開発する。

## 環境

- エンジン: **UE 5.8**
- MCP: `unreal-mcp` — [.mcp.json](.mcp.json) で `http://127.0.0.1:8000/mcp` に接続。
  実体は Unreal Editor 内蔵の `ModelContextProtocol` プラグイン。エディタが起動していないと使えない
- 有効プラグイン: `ModelContextProtocol`, `AllToolsets`, `ModelingToolsEditorMode`
- **エディタのロケールが日本語**。MCP が返すノード ID やカテゴリ名も日本語で返る

## 報告の基準

- **「動くはず」を報告しない。** コンパイル成功・アセット生成・スクリーンショット上の見た目は、
  いずれも動作確認ではない。実際に実行して実行時の状態を読むまでは「未検証」と明記する
- 自分で検証できないときは未検証と書き、**何を確認すれば確定するか**を添える
- 実行時の真偽値を根拠にするときは、その API が本当に知りたいことを答えているか疑う。
  例: `HasMappingContext` はコンテキストの登録有無しか見ない。中身が空でも `true` を返す
- 失敗の切り分けでは、まず**対照実験が成立するか**を確かめる。
  プローブが出ない場合、プローブ機構自体が動いているかを先に確認する

## Unreal を触る前に

[.claude/skills/unreal-mcp/SKILL.md](.claude/skills/unreal-mcp/SKILL.md) に、このプロジェクトで実際に踏んだ
API の罠をまとめてある。Blueprint グラフ・Enhanced Input・マテリアル・ビューポートキャプチャを
扱う前に読むこと。

同じ内容を Unreal 側の Agent Skill (`/Game/Skills/`) にも登録してある。
`AgentSkillToolset.ListSkills` で参照できる。
