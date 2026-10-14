#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/NetworkDelegates.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "GameAuth.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Game Authentication"))
class GAMEAUTH_API UGameAuthSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="PlayFab") FString TitleId;
    // Public SHA-256 pin only. The server PEM/private key is read from a server-only environment variable.
    UPROPERTY(Config, EditAnywhere, Category="Transport") FString ServerCertificateFingerprint;
    // Only effective in an editor PIE world with an empty TitleId. Never enabled in packaged builds.
    UPROPERTY(Config, EditAnywhere, Category="Development") bool bEnableEditorMock = false;
    UPROPERTY(Config, EditAnywhere, Category="Server", meta=(ClampMin="30", ClampMax="600")) float LoginTimeoutSeconds = 300.f;
};

UCLASS()
class GAMEAUTH_API UAccountGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void Shutdown() override;
    virtual void ReceivedNetworkEncryptionToken(const FString& Token, const FOnEncryptionKeyResponse& Delegate) override;
    virtual void ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate) override;
    virtual EEncryptionFailureAction ReceivedNetworkEncryptionFailure(UNetConnection* Connection) override;
private:
    bool IsEditorPIE() const;
    FDelegateHandle PendingConnectionHandle;
    TArray<FString> ClientCertificateIds;
};

UCLASS()
class GAMEAUTH_API AAccountPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="Account") void Login(const FString& Email, const FString& Password, bool bRegister);
    UFUNCTION(BlueprintCallable, Category="Account") void LoginEditorMock();
    UFUNCTION(BlueprintCallable, Category="Account") void LogoutAccount();
    UFUNCTION(BlueprintCallable, Category="Account") void ToggleAccountMenu();
    UFUNCTION(BlueprintPure, Category="Account") bool IsEditorMock() const;
    UFUNCTION(BlueprintPure, Category="Account") bool IsAccountAuthenticated() const { return bAuthenticated; }
    UPROPERTY(BlueprintReadOnly, Category="Account") FString AccountStatus;
    UPROPERTY(BlueprintReadOnly, Category="Account") FString AuthenticatedPlayerId;
    UFUNCTION(BlueprintPure, Category="Account") bool IsGameConnectionEncrypted() const;

    // Server-side state is private and never accepted from a client-provided player ID.
    bool IsServerAuthenticated() const { return HasAuthority() && bAuthenticated; }
    void RejectAccount(const FString& Reason);
private:
    bool bAuthenticated = false;
    bool bVerifying = false;
    bool bRejected = false;
    bool bClientRequestPending = false;
    int32 Attempts = 0;
    FTimerHandle LoginDeadline;
    FTimerHandle VerificationDeadline;
    FTimerHandle DisconnectTimer;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ClientRequest;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ServerRequest;
    UPROPERTY(Transient) TObjectPtr<class UAccountLoginWidget> LoginWidget;
    void SetStatus(const FString& Message);
    void AcceptAccount(const FString& PlayerId);
    bool CanSendRealTicket() const;
    UFUNCTION(Server, Reliable) void ServerSubmitTicket(const FString& Ticket);
    UFUNCTION(Server, Reliable) void ServerLogoutAccount();
    UFUNCTION(Client, Reliable) void ClientAccountResult(bool bSuccess, const FString& Message, const FString& PlayerId);
};

UCLASS()
class GAMEAUTH_API AAccountGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAccountGameMode();
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& Transform) override;
};

UCLASS()
class GAMEAUTH_API UAccountLoginWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    void RefreshStatus(const FString& Message);
private:
    UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> EmailBox;
    UPROPERTY(Transient) TObjectPtr<class UEditableTextBox> PasswordBox;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<class UVerticalBox> Form;
    UPROPERTY(Transient) TObjectPtr<class UButton> LogoutButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> MockButton;
    UPROPERTY(Transient) TObjectPtr<class UButton> ResumeButton;
    UFUNCTION() void OnLogin();
    UFUNCTION() void OnRegister();
    UFUNCTION() void OnMock();
    UFUNCTION() void OnLogout();
    UFUNCTION() void OnResume();
};
