// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class EasyFFMPEG : ModuleRules
{
	private string ModulePath
    {
		get { return ModuleDirectory; }
    }

	private string ThirdPartyPath
    {
		get { return Path.GetFullPath(Path.Combine(ModulePath, "../../ThirdParty/")); }
    }

	private string UProjectPath
    {
		get { return Directory.GetParent(ModulePath).Parent.FullName; }
    }

	private void CopyToBinaries(string FilePath, ReadOnlyTargetRules Type)
    {
		string binariesDir = Path.Combine(UProjectPath, "Binaries", Target.Platform.ToString());
		string filename = Path.GetFileName(FilePath);

		System.Console.WriteLine("Writing file " + FilePath + " to " + binariesDir);

		if (!Directory.Exists(binariesDir))
        {
			Directory.CreateDirectory(binariesDir);
        }

		if (!File.Exists(Path.Combine(binariesDir, filename)))
        {
			File.Copy(FilePath, Path.Combine(binariesDir, filename), true);
        }
    }

	public bool LoadFFmpeg(ReadOnlyTargetRules Target)
    {
		bool isLibrarySupported = false;

		if (Target.Platform == UnrealTargetPlatform.Win64)
        {
			isLibrarySupported = true;

			string libType = "x64_" + (Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release");
			string LibrariesPath = Path.Combine(ThirdPartyPath, "ffmpeg", "libs", libType);
			string BinariesPath = Path.Combine(ThirdPartyPath, "ffmpeg", "bin", libType);

			System.Console.WriteLine("... LibrariesPath ---->" + LibrariesPath);

			string zlibLibName = "zlib" + (Target.Configuration == UnrealTargetConfiguration.Debug ? "d" : "") + ".lib";

            PublicAdditionalLibraries.AddRange(
				new string[] 
				{
                    Path.Combine(LibrariesPath, "avcodec.lib"),
                    Path.Combine(LibrariesPath, "avdevice.lib"),
                    Path.Combine(LibrariesPath, "avfilter.lib"),
                    Path.Combine(LibrariesPath, "avformat.lib"),
                    Path.Combine(LibrariesPath, "AviSynth.lib"),
                    Path.Combine(LibrariesPath, "avresample.lib"),
                    Path.Combine(LibrariesPath, "avutil.lib"),
                    Path.Combine(LibrariesPath, "libmp3lame.lib"),
                    Path.Combine(LibrariesPath, "libx264.lib"),
                    Path.Combine(LibrariesPath, "postproc.lib"),
                    Path.Combine(LibrariesPath, "swresample.lib"),
                    Path.Combine(LibrariesPath, "swscale.lib"),
                    Path.Combine(LibrariesPath, "wavpackdll.lib"),
                    //Path.Combine(LibrariesPath, zlibLibName),
                }
			);

			string zlibDllName = "zlib" + (Target.Configuration == UnrealTargetConfiguration.Debug ? "d1" : "1") + ".dll";

			string[] dllNames = 
			{
				"avcodec-58.dll",
				"avdevice-58.dll",
				"avfilter-7.dll",
				"avformat-58.dll",
				"AviSynth.dll",
				"avresample-4.dll",
				"avutil-56.dll",
				"libmp3lame.dll",
				"libx264-157.dll",
				"postproc-55.dll",
				"swresample-3.dll",
				"swscale-5.dll",
				"wavpackdll.dll",
				//zlibDllName,
			};

			foreach (string dllName in dllNames)
			{
				PublicDelayLoadDLLs.Add(dllName);
				RuntimeDependencies.Add(Path.Combine(BinariesPath, dllName), StagedFileType.NonUFS);
            }
        }

		if (isLibrarySupported)
        {
			PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "ffmpeg", "include"));
        }

		return isLibrarySupported;
    }

	public EasyFFMPEG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects",
				"MovieSceneCapture",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        if (Target.Type == TargetRules.TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        LoadFFmpeg(Target);
	}
}
