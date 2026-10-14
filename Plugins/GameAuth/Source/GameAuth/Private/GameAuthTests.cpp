#include "GameAuth.h"
#include "AuthProtocol.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameAuthResponseTest, "GameAuth.Protocol.ExpiredAndMalformedResponses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGameAuthResponseTest::RunTest(const FString&)
{
    FString Id;
    TestFalse(TEXT("Missing response fails closed"), GameAuth::ReadVerifiedPlayerId(nullptr, Id));
    auto Data = MakeShared<FJsonObject>();
    auto User = MakeShared<FJsonObject>();
    User->SetStringField(TEXT("PlayFabId"), TEXT("SERVER-VERIFIED-ID"));
    Data->SetObjectField(TEXT("UserInfo"), User);
    TestFalse(TEXT("Missing expiration state fails closed"), GameAuth::ReadVerifiedPlayerId(Data, Id));
    Data->SetBoolField(TEXT("IsSessionTicketExpired"), true);
    TestFalse(TEXT("HTTP success with expired ticket is rejected"), GameAuth::ReadVerifiedPlayerId(Data, Id));
    Data->SetBoolField(TEXT("IsSessionTicketExpired"), false);
    TestTrue(TEXT("Valid response accepted"), GameAuth::ReadVerifiedPlayerId(Data, Id));
    TestEqual(TEXT("Identity comes from server response"), Id, FString(TEXT("SERVER-VERIFIED-ID")));
    User->SetStringField(TEXT("PlayFabId"), TEXT(""));
    TestFalse(TEXT("Empty identity rejected"), GameAuth::ReadVerifiedPlayerId(Data, Id));
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameAuthTicketTest, "GameAuth.Protocol.MockTicketExpiryAndReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGameAuthTicketTest::RunTest(const FString&)
{
    GameAuth::FEditorTicketStore Store;
    FString Id;
    TestFalse(TEXT("Forged token rejected"), Store.Consume(TEXT("EDITOR-forged"), 10, Id));
    FString Ticket = Store.Issue(10);
    TestTrue(TEXT("Issued ticket accepted"), Store.Consume(Ticket, 11, Id));
    TestTrue(TEXT("Issued identity is nonempty"), !Id.IsEmpty());
    TestFalse(TEXT("Ticket replay rejected"), Store.Consume(Ticket, 12, Id));
    Ticket = Store.Issue(10);
    TestFalse(TEXT("Expiry boundary rejected"), Store.Consume(Ticket, 40, Id));
    Ticket = Store.Issue(50);
    TestTrue(TEXT("Before expiry accepted"), Store.Consume(Ticket, 79.9, Id));
    return true;
}

class FGameAuthPIECheck : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    bool bSubmitted = false;
    double Started = FPlatformTime::Seconds();
    TArray<TWeakObjectPtr<AAccountPlayerController>> Clients;
    TWeakObjectPtr<UWorld> Server;
    FString PreviousTitleId;
    bool bPreviousMock = false;
public:
    explicit FGameAuthPIECheck(FAutomationTestBase* InTest) : Test(InTest)
    {
        auto* Settings = GetMutableDefault<UGameAuthSettings>();
        PreviousTitleId = Settings->TitleId;
        bPreviousMock = Settings->bEnableEditorMock;
        Settings->TitleId.Empty();
        Settings->bEnableEditorMock = true;
    }
    virtual ~FGameAuthPIECheck()
    {
        auto* Settings = GetMutableDefault<UGameAuthSettings>();
        Settings->TitleId = PreviousTitleId;
        Settings->bEnableEditorMock = bPreviousMock;
    }
    virtual bool Update() override
    {
        if (!bSubmitted)
        {
            Clients.Reset();
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* World = Context.World();
                if (!World || World->WorldType != EWorldType::PIE) continue;
                if (World->GetNetMode() == NM_DedicatedServer) Server = World;
                if (World->GetNetMode() == NM_Client)
                    if (auto* PC = Cast<AAccountPlayerController>(World->GetFirstPlayerController())) Clients.Add(PC);
            }
            if (!Server.IsValid() || Clients.Num() != 2)
            {
                if (FPlatformTime::Seconds() - Started < 15) return false;
                Test->AddError(TEXT("Start PIE with Play As Client, 2 clients, Run Under One Process, and GameAuth mock enabled first."));
                return true;
            }
            for (auto Client : Clients)
            {
                if (!Client->IsEditorMock()) { Test->AddError(TEXT("This test only uses EditorMock; no real accounts are created.")); return true; }
                Test->TestNull(TEXT("Client has no pawn before authentication"), Client->GetPawn());
                Test->TestFalse(TEXT("Client starts unauthenticated"), Client->IsAccountAuthenticated());
            }
            for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
            {
                Test->TestNull(TEXT("Server has no pawn before authentication"), It->Get()->GetPawn());
                Server->GetAuthGameMode()->RestartPlayer(It->Get());
                Test->TestNull(TEXT("RestartPlayer cannot bypass authentication"), It->Get()->GetPawn());
            }
            for (auto Client : Clients) Client->LoginEditorMock();
            bSubmitted = true;
            Started = FPlatformTime::Seconds();
            return false;
        }
        if (FPlatformTime::Seconds() - Started < 3) return false;
        if (!Server.IsValid()) { Test->AddError(TEXT("Dedicated server disappeared")); return true; }
        TSet<FString> Ids;
        int32 ServerPlayers = 0;
        for (auto It = Server->GetPlayerControllerIterator(); It; ++It)
        {
            auto* PC = Cast<AAccountPlayerController>(It->Get());
            if (!PC) continue;
            ++ServerPlayers;
            Test->TestTrue(TEXT("Server verified the account"), PC->IsServerAuthenticated());
            Test->TestNotNull(TEXT("Server spawned the player after verification"), PC->GetPawn().Get());
            Ids.Add(PC->AuthenticatedPlayerId);
        }
        Test->TestEqual(TEXT("Two server players"), ServerPlayers, 2);
        Test->TestEqual(TEXT("Two distinct verified identities"), Ids.Num(), 2);
        for (auto Client : Clients)
        {
            if (!Client.IsValid()) { Test->AddError(TEXT("Client disconnected")); continue; }
            Test->TestTrue(TEXT("Client received acceptance via RPC"), Client->IsAccountAuthenticated());
            Test->TestNotNull(TEXT("Client possesses its replicated pawn"), Client->GetPawn().Get());
            Test->TestTrue(TEXT("Client identity matches a server-verified identity"), Ids.Contains(Client->AuthenticatedPlayerId));
            int32 Characters = 0;
            for (TActorIterator<ACharacter> It(Client->GetWorld()); It; ++It) ++Characters;
            Test->TestEqual(TEXT("Both players replicated to client"), Characters, 2);
        }
        return true;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameAuthPIETest, "GameAuth.PIE.TwoPlayerDedicatedServerAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGameAuthPIETest::RunTest(const FString&)
{
    auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
    Settings->SetPlayNetMode(PIE_Client);
    Settings->SetPlayNumberOfClients(2);
    Settings->SetRunUnderOneProcess(true);
    FRequestPlaySessionParams Params;
    Params.SessionDestination = EPlaySessionDestinationType::InProcess;
    Params.WorldType = EPlaySessionWorldType::PlayInEditor;
    GEditor->RequestPlaySession(Params);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGameAuthPIECheck>(this));
    return true;
}
#endif
#endif
