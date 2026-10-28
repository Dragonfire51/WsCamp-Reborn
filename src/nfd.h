// nfd.h -- Dialogo nativo de apertura de ficheros.
//
// Usa la API del sistema cuando esta disponible y, si el usuario cancela o la
// plataforma no la ofrece, cae en el explorador integrado de SDL.
#pragma once

#include <string>

// Devuelve la ruta elegida, o cadena vacia si el usuario cancela.
std::string OpenFileDialog(const std::string &start_dir);
