#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")

static HMODULE g_orig;
static volatile LONG g_license_once;

static const char kSerial[] = "FCRCL-PWK9Z-ARDTZ-QRLTM";
static const char kXmlTail[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><settings>"
    "<setting namestring=\"Name\" valuestring=\"QRCC\"/>"
    "<setting namestring=\"LizenzWin1\" valuestring=\"FCRCL-PWK9Z-ARDTZ-QRLTM\"/>"
    "<setting namestring=\"LizenzWinOK\" valuestring=\"FCRCL-PWK9Z-ARDTZ-QRLTM\"/>"
    "<setting namestring=\"LizenzWinOKfirma\" valuestring=\"Private\"/>"
    "<setting namestring=\"LizenzDatum\" valuestring=\"01.01.2099\"/>"
    "<setting namestring=\"settLizenzInTitel\" valuestring=\"True\"/>"
    "</settings>";

static void load_orig(HINSTANCE self)
{
    WCHAR path[MAX_PATH];
    WCHAR *slash;

    if (g_orig)
        return;
    if (!GetModuleFileNameW(self, path, MAX_PATH))
        return;
    slash = path + lstrlenW(path);
    while (slash > path && slash[-1] != L'\\')
        slash--;
    lstrcpyW(slash, L"msvcr120_orig.dll");
    g_orig = LoadLibraryW(path);
}

static void str_cat(char *dst, const char *src, DWORD cch)
{
    DWORD n = (DWORD)lstrlenA(dst);
    while (n + 1 < cch && *src)
        dst[n++] = *src++;
    dst[n] = 0;
}

static void insert_after(char *xml, DWORD cch, const char *after_key, const char *insert_xml)
{
    char pat[96];
    char *p, *close;
    SIZE_T tail_len, ins_len, total;

    pat[0] = 0;
    str_cat(pat, "namestring=\"", sizeof(pat));
    str_cat(pat, after_key, sizeof(pat));
    str_cat(pat, "\"", sizeof(pat));
    p = strstr(xml, pat);
    if (!p)
        return;
    close = strstr(p, "/>");
    if (!close)
        return;
    close += 2;
    ins_len = lstrlenA(insert_xml);
    tail_len = lstrlenA(close);
    total = (SIZE_T)(close - xml) + ins_len + tail_len + 1;
    if (total >= cch)
        return;
    memmove(close + ins_len, close, tail_len + 1);
    CopyMemory(close, insert_xml, ins_len);
}

static void patch_setting(char *xml, DWORD cch, const char *key, const char *val)
{
    char pat[128];
    char rep[256];
    char *p, *q;
    char *tail;
    char *tmp;
    SIZE_T need;

    pat[0] = 0;
    str_cat(pat, "namestring=\"", sizeof(pat));
    str_cat(pat, key, sizeof(pat));
    str_cat(pat, "\" valuestring=\"", sizeof(pat));
    p = strstr(xml, pat);
    if (!p)
        return;
    q = p + lstrlenA(pat);
    while (*q && *q != '"')
        q++;
    if (!*q)
        return;
    rep[0] = 0;
    str_cat(rep, pat, sizeof(rep));
    str_cat(rep, val, sizeof(rep));
    str_cat(rep, "\"", sizeof(rep));
    need = (SIZE_T)lstrlenA(rep) + lstrlenA(q + 1) + 1;
    if (need >= cch)
        return;
    tail = (char *)HeapAlloc(GetProcessHeap(), 0, need);
    tmp = (char *)HeapAlloc(GetProcessHeap(), 0, need);
    if (!tail || !tmp) {
        if (tail) HeapFree(GetProcessHeap(), 0, tail);
        if (tmp) HeapFree(GetProcessHeap(), 0, tmp);
        return;
    }
    lstrcpyA(tail, q + 1);
    lstrcpyA(tmp, rep);
    lstrcatA(tmp, tail);
    lstrcpyA(p, tmp);
    HeapFree(GetProcessHeap(), 0, tail);
    HeapFree(GetProcessHeap(), 0, tmp);
}

static void ensure_license_xml(void)
{
    WCHAR wpath[MAX_PATH];
    char path[MAX_PATH];
    char *xml;
    DWORD n, read, wrote;
    HANDLE h;

    if (InterlockedCompareExchange(&g_license_once, 1, 0) != 0)
        return;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, wpath)))
        return;
    lstrcatW(wpath, L"\\QR Code Creator.exe.xml");
    if (!WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, MAX_PATH, NULL, NULL))
        return;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return;
        WriteFile(h, kXmlTail, (DWORD)lstrlenA(kXmlTail), &wrote, NULL);
        CloseHandle(h);
        return;
    }

    n = GetFileSize(h, NULL);
    if (n == INVALID_FILE_SIZE || n > 60000) {
        CloseHandle(h);
        return;
    }
    xml = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n + 4096);
    if (!xml) {
        CloseHandle(h);
        return;
    }
    if (!ReadFile(h, xml, n, &read, NULL) || read != n) {
        HeapFree(GetProcessHeap(), 0, xml);
        CloseHandle(h);
        return;
    }
    xml[read] = 0;

    if (strstr(xml, "LizenzWinOK\" valuestring=\"") && strstr(xml, kSerial)) {
        HeapFree(GetProcessHeap(), 0, xml);
        CloseHandle(h);
        return;
    }

    if (!strstr(xml, "LizenzWinOK")) {
        insert_after(
            xml,
            n + 4096,
            "LizenzWin1",
            "<setting namestring=\"LizenzWinOK\" valuestring=\"FCRCL-PWK9Z-ARDTZ-QRLTM\"/>"
            "<setting namestring=\"LizenzWinOKfirma\" valuestring=\"Private\"/>");
    }
    patch_setting(xml, n + 4096, "LizenzWin1", kSerial);
    patch_setting(xml, n + 4096, "LizenzWinOK", kSerial);
    patch_setting(xml, n + 4096, "LizenzWinOKfirma", "Private");
    patch_setting(xml, n + 4096, "LizenzDatum", "01.01.2099");

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    WriteFile(h, xml, (DWORD)lstrlenA(xml), &wrote, NULL);
    HeapFree(GetProcessHeap(), 0, xml);
    CloseHandle(h);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        ensure_license_xml();
        load_orig(inst);
    } else if (reason == DLL_PROCESS_DETACH && g_orig) {
        FreeLibrary(g_orig);
        g_orig = NULL;
    }
    return TRUE;
}
