---
name: unreal-mcp-gotchas
description: このプロジェクトで Unreal Editor を unreal-mcp 経由で操作するときの既知の落とし穴と検証手順。Blueprint グラフの構築、Enhanced Input (InputAction / InputMappingContext) の設定、マテリアル作成、アクタ配置、ビューポートキャプチャ、ProgrammaticToolset でのバッチ実行を行う前に読む。UE 5.8 / 日本語エディタ環境。
---

# unreal-mcp の落とし穴（UE 5.8 / 日本語エディタ）

このプロジェクトで**実際に踏んだ**ものだけを記録している。すべて UE 5.8 での事実であり、
他のバージョンでは異なる可能性がある。追記するときは末尾の「書き方」に従うこと。

---

## 症状インデックス

**まずここから引く。** 症状が一致したら、該当項目だけを読めばよい。

| 症状 | 項目 |
|---|---|
| キーを押しても InputAction が発火しない（エラーも警告も出ない） | InputMappingContext のキーが一切効かない |
| `The pins may be incompatible types` で ActionValue が繋がらない | ActionValue ピンの型は valueType で変わる |
| 移動の向きがずれる / マウスのピッチが上下反転する | WASD を Axis2D に流すモディファイア |
| `' Target 'には接続が必要です` でコンパイルが落ちる | 同名ノードが別クラスに解決される |
| `The node could not be created / ... does not exist` | ノード type_id がエディタのロケール依存 |
| DSL で Enhanced Input のイベントが書けない | write_graph_dsl で書けないもの |
| `ArrayAdd: elements changed alongside the size change` | 配列はサイズ変更と要素変更を同時にできない |
| キャラが動かない / 移動が引っかかる | Character に見た目用メッシュを足すときはコリジョンを切る |
| 配置したアクタが宙に浮く | Blueprint アクタの snap_to_ground は浮く |
| `Section not found` で設定セクションが引けない | Project Settings のセクション名 |
| `needs a default value` でキャプチャが失敗する / 戻り値が大きすぎる | ビューポートキャプチャ |
| `.get() does not support a default value` | ProgrammaticToolset |
| 人型キャラが欲しい / アニメーションアセットが 0 件 | 人型モデルとアニメーションの入手先 |
| スケルタルメッシュを設定しても反映されない | 人型モデルとアニメーションの入手先 |
| 入力やグラフが実際に動いているか確かめたい | 検証のしかた |

---

## Enhanced Input

### InputMappingContext のキーが一切効かない

**症状**: `HasMappingContext` が `true` を返し、グラフもコンパイルが通っているのに、
キーを押しても InputAction が一度も発火しない。エラーもログ警告も出ない。

**原因**: `UInputMappingContext` にはキー配列が **2 箇所** ある。

| プロパティ | 実行時 |
|---|---|
| `mappings`（トップレベル） | **読まれない**（レガシー） |
| `defaultKeyMappings.mappings` | **これが読まれる** |

**対処**:
```python
set_properties(IMC, {"defaultKeyMappings": {"mappings": [...]}})
set_properties(IMC, {"mappings": []})   # 二重登録を避けるため空に
```

**罠**: `HasMappingContext` は**コンテキストが登録されているか**しか見ない。
中身が空でも `true` を返す。これを「マッピング適用済み」の根拠にしてはいけない。
確認するなら `defaultKeyMappings.mappings` の件数を直接読むこと。

### ActionValue ピンの型は valueType で変わる

**症状**: `EnhancedInputAction` イベントの `ActionValue` を `入力|BreakInputActionValue` に
繋ごうとすると `The pins may be incompatible types` で失敗する。

**原因**: `ActionValue` ピンは InputAction の `valueType` に応じて特殊化される。
`Axis2D` なら **Vector2D 構造体**であって `FInputActionValue` ではない。

**対処**: `演算|ベクター2D|BreakVector2D` を使う。型は `get_node_infos` の
`output_pins[].type_id` で確認できる。

### WASD を Axis2D に流すモディファイア

modifiers はクラスパスを渡すとインスタンス化される（`{"refPath": "/Script/EnhancedInput.InputModifierSwizzleAxis"}`）。
Axis2D で X=右 / Y=前 にする構成:

