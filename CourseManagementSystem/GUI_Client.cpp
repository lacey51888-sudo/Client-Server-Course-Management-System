#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <commctrl.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

using namespace std;

SOCKET g_sock = INVALID_SOCKET;
bool g_loggedIn = false;
string g_role;
HWND g_hMainWnd = NULL;
HWND g_hResultEdit = NULL; // kept for color handling of status edit
HWND g_hResultList = NULL;
HWND g_hStatusEdit = NULL;

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 50000;
const char XOR_KEY = 'K';

int g_lastVersion = -1;


// ==================== Light UI Theme Helpers (visual only) ====================
HFONT g_hFont = NULL;
HFONT g_hBoldFont = NULL;
HFONT g_hTitleFont = NULL;
HFONT g_hMonoFont = NULL;
HBRUSH g_hBgBrush = NULL;
HBRUSH g_hEditBrush = NULL;

const COLORREF UI_BG = RGB(245, 247, 250);
const COLORREF UI_TEXT = RGB(35, 39, 47);
const COLORREF UI_MUTED = RGB(90, 96, 110);

void InitUiResources() {
    if (g_hFont) return;
    g_hFont = CreateFontA(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hBoldFont = CreateFontA(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hTitleFont = CreateFontA(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hMonoFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, "Consolas");
    g_hBgBrush = CreateSolidBrush(UI_BG);
    g_hEditBrush = CreateSolidBrush(RGB(255, 255, 255));
}

void CleanupUiResources() {
    if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
    if (g_hBoldFont) { DeleteObject(g_hBoldFont); g_hBoldFont = NULL; }
    if (g_hTitleFont) { DeleteObject(g_hTitleFont); g_hTitleFont = NULL; }
    if (g_hMonoFont) { DeleteObject(g_hMonoFont); g_hMonoFont = NULL; }
    if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = NULL; }
    if (g_hEditBrush) { DeleteObject(g_hEditBrush); g_hEditBrush = NULL; }
}

HWND ApplyFont(HWND h, HFONT font = NULL) {
    if (h) SendMessageA(h, WM_SETFONT, (WPARAM)(font ? font : g_hFont), TRUE);
    return h;
}

HWND MakeLabel(HWND parent, const char* text, int x, int y, int w, int h, HFONT font = NULL) {
    return ApplyFont(CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, NULL, GetModuleHandle(NULL), NULL), font ? font : g_hFont);
}

HWND MakeGroup(HWND parent, const char* text, int x, int y, int w, int h) {
    return ApplyFont(CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, parent, NULL, GetModuleHandle(NULL), NULL), g_hBoldFont);
}

HWND MakeEdit(HWND parent, const char* text, int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
    return ApplyFont(CreateWindowA("EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extraStyle,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL), g_hFont);
}

HWND MakeButton(HWND parent, const char* text, int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
    return ApplyFont(CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | extraStyle,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL), g_hFont);
}


string Trim(const string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

vector<string> SplitClient(const string& s, char delim) {
    vector<string> result;
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) result.push_back(item);
    return result;
}

void XorString(string& data) {
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= XOR_KEY;
    }
}

int ParseVersionResponse(const string& resp) {
    if (resp.find("SUCCESS") != 0) return -1;
    string n = Trim(resp.substr(7));
    if (n.empty()) return -1;
    return atoi(n.c_str());
}

