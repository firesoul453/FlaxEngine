// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#if PLATFORM_TOOLS_IOS

#include "iOSPlatformTools.h"
#include "Engine/Platform/File.h"
#include "Engine/Platform/FileSystem.h"
#include "Engine/Platform/CreateProcessSettings.h"
#include "Engine/Platform/iOS/iOSPlatformSettings.h"
#include "Engine/Core/Config/GameSettings.h"
#include "Engine/Core/Config/BuildSettings.h"
#include "Engine/Core/Collections/Dictionary.h"
#include "Engine/Graphics/Textures/TextureData.h"
#include "Engine/Graphics/PixelFormatExtensions.h"
#include "Engine/Content/Content.h"
#include "Engine/Content/JsonAsset.h"
#include "Engine/Engine/Globals.h"
#include "Editor/Editor.h"
#include "Editor/ProjectInfo.h"
#include "Editor/Cooker/GameCooker.h"
#include "Editor/Utilities/EditorUtilities.h"
#include <ThirdParty/pugixml/pugixml.hpp>
using namespace pugi;

IMPLEMENT_SETTINGS_GETTER(iOSPlatformSettings, iOSPlatform);

namespace
{
    String GetAppName()
    {
        const auto gameSettings = GameSettings::Get();
        String productName = gameSettings->ProductName;
        productName.Replace(TEXT(" "), TEXT(""));
        productName.Replace(TEXT("."), TEXT(""));
        productName.Replace(TEXT("-"), TEXT(""));
        return productName;
    }
}

const Char* iOSPlatformTools::GetDisplayName() const
{
    return TEXT("iOS");
}

const Char* iOSPlatformTools::GetName() const
{
    return TEXT("iOS");
}

PlatformType iOSPlatformTools::GetPlatform() const
{
    return PlatformType::iOS;
}

ArchitectureType iOSPlatformTools::GetArchitecture() const
{
    return ArchitectureType::ARM64;
}

DotNetAOTModes iOSPlatformTools::UseAOT() const
{
    return DotNetAOTModes::MonoAOTDynamic;    
}

PixelFormat iOSPlatformTools::GetTextureFormat(CookingData& data, TextureBase* texture, PixelFormat format)
{
    // TODO: add ETC compression support for iOS
    // TODO: add ASTC compression support for iOS

    if (PixelFormatExtensions::IsCompressedBC(format))
    {
        switch (format)
        {
        case PixelFormat::BC1_Typeless:
        case PixelFormat::BC2_Typeless:
        case PixelFormat::BC3_Typeless:
            return PixelFormat::R8G8B8A8_Typeless;
        case PixelFormat::BC1_UNorm:
        case PixelFormat::BC2_UNorm:
        case PixelFormat::BC3_UNorm:
            return PixelFormat::R8G8B8A8_UNorm;
        case PixelFormat::BC1_UNorm_sRGB:
        case PixelFormat::BC2_UNorm_sRGB:
        case PixelFormat::BC3_UNorm_sRGB:
            return PixelFormat::R8G8B8A8_UNorm_sRGB;
        case PixelFormat::BC4_Typeless:
            return PixelFormat::R8_Typeless;
        case PixelFormat::BC4_UNorm:
            return PixelFormat::R8_UNorm;
        case PixelFormat::BC4_SNorm:
            return PixelFormat::R8_SNorm;
        case PixelFormat::BC5_Typeless:
            return PixelFormat::R16G16_Typeless;
        case PixelFormat::BC5_UNorm:
            return PixelFormat::R16G16_UNorm;
        case PixelFormat::BC5_SNorm:
            return PixelFormat::R16G16_SNorm;
        case PixelFormat::BC7_Typeless:
        case PixelFormat::BC6H_Typeless:
            return PixelFormat::R16G16B16A16_Typeless;
        case PixelFormat::BC7_UNorm:
        case PixelFormat::BC6H_Uf16:
        case PixelFormat::BC6H_Sf16:
            return PixelFormat::R16G16B16A16_Float;
        case PixelFormat::BC7_UNorm_sRGB:
            return PixelFormat::R16G16B16A16_UNorm;
        default:
            return format;
        }
    }

    return format;
}

bool iOSPlatformTools::IsNativeCodeFile(CookingData& data, const String& file)
{
    String extension = FileSystem::GetExtension(file);
    return extension.IsEmpty() || extension == TEXT("dylib");
}

void iOSPlatformTools::OnBuildStarted(CookingData& data)
{
    // Adjust the cooking output folders for packaging app
    const Char* subDir = TEXT("FlaxGame/Data");
    data.DataOutputPath /= subDir;
    data.NativeCodeOutputPath /= subDir;
    data.ManagedCodeOutputPath /= subDir;

    PlatformTools::OnBuildStarted(data);
}

