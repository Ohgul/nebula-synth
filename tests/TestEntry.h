#pragma once
// Cada prueba puede compilarse sola (su función es main) o dentro del ejecutable único
// NebulaTests (CMake le da otro nombre con NEBULA_TEST_ENTRY y TestMain.cpp elige cuál correr).
#ifndef NEBULA_TEST_ENTRY
 #define NEBULA_TEST_ENTRY main
#endif
