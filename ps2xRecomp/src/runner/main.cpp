#include "ps2recomp/ps2_recompiler.h"
#include <iostream>
#include <string>

using namespace ps2recomp;

void printUsage()
{
    std::cout << "PS2Recomp - A static recompiler for PlayStation 2 ELF files\n";
    std::cout << "Usage: ps2recomp <config.toml>\n";
    std::cout << "  config.toml: Configuration file for the recompiler\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    std::string configPath;
    bool useIR = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--use-ir") {
            useIR = true;
        } else if (configPath.empty()) {
            configPath = arg;
        }
    }

    if (configPath.empty()) {
        printUsage();
        return 1;
    }

    try
    {
        PS2Recompiler recompiler(configPath);
        recompiler.setUseIR(useIR);

        if (!recompiler.initialize())
        {
            std::cerr << "Failed to initialize recompiler\n";
            return 1;
        }

        if (!recompiler.recompile())
        {
            std::cerr << "Recompilation failed\n";
            return 1;
        }

        recompiler.generateOutput();

        std::cout << "Recompilation completed successfully\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}