#include "GameAuth.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/InputComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/NetConnection.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerState.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "AuthProtocol.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, GameAuth)
DEFINE_LOG_CATEGORY_STATIC(LogGameAuth, Log, All);

namespace
{
    bool UseEditorMock(const UWorld* World)
    {
#if WITH_EDITOR
        const auto* Settings = GetDefault<UGameAuthSettings>();
        return World && World->WorldType == EWorldType::PIE && Settings->bEnableEditorMock && Settings->TitleId.IsEmpty();
#else
        return false;
#endif
    }

    bool ValidTitleId(const FString& TitleId)
    {
        if (TitleId.IsEmpty() || TitleId.Len() > 32) return false;
        for (TCHAR C : TitleId) if (!FChar::IsHexDigit(C)) return false;
        return true;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Api, const TSharedRef<FJsonObject>& Body)
    {
        auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(FString::Printf(TEXT("https://%s.playfabapi.com/%s"), *GetDefault<UGameAuthSettings>()->TitleId, *Api));
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetTimeout(15.f);
        FString Json;
        FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Json));
        Request->SetContentAsString(Json);
        return Request;
    }

    TSharedPtr<FJsonObject> ReadData(const FHttpResponsePtr& Response, bool bConnected)
    {
        if (!bConnected || !Response || Response->GetResponseCode() != 200 || Response->GetContentLength() > 1024 * 1024) return nullptr;
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Root) || !Root) return nullptr;
        const TSharedPtr<FJsonObject>* Data = nullptr;
        double Code = 0;
        if (!Root->TryGetNumberField(TEXT("code"), Code) || Code != 200 || !Root->TryGetObjectField(TEXT("data"), Data)) return nullptr;
        return *Data;
    }

    bool AuthenticatedController(AController* Controller)
    {
        const auto* Account = Cast<AAccountPlayerController>(Controller);
        return Account && Account->IsServerAuthenticated();
    }
}

AAccountGameMode::AAccountGameMode()
{
    PlayerControllerClass = AAccountPlayerController::StaticClass();
    // Existing project Blueprint retains the character and animation configuration.
    static ConstructorHelpers::FClassFinder<APawn> Character(TEXT("/Game/Blueprints/BP_MCPCharacter"));
    if (Character.Succeeded()) DefaultPawnClass = Character.Class;
}

void AAccountGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (AuthenticatedController(Player)) Super::HandleStartingNewPlayer_Implementation(Player);
}

void AAccountGameMode::RestartPlayer(AController* Player)
{
    if (AuthenticatedController(Player)) Super::RestartPlayer(Player);
}

APawn* AAccountGameMode::SpawnDefaultPawnFor_Implementation(AController* Player, AActor* StartSpot)
{
    return AuthenticatedController(Player) ? Super::SpawnDefaultPawnFor_Implementation(Player, StartSpot) : nullptr;
}

APawn* AAccountGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* Player, const FTransform& Transform)
{
    return AuthenticatedController(Player) ? Super::SpawnDefaultPawnAtTransform_Implementation(Player, Transform) : nullptr;
}

void AAccountPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(LoginDeadline, FTimerDelegate::CreateWeakLambda(this, [this]
        {
            if (!bAuthenticated) RejectAccount(TEXT("ログインの制限時間を超えました。接続し直してください。"));
        }), FMath::Clamp(GetDefault<UGameAuthSettings>()->LoginTimeoutSeconds, 30.f, 600.f), false);
    }
    if (IsLocalController() && GetNetMode() != NM_DedicatedServer)
    {
        LoginWidget = CreateWidget<UAccountLoginWidget>(this);
        if (LoginWidget) LoginWidget->AddToViewport(100);
        bShowMouseCursor = true;
        FInputModeUIOnly Mode;
        if (LoginWidget) Mode.SetWidgetToFocus(LoginWidget->TakeWidget());
        SetInputMode(Mode);
        SetStatus(IsEditorMock() ? TEXT("開発用の仮認証です。PlayFabには接続しません。") : TEXT("メールアドレスとパスワードでログインしてください。"));
    }
}

void AAccountPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ClientRequest) { ClientRequest->OnProcessRequestComplete().Unbind(); ClientRequest->CancelRequest(); ClientRequest.Reset(); }
    if (ServerRequest) { ServerRequest->OnProcessRequestComplete().Unbind(); ServerRequest->CancelRequest(); ServerRequest.Reset(); }
    GetWorldTimerManager().ClearAllTimersForObject(this);
    GetWorldTimerManager().ClearTimer(LoginDeadline);
    GetWorldTimerManager().ClearTimer(VerificationDeadline);
    GetWorldTimerManager().ClearTimer(DisconnectTimer);
    if (LoginWidget) LoginWidget->RemoveFromParent();
    Super::EndPlay(Reason);
}

void AAccountPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &AAccountPlayerController::ToggleAccountMenu);
}

void AAccountPlayerController::ToggleAccountMenu()
{
    if (!IsLocalController() || !bAuthenticated || !LoginWidget) return;
    if (LoginWidget->IsInViewport())
    {
        LoginWidget->RemoveFromParent();
        bShowMouseCursor = false;
        SetInputMode(FInputModeGameOnly());
    }
    else
    {
        LoginWidget->AddToViewport(100);
        LoginWidget->RefreshStatus(AccountStatus);
        bShowMouseCursor = true;
        FInputModeUIOnly Mode;
        Mode.SetWidgetToFocus(LoginWidget->TakeWidget());
        SetInputMode(Mode);
    }
}

bool AAccountPlayerController::IsEditorMock() const { return UseEditorMock(GetWorld()); }

bool AAccountPlayerController::CanSendRealTicket() const
{
    const UNetConnection* Connection = GetNetConnection();
    return Connection && Connection->IsEncryptionEnabled();
}

bool AAccountPlayerController::IsGameConnectionEncrypted() const { return CanSendRealTicket(); }

void AAccountPlayerController::SetStatus(const FString& Message)
{
    AccountStatus = Message;
    if (LoginWidget) LoginWidget->RefreshStatus(Message);
}

void AAccountPlayerController::LoginEditorMock()
{
    if (!IsLocalController() || !IsEditorMock() || bAuthenticated || bClientRequestPending) return;
#if WITH_EDITOR
    bClientRequestPending = true;
    SetStatus(TEXT("Dedicated Serverで仮認証を検証中…"));
    // Issued in this editor process; the server consumes it once. A client-supplied ID is never trusted.
    ServerSubmitTicket(GameAuth::EditorTickets().Issue(FPlatformTime::Seconds()));
#endif
}

void AAccountPlayerController::Login(const FString& Email, const FString& Password, bool bRegister)
{
    if (!IsLocalController() || bAuthenticated || bClientRequestPending) return;
    if (IsEditorMock()) { SetStatus(TEXT("現在は仮認証モードです。下の開発用ログインを使用してください。")); return; }
    if (!ValidTitleId(GetDefault<UGameAuthSettings>()->TitleId)) { SetStatus(TEXT("PlayFab Title IDが未設定です。")); return; }
    if (!CanSendRealTicket()) { SetStatus(TEXT("ゲーム接続の暗号化が未設定です。認証チケットは送信しません。")); return; }
    if (!Email.Contains(TEXT("@")) || Email.Len() > 254 || Password.IsEmpty() || Password.Len() > 1000)
    { SetStatus(TEXT("メールアドレスとパスワードを確認してください。")); return; }
    bClientRequestPending = true;
    SetStatus(bRegister ? TEXT("アカウントを作成中…") : TEXT("PlayFabにログイン中…"));
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("TitleId"), GetDefault<UGameAuthSettings>()->TitleId);
    Body->SetStringField(TEXT("Email"), Email.TrimStartAndEnd());
    Body->SetStringField(TEXT("Password"), Password);
    if (bRegister) Body->SetBoolField(TEXT("RequireBothUsernameAndEmail"), false);
    ClientRequest = MakeRequest(bRegister ? TEXT("Client/RegisterPlayFabUser") : TEXT("Client/LoginWithEmailAddress"), Body);
    ClientRequest->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
    {
        Request->SetContentAsString(TEXT("")); // Do not retain credentials after the HTTP operation.
        ClientRequest.Reset();
        auto Data = ReadData(Response, bConnected);
        FString Ticket;
        if (!Data || !Data->TryGetStringField(TEXT("SessionTicket"), Ticket) || Ticket.IsEmpty() || Ticket.Len() > 8192)
        {
            bClientRequestPending = false;
            double ErrorCode = 0;
            TSharedPtr<FJsonObject> ErrorBody;
            if (Response && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ErrorBody) && ErrorBody)
                ErrorBody->TryGetNumberField(TEXT("errorCode"), ErrorCode);
            const int32 HttpCode = Response ? Response->GetResponseCode() : 0;
            UE_LOG(LogGameAuth, Warning, TEXT("PlayFab client authentication failed (HTTP=%d, APIError=%d)."), HttpCode, int32(ErrorCode));
            SetStatus(FString::Printf(TEXT("登録またはログインに失敗しました（HTTP %d / PlayFab %d）。入力内容と通信状態を確認してください。"), HttpCode, int32(ErrorCode)));
            return;
        }
        if (!CanSendRealTicket())
        { bClientRequestPending = false; SetStatus(TEXT("暗号化されたゲーム接続が失われました。")); return; }
        SetStatus(TEXT("Dedicated Serverで認証を確認中…"));
        UE_LOG(LogGameAuth, Display, TEXT("PlayFab client authentication succeeded; sending session ticket over encrypted transport."));
        ServerSubmitTicket(Ticket);
    });
    if (!ClientRequest->ProcessRequest())
    {
        ClientRequest->SetContentAsString(TEXT("")); ClientRequest.Reset(); bClientRequestPending = false;
        SetStatus(TEXT("認証サーバーへの通信を開始できませんでした。"));
    }
}

