// nfd.cpp -- Dialogo nativo de apertura de ficheros.
//
// Windows: GetOpenFileNameW (commdlg32).
// macOS:   osascript -> choose file.
// Linux:   zenity o kdialog, los dos habituales en escritorios GTK y KDE.
//
// Si no hay dialogo nativo disponible, devuelve una cadena vacia y el
// llamador abre el explorador integrado de SDL.
#include "nfd.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#else
#  include <sys/wait.h>
#endif

namespace {

const char *FILTER_PAT   = "*.ws;*.wsc";
const char *DIALOG_TITLE = "Abrir ROM";

#if defined(_WIN32)
const char *FILTER_DESC  = "ROMs WonderSwan (*.ws;*.wsc)";
#else
// Envuelve una ruta entre comillas simples para pasarla a un shell.
std::string ShellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

#if defined(__APPLE__)
// Cita una cadena para AppleScript: solo hay que escapar la barra invertida y
// la comilla doble. No sirve ShellQuote(), porque las comillas simples
// cerrarian el argumento que ya va entrecomillado para el shell.
std::string AppleScriptQuote(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}
#endif

// Ejecuta cmd y devuelve la primera linea de la salida estandar.
std::string RunCapture(const std::string &cmd) {
    FILE *p = popen(cmd.c_str(), "r");
    if (p == nullptr)
        return std::string();

    std::string line;
    int c;
    while ((c = fgetc(p)) != EOF && c != '\n')
        line += (char)c;

    int status = pclose(p);
    if (status != 0)
        return std::string();   // el usuario cancelo o fallo el dialogo

    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

bool HaveCommand(const char *name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}
#endif

} // namespace

std::string OpenFileDialog(const std::string &start_dir) {
#if defined(_WIN32)

    // GetOpenFileNameW trabaja con una cadena mutable de tamano fijo.
    wchar_t file[MAX_PATH * 4] = L"";

    std::wstring wdir;
    if (!start_dir.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, start_dir.c_str(), -1, nullptr, 0);
        if (n > 0) {
            wdir.resize((size_t)n);
            MultiByteToWideChar(CP_UTF8, 0, start_dir.c_str(), -1, &wdir[0], n);
        }
    }

    std::wstring wfilter;
    for (const char *p = FILTER_DESC; *p; ++p) wfilter += (wchar_t)(unsigned char)*p;
    wfilter += L'\0';
    for (const char *p = FILTER_PAT; *p; ++p) wfilter += (wchar_t)(unsigned char)*p;
    wfilter += L'\0';
    wfilter += L'\0';

    std::wstring wtitle;
    for (const char *p = DIALOG_TITLE; *p; ++p) wtitle += (wchar_t)(unsigned char)*p;

    OPENFILENAMEW ofn = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = nullptr;
    ofn.lpstrFilter     = wfilter.c_str();
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.lpstrTitle      = wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
        return std::string();   // cancelado o error

    int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return std::string();
    std::string utf8((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, &utf8[0], n, nullptr, nullptr);
    return utf8;

#elif defined(__APPLE__)
    // Se construye el AppleScript y despues se cita entero para el shell: asi
    // una ruta con comillas simples tampoco rompe el comando.
    std::string script = "POSIX path of (choose file with prompt \"";
    script += DIALOG_TITLE;
    script += "\"";
    if (!start_dir.empty())
        script += " default location POSIX file " + AppleScriptQuote(start_dir);
    script += ")";
    return RunCapture("osascript -e " + ShellQuote(script) + " 2>/dev/null");

#else
    std::string dir = start_dir.empty() ? std::string(".") : start_dir;

    if (HaveCommand("zenity")) {
        std::string cmd = "zenity --file-selection --title=" +
                          ShellQuote(DIALOG_TITLE) + " --filename=" +
                          ShellQuote(dir + "/") + " --file-filter=" +
                          ShellQuote(std::string(FILTER_PAT)) + " 2>/dev/null";
        return RunCapture(cmd);
    }
    if (HaveCommand("kdialog")) {
        std::string cmd = "kdialog --getopenfilename " + ShellQuote(dir) + " " +
                          ShellQuote(std::string(FILTER_PAT)) + " --title " +
                          ShellQuote(DIALOG_TITLE) + " 2>/dev/null";
        return RunCapture(cmd);
    }
    return std::string();   // sin dialogo nativo: el llamador usa el integrado
#endif
}
