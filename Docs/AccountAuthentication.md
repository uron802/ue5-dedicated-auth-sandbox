# アカウント認証 / Dedicated Server

## 現在の構成

`Plugins/GameAuth` が提供する `AAccountGameMode` と `AAccountPlayerController` を使用する。
クライアント接続直後はログイン画面だけを表示し、サーバー側で認証が完了するまで操作用Pawnを生成しない。
`RestartPlayer` と標準Pawn生成経路にも認証チェックがあり、通常のリスポーンで認証を迂回できない。

### PlayFab未作成の間

`Config/DefaultGame.ini` の `TitleId` が空、`bEnableEditorMock=True`、実行ワールドがPIEの場合だけ仮認証を使う。
Play As Client / 2人 / Run Under One Process を使い、各画面の「開発用ログイン」を押す。
同じエディタプロセス内で発行した30秒有効・一度だけ使用できるチケットをDedicated Serverが照合する。
開発IDはログインごとに変わり、アカウントやパスワードを永続保存しない。
これはPlayFabアカウント登録・認証の成功を検証するものではない。
パッケージ版、別プロセス、VPSでは仮認証を利用できない。

### PlayFabを使えるようになったら

このPCでの起動はプロジェクト直下の `StartPlayFabEditor.bat` をダブルクリックする。
Unreal Editorを保存して終了してから起動し、`PlayFab Secret Key (hidden)` にキーを貼り付けてEnterを押す。
キーは起動するエディタのプロセス環境変数だけに渡し、ファイルやユーザー環境変数には保存しない。
同梱の `Scripts/StartPlayFabEditor.ps1` が必要。エディタ内PIEの通信暗号化は実装済み。実認証の成否は実行結果で確認する。

1. PlayFabでゲームTitleを作成し、Project Settings → Game AuthenticationにTitle IDを設定する。
2. Dedicated Serverの起動環境だけに `PLAYFAB_TITLE_SECRET_KEY` を設定する。リポジトリ、ini、Blueprint、クライアント配布物には保存しない。
3. ゲーム接続にはDTLSの証明書方式を使用する。エディタ内PIEでは、同じエディタ内で生成した一時証明書とそのSHA-256指紋を使う。別プロセス・VPSで運用する場合は、サーバーに証明書を配備し、クライアントに信頼する指紋を設定する。
4. 実アカウントで新規登録・ログイン・期限切れ・不正チケット・認証サービス障害を実機検証する。

クライアントはHTTPSで `Client/RegisterPlayFabUser` または `Client/LoginWithEmailAddress` を呼び出す。
得られたSessionTicketを暗号化済みのゲーム接続でサーバーへ送り、サーバーがHTTPSで `Server/AuthenticateSessionTicket` を呼ぶ。
HTTP 200だけでは許可しない。`IsSessionTicketExpired=false` と `UserInfo.PlayFabId` を確認する。
プレイヤーIDはこのサーバー応答から取得し、クライアント申告のIDを使わない。
同じIDの同時接続は拒否する。認証サービスの障害・タイムアウト・設定不足も拒否する。

登録時のメール確認、パスワード再設定、セッションの定期再認証、部屋への参加権限、永続データ保存は今回の基盤には含まない。
F10でアカウントメニューを開き、ログアウトできる。ControllerのBlueprint関数 `LogoutAccount` からも呼び出せる。現在のゲーム接続を終了し、Pawnを破棄する。
未認証でも認証用PlayerControllerとネット接続は一時的に存在する。レプリケーションされる機密データは別途認証・部屋権限で制限する必要がある。

## テスト

Unreal Automation:

- `GameAuth.Protocol.ExpiredAndMalformedResponses`：PlayFab応答の期限切れ・欠落・IDの検証。
- `GameAuth.Protocol.MockTicketExpiryAndReplay`：偽造・期限切れ・再利用の拒否。
- `GameAuth.PIE.TwoPlayerDedicatedServerAdmission`：テスト自身が2人Dedicated Server PIEを起動する。DemoMapを開いて実行する。ログイン前のPawn不在、RestartPlayerの拒否、クライアントからの仮ログインRPC、サーバーの認証ID、2人分のPawnレプリケーションを確認する。実際のキーボード操作は別途確認する。

PlayFabタイトルは作成済み（Title ID `167B7`）。実PlayFabと公開ネットワークでの認証は、それぞれ実行検証の結果を記録する。

## DTLS通信暗号化

`UAccountGameInstance` が暗号化のコールバックを処理する。
`DTLSHandlerComponent` を有効化し、`net.AllowEncryption=2` と `DTLS.PreSharedKeys=0` を設定した。
暗号化用ハンドシェイクが欠落した接続は、PIEでも許可しない。
`OnPendingNetGameConnectionCreated` で、接続ハンドシェイク開始前に公開のプロトコル識別子をURLへ設定する。
この識別子に秘密キー・パスワード・PlayFabチケットは含まれない。

クライアントが期待するSHA-256指紋と、実際のサーバー証明書が一致しない場合はDTLSが接続を拒否する。
PIEの証明書と秘密鍵はメモリ内だけに保持する。PIE終了後も同じエディタプロセスの証明書ストアには残るため、証明書の有効期限を超える長時間運用には使わない。
エディタ内の仮認証は従来どおりアカウント認証だけを置き換える。通信暗号化は仮認証でも実施する。

