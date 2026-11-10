#include <cstdlib>
#include <iostream>
import oryn.app;

int main() {
    // Select before Slint creates its backend and before any worker starts.
    // Keep an explicit environment override available for renderer diagnostics.
    if (!std::getenv("SLINT_BACKEND")) {
#ifdef _WIN32
        const auto error = _putenv_s("SLINT_BACKEND", "winit-software");
#else
        const auto error = setenv("SLINT_BACKEND", "winit-software", 0);
#endif
        if (error != 0) {
            std::cerr << "Unable to select the software renderer.\n";
            return 1;
        }
    }
    return oryn::app::Application{}.run();
}
