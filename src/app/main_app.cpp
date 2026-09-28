#include "app/application.h"
#include <iostream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main() {
    try {
        photon::Application app;
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "PhotonEngine — Fatal Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