bool iOSPlatformTools::OnPostProcess(CookingData& data)
{
    const auto gameSettings = GameSettings::Get();
    const auto platformSettings = iOSPlatformSettings::Get();
    const auto platformDataPath = data.GetPlatformBinariesRoot();
    const auto projectVersion = Editor::Project->Version.ToString();
    const auto appName = GetAppName();

    // Setup package name (eg. com.company.project)
    String appIdentifier = platformSettings->AppIdentifier;
    {
        String productName = gameSettings->ProductName;
        productName.Replace(TEXT(" "), TEXT(""));
        productName.Replace(TEXT("."), TEXT(""));
        productName.Replace(TEXT("-"), TEXT(""));
        String companyName = gameSettings->CompanyName;
        companyName.Replace(TEXT(" "), TEXT(""));
        companyName.Replace(TEXT("."), TEXT(""));
        companyName.Replace(TEXT("-"), TEXT(""));
        appIdentifier.Replace(TEXT("${PROJECT_NAME}"), *productName, StringSearchCase::IgnoreCase);
        appIdentifier.Replace(TEXT("${COMPANY_NAME}"), *companyName, StringSearchCase::IgnoreCase);
        appIdentifier = appIdentifier.ToLower();
        for (int32 i = 0; i < appIdentifier.Length(); i++)
        {
            const auto c = appIdentifier[i];
            if (c != '_' && c != '.' && !StringUtils::IsAlnum(c))
            {
                LOG(Error, "Apple app identifier \'{0}\' contains invalid character. Only letters, numbers, dots and underscore characters are allowed.", appIdentifier);
                return true;
            }
        }
        if (appIdentifier.IsEmpty())
        {
            LOG(Error, "Apple app identifier is empty.", appIdentifier);
            return true;
        }
    }

    // Copy fresh Gradle project template
    if (FileSystem::CopyDirectory(data.OriginalOutputPath, platformDataPath / TEXT("Project"), true))
    {
        LOG(Error, "Failed to deploy XCode project to {0} from {1}", data.OriginalOutputPath, platformDataPath);
        return true;
    }

    // Format project template files
    Dictionary<String, String> configReplaceMap;
    configReplaceMap[TEXT("${AppName}")] = appName;
    configReplaceMap[TEXT("${AppIdentifier}")] = appIdentifier;
    configReplaceMap[TEXT("${AppTeamId}")] = platformSettings->AppTeamId;
    configReplaceMap[TEXT("${AppVersion}")] = TEXT("1"); // TODO: expose to iOS platform settings (matches CURRENT_PROJECT_VERSION in XCode)
    configReplaceMap[TEXT("${ProjectName}")] = gameSettings->ProductName;
    configReplaceMap[TEXT("${ProjectVersion}")] = projectVersion;
    configReplaceMap[TEXT("${HeaderSearchPaths}")] = Globals::StartupFolder;
    // TODO: screen rotation settings in XCode project from iOS Platform Settings
    {
        // Initialize auto-generated areas as empty
        configReplaceMap[TEXT("${PBXBuildFile}")] = String::Empty;
        configReplaceMap[TEXT("${PBXCopyFilesBuildPhaseFiles}")] = String::Empty;
        configReplaceMap[TEXT("${PBXFileReference}")] = String::Empty;
        configReplaceMap[TEXT("${PBXFrameworksBuildPhase}")] = String::Empty;
        configReplaceMap[TEXT("${PBXFrameworksGroup}")] = String::Empty;
        configReplaceMap[TEXT("${PBXFilesGroup}")] = String::Empty;
        configReplaceMap[TEXT("${PBXResourcesGroup}")] = String::Empty;
    }
    {
        // Rename dotnet license files to not mislead the actual game licensing
        FileSystem::MoveFile(data.DataOutputPath / TEXT("Dotnet/DOTNET-LICENSE.TXT"), data.DataOutputPath / TEXT("Dotnet/LICENSE.TXT"), true);
        FileSystem::MoveFile(data.DataOutputPath / TEXT("Dotnet/DOTNET-THIRD-PARTY-NOTICES.TXT"), data.DataOutputPath / TEXT("Dotnet/THIRD-PARTY-NOTICES.TXT"), true);
    }
    Array<String> files;
    FileSystem::DirectoryGetFiles(files, data.DataOutputPath, TEXT("*"), DirectorySearchOption::AllDirectories);
    for (const String& file : files)
    {
        String name = StringUtils::GetFileName(file);
        if (name == TEXT(".DS_Store") || name == TEXT("FlaxGame"))
            continue;
        String fileId = Guid::New().ToString(Guid::FormatType::N).Left(24);
        String projectPath = FileSystem::ConvertAbsolutePathToRelative(data.DataOutputPath, file);
        if (name.EndsWith(TEXT(".dylib")))
        {
            String frameworkId = Guid::New().ToString(Guid::FormatType::N).Left(24);
            String frameworkEmbedId = Guid::New().ToString(Guid::FormatType::N).Left(24);
            configReplaceMap[TEXT("${PBXBuildFile}")] += String::Format(TEXT("\t\t{0} /* {1} in Frameworks */ = {{isa = PBXBuildFile; fileRef = {2} /* {1} */; }};\n"), frameworkId, name, fileId);
            configReplaceMap[TEXT("${PBXBuildFile}")] += String::Format(TEXT("\t\t{0} /* {1} in Embed Frameworks */ = {{isa = PBXBuildFile; fileRef = {2} /* {1} */; settings = {{ATTRIBUTES = (CodeSignOnCopy, ); }}; }};\n"), frameworkEmbedId, name, fileId);
            configReplaceMap[TEXT("${PBXCopyFilesBuildPhaseFiles}")] += String::Format(TEXT("\t\t\t\t{0} /* {1} in Embed Frameworks */,\n"), frameworkEmbedId, name);
            configReplaceMap[TEXT("${PBXFileReference}")] += String::Format(TEXT("\t\t{0} /* {1} */ = {{isa = PBXFileReference; lastKnownFileType = \"compiled.mach-o.dylib\"; name = \"{1}\"; path = \"FlaxGame/Data/{2}\"; sourceTree = \"<group>\"; }};\n"), fileId, name, projectPath);
            configReplaceMap[TEXT("${PBXFrameworksBuildPhase}")] += String::Format(TEXT("\t\t\t\t{0} /* {1} in Frameworks */,\n"), frameworkId, name);
            configReplaceMap[TEXT("${PBXFrameworksGroup}")] += String::Format(TEXT("\t\t\t\t{0} /* {1} */,\n"), fileId, name);

            // Fix rpath id
            // TODO: run this only for dylibs during AOT process (other libs are fine)
            CreateProcessSettings proc;
            proc.FileName = TEXT("install_name_tool");
            proc.Arguments = String::Format(TEXT("-id \"@rpath/{0}\" \"{1}\""), name, file);
            Platform::CreateProcess(proc);
        }
        else
        {
            String fileRefId = Guid::New().ToString(Guid::FormatType::N).Left(24);
            configReplaceMap[TEXT("${PBXBuildFile}")] += String::Format(TEXT("\t\t{0} /* {1} in Resources */ = {{isa = PBXBuildFile; fileRef = {2} /* {1} */; }};\n"), fileRefId, name, fileId);
            configReplaceMap[TEXT("${PBXFileReference}")] += String::Format(TEXT("\t\t{0} /* {1} */ = {{isa = PBXFileReference; lastKnownFileType = file; name = \"{1}\"; path = \"Data/{2}\"; sourceTree = \"<group>\"; }};\n"), fileId, name, projectPath);
            configReplaceMap[TEXT("${PBXFilesGroup}")] += String::Format(TEXT("\t\t\t\t{0} /* {1} */,\n"), fileId, name);
            configReplaceMap[TEXT("${PBXResourcesGroup}")] += String::Format(TEXT("\t\t\t\t{0} /* {1} in Resources */,\n"), fileRefId, name);
        }
    }
    bool failed = false;
    failed |= EditorUtilities::ReplaceInFile(data.OriginalOutputPath / TEXT("FlaxGame.xcodeproj/project.pbxproj"), configReplaceMap);
    if (failed)
    {
        LOG(Error, "Failed to format XCode project");
        return true;
    }

    // TODO: update splash screen images

    // TODO: update game icon

    // Package application
    const auto buildSettings = BuildSettings::Get();
    if (buildSettings->SkipPackaging)
        return false;
    GameCooker::PackageFiles();
    LOG(Info, "Building app package...");
    // TODO: run XCode archive and export
#if 0
    const String ipaPath = data.OriginalOutputPath / appName + TEXT(".ipa");
    const String ipaCommand = String::Format(TEXT("zip -r -X {0}.ipa Payload iTunesArtwork"), appName);
    const int32 result = Platform::RunProcess(ipaCommand, data.OriginalOutputPath);
    if (result != 0)
    {
        data.Error(String::Format(TEXT("Failed to package app (result code: {0}). See log for more info."), result));
        return true;
    }
    LOG(Info, "Output application package: {0} (size: {1} MB)", ipaPath, FileSystem::GetFileSize(ipaPath) / 1024 / 1024);
#endif

    return false;
}

#endif