// ==================== Single-line Input Dialog ====================
string SimpleInputBox(HWND hParent, const string& title, const string& prompt, const string& defaultValue = "") {
    char buffer[256] = { 0 };
    strncpy(buffer, defaultValue.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    class InputDialog {
        HWND hWnd;
        HWND hEdit;
        char* result;
        bool finished;
        string m_prompt;
        static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
            InputDialog* pThis = (InputDialog*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
            if (!pThis && msg == WM_CREATE) {
                pThis = (InputDialog*)((LPCREATESTRUCT)l)->lpCreateParams;
                SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
                pThis->hWnd = hWnd;
            }
            if (pThis) return pThis->HandleMessage(msg, w, l);
            return DefWindowProc(hWnd, msg, w, l);
        }
        LRESULT HandleMessage(UINT msg, WPARAM w, LPARAM l) {
            switch (msg) {
            case WM_CREATE: {
                CreateWindowA("STATIC", m_prompt.c_str(), WS_CHILD | WS_VISIBLE, 10, 10, 200, 20, hWnd, NULL, GetModuleHandle(NULL), NULL);
                hEdit = CreateWindowA("EDIT", result, WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 30, 200, 20, hWnd, (HMENU)100, GetModuleHandle(NULL), NULL);
                CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE, 40, 60, 60, 25, hWnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
                CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 120, 60, 60, 25, hWnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
                SetFocus(hEdit);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(w) == IDOK) {
                    GetWindowTextA(hEdit, result, 255);
                    finished = true;
                    DestroyWindow(hWnd);
                }
                else if (LOWORD(w) == IDCANCEL) {
                    finished = false;
                    DestroyWindow(hWnd);
                }
                return 0;
            case WM_DESTROY:
                return 0;
            }
            return DefWindowProc(hWnd, msg, w, l);
        }
    public:
        InputDialog(const string& prompt) : hWnd(NULL), hEdit(NULL), result(NULL), finished(false), m_prompt(prompt) {}
        bool Show(HWND hParent, const string& title, char* outBuffer) {
            result = outBuffer;
            WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
            wc.lpfnWndProc = InputDialog::WndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = "SimpleInputClass";
            RegisterClassExA(&wc);
            EnableWindow(hParent, FALSE);
            hWnd = CreateWindowExA(0, "SimpleInputClass", title.c_str(), WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                CW_USEDEFAULT, CW_USEDEFAULT, 240, 130, hParent, NULL, wc.hInstance, this);
            if (!hWnd) {
                EnableWindow(hParent, TRUE);
                return false;
            }
            MSG msg;
            while (IsWindow(hWnd) && GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            EnableWindow(hParent, TRUE);
            return finished;
        }
    };

    InputDialog dlg(prompt);
    if (dlg.Show(hParent, title, buffer)) {
        return string(buffer);
    }
    return "";
}

// ==================== Multi-field Add Course Dialog (fixed display clipping) ====================
struct AddCourseData {
    string code, title, section, instructor, time, classroom, semester;
    bool ok;
};

LRESULT CALLBACK AddCourseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static AddCourseData* pData;
    static HWND hCode, hTitle, hSection, hInstructor, hTime, hRoom, hSemester;

    switch (msg) {
    case WM_CREATE: {
        pData = (AddCourseData*)((LPCREATESTRUCT)lParam)->lpCreateParams;

        // Increase label width to make sure the text is fully displayed
        CreateWindowA("STATIC", "Course Code:", WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, hWnd, NULL, NULL, NULL);
        hCode = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 8, 250, 22, hWnd, (HMENU)101, NULL, NULL);

        CreateWindowA("STATIC", "Title:", WS_CHILD | WS_VISIBLE, 10, 40, 100, 20, hWnd, NULL, NULL, NULL);
        hTitle = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 38, 250, 22, hWnd, (HMENU)102, NULL, NULL);

        CreateWindowA("STATIC", "Section:", WS_CHILD | WS_VISIBLE, 10, 70, 100, 20, hWnd, NULL, NULL, NULL);
        hSection = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 68, 250, 22, hWnd, (HMENU)103, NULL, NULL);

        CreateWindowA("STATIC", "Instructor:", WS_CHILD | WS_VISIBLE, 10, 100, 100, 20, hWnd, NULL, NULL, NULL);
        hInstructor = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 98, 250, 22, hWnd, (HMENU)104, NULL, NULL);

        // The "Time (e.g., Mon10:00-12:00):" label is long, so set the label width to 180
        CreateWindowA("STATIC", "Time (e.g., Mon10:00-12:00):", WS_CHILD | WS_VISIBLE, 10, 130, 180, 20, hWnd, NULL, NULL, NULL);
        hTime = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 200, 128, 170, 22, hWnd, (HMENU)105, NULL, NULL);

        CreateWindowA("STATIC", "Classroom:", WS_CHILD | WS_VISIBLE, 10, 160, 100, 20, hWnd, NULL, NULL, NULL);
        hRoom = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 158, 250, 22, hWnd, (HMENU)106, NULL, NULL);

        CreateWindowA("STATIC", "Semester:", WS_CHILD | WS_VISIBLE, 10, 190, 100, 20, hWnd, NULL, NULL, NULL);
        hSemester = CreateWindowA("EDIT", "2026S1", WS_CHILD | WS_VISIBLE | WS_BORDER, 120, 188, 250, 22, hWnd, (HMENU)107, NULL, NULL);

        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE, 100, 230, 70, 25, hWnd, (HMENU)IDOK, NULL, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 200, 230, 70, 25, hWnd, (HMENU)IDCANCEL, NULL, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char buf[256];
            GetWindowTextA(hCode, buf, 255); pData->code = buf;
            GetWindowTextA(hTitle, buf, 255); pData->title = buf;
            GetWindowTextA(hSection, buf, 255); pData->section = buf;
            GetWindowTextA(hInstructor, buf, 255); pData->instructor = buf;
            GetWindowTextA(hTime, buf, 255); pData->time = buf;
            GetWindowTextA(hRoom, buf, 255); pData->classroom = buf;
            GetWindowTextA(hSemester, buf, 255); pData->semester = buf;
            pData->ok = true;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            pData->ok = false;
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_DESTROY:
        // Do not call PostQuitMessage here; otherwise the whole program will close
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool ShowAddCourseDialog(HWND parent, AddCourseData& data) {
    data.ok = false;
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = AddCourseWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "AddCourseClass";
    RegisterClassExA(&wc);

    EnableWindow(parent, FALSE);
    // Increase the window width to 400
    HWND hDlg = CreateWindowExA(0, "AddCourseClass", "Add New Course", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 310, parent, NULL, wc.hInstance, &data);
    if (!hDlg) {
        EnableWindow(parent, TRUE);
        return false;
    }

    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnableWindow(parent, TRUE);
    return data.ok;
}

