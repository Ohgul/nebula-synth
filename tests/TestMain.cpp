// Punto de entrada único de las pruebas: NebulaTests <Nombre> [argumentos...]
#include <cstdio>
#include <cstring>

#define NEBULA_TEST(name) int nebulaTest_##name (int, char**);
#include "nebula_test_list.h"
#undef NEBULA_TEST

int main (int argc, char** argv)
{
    if (argc >= 2)
    {
        const char* want = argv[1];
       #define NEBULA_TEST(name) if (std::strcmp (want, #name) == 0) return nebulaTest_##name (argc - 1, argv + 1);
       #include "nebula_test_list.h"
       #undef NEBULA_TEST
    }
    std::printf ("Uso: NebulaTests <prueba> [argumentos]\nPruebas:");
   #define NEBULA_TEST(name) std::printf (" %s", #name);
   #include "nebula_test_list.h"
   #undef NEBULA_TEST
    std::printf ("\n");
    return 2;
}