void AAccountPlayerController::ServerSubmitTicket_Implementation(const FString& Ticket)
{
    if (bRejected || bAuthenticated) return;
    if (bVerifying || ++Attempts > 3 || Ticket.IsEmpty() || Ticket.Len() > 8192)
    { RejectAccount(TEXT("認証リクエストを拒否しました。")); return; }
    if (IsEditorMock())
    {
#if WITH_EDITOR
        FString Id;
        if (GameAuth::EditorTickets().Consume(Ticket, FPlatformTime::Seconds(), Id)) AcceptAccount(Id);
        else RejectAccount(TEXT("開発用チケットが無効または期限切れです。"));
#endif
        return;
    }
    if (!ValidTitleId(GetDefault<UGameAuthSettings>()->TitleId) || !CanSendRealTicket())
    { RejectAccount(TEXT("サーバーの認証設定が完了していません。")); return; }
    const FString Secret = FPlatformMisc::GetEnvironmentVariable(TEXT("PLAYFAB_TITLE_SECRET_KEY"));
    if (Secret.IsEmpty()) { RejectAccount(TEXT("サーバーの認証設定が完了していません。")); return; }
    bVerifying = true;
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("SessionTicket"), Ticket);
    ServerRequest = MakeRequest(TEXT("Server/AuthenticateSessionTicket"), Body);
    ServerRequest->SetHeader(TEXT("X-SecretKey"), Secret);
    GetWorldTimerManager().SetTimer(VerificationDeadline, FTimerDelegate::CreateWeakLambda(this, [this]
    { RejectAccount(TEXT("認証サービスから応答がありません。")); }), 18.f, false);
    ServerRequest->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
    {
        Request->SetHeader(TEXT("X-SecretKey"), TEXT("")); Request->SetContentAsString(TEXT(""));
        ServerRequest.Reset();
        if (bRejected || !bVerifying) return;
        bVerifying = false;
        GetWorldTimerManager().ClearTimer(VerificationDeadline);
        const auto Data = ReadData(Response, bConnected);
        FString Id;
        if (!GameAuth::ReadVerifiedPlayerId(Data, Id))
        { RejectAccount(TEXT("認証チケットを確認できませんでした。")); return; }
        AcceptAccount(Id);
    });
    if (!ServerRequest->ProcessRequest()) RejectAccount(TEXT("認証サービスに接続できません。"));
}

