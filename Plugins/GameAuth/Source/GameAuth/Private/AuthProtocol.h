#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GameAuth
{
    inline bool ReadVerifiedPlayerId(const TSharedPtr<FJsonObject>& Data, FString& OutId)
    {
        OutId.Empty();
        if (!Data) return false;
        // PlayFab can return HTTP 200 with IsSessionTicketExpired=true.
        bool bExpired = false;
        if (!Data->TryGetBoolField(TEXT("IsSessionTicketExpired"), bExpired) || bExpired) return false;
        const TSharedPtr<FJsonObject>* User = nullptr;
        return Data->TryGetObjectField(TEXT("UserInfo"), User) && *User &&
            (*User)->TryGetStringField(TEXT("PlayFabId"), OutId) && !OutId.IsEmpty() && OutId.Len() <= 128;
    }

#if WITH_EDITOR
    // Only used inside one editor process. There is deliberately no packaged/mock network API.
    class FEditorTicketStore
    {
        struct FTicket { FString Id; double Expires; };
        TMap<FString, FTicket> Tickets;
    public:
        FString Issue(double Now)
        {
            for (auto It = Tickets.CreateIterator(); It; ++It) if (It.Value().Expires <= Now) It.RemoveCurrent();
            if (Tickets.Num() >= 128) return TEXT("");
            const FString Token = TEXT("EDITOR-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Tickets.Add(Token, {TEXT("DEV-") + FGuid::NewGuid().ToString(EGuidFormats::Digits), Now + 30.});
            return Token;
        }
        bool Consume(const FString& Token, double Now, FString& Id)
        {
            Id.Empty();
            FTicket Ticket;
            if (!Tickets.RemoveAndCopyValue(Token, Ticket) || Ticket.Expires <= Now) return false;
            Id = Ticket.Id;
            return true;
        }
    };
    inline FEditorTicketStore& EditorTickets() { static FEditorTicketStore Store; return Store; }
#endif
}
