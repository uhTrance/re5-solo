// Loads build\dinput8.dll in a plain 32-bit process to check DllMain and the export work outside the game.
#include <windows.h>
#include <cstdio>

int main(int argc, char** argv) {
    HMODULE h = LoadLibraryA(argc > 1 ? argv[1] : "dinput8.dll");
    printf("LoadLibrary -> %p (err %lu)\n", h, GetLastError());
    if (!h) return 1;
    printf("DirectInput8Create -> %p\n", GetProcAddress(h, "DirectInput8Create"));
    Sleep(1500);
    return 0;
}