void AAccountPlayerController::AcceptAccount(const FString& PlayerId)
{
    if (!HasAuthority() || bRejected || bAuthenticated || PlayerId.IsEmpty()) return;
    // A second connection cannot take over an account that is already playing.
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const auto* Other = Cast<AAccountPlayerController>(It->Get());
        if (Other && Other != this && Other->bAuthenticated && Other->AuthenticatedPlayerId == PlayerId)
        { RejectAccount(TEXT("このアカウントは既に接続中です。")); return; }
    }
    bAuthenticated = true;
    AuthenticatedPlayerId = PlayerId;
    GetWorldTimerManager().ClearTimer(LoginDeadline);
    GetWorldTimerManager().ClearTimer(VerificationDeadline);
    if (PlayerState) PlayerState->SetPlayerName(IsEditorMock() ? TEXT("開発プレイヤー") : TEXT("プレイヤー"));
    if (auto* Mode = GetWorld()->GetAuthGameMode<AAccountGameMode>()) Mode->RestartPlayer(this);
    if (!GetPawn()) { bAuthenticated = false; RejectAccount(TEXT("プレイヤーを生成できませんでした。")); return; }
    UE_LOG(LogGameAuth, Display, TEXT("Authentication accepted; pawn spawned (provider=%s)."), IsEditorMock() ? TEXT("EditorMock") : TEXT("PlayFab"));
    ClientAccountResult(true, IsEditorMock() ? TEXT("開発用ログイン成功") : TEXT("ログイン成功"), PlayerId);
}

void AAccountPlayerController::RejectAccount(const FString& Reason)
{
    if (!HasAuthority() || bRejected) return;
    bRejected = true;
    bAuthenticated = false;
    bVerifying = false;
    AuthenticatedPlayerId.Empty();
    if (ServerRequest) { ServerRequest->OnProcessRequestComplete().Unbind(); ServerRequest->CancelRequest(); ServerRequest.Reset(); }
    GetWorldTimerManager().ClearTimer(LoginDeadline);
    GetWorldTimerManager().ClearTimer(VerificationDeadline);
    if (APawn* CurrentPawn = GetPawn()) { UnPossess(); CurrentPawn->Destroy(); }
    UE_LOG(LogGameAuth, Display, TEXT("Authentication rejected; no playable pawn."));
    ClientAccountResult(false, Reason, TEXT(""));
    GetWorldTimerManager().SetTimer(DisconnectTimer, FTimerDelegate::CreateWeakLambda(this, [this, Reason]
    {
        auto* Mode = GetWorld()->GetAuthGameMode();
        if (!Mode || !Mode->GameSession || !Mode->GameSession->KickPlayer(this, FText::FromString(Reason))) Destroy();
    }), 1.f, false);
}

void AAccountPlayerController::ClientAccountResult_Implementation(bool bSuccess, const FString& Message, const FString& PlayerId)
{
    bClientRequestPending = false;
    bAuthenticated = bSuccess;
    AuthenticatedPlayerId = bSuccess ? PlayerId : TEXT("");
    SetStatus(Message);
    if (bSuccess)
    {
        if (LoginWidget) LoginWidget->RemoveFromParent();
        bShowMouseCursor = false;
        SetInputMode(FInputModeGameOnly());
    }
}

void AAccountPlayerController::LogoutAccount()
{
    if (IsLocalController()) ServerLogoutAccount();
}

void AAccountPlayerController::ServerLogoutAccount_Implementation()
{
    RejectAccount(TEXT("ログアウトしました。"));
}