### 別プロセス・VPSでの配備（今回のエディタ内検証とは別）

- サーバーの `GAMEAUTH_DTLS_CERTIFICATE_FILE` に自己署名証明書と秘密鍵を含むPEMファイルの絶対パスを設定する。
- クライアント設定の `ServerCertificateFingerprint` に、その証明書のSHA-256指紋を設定する（64桁の16進数、コロン区切りも可）。指紋は公開情報で、秘密鍵ではない。
- サーバー証明書の指紋は信頼できる配布経路でクライアントへ提供する。接続中の相手から受け取った値を無条件には信用しない。
- `PLAYFAB_TITLE_SECRET_KEY` と証明書秘密鍵はDedicated Server側だけに配備する。
- パッケージ版、Linux、VPS、公開ネットワークでの運用は未検証。DTLSプラグインはエンジン上Experimental扱い。

### 追加した通信テスト

- `GameAuth.Transport.TwoEncryptedDedicatedClients`：Dedicated Serverに2人が暗号化接続し、アカウント認証前のPawnが存在しないことを確認。
- `GameAuth.Transport.RejectWrongServerCertificate`：実際に異なる証明書指紋を設定して接続し、DTLSの指紋検証失敗とプレイヤー未受け入れを確認。

2026-10-14のビルド成功後、通信2件・認証プロトコル2件・仮認証の2人参加1件の計5件が成功（エラー・警告なし）。
結果は `Docs/DTLSVerification20261014.json` に保存。
実認証用の2人PIEでも、4つの接続側（各クライアントとサーバー側2接続）にDTLSハンドシェイク完了が記録された。
この段階では実プレイヤーの登録・ログイン結果はまだ未検証。

### 2026-10-14 実PlayFab認証の検証完了

Title ID `167B7`、仮認証無効、Dedicated Server + クライアント2人の同一プロセスPIEで実行した。
`example.com` の一意なテスト用メール形式ID2件とランダムなパスワードを生成し、Slate操作でゲーム内の新規登録画面に入力した。
両方で `Client/RegisterPlayFabUser` が成功し、サーバーの `Server/AuthenticateSessionTicket` を通過してPawnを生成した。
続いてPIEを起動し直し、同じ資格情報でゲーム内の `Client/LoginWithEmailAddress` を実行し、両方のサーバー認証とPawn生成が再度成功した。
仮認証ではなく、サーバーログの `provider=PlayFab` を各回2件確認済み。
再ログイン後のサーバー上のPlayFab IDは `80898F7EA3B52047` と `9D0CC0F68436738C`。Characterも2体存在した。
ユーザーの実キー操作により、両クライアントの移動・ジャンプ・視点操作、および相手への移動・ジャンプ・歩行アニメーションの同期を確認済み。

テスト資格情報は `Saved/CodexPrivate/PlayFabTestAccounts.clixml` にWindows DPAPIで暗号化して保存した。
パスワード、PlayFab秘密キー、SessionTicketは検証結果やログへ出力していない。
再検証用のローカル操作スクリプトは `Saved/TestPlayFabAccounts.ps1`（`-Mode Register` または `-Mode Login`）。
このテスト用メールIDには受信箱がないため、メール確認やパスワード再設定の検証には使わない。
実PlayFab認証とDTLSのエディタ内検証は完了。別プロセス・パッケージ版・Linux/VPS・公開ネットワークでの検証は未実施。

### 2026-10-14 手動確認

Dedicated Server + クライアント2人のPIEで、両画面の「開発用ログイン」をMCPのSlate Clickで操作した。
サーバーログにEditorMockの認証成功とPawn生成が2件記録され、クライアント画面に2人のキャラクターが表示された。
ユーザーの実キー操作により、両方のWASD移動・Spaceジャンプ・マウス視点操作、および相手への移動・ジャンプ・歩行アニメーションの反映を確認済み。
今回の確認はエディタ内の仮認証。実PlayFab認証、公開ネットワーク、パッケージ版、F10メニューとログアウトは今回の手動確認の対象外。

## ビルド

このPCのUE 5.8はMSVC 14.50を推奨。旧MSVC 14.38ではエンジンヘッダーでコンパイルが失敗したため、Microsoft Build Toolsを追加した。

```powershell
& 'C:\UnrealEngine\UE_5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe' 'C:\UnrealEngine\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll' UnrealEditor Win64 Development '-Project=E:\UnrealProjects\GameProject20260916\GameProject20260916.uproject' -2026 '-CompilerVersion=14.50.35717' -NoHotReloadFromIDE -NoUBA -nocache -UBADisableWaitOnMem -MaxParallelActions=2
```

変更前のアセットと設定は `Saved/CodexBackups/Auth20261014/` に保存。

## API資料

- https://learn.microsoft.com/en-us/rest/api/playfab/client/authentication/login-with-email-address
- https://learn.microsoft.com/en-us/rest/api/playfab/client/authentication/register-playfab-user
- https://learn.microsoft.com/en-us/rest/api/playfab/server/authentication/authenticate-session-ticket