| キー | modifiers | 結果 |
|---|---|---|
| `W` | `[SwizzleAxis]` | +Y |
| `S` | `[SwizzleAxis, Negate]` | -Y |
| `D` | `[]` | +X |
| `A` | `[Negate]` | -X |
| `MouseX` | `[]` | X (yaw) |
| `MouseY` | `[SwizzleAxis, Negate]` | Y (pitch、上を向く向き) |

`MouseY` に `Negate` を付けないとピッチが上下反転する。

### マッピングコンテキストの登録に BeginPlay の配線は要らない

Project Settings → Engine → `EnhancedInputDeveloperSettings` の `DefaultMappingContexts` に
登録すれば、サブシステムを辿って `AddMappingContext` する定番の配線は不要。

```python
SetSectionProperties("Project", "Engine", "EnhancedInputDeveloperSettings",
  '{"DefaultMappingContexts":[{"InputMappingContext":{"refPath":"..."},'
  '"Priority":0,"bAddImmediately":true,"bRegisterWithSettings":true}]}')
```

---

## Blueprint グラフ

### ノード type_id がエディタのロケール依存

`find_node_types` が返す ID は表示言語で変わる。日本語環境では:

| 用途 | type_id |
|---|---|
| 移動入力 | `ポーン|インプット|AddMovementInput` |
| Yaw / Pitch | `ポーン|インプット|AddControllerYawInput` / `AddControllerPitchInput` |
| コントロール回転 | `ポーン|GetControlRotation` |
| Rotator→ベクター | `演算|ベクター|GetForwardVector` / `GetRightVector` |
| ジャンプ | `キャラクター|Jump` / `キャラクター|StopJumping` |
| ログ出力 | `デベロップメント|PrintString` |
| 入力イベント | `インプット|EnhancedActionEvents|<IA名>` |

**ID をハードコードしない。** 毎回 `find_node_types` で引くこと。英語環境の他プロジェクトに
同じスクリプトは通らない。

### 同名ノードが別クラスに解決される

**症状**: コンパイルで
`このブループリント(自身)は <Class> ではないため、' Target 'には接続が必要です`。

**原因**: 同じ `type_id` を持つノードが複数あり、意図しないクラスのものが選ばれた。
実例:
- `Transformation|GetForwardVector` → **SceneComponent** のメソッド。Character 自身は
  SceneComponent ではないので繋がらない。Rotator を取る `演算|ベクター|GetForwardVector` が正解
- `ポーン|GetControlRotation` → **Controller** 側に解決された

**対処**: `create_node` の `declaring_class` で明示する。
```python
create_node(graph, "ポーン|GetControlRotation", pos,
            declaring_class={"refPath": "/Script/Engine.Pawn"})
```

### write_graph_dsl で書けないもの

`(event X)` は内部で `AddEvent|X` にしか解決しない。Enhanced Input のイベントノードは
その名前空間にないため **DSL では作れない**。`create_node` + `connect_pins` で直接組む。

`arrange_nodes` の引数は `graph` ではなく **`nodes`（配列）**。

### ピン ID の取り方

`get_node_infos` が返す `PinInfo` は `{"name", "value", "type_id", "pin_id", "connected_pins"}`。
`connect_pins` / `set_pin_value` に渡すのは `pin_id`。

bool → String のような型変換は `connect_pins` が自動でキャストノードを挿入してくれる。
ただし**そのノードは残る**ので、片付けるときは孤立ノード（接続ゼロ）も走査して消すこと。

---

## プロパティ操作

### 配列はサイズ変更と要素変更を同時にできない

**症状**: `ArrayAdd: elements changed alongside the size change; insertion points are ambiguous`

**原因**: 既存要素を読み戻して再送すると、インスタンス化サブオブジェクト（modifier など）の
`refPath` がクラスパス → インスタンスパスに変わっているため「変更された」と判定される。
1 件ずつ追記しても同じ理由で 2 件目で落ちる。

**対処**: **一度 `[]` でクリアしてから全件を一括で書く。**

### Character に見た目用メッシュを足すときはコリジョンを切る

