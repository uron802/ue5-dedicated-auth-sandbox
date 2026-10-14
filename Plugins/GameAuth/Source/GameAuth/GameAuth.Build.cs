using UnrealBuildTool;

public class GameAuth : ModuleRules
{
    public GameAuth(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "DeveloperSettings" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "Json", "Slate", "SlateCore", "NetCore", "InputCore", "DTLSHandlerComponent", "PacketHandler" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
