using UnrealBuildTool;

public class MRSuitViewer : ModuleRules
{
	public MRSuitViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "XRBase" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AndroidPermission",        // runtime request for com.oculus.permission.USE_SCENE
			"MRUtilityKit",             // room + floor understanding (Meta MR Utility Kit)
			"OculusXRAnchors",          // types referenced by MRUK public headers
			"OculusXRHMD",              // types referenced by the passthrough headers
			"OculusXRPassthrough",      // persistent passthrough underlay
			"OculusXRInput",            // Hand tracking and pinch input
			"ProceduralMeshComponent",  // included by the MRUK anchor header
		});
	}
}
