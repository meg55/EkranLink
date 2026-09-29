#include "pair_prompt.h"

#include "log.h"

namespace ekran {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

namespace {

struct PairAsk {
    std::wstring text;
    int result = IDNO;
};

DWORD WINAPI pairAskThread(void* p) {
    auto* a = static_cast<PairAsk*>(p);
    a->result = MessageBoxW(nullptr, a->text.c_str(), L"EKRAN — eşleştirme",
                            MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
    return 0;
}

BOOL CALLBACK closeDialog(HWND w, LPARAM) {
    wchar_t cls[16];
    if (GetClassNameW(w, cls, 16) && wcscmp(cls, L"#32770") == 0) PostMessageW(w, WM_COMMAND, IDNO, 0);
    return TRUE;
}

}  // namespace

bool askPairing(const std::wstring& device, const std::wstring& code, HANDLE cancel) {
    PairAsk a;
    a.text = L"\"" + device + L"\" bu bilgisayarla eşleşmek istiyor.\n\n"
             L"Tablette görünen kod:\n\n        " + code +
             L"\n\nKodlar aynıysa Evet'e basın ve tablette de onaylayın.\n"
             L"Farklıysa Hayır'a basın: ağda başka biri araya girmeye çalışıyor olabilir.";
    DWORD tid = 0;
    HANDLE th = CreateThread(nullptr, 0, pairAskThread, &a, 0, &tid);
    HANDLE hs[2] = {th, cancel};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        logf("Tablet eşleştirmeyi reddetti; soru kapatılıyor");
        for (int i = 0; i < 50 && WaitForSingleObject(th, 100) == WAIT_TIMEOUT; i++)
            EnumThreadWindows(tid, closeDialog, 0);
        a.result = IDNO;
    }
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    return a.result == IDYES;
}

}  // namespace ekran
