# Firmware unit tests (run on the PC)

Tests the hardware-independent logic in `irrigation/src/irrigation_logic/` (controller, relay polarity, soil %).
Needs any C++11 compiler, no Arduino or ESP32.

```bash
cd firmware/tests
g++ -std=c++11 -Wall -Wextra -o test_controller test_controller.cpp && ./test_controller
g++ -std=c++11 -Wall -Wextra -o test_net test_net.cpp && ./test_net     # each prints "N checks, 0 failed"
```

Windows: use g++ from MSYS2/MinGW (`test_controller.exe`), or `cl /EHsc test_controller.cpp` from a Visual Studio prompt.

No compiler installed? `pip install ziglang`, then `python -m ziglang c++ -std=c++11 -o test_controller test_controller.cpp`
(this is how the tests were run on the development PC).
