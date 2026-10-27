// version.h -- Fuente unica de verdad de la version de WsCampReborn.
//
// La version se usa en tres sitios, todos derivados de aqui:
//   1. El cuadro de dialogo "Acerca de..." (Ayuda -> Acerca de...).
//   2. El titulo de la ventana.
//   3. El nombre del release "latest" y de los ficheros subidos.
//
// CI puede sobreescribirla con -DWSCAMP_VERSION_MAJOR=... etc.
#pragma once

#ifndef WSCAMP_VERSION_MAJOR
#define WSCAMP_VERSION_MAJOR 1
#endif
#ifndef WSCAMP_VERSION_MINOR
#define WSCAMP_VERSION_MINOR 2
#endif
#ifndef WSCAMP_VERSION_PATCH
#define WSCAMP_VERSION_PATCH 0
#endif

#define WSCAMP_STRINGIFY_(x) #x
#define WSCAMP_STRINGIFY(x)  WSCAMP_STRINGIFY_(x)

#define WSCAMP_VERSION_STRING            \
    WSCAMP_STRINGIFY(WSCAMP_VERSION_MAJOR) "." \
    WSCAMP_STRINGIFY(WSCAMP_VERSION_MINOR) "." \
    WSCAMP_STRINGIFY(WSCAMP_VERSION_PATCH)

#define WSCAMP_APP_NAME "WsCampReborn"
#define WSCAMP_VERSION_FULL WSCAMP_APP_NAME " " WSCAMP_VERSION_STRING
