#include "RuntimeAssets.h"
#include "ResourceIds.h"

#include <windows.h>

namespace dfvst
{
namespace
{
HMODULE getCurrentModuleHandle()
{
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCSTR>(&getCurrentModuleHandle);
    if (!::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              address,
                              &module))
    {
        return nullptr;
    }

    return module;
}

juce::File getCurrentModuleFile()
{
    const auto module = getCurrentModuleHandle();
    if (module == nullptr)
        return {};

    char path[MAX_PATH] = {};
    const auto length = ::GetModuleFileNameA(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};

    return juce::File(juce::String::fromUTF8(path, static_cast<int>(length)));
}

juce::File buildRuntimeRootDirectory()
{
    const auto moduleFile = getCurrentModuleFile();
    if (!moduleFile.existsAsFile())
        return {};

    const auto versionedName =
        moduleFile.getFileNameWithoutExtension()
        + "-"
        + juce::String(moduleFile.getSize())
        + "-"
        + juce::String(moduleFile.getLastModificationTime().toMilliseconds());

    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("DeepFilterNetVst")
        .getChildFile(versionedName);
}

bool resourceMatchesFile(HMODULE module, int resourceId, const juce::File& target)
{
    if (!target.existsAsFile())
        return false;

    const auto resource = ::FindResourceA(module, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (resource == nullptr)
        return false;

    const auto resourceSize = static_cast<int64_t>(::SizeofResource(module, resource));
    return resourceSize > 0 && target.getSize() == resourceSize;
}

bool extractResourceToFile(HMODULE module, int resourceId, const juce::File& target)
{
    if (module == nullptr || target == juce::File())
        return false;

    if (resourceMatchesFile(module, resourceId, target))
        return true;

    const auto resource = ::FindResourceA(module, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (resource == nullptr)
        return false;

    const auto resourceSize = static_cast<int64_t>(::SizeofResource(module, resource));
    if (resourceSize <= 0)
        return false;

    const auto loadedResource = ::LoadResource(module, resource);
    if (loadedResource == nullptr)
        return false;

    const auto* data = static_cast<const char*>(::LockResource(loadedResource));
    if (data == nullptr)
        return false;

    const auto parentDirectory = target.getParentDirectory();
    if (!parentDirectory.exists() && !parentDirectory.createDirectory())
        return false;

    juce::TemporaryFile temporaryFile(target);
    {
        juce::FileOutputStream stream(temporaryFile.getFile());
        if (!stream.openedOk())
            return false;

        if (!stream.write(data, resourceSize))
            return false;

        stream.flush();
    }

    return temporaryFile.overwriteTargetFileWithTemporary() && resourceMatchesFile(module, resourceId, target);
}
}

std::optional<RuntimeAssetPaths> EmbeddedRuntimeAssets::ensureExtracted()
{
    const auto module = getCurrentModuleHandle();
    if (module == nullptr)
        return std::nullopt;

    RuntimeAssetPaths paths;
    paths.rootDirectory = buildRuntimeRootDirectory();
    if (paths.rootDirectory == juce::File())
        return std::nullopt;

    paths.dfDll = paths.rootDirectory.getChildFile("df.dll");
    paths.modelArchive = paths.rootDirectory.getChildFile("Models").getChildFile("DeepFilterNet3_onnx.tar.gz");

    if (!extractResourceToFile(module, IDR_DF_DLL, paths.dfDll))
        return std::nullopt;

    if (!extractResourceToFile(module, IDR_MODEL_FILE, paths.modelArchive))
        return std::nullopt;

    return paths;
}
}
