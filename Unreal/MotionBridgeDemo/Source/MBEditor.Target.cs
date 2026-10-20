// Short editor target used to keep Unreal intermediate paths below Windows'
// legacy 260-character limit in this deeply nested workspace.

using UnrealBuildTool;

public class MBEditorTarget : TargetRules
{
    public MBEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        ExtraModuleNames.Add("TP_ThirdPerson");
    }
}
