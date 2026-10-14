#include "GameAuth.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace
{
    void StartEncryptedPIE()
    {
        auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
        Settings->SetPlayNetMode(PIE_Client);
        Settings->SetPlayNumberOfClients(2);
        Settings->SetRunUnderOneProcess(true);
        FRequestPlaySessionParams Params;
        Params.SessionDestination = EPlaySessionDestinationType::InProcess;
        Params.WorldType = EPlaySessionWorldType::PlayInEditor;
        GEditor->RequestPlaySession(Params);
    }

    struct FPinFailureObserver : FOutputDevice
    {
        int32 Failures = 0;
        virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            if (Category == TEXT("LogDTLSHandler") && FString(Message).Contains(TEXT("Fingerprint validation failure"))) ++Failures;
        }
    };

    class FTransportPIECheck : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        double Started = FPlatformTime::Seconds();
        bool bWrongPin;
        FString OldPin;
        FPinFailureObserver Observer;
    public:
        FTransportPIECheck(FAutomationTestBase* InTest, bool InWrongPin) : Test(InTest), bWrongPin(InWrongPin)
        {
            auto* Settings = GetMutableDefault<UGameAuthSettings>();
            OldPin = Settings->ServerCertificateFingerprint;
            Settings->ServerCertificateFingerprint = bWrongPin ? FString::ChrN(64, TCHAR('0')) : TEXT("");
            if (bWrongPin) GLog->AddOutputDevice(&Observer);
            StartEncryptedPIE();
        }
        virtual ~FTransportPIECheck()
        {
            if (bWrongPin) GLog->RemoveOutputDevice(&Observer);
            GetMutableDefault<UGameAuthSettings>()->ServerCertificateFingerprint = OldPin;
        }
        virtual bool Update() override
        {
            UWorld* Server = nullptr;
            TArray<AAccountPlayerController*> Clients;
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* World = Context.World();
                if (!World || World->WorldType != EWorldType::PIE) continue;
                if (World->GetNetMode() == NM_DedicatedServer) Server = World;
                if (World->GetNetMode() == NM_Client)
                    if (auto* PC = Cast<AAccountPlayerController>(World->GetFirstPlayerController())) Clients.Add(PC);
            }
            const double Elapsed = FPlatformTime::Seconds() - Started;
            if (bWrongPin)
            {
                if (Elapsed < 5) return false;
                Test->TestTrue(TEXT("DTLS actually rejected a mismatched server certificate"), Observer.Failures >= 2);
                Test->TestEqual(TEXT("No player controllers established with the wrong pin"), Clients.Num(), 0);
                int32 ServerPlayers = 0;
                if (Server) for (auto It = Server->GetPlayerControllerIterator(); It; ++It) ++ServerPlayers;
                Test->TestEqual(TEXT("Server did not admit unauthenticated transport"), ServerPlayers, 0);
                GEditor->RequestEndPlayMap();
                return true;
            }
            if (!Server || Clients.Num() != 2)
            {
                if (Elapsed < 15) return false;
                Test->AddError(TEXT("Two DTLS clients failed to reach the dedicated server."));
                return true;
            }
            for (auto* PC : Clients)
            {
                if (!PC->HasActorBegunPlay() || !PC->GetNetConnection())
                {
                    if (Elapsed < 15) return false;
                    Test->AddError(TEXT("Client actor initialization did not complete."));
                    return true;
                }
            }
            for (auto* PC : Clients)
            {
                Test->TestTrue(TEXT("Client controller was delivered on an encrypted connection"), PC->IsGameConnectionEncrypted());
                Test->TestTrue(TEXT("Client encryption component is enabled"), PC->GetNetConnection() && PC->GetNetConnection()->IsEncryptionEnabled());
                Test->TestFalse(TEXT("Encrypted transport does not bypass account authentication"), PC->IsAccountAuthenticated());
                Test->TestNull(TEXT("No playable pawn before account login"), PC->GetPawn());
            }
            int32 ServerPlayers = 0;
            for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC = Cast<AAccountPlayerController>(It->Get());
                if (!PC) continue;
                ++ServerPlayers;
                Test->TestTrue(TEXT("Server-side connection is encrypted"), PC->IsGameConnectionEncrypted());
                Test->TestNull(TEXT("Server has not spawned an unauthenticated pawn"), PC->GetPawn());
            }
            Test->TestEqual(TEXT("Two encrypted server connections"), ServerPlayers, 2);
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTLSTwoClientsTest, "GameAuth.Transport.TwoEncryptedDedicatedClients",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTLSTwoClientsTest::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTransportPIECheck>(this, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTLSWrongPinTest, "GameAuth.Transport.RejectWrongServerCertificate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTLSWrongPinTest::RunTest(const FString&)
{
    AddExpectedError(TEXT("Fingerprint validation failure"), EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("Handshaking failed with result"), EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("Alert: fatal Desc: certificate unknown"), EAutomationExpectedErrorFlags::Contains, 0);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTransportPIECheck>(this, true));
    return true;
}
#endif