// ==================== Modify Course Dialog (single window, fixed display clipping) ====================
struct ModifyCourseData {
    string originalCode;   // Original course code used to locate the course
    string code, title, section, instructor, time, classroom, semester;
    bool ok;
};

LRESULT CALLBACK ModifyCourseWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static ModifyCourseData* pData;
    static HWND hCode, hTitle, hSection, hInstructor, hTime, hRoom, hSemester;

    switch (msg) {
    case WM_CREATE: {
        pData = (ModifyCourseData*)((LPCREATESTRUCT)lParam)->lpCreateParams;

        CreateWindowA("STATIC", "Course Code (read only):", WS_CHILD | WS_VISIBLE, 10, 10, 140, 20, hWnd, NULL, NULL, NULL);
        hCode = CreateWindowA("EDIT", pData->code.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
            160, 8, 210, 22, hWnd, (HMENU)101, NULL, NULL);

        CreateWindowA("STATIC", "Title:", WS_CHILD | WS_VISIBLE, 10, 40, 100, 20, hWnd, NULL, NULL, NULL);
        hTitle = CreateWindowA("EDIT", pData->title.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 38, 250, 22, hWnd, (HMENU)102, NULL, NULL);

        CreateWindowA("STATIC", "Section:", WS_CHILD | WS_VISIBLE, 10, 70, 100, 20, hWnd, NULL, NULL, NULL);
        hSection = CreateWindowA("EDIT", pData->section.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 68, 250, 22, hWnd, (HMENU)103, NULL, NULL);

        CreateWindowA("STATIC", "Instructor:", WS_CHILD | WS_VISIBLE, 10, 100, 100, 20, hWnd, NULL, NULL, NULL);
        hInstructor = CreateWindowA("EDIT", pData->instructor.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 98, 250, 22, hWnd, (HMENU)104, NULL, NULL);

        CreateWindowA("STATIC", "Time (e.g., Mon10:00-12:00):", WS_CHILD | WS_VISIBLE, 10, 130, 180, 20, hWnd, NULL, NULL, NULL);
        hTime = CreateWindowA("EDIT", pData->time.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            200, 128, 170, 22, hWnd, (HMENU)105, NULL, NULL);

        CreateWindowA("STATIC", "Classroom:", WS_CHILD | WS_VISIBLE, 10, 160, 100, 20, hWnd, NULL, NULL, NULL);
        hRoom = CreateWindowA("EDIT", pData->classroom.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 158, 250, 22, hWnd, (HMENU)106, NULL, NULL);

        CreateWindowA("STATIC", "Semester:", WS_CHILD | WS_VISIBLE, 10, 190, 100, 20, hWnd, NULL, NULL, NULL);
        hSemester = CreateWindowA("EDIT", pData->semester.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER,
            120, 188, 250, 22, hWnd, (HMENU)107, NULL, NULL);

        CreateWindowA("BUTTON", "Save Changes", WS_CHILD | WS_VISIBLE, 100, 230, 110, 25, hWnd, (HMENU)IDOK, NULL, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 230, 230, 70, 25, hWnd, (HMENU)IDCANCEL, NULL, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char buf[256];
            GetWindowTextA(hCode, buf, 255);   pData->code = buf;
            GetWindowTextA(hTitle, buf, 255);  pData->title = buf;
            GetWindowTextA(hSection, buf, 255);pData->section = buf;
            GetWindowTextA(hInstructor, buf, 255); pData->instructor = buf;
            GetWindowTextA(hTime, buf, 255);   pData->time = buf;
            GetWindowTextA(hRoom, buf, 255);   pData->classroom = buf;
            GetWindowTextA(hSemester, buf, 255); pData->semester = buf;
            pData->ok = true;
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            pData->ok = false;
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool ShowModifyCourseDialog(HWND parent, ModifyCourseData& data) {
    data.ok = false;
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = ModifyCourseWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ModifyCourseClass";
    RegisterClassExA(&wc);

    EnableWindow(parent, FALSE);
    // Window width: 420
    HWND hDlg = CreateWindowExA(0, "ModifyCourseClass", "Modify Course", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 310, parent, NULL, wc.hInstance, &data);
    if (!hDlg) {
        EnableWindow(parent, TRUE);
        return false;
    }

    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnableWindow(parent, TRUE);
    return data.ok;
}

// ==================== Result ListView Helpers ====================
// Use a real Windows ListView control instead of drawing a text table.
// This gives true column alignment and avoids text overlap in the result area.
string FormatResponse(const string& resp) {
    // Keep the raw server response. SetResultText() will parse it and fill the ListView columns.
    return resp;
}

void ClearResultList() {
    if (g_hResultList && IsWindow(g_hResultList)) {
        ListView_DeleteAllItems(g_hResultList);
    }
}

void SetStatusText(const string& text) {
    if (g_hStatusEdit && IsWindow(g_hStatusEdit)) {
        SetWindowTextA(g_hStatusEdit, text.c_str());
    }
}

void AddListColumn(int index, const char* title, int width) {
    if (!g_hResultList) return;
    LVCOLUMNA col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPSTR)title;
    col.cx = width;
    col.iSubItem = index;
    ListView_InsertColumn(g_hResultList, index, &col);
}

void InitResultColumns() {
    AddListColumn(0, "Code", 95);
    AddListColumn(1, "Title", 270);
    AddListColumn(2, "Section", 75);
    AddListColumn(3, "Instructor", 135);
    AddListColumn(4, "Time", 140);
    AddListColumn(5, "Classroom", 115);
    AddListColumn(6, "Semester", 95);
}

void AddResultRow(const vector<string>& fields) {
    if (!g_hResultList || !IsWindow(g_hResultList)) return;

    int row = ListView_GetItemCount(g_hResultList);
    string code = fields.size() > 0 ? fields[0] : "";

    LVITEMA item;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = (LPSTR)code.c_str();
    ListView_InsertItem(g_hResultList, &item);

    for (int i = 1; i < 7; ++i) {
        string value = fields.size() > (size_t)i ? fields[i] : "";
        ListView_SetItemText(g_hResultList, row, i, (LPSTR)value.c_str());
    }
}

void AddMessageRow(const string& message) {
    vector<string> row;
    row.push_back(message);
    row.push_back("");
    row.push_back("");
    row.push_back("");
    row.push_back("");
    row.push_back("");
    row.push_back("");
    AddResultRow(row);
}

void DisplayServerResponse(const string& statusLine, const string& resp) {
    ClearResultList();
    SetStatusText(statusLine);

    string response = Trim(resp);
    if (response.empty()) {
        AddMessageRow("No response from server.");
        return;
    }

    if (response.find("SUCCESS") != 0) {
        AddMessageRow(response);
        return;
    }

    string content = Trim(response.substr(7));
    if (content.empty()) {
        AddMessageRow("SUCCESS");
        return;
    }

    bool hasCourse = false;
    stringstream ss(content);
    string line;
    while (getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        vector<string> fields = SplitClient(line, '|');
        if (fields.size() >= 6) {
            if (fields.size() < 7) fields.push_back("");
            AddResultRow(fields);
            hasCourse = true;
        }
        else {
            AddMessageRow(line);
        }
    }

    if (!hasCourse && ListView_GetItemCount(g_hResultList) == 0) {
        AddMessageRow("SUCCESS - No course records found.");
    }
}

// ==================== Network Functions ====================
bool ConnectToServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock == INVALID_SOCKET) return false;
    int timeout = 5000;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    if (connect(g_sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    return true;
}

void Disconnect() {
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    WSACleanup();
}

string SendRequest(const string& req) {
    if (g_sock == INVALID_SOCKET) return "ERROR 500|Not connected";

    // Member 4 Bonus: XOR encrypt every client request before sending.
    string encryptedReq = req;
    XorString(encryptedReq);
    send(g_sock, encryptedReq.c_str(), (int)encryptedReq.length(), 0);

    char buffer[8192];
    int recvLen = recv(g_sock, buffer, sizeof(buffer) - 1, 0);
    if (recvLen > 0) {
        string resp(buffer, recvLen);
        // Member 4 Bonus: decrypt server response.
        XorString(resp);
        return resp;
    }
    else if (recvLen == 0) {
        return "ERROR 500|Server closed connection";
    }
    else {
        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT) return "ERROR 500|Receive timeout";
        else return "ERROR 500|Receive failed";
    }
}

void AppendResult(const string& text) {
    // Keep normal messages visible in the status bar area without touching course rows.
    SetStatusText(text);
}

void SetResultText(const string& text) {
    // Expected input is usually: ">>> REQUEST\r\n" + raw server response.
    string msg = text;
    string status = "";
    string resp = msg;

    size_t pos = msg.find("SUCCESS");
    if (pos == string::npos) pos = msg.find("ERROR");

    if (pos != string::npos) {
        status = Trim(msg.substr(0, pos));
        resp = msg.substr(pos);
    }
    else {
        status = Trim(msg);
        resp = msg;
    }

    if (status.empty()) status = "Server Response";
    DisplayServerResponse(status, resp);
}


void AutoSyncIfNeeded(HWND hWnd) {
    if (!IsWindowEnabled(hWnd)) return; // do not sync while a dialog is open

    string verResp = SendRequest("VERSION");
    int newVersion = ParseVersionResponse(verResp);
    if (newVersion < 0) return;

    if (g_lastVersion == -1) {
        g_lastVersion = newVersion;
        return;
    }

    if (newVersion != g_lastVersion) {
        g_lastVersion = newVersion;
        string allResp = SendRequest("QUERY ALL");
        SetResultText("[Auto Sync] Timetable was updated by admin. Refreshing...\r\n" + FormatResponse(allResp));
    }
}

// ==================== Main Window Procedure ====================
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitUiResources();
        SetWindowTextA(hWnd, "Course Timetable Inquiry System - Aligned List Display");

        MakeLabel(hWnd, "Course Timetable Inquiry System", 24, 18, 520, 34, g_hTitleFont);
        MakeLabel(hWnd, (string("Logged in as: ") + g_role + "    |    Server: 127.0.0.1:50000    |    Auto-sync: 1s").c_str(),
            26, 56, 760, 24, g_hFont);

        // Left side: basic course query functions
        MakeGroup(hWnd, "Basic Query", 24, 92, 530, 230);
        MakeLabel(hWnd, "Course Code", 48, 128, 110, 24, g_hBoldFont);
        HWND hCodeEdit = MakeEdit(hWnd, "COMP3003", 170, 124, 170, 28, 1002);
        MakeButton(hWnd, "Query by Code", 360, 123, 160, 31, 101);

        MakeLabel(hWnd, "Instructor", 48, 172, 110, 24, g_hBoldFont);
        HWND hInstEdit = MakeEdit(hWnd, "Dr.Lee", 170, 168, 170, 28, 1003);
        MakeButton(hWnd, "Query by Instructor", 360, 167, 160, 31, 102);

        MakeLabel(hWnd, "Semester", 48, 216, 110, 24, g_hBoldFont);
        HWND hSemEdit = MakeEdit(hWnd, "2026S1", 170, 212, 170, 28, 1004);
        MakeButton(hWnd, "Query by Semester", 360, 211, 160, 31, 107);

        MakeButton(hWnd, "View All Courses", 48, 264, 472, 36, 103);

        // Right side: bonus advanced search functions
        MakeGroup(hWnd, "Advanced Search", 580, 92, 530, 154);
        MakeLabel(hWnd, "Time Keyword", 604, 128, 125, 24, g_hBoldFont);
        HWND hTimeEdit = MakeEdit(hWnd, "Mon", 742, 124, 170, 28, 1005);
        MakeButton(hWnd, "Search by Time", 932, 123, 150, 31, 108);

        MakeLabel(hWnd, "Section", 604, 172, 125, 24, g_hBoldFont);
        HWND hSectionEdit = MakeEdit(hWnd, "A", 742, 168, 170, 28, 1006);
        MakeButton(hWnd, "Search by Section", 932, 167, 150, 31, 109);

        MakeLabel(hWnd, "Examples:  Time = Mon / 10:00 / 14:00     Section = A / B / C", 604, 210, 470, 22, g_hFont);

        // Admin-only operations
        MakeGroup(hWnd, "Administrator Actions", 580, 260, 530, 62);
        HWND hAddBtn = MakeButton(hWnd, "Add Course", 604, 285, 145, 31, 104, WS_DISABLED);
        HWND hModifyBtn = MakeButton(hWnd, "Modify Course", 766, 285, 145, 31, 105, WS_DISABLED);
        HWND hDeleteBtn = MakeButton(hWnd, "Delete Course", 928, 285, 145, 31, 106, WS_DISABLED);

        // Result panel: use ListView columns for true alignment.
        MakeGroup(hWnd, "Result / Server Response", 24, 340, 1086, 340);
        g_hStatusEdit = ApplyFont(CreateWindowA("EDIT", "Ready.",
            WS_CHILD | WS_VISIBLE | ES_READONLY | WS_BORDER | ES_AUTOHSCROLL,
            48, 366, 1038, 26, hWnd, (HMENU)1000, GetModuleHandle(NULL), NULL), g_hFont);
        g_hResultEdit = g_hStatusEdit; // for WM_CTLCOLORSTATIC background handling

        g_hResultList = CreateWindowA(WC_LISTVIEWA, "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | WS_VSCROLL | WS_HSCROLL,
            48, 400, 1038, 250, hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        SendMessageA(g_hResultList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        ListView_SetExtendedListViewStyle(g_hResultList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ListView_SetBkColor(g_hResultList, RGB(255, 255, 255));
        ListView_SetTextBkColor(g_hResultList, RGB(255, 255, 255));
        ListView_SetTextColor(g_hResultList, UI_TEXT);
        InitResultColumns();

        SetPropA(hWnd, "CODE_EDIT", hCodeEdit);
        SetPropA(hWnd, "INST_EDIT", hInstEdit);
        SetPropA(hWnd, "SEM_EDIT", hSemEdit);
        SetPropA(hWnd, "TIME_EDIT", hTimeEdit);
        SetPropA(hWnd, "SECTION_EDIT", hSectionEdit);
        SetPropA(hWnd, "ADD_BTN", hAddBtn);
        SetPropA(hWnd, "MODIFY_BTN", hModifyBtn);
        SetPropA(hWnd, "DELETE_BTN", hDeleteBtn);

        if (g_role == "admin") {
            EnableWindow(hAddBtn, TRUE);
            EnableWindow(hModifyBtn, TRUE);
            EnableWindow(hDeleteBtn, TRUE);
        }

        AppendResult("Logged in as " + g_role);
        AppendResult("Clean Card Display Version. Query results will be shown as Course 1 / Course 2 cards, not as a table.\r\nTip: Students can query only. Administrators can add, modify, and delete courses.");

        // Member 4: check timetable version every 1 second for near real-time auto-sync.
        string verResp = SendRequest("VERSION");
        g_lastVersion = ParseVersionResponse(verResp);
        SetTimer(hWnd, 1, 1000, NULL);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetTextColor(hdc, UI_TEXT);
        // Read-only EDIT controls send WM_CTLCOLORSTATIC. If we return a transparent
        // background for the result box, text can smear/overlap while scrolling.
        if (hCtrl == g_hResultEdit) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)g_hEditBrush;
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hBgBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, UI_TEXT);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)g_hEditBrush;
    }

    case WM_TIMER:
        if (wParam == 1) {
            AutoSyncIfNeeded(hWnd);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 101: {
            HWND hEdit = (HWND)GetPropA(hWnd, "CODE_EDIT");
            char code[256];
            GetWindowTextA(hEdit, code, 256);
            if (strlen(code) == 0) { AppendResult("Enter course code"); break; }
            string resp = SendRequest(string("QUERY BY_CODE ") + code);
            SetResultText(">>> QUERY BY_CODE " + string(code) + "\r\n" + FormatResponse(resp));
            break;
        }
        case 102: {
            HWND hEdit = (HWND)GetPropA(hWnd, "INST_EDIT");
            char inst[256];
            GetWindowTextA(hEdit, inst, 256);
            if (strlen(inst) == 0) { AppendResult("Enter instructor name"); break; }
            string instructor(inst);
            instructor.erase(0, instructor.find_first_not_of(" \t\r\n"));
            instructor.erase(instructor.find_last_not_of(" \t\r\n") + 1);
            string resp = SendRequest(string("QUERY BY_INSTRUCTOR ") + instructor);
            SetResultText(">>> QUERY BY_INSTRUCTOR " + instructor + "\r\n" + FormatResponse(resp));
            break;
        }
        case 103: {
            string resp = SendRequest("QUERY ALL");
            SetResultText(">>> QUERY ALL\r\n" + FormatResponse(resp));
            break;
        }
        case 107: { // Query by Semester
            HWND hEdit = (HWND)GetPropA(hWnd, "SEM_EDIT");
            char sem[256];
            GetWindowTextA(hEdit, sem, 256);
            if (strlen(sem) == 0) { AppendResult("Enter semester, e.g., 2026S1"); break; }
            string resp = SendRequest(string("QUERY BY_SEMESTER ") + sem);
            SetResultText(">>> QUERY BY_SEMESTER " + string(sem) + "\r\n" + FormatResponse(resp));
            break;
        }
        case 108: { // Advanced Search by Time
            HWND hEdit = (HWND)GetPropA(hWnd, "TIME_EDIT");
            char keyword[256];
            GetWindowTextA(hEdit, keyword, 256);
            if (strlen(keyword) == 0) { AppendResult("Enter time keyword, e.g., Mon or 10:00"); break; }
            string resp = SendRequest(string("QUERY BY_TIME ") + keyword);
            SetResultText(">>> QUERY BY_TIME " + string(keyword) + "\r\n" + FormatResponse(resp));
            break;
        }
        case 109: { // Advanced Search by Section
            HWND hEdit = (HWND)GetPropA(hWnd, "SECTION_EDIT");
            char section[256];
            GetWindowTextA(hEdit, section, 256);
            if (strlen(section) == 0) { AppendResult("Enter section, e.g., A"); break; }
            string resp = SendRequest(string("QUERY BY_SECTION ") + section);
            SetResultText(">>> QUERY BY_SECTION " + string(section) + "\r\n" + FormatResponse(resp));
            break;
        }
        case 104: { // Add Course
            if (g_role != "admin") {
                AppendResult("Permission denied. Admin only.");
                break;
            }
            AddCourseData data;
            if (ShowAddCourseDialog(hWnd, data)) {
                string req = "UPDATE ADD " + data.code + "|" + data.title + "|" + data.section + "|"
                    + data.instructor + "|" + data.time + "|" + data.classroom + "|" + data.semester;
                string resp = SendRequest(req);
                AppendResult(">>> " + req);
                AppendResult(resp);
                AutoSyncIfNeeded(hWnd);
            }
            break;
        }
        case 105: { // Modify Course (single-window)
            if (g_role != "admin") {
                AppendResult("Permission denied. Admin only.");
                break;
            }
            string code = SimpleInputBox(hWnd, "Modify Course", "Enter Course Code to modify:", "");
            if (code.empty()) break;

            string queryResp = SendRequest(string("QUERY BY_CODE ") + code);
            if (queryResp.find("SUCCESS") != 0) {
                AppendResult("Error: " + queryResp);
                break;
            }

            string dataPart = queryResp.substr(8);
            vector<string> fields;
            size_t start = 0, end;
            while ((end = dataPart.find('|', start)) != string::npos) {
                fields.push_back(dataPart.substr(start, end - start));
                start = end + 1;
            }
            fields.push_back(dataPart.substr(start));
            if (fields.size() < 6) {
                AppendResult("Error: Invalid course data format from server");
                break;
            }
            if (fields.size() < 7) fields.push_back("2026S1");

            ModifyCourseData modData;
            modData.originalCode = code;
            modData.code = fields[0];
            modData.title = fields[1];
            modData.section = fields[2];
            modData.instructor = fields[3];
            modData.time = fields[4];
            modData.classroom = fields[5];
            modData.semester = fields[6];

            if (ShowModifyCourseDialog(hWnd, modData)) {
                if (modData.title != fields[1]) {
                    string req = "UPDATE MODIFY " + code + "|title|" + modData.title;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                if (modData.section != fields[2]) {
                    string req = "UPDATE MODIFY " + code + "|section|" + modData.section;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                if (modData.instructor != fields[3]) {
                    string req = "UPDATE MODIFY " + code + "|instructor|" + modData.instructor;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                if (modData.time != fields[4]) {
                    string req = "UPDATE MODIFY " + code + "|time|" + modData.time;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                if (modData.classroom != fields[5]) {
                    string req = "UPDATE MODIFY " + code + "|classroom|" + modData.classroom;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                if (modData.semester != fields[6]) {
                    string req = "UPDATE MODIFY " + code + "|semester|" + modData.semester;
                    string resp = SendRequest(req);
                    AppendResult(">>> " + req + " -> " + resp);
                }
                AppendResult("Modify operation completed.");
                AutoSyncIfNeeded(hWnd);
            }
            break;
        }
        case 106: { // Delete Course
            if (g_role != "admin") {
                AppendResult("Permission denied. Admin only.");
                break;
            }
            string code = SimpleInputBox(hWnd, "Delete Course", "Course Code:");
            if (code.empty()) break;
            string req = "UPDATE DELETE " + code;
            string resp = SendRequest(req);
            AppendResult(">>> " + req);
            AppendResult(resp);
            AutoSyncIfNeeded(hWnd);
            break;
        }
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        RemovePropA(hWnd, "CODE_EDIT");
        RemovePropA(hWnd, "INST_EDIT");
        RemovePropA(hWnd, "SEM_EDIT");
        RemovePropA(hWnd, "TIME_EDIT");
        RemovePropA(hWnd, "SECTION_EDIT");
        RemovePropA(hWnd, "ADD_BTN");
        RemovePropA(hWnd, "MODIFY_BTN");
        RemovePropA(hWnd, "DELETE_BTN");
        Disconnect();
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        if (MessageBoxA(hWnd, "Exit the program?", "Confirm", MB_YESNO) == IDYES) {
            DestroyWindow(hWnd);
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ==================== Login Window Procedure ====================
LRESULT CALLBACK LoginWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        InitUiResources();
        MakeLabel(hWnd, "Course Timetable Login", 28, 22, 330, 34, g_hTitleFont);
        MakeLabel(hWnd, "Connect to the server and choose your role.", 30, 60, 330, 24, g_hFont);

        MakeLabel(hWnd, "Role", 42, 102, 90, 24, g_hBoldFont);
        HWND hCombo = ApplyFont(CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            145, 98, 170, 120, hWnd, (HMENU)101, GetModuleHandle(NULL), NULL), g_hFont);
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"student");
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"admin");
        SendMessageA(hCombo, CB_SETCURSEL, 0, 0);

        MakeLabel(hWnd, "Username", 42, 140, 90, 24, g_hBoldFont);
        HWND hUser = MakeEdit(hWnd, "student", 145, 136, 170, 28, 102);

        MakeLabel(hWnd, "Password", 42, 178, 90, 24, g_hBoldFont);
        HWND hPwd = MakeEdit(hWnd, "student", 145, 174, 170, 28, 103, ES_PASSWORD);

        MakeButton(hWnd, "Login", 95, 222, 105, 34, IDOK);
        MakeButton(hWnd, "Cancel", 215, 222, 105, 34, IDCANCEL);

        SetPropA(hWnd, "COMBO", hCombo);
        SetPropA(hWnd, "USER", hUser);
        SetPropA(hWnd, "PWD", hPwd);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetTextColor(hdc, UI_TEXT);
        // Read-only EDIT controls send WM_CTLCOLORSTATIC. If we return a transparent
        // background for the result box, text can smear/overlap while scrolling.
        if (hCtrl == g_hResultEdit) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)g_hEditBrush;
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hBgBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, UI_TEXT);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(255, 255, 255));
        return (LRESULT)g_hEditBrush;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hCombo = (HWND)GetPropA(hWnd, "COMBO");
            HWND hUser = (HWND)GetPropA(hWnd, "USER");
            HWND hPwd = (HWND)GetPropA(hWnd, "PWD");
            EnableWindow(GetDlgItem(hWnd, IDOK), FALSE);
            char role[32], user[64], pwd[64];
            int idx = SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
            SendMessageA(hCombo, CB_GETLBTEXT, idx, (LPARAM)role);
            GetWindowTextA(hUser, user, 64);
            GetWindowTextA(hPwd, pwd, 64);
            string resp = SendRequest(string("LOGIN ") + role + " " + user + " " + pwd);
            if (resp.find("SUCCESS") == 0) {
                g_loggedIn = true;
                g_role = role;
                DestroyWindow(hWnd);
            }
            else {
                MessageBoxA(hWnd, ("Login failed: " + resp).c_str(), "Error", MB_OK);
                EnableWindow(GetDlgItem(hWnd, IDOK), TRUE);
            }
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        RemovePropA(hWnd, "COMBO");
        RemovePropA(hWnd, "USER");
        RemovePropA(hWnd, "PWD");
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    if (!ConnectToServer()) {
        MessageBoxA(NULL, "Cannot connect to server.\nPlease run server.exe first.", "Error", MB_OK);
        return -1;
    }

    InitUiResources();

    WNDCLASSEXA wcLogin = { sizeof(WNDCLASSEXA) };
    wcLogin.lpfnWndProc = LoginWndProc;
    wcLogin.hInstance = hInst;
    wcLogin.hbrBackground = g_hBgBrush ? g_hBgBrush : (HBRUSH)(COLOR_BTNFACE + 1);
    wcLogin.lpszClassName = "LoginClass";
    RegisterClassExA(&wcLogin);

    HWND hLogin = CreateWindowExA(0, "LoginClass", "Course Timetable Login", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 390, 310, NULL, NULL, hInst, NULL);
    if (!hLogin) { Disconnect(); return -1; }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (g_loggedIn) break;
        if (!IsWindow(hLogin)) break;
    }
    if (!g_loggedIn) {
        Disconnect();
        return 0;
    }

    WNDCLASSEXA wcMain = { sizeof(WNDCLASSEXA) };
    wcMain.lpfnWndProc = MainWndProc;
    wcMain.hInstance = hInst;
    wcMain.hbrBackground = g_hBgBrush ? g_hBgBrush : (HBRUSH)(COLOR_WINDOW + 1);
    wcMain.lpszClassName = "MainClass";
    RegisterClassExA(&wcMain);

    g_hMainWnd = CreateWindowExA(0, "MainClass", "Course Timetable Client - Aligned List Display", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1155, 735, NULL, NULL, hInst, NULL);
    if (!g_hMainWnd) { Disconnect(); return -1; }
    ShowWindow(g_hMainWnd, nCmdShow);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CleanupUiResources();
    return (int)msg.wParam;
}