#include "app/application.h"
#include <iostream>

int main() {
    try {
        photon::Application app;
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
