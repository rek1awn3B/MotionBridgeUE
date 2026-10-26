using UnrealBuildTool;

public class MotionBridgeEditor : ModuleRules
{
    public MotionBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "Kismet",
            "BlueprintGraph",
            "AnimGraph",
            "AnimGraphRuntime",
            "IKRig",
            "IKRigDeveloper",
            "TP_ThirdPerson"
        });
    }
}
