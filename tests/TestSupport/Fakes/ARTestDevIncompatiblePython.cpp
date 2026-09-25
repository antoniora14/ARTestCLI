#include <iostream>

int main() {
    // Valid probe JSON from a deliberately unsupported interpreter, without any generation tooling.
    std::cout << R"({"implementation":"CPython","version":[3,12],"platform":"win32","machine":"AMD64","bits":64,"gil":true,"venv":true,"pip":true})" << '\n';
    return 0;
}