`add_component` で足した StaticMeshComponent は `collisionEnabled` が既定で
`QueryAndPhysics`。カプセルとほぼ同サイズのコライダーが二重に付く。

```python
set_properties(comp, {"bodyInstance": {"collisionEnabled": "NoCollision"}})
```

### Blueprint アクタの snap_to_ground は浮く

`add_to_scene_from_asset` の `snap_to_ground` は**バウンディングボックス**を接地させる。
Blueprint アクタは DefaultSceneRoot のエディタスプライトが箱を下方向に約 128 拡張するため、
メッシュが 128 浮く。可視メッシュの底だけを基準に Z を直接指定すること。

### Project Settings のセクション名

Maps & Modes の実際のセクション名は **`Maps`**（`Maps & Modes` ではない）。
`ListSections("Project", "Project")` で確認できる。

---

## キャラクター

### 人型モデルとアニメーションの入手先

**症状**: `find_assets` で AnimSequence / AnimBlueprint が **0 件**。人型スケルタルメッシュも
`SKM_Biped_Template`（ControlRigModules プラグインの Control Rig 用テンプレート）しかない。
そのまま使うとアニメーションなしで A ポーズのまま滑る。

**原因**: Blank テンプレートのプロジェクトにはキャラクター用アセットが一切入らない。
マネキン一式は**エンジンのインストール先**にあり、プロジェクトには自動で入らない。

```
<Engine>/Templates/TemplateResources/High/Characters/Content/Mannequins/
  Meshes/SKM_Manny_Simple, SKM_Quinn_Simple, SK_Mannequin
  Anims/Unarmed/ABP_Unarmed, BS_Idle_Walk_Run, MM_Idle, Walk/, Jog/, Jump/
  Materials/, Textures/, Rigs/
```
計 128 ファイル / 約 126MB。エンジンのインストール先は
`HKLM:\SOFTWARE\EpicGames\Unreal Engine\<ver>` の `InstalledDirectory` で引ける。

**対処**: 取り込みは**エディタの UI フローでユーザーにやってもらう**。
コンテンツブラウザ → 追加 → 「フィーチャーまたはコンテンツパックを追加」→ Third Person。
`/Game/Characters/Mannequins/` に展開される。MCP からは実行できない。

**罠**: SkeletalMeshComponent には似た名前のプロパティが 3 つある
(`skeletalMeshAsset` / `skeletalMesh` / `skinnedAsset`)。**設定するのは `skeletalMeshAsset`**。

### Character にマネキンを載せる

`ACharacter` 継承の Blueprint は、CDO 経由で継承コンポーネントを引ける。
`get_default_object` → `get_components` で
`CharacterMesh0`（SkeletalMeshComponent）, `CharMoveComp`, `CollisionCylinder` が取れる。

```python
setp(MESH, {"skeletalMeshAsset": ref(".../SKM_Manny_Simple.SKM_Manny_Simple")})
setp(MESH, {"animClass": ref(".../ABP_Unarmed.ABP_Unarmed_C")})   # _C を付ける
setp(MESH, {"animationMode": "AnimationBlueprint"})
setp(MESH, {"relativeLocation": {"x":0,"y":0,"z":-88},             # 足をカプセル底に
            "relativeRotation": {"pitch":0,"yaw":-90,"roll":0}})   # 前方を向かせる
```

三人称らしく進行方向を向かせるには、CDO の `bUseControllerRotationYaw` を `False` にし、
`CharMoveComp` の `bOrientRotationToMovement` を `True`、`rotationRate` の yaw を 500 にする。

**検証**: PIE のキャプチャで**アイドルポーズ（腕が自然に下がった姿勢）**になっていれば
AnimBlueprint は動いている。T ポーズ / A ポーズなら `animClass` か `animationMode` が
効いていない。歩行アニメーションの確認は移動が必要なので、人間に動かしてもらう。

## ビューポートキャプチャ

`CaptureViewport` は `captureTransform` と `annotations` の**両方を明示**しないと
`needs a default value` で失敗する。オーバーレイを消すには
`{"gridSpacing":0,"gridExtent":0,"gridHeight":0,"maxLabelDistance":0,"classFilter":{"refPath":""},"maxLabels":0}`。