TSharedRef<SWidget> UAccountLoginWidget::RebuildWidget()
{
    SetIsFocusable(true);
    if (WidgetTree->RootWidget) return Super::RebuildWidget();
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetPadding(FMargin(60.f, 35.f));
    Panel->SetBrushColor(FLinearColor(.025f, .035f, .06f, .98f));
    Panel->SetHorizontalAlignment(HAlign_Center);
    Panel->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = Panel;
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->SetContent(Column);
    auto AddText = [this, Column](const FString& Text, int32 Size)
    {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        auto Font = Label->GetFont(); Font.Size = Size; Label->SetFont(Font);
        Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, 6));
        return Label;
    };
    AddText(TEXT("GAME ACCOUNT"), 30);
    const auto* PC = GetOwningPlayer<AAccountPlayerController>();
    AddText(PC && PC->IsEditorMock() ? TEXT("ローカル開発用 / PlayFab 未接続") : TEXT("Dedicated Server ログイン"), 16);
    Form = WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(Form);
    EmailBox = WidgetTree->ConstructWidget<UEditableTextBox>();
    EmailBox->SetHintText(FText::FromString(TEXT("メールアドレス")));
    EmailBox->SetMinDesiredWidth(440);
    Form->AddChildToVerticalBox(EmailBox)->SetPadding(FMargin(0, 8));
    PasswordBox = WidgetTree->ConstructWidget<UEditableTextBox>();
    PasswordBox->SetHintText(FText::FromString(TEXT("パスワード")));
    PasswordBox->SetIsPassword(true);
    Form->AddChildToVerticalBox(PasswordBox)->SetPadding(FMargin(0, 8));
    auto AddButton = [this](UVerticalBox* Parent, const FString& Text)
    {
        auto* Button = WidgetTree->ConstructWidget<UButton>();
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text)); Button->AddChild(Label);
        Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 8));
        return Button;
    };
    auto* LoginButton = AddButton(Form, TEXT("ログイン"));
    LoginButton->OnClicked.AddDynamic(this, &UAccountLoginWidget::OnLogin);
    auto* RegisterButton = AddButton(Form, TEXT("新規アカウント作成"));
    RegisterButton->OnClicked.AddDynamic(this, &UAccountLoginWidget::OnRegister);
    if (PC && PC->IsEditorMock())
    {
        Form->SetVisibility(ESlateVisibility::Collapsed);
        MockButton = AddButton(Column, TEXT("開発用ログイン（このエディタ内のみ）"));
        MockButton->OnClicked.AddDynamic(this, &UAccountLoginWidget::OnMock);
    }
    ResumeButton = AddButton(Column, TEXT("ゲームに戻る"));
    ResumeButton->OnClicked.AddDynamic(this, &UAccountLoginWidget::OnResume);
    LogoutButton = AddButton(Column, TEXT("ログアウトして切断"));
    LogoutButton->OnClicked.AddDynamic(this, &UAccountLoginWidget::OnLogout);
    StatusText = AddText(TEXT(""), 16);
    StatusText->SetAutoWrapText(true);
    StatusText->SetWrapTextAt(500.f);
    if (PC) RefreshStatus(PC->AccountStatus);
    return Super::RebuildWidget();
}

void UAccountLoginWidget::RefreshStatus(const FString& Message)
{
    if (StatusText) StatusText->SetText(FText::FromString(Message));
    const auto* PC = GetOwningPlayer<AAccountPlayerController>();
    const bool bLoggedIn = PC && PC->IsAccountAuthenticated();
    if (Form) Form->SetVisibility(!bLoggedIn && PC && !PC->IsEditorMock() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (MockButton) MockButton->SetVisibility(bLoggedIn ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    if (ResumeButton) ResumeButton->SetVisibility(bLoggedIn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (LogoutButton) LogoutButton->SetVisibility(bLoggedIn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void UAccountLoginWidget::OnLogin()
{
    if (auto* PC = GetOwningPlayer<AAccountPlayerController>()) PC->Login(EmailBox->GetText().ToString(), PasswordBox->GetText().ToString(), false);
    PasswordBox->SetText(FText::GetEmpty());
}
void UAccountLoginWidget::OnRegister()
{
    if (auto* PC = GetOwningPlayer<AAccountPlayerController>()) PC->Login(EmailBox->GetText().ToString(), PasswordBox->GetText().ToString(), true);
    PasswordBox->SetText(FText::GetEmpty());
}
void UAccountLoginWidget::OnMock() { if (auto* PC = GetOwningPlayer<AAccountPlayerController>()) PC->LoginEditorMock(); }
void UAccountLoginWidget::OnLogout() { if (auto* PC = GetOwningPlayer<AAccountPlayerController>()) PC->LogoutAccount(); }
void UAccountLoginWidget::OnResume() { if (auto* PC = GetOwningPlayer<AAccountPlayerController>()) PC->ToggleAccountMenu(); }
