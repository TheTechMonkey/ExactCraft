using UnrealBuildTool;

public class ExactCraft : ModuleRules
{
    public ExactCraft(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        bLegacyPublicIncludePaths = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "FactoryGame",
            "InputCore",
            "SML",
            "Slate",
            "SlateCore",
            "UMG"
        });
        PrivateDependencyModuleNames.Add("AkAudio");
    }
}