戻り値の PNG は base64 で **1〜5MB** あり、そのままではコンテキストに載らない。
ツール結果はファイルに退避されるので、そこから base64 を抜いてデコードし、
リサイズしてから読む。

---

## ProgrammaticToolset

ループや座標計算を伴う一括処理は `execute_tool_script` で 1 往復にまとめる。
32 回のツール呼び出しが 1 回になる。

- `run()` が `Dict[str, Any]` を返す必要がある
- 使えるモジュールは `json` `math` `time` `datetime` `copy` `re` のみ
- 返り値は `_StrictDict`。**`.get()` にデフォルト値を渡せない**。
  `json.loads(json.dumps(x))` で通常の dict に変換してから扱うと楽
- ゲームスレッド上で動くのでツール呼び出しは**必ず逐次**。並列にしない

---

## 検証のしかた

MCP 経由では実キー入力を送れない。`SlateInspectorToolset.PressKey` は
フォーカス中の Slate ウィジェットに送るもので、**ゲームの入力パイプラインには届かない**
（`true` を返すが何も起きない）。入力まわりの最終確認はユーザーに実機で押してもらう。

グラフの挙動を確認する手順:

1. 確認したい exec 経路の**途中に** `デベロップメント|PrintString` を挿す
   （イベントの未使用 exec ピンに付けると、そのピンが発火しない場合に偽陰性になる）
2. `StartPIE` → 操作 → `LogsToolset.GetLogEntries(category="LogBlueprintUserMessages")`
3. **必ず対照実験を用意する。** BeginPlay など確実に通る経路にも同じプローブを置き、
   それが出ることを確認してから「出ない＝到達していない」と判断する

---

## 書き方

追記するときは症状を先頭に置く。エージェントは症状で grep するので、
エラー文字列・プロパティ名・ツール名は**原文のまま**含めること。

```markdown
### 短い症状の見出し
**症状**: 観測された挙動（エラー文字列は原文で）
**原因**: 実際の理由（UE のバージョンを明記）
**対処**: 動くコード
**罠**: 何を信じてはいけないか（あれば。ここが一番価値がある）
```

解決した**その場で**書く。セッション末にまとめると、どこで判断を誤ったかが抜け落ちて
正解手順だけの痩せたメモになる。同じ症状の記述が 2 つあると古いほうを引く危険があるので、
重複を作らず既存項目を更新する。

**項目を追加したら、必ず「症状インデックス」にも 1 行足すこと。** インデックスに載っていない
項目は存在しないのと同じになる。

## 肥大化したら分割する

このファイルは**触った領域だけが偏って伸びる**。放置すると、関係ない項目まで毎回
コンテキストに載る。以下の閾値で機械的に判断すること。

| 状態 | 対応 |
|---|---|
| 〜400 行 / 〜25 項目 | 1 ファイルのまま。インデックスだけ最新に保つ |
| 400 行超 **または** 25 項目超 | 各項目の本文を `references/<領域>.md` に移す。SKILL.md には**症状インデックスと「検証のしかた」だけ**を残す |
| ある塊が別の `description` でないと正しく起動しない | 独立したスキルに切り出す（`.Codex/skills/<新名>/`） |

分割の軸は **UE のサブシステム分類ではなく「エージェントが必要とする瞬間」**にする。
「キーが効かない」で詰まっているエージェントは、それが Enhanced Input の問題なのか
Blueprint の問題なのかを知らない。だから `references/` に分けても、
**症状インデックスは SKILL.md に残して入口を 1 箇所に保つ**こと。
インデックスは 1 項目 1 行なので、100 項目になっても 100 行にしかならない。

`references/` に移すときは本文をコピーせず移動し、インデックスの参照先を
`references/<file>.md` に書き換える。同じ記述が 2 箇所に残る状態を作らない。

## 古くなったら消す

エンジンをバージョンアップしたら、各項目の「UE 5.8 で確認」の記述を洗い直す。
再現しなくなった項目は**残さず削除する**。古い回避策が残っていると、エージェントは
不要な遠回りをするか、誤った前提で判断する。「念のため残す」はしない。
