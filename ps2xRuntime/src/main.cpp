#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "register_functions.h"
#include "games_database.h"
#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <iostream>
#include <string>
#include <filesystem>

std::string normalizeGameId(const std::string& folderName)
{
    std::string result = folderName;

    size_t underscore = result.find('_');
    if (underscore != std::string::npos)
        result[underscore] = '-';

    size_t dot = result.find('.');
    if (dot != std::string::npos)
        result.erase(dot, 1);

    return result;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        return 1;
    }

    std::string elfPath = argv[1];
    std::filesystem::path pathObj(elfPath);
    std::string folderName = pathObj.filename().string();
    std::string normalizedId = normalizeGameId(folderName);

    std::string windowTitle = "PS2-Recomp | ";
    const char* gameName = getGameName(normalizedId);

    if (gameName)
    {
        windowTitle += std::string(gameName) + " | " + folderName;
    }
    else
    {
        windowTitle += folderName;
    }

    PS2Runtime runtime;
    if (!runtime.initialize(windowTitle.c_str()))
    {
        std::cerr << "Failed to initialize PS2 runtime" << std::endl;
        return 1;
    }

    registerAllFunctions(runtime);

    // .ctors static initializers (0x1452e0 table) — missed by recompiler
    // due to 0x98C698C6 padding bytes before them making Ghidra classify them as data.
    // Called via jalr v0 from __do_global_ctors_aux at 0x12fbc0.
    // Both are simple BSS/global-zeroing init functions; safe to no-op.
    static auto ctorsNoop = [](uint8_t*, R5900Context* ctx, PS2Runtime*) {
        ctx->pc = getRegU32(ctx, 31); // return via ra
    };
    runtime.registerFunction(0x00145120, ctorsNoop);
    runtime.registerFunction(0x00145280, ctorsNoop);

    if (!runtime.loadELF(elfPath))
    {
        std::cerr << "Failed to load ELF file: " << elfPath << std::endl;
        return 1;
    }

    runtime.run();

#ifdef _DEBUG
    ps2_log::print_saved_location();
#endif
    return 0;
}