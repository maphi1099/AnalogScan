// Kompositionswurzel der Kommandozeile. Platzhalter des Projektgerüsts (M0).

#include <iostream>
#include <span>
#include <string_view>

int main(int argc, char** argv)
{
    const std::span args(argv, static_cast<std::size_t>(argc));
    for (const char* arg : args.subspan(1)) {
        if (std::string_view(arg) == "--version") {
            std::cout << "autoneg-cli 1.0.0\n";
        }
    }
    return 0;
}
