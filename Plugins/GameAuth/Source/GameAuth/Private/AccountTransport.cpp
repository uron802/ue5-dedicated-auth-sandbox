#include "GameAuth.h"
#include "DTLSCertStore.h"
#include "DTLSCertificate.h"
#include "Engine/Engine.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAccountTransport, Log, All);

namespace
{
    const FString TransportToken = TEXT("GameAuth-DTLS-v1");
    const FString PIEServerCertId = TEXT("GameAuth-PIE-Server");
    const FString ServerCertId = TEXT("GameAuth-Server");

#if WITH_SSL
    TSharedPtr<FDTLSCertificate> PIECertificate()
    {
#if WITH_EDITOR
        auto Cert = FDTLSCertStore::Get().GetCert(PIEServerCertId);
        if (!Cert) Cert = FDTLSCertStore::Get().CreateCert(FTimespan::FromDays(1), PIEServerCertId);
        return Cert;
#else
        return nullptr;
#endif
    }

    bool DecodeFingerprint(const FString& Text, TArray<uint8>& Result)
    {
        Result.Reset();
        FString Hex = Text.Replace(TEXT(":"), TEXT("")).Replace(TEXT(" "), TEXT(""));
        if (Hex.Len() != 64) return false;
        for (TCHAR C : Hex) if (!FChar::IsHexDigit(C)) return false;
        Result.SetNumUninitialized(32);
        return HexToBytes(Hex, Result.GetData()) == 32;
    }
#endif
}

bool UAccountGameInstance::IsEditorPIE() const
{
#if WITH_EDITOR
    return GetWorldContext() && GetWorldContext()->WorldType == EWorldType::PIE;
#else
    return false;
#endif
}

void UAccountGameInstance::Init()
{
    Super::Init();
    // Engine broadcasts this before BeginHandshake/SendInitialJoin, including in PIE.
    // The token identifies our protocol, never a password, key, or PlayFab ticket.
    PendingConnectionHandle = FNetDelegates::OnPendingNetGameConnectionCreated.AddWeakLambda(this, [this](UPendingNetGame* Pending)
    {
        const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGame(Pending);
        if (Context && Context->OwningGameInstance == this && !Pending->URL.HasOption(TEXT("EncryptionToken=")))
            Pending->URL.AddOption(*FString::Printf(TEXT("EncryptionToken=%s"), *TransportToken));
    });
}

void UAccountGameInstance::Shutdown()
{
    FNetDelegates::OnPendingNetGameConnectionCreated.Remove(PendingConnectionHandle);
#if WITH_SSL
    for (const FString& Id : ClientCertificateIds) FDTLSCertStore::Get().RemoveCert(Id);
#endif
    ClientCertificateIds.Empty();
    Super::Shutdown();
}

void UAccountGameInstance::ReceivedNetworkEncryptionToken(const FString& Token, const FOnEncryptionKeyResponse& Delegate)
{
    FEncryptionKeyResponse Response;
    if (Token != TransportToken)
    {
        Response.Response = EEncryptionResponse::InvalidToken;
        Response.ErrorMsg = TEXT("Unsupported encrypted transport protocol.");
        Delegate.ExecuteIfBound(Response);
        return;
    }
#if WITH_SSL
    TSharedPtr<FDTLSCertificate> Cert;
    if (IsEditorPIE())
    {
        Cert = PIECertificate();
        Response.EncryptionData.Identifier = PIEServerCertId;
    }
    else
    {
        // Private key PEM must be provisioned only on the actual dedicated server.
        Cert = FDTLSCertStore::Get().GetCert(ServerCertId);
        if (!Cert)
        {
            const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("GAMEAUTH_DTLS_CERTIFICATE_FILE"));
            if (!Path.IsEmpty() && FPaths::FileExists(Path)) Cert = FDTLSCertStore::Get().ImportCert(Path, ServerCertId);
        }
        Response.EncryptionData.Identifier = ServerCertId;
    }
    if (Cert) Response.Response = EEncryptionResponse::Success;
    else Response.ErrorMsg = TEXT("Server DTLS certificate is not configured.");
#else
    Response.ErrorMsg = TEXT("DTLS is unavailable on this platform.");
#endif
    Delegate.ExecuteIfBound(Response);
}

void UAccountGameInstance::ReceivedNetworkEncryptionAck(const FOnEncryptionKeyResponse& Delegate)
{
    FEncryptionKeyResponse Response;
#if WITH_SSL
    // A configured pin is also honored in PIE, allowing an actual wrong-pin test.
    const FString Pin = GetDefault<UGameAuthSettings>()->ServerCertificateFingerprint;
    bool bHavePin = false;
    if (!Pin.IsEmpty()) bHavePin = DecodeFingerprint(Pin, Response.EncryptionData.Fingerprint);
    else if (IsEditorPIE())
    {
        const auto Cert = PIECertificate();
        if (Cert)
        {
            const auto Fingerprint = Cert->GetFingerprint();
            Response.EncryptionData.Fingerprint.Append(Fingerprint.GetData(), Fingerprint.Num());
            bHavePin = true;
        }
    }
    if (bHavePin)
    {
        const FString ClientId = TEXT("GameAuth-Client-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (FDTLSCertStore::Get().CreateCert(FTimespan::FromHours(4), ClientId))
        {
            ClientCertificateIds.Add(ClientId);
            Response.EncryptionData.Identifier = ClientId;
            Response.Response = EEncryptionResponse::Success;
        }
        else Response.ErrorMsg = TEXT("Client DTLS certificate generation failed.");
    }
    else Response.ErrorMsg = TEXT("A trusted SHA-256 server certificate fingerprint is required.");
#else
    Response.ErrorMsg = TEXT("DTLS is unavailable on this platform.");
#endif
    Delegate.ExecuteIfBound(Response);
}

EEncryptionFailureAction UAccountGameInstance::ReceivedNetworkEncryptionFailure(UNetConnection* Connection)
{
    // Explicitly reject even in PIE; no downgrade to a plaintext game connection.
    UE_LOG(LogAccountTransport, Warning, TEXT("Rejected connection without the required encrypted handshake."));
    return EEncryptionFailureAction::RejectConnection;
}
