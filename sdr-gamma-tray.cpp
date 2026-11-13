#include "tray-core.h"
#include "hdr-state.h"

constexpr wchar_t title[] = L"Fuck sRGB";
constexpr wchar_t windowClass[] = L"Codex.SdrGamma.Tray.1";
constexpr UINT trayId = 1;
constexpr UINT iconMessage = WM_APP + 1;
constexpr UINT doneMessage = WM_APP + 2;
constexpr UINT firstMode = 100;
constexpr UINT exitCommand = 110;
constexpr UINT iconResource = 101;
constexpr const wchar_t* modes[] = {
    L"Native", L"Gamma2.2", L"Gamma2.4", L"Linear"
};
static_assert(ARRAYSIZE(modes) == selectableCount);

void logMessage(const std::wstring& message) noexcept {
    try {
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
        if (!length || length >= ARRAYSIZE(executable)) return;
        const auto directory = std::filesystem::path(executable).parent_path() / L"logs";
        std::filesystem::create_directories(directory);
        std::ofstream log(directory / L"startup.log", std::ios::app);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, message.data(),
            static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
                            utf8.data(), bytes, nullptr, nullptr);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char stamp[40];
        sprintf_s(stamp, "%04u-%02u-%02u %02u:%02u:%02u ", time.wYear, time.wMonth,
                  time.wDay, time.wHour, time.wMinute, time.wSecond);
        log << stamp << utf8 << '\n';
    } catch (...) {}
}

std::wstring errorText(Failure failure) {
    wchar_t code[32];
    swprintf_s(code, L"\nError code 0x%08lX", failure.code);
    return std::wstring(failure.operation) + code;
}

class Tray {
public:
    explicit Tray(HINSTANCE instance, bool quiet, bool gamma22)
        : icon_(LoadIconW(instance, MAKEINTRESOURCEW(iconResource))),
          quiet_(quiet), startGamma22_(gamma22) {
        if (!icon_) fail(L"Load tray icon");
    }

    int run(HINSTANCE instance) {
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (!taskbarCreated_) fail(L"Register taskbar message");

        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = procedure;
        wc.lpszClassName = windowClass;
        wc.hIcon = icon_;
        if (!RegisterClassW(&wc)) fail(L"Register tray window class");
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass, title, WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!hwnd_) fail(L"Create tray window");

        if (!ChangeWindowMessageFilterEx(hwnd_, iconMessage, MSGFLT_ALLOW, nullptr) ||
            !ChangeWindowMessageFilterEx(hwnd_, taskbarCreated_, MSGFLT_ALLOW, nullptr))
            fail(L"Connect to the Windows taskbar");
        hdr_ = detectHdr();
        if (!refresh()) report(error_, MB_ICONWARNING);
        if (!addIcon()) fail(L"Add tray icon");
        if (!SetTimer(hwnd_, 1, 2000, nullptr)) fail(L"Start display monitoring");
        desired_ = startGamma22_ ? Mode::Gamma22 : current_.mode;
        logMessage(L"Started DWM-only; HDR=" + std::to_wstring(static_cast<unsigned>(hdr_)) +
                   L"; actual=" + std::to_wstring(index(current_.mode)) +
                   L"; desired=" + std::to_wstring(index(desired_)));
        const Mode target = automaticTarget(desired_, hdr_);
        if (current_.mode != Mode::Unknown && index(target) < selectableCount && target != current_.mode)
            startWork(target);

        MSG msg{};
        int status;
        while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (status < 0) fail(L"Read window messages");
        return 0;
    }

    ~Tray() {
        if (worker_.joinable()) worker_.join();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    }

private:
    HWND hwnd_{};
    HICON icon_{};
    const bool quiet_, startGamma22_;
    Mode desired_ = Mode::Srgb;
    bool automaticBlocked_ = false;
    UINT taskbarCreated_{};
    Patcher patcher_;
    Current current_{};
    Current result_{};
    HdrState hdr_ = HdrState::Unavailable;
    bool busy_ = false;
    std::wstring error_, workerError_;
    std::thread worker_;

    void report(const std::wstring& message, UINT icon = MB_ICONERROR) const {
        logMessage(message);
        if (!quiet_) MessageBoxW(nullptr, message.c_str(), title, MB_OK | icon);
    }

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto creation = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* tray = static_cast<Tray*>(creation->lpCreateParams);
            tray->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
        }
        auto* tray = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return tray ? tray->onMessage(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    NOTIFYICONDATAW data() const {
        NOTIFYICONDATAW info{};
        info.cbSize = sizeof(info);
        info.hWnd = hwnd_;
        info.uID = trayId;
        return info;
    }

    void tooltip(NOTIFYICONDATAW& info) const {
        const wchar_t* status = L"Checking DWM";
        if (hdr_ == HdrState::Inactive) status = L"HDR inactive";
        else if (hdr_ == HdrState::Unavailable) status = L"HDR status unavailable";
        else if (busy_) status = L"Applying gamma";
        else if (!error_.empty()) status = L"DWM verification failed";
        else if (index(current_.mode) < selectableCount) status = modes[index(current_.mode)];
        else if (current_.mode == Mode::LegacyGamma26) status = L"Legacy gamma active";
        swprintf_s(info.szTip, L"%ls · %ls", title, status);
    }

    bool addIcon() const {
        auto info = data();
        info.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        info.uCallbackMessage = iconMessage;
        info.hIcon = icon_;
        tooltip(info);
        if (!Shell_NotifyIconW(NIM_ADD, &info)) return false;
        info.uVersion = NOTIFYICON_VERSION_4;
        return Shell_NotifyIconW(NIM_SETVERSION, &info);
    }

    void updateTooltip() const {
        auto info = data();
        info.uFlags = NIF_TIP | NIF_SHOWTIP;
        tooltip(info);
        Shell_NotifyIconW(NIM_MODIFY, &info);
    }

    void refreshHdr() {
        const auto state = detectHdr();
        if (hdr_ == state) return;
        hdr_ = state;
        automaticBlocked_ = false;
        logMessage(L"HDR state=" + std::to_wstring(static_cast<unsigned>(hdr_)));
        updateTooltip();
    }

    bool refresh() {
        try {
            current_ = patcher_.inspect();
            error_.clear();
            if (current_.mode == Mode::Unknown)
                error_ = L"DWM shaders do not match a supported gamma mode.";
        } catch (const Failure& error) {
            current_ = {};
            error_ = errorText(error);
        } catch (...) {
            current_ = {};
            error_ = L"Could not read DWM state.";
        }
        return error_.empty();
    }

    void menu(POINT where) {
        refreshHdr();
        const bool active = hdr_ == HdrState::Active;
        if (active && !busy_ && current_.mode == Mode::Unknown) { refresh(); updateTooltip(); }
        HMENU popup = CreatePopupMenu();
        if (!popup) return;
        for (UINT i = 0; i < selectableCount; ++i) {
            UINT flags = MF_STRING;
            if (!canSelectMode(static_cast<Mode>(i), hdr_, current_.mode, busy_)) flags |= MF_GRAYED;
            AppendMenuW(popup, flags, firstMode + i, modes[i]);
        }
        if (index(current_.mode) < selectableCount) {
            CheckMenuRadioItem(popup, firstMode, firstMode + selectableCount - 1,
                              firstMode + index(current_.mode), MF_BYCOMMAND);
        }
        if (busy_ || !error_.empty() || current_.mode == Mode::LegacyGamma26) {
            AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, busy_ ? L"Updating..." :
                        !error_.empty() ? L"DWM verification failed" : L"Select a mode to replace legacy gamma");
        }
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, exitCommand, L"Exit");
        SetForegroundWindow(hwnd_);
        auto command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                        where.x, where.y, hwnd_, nullptr);
        DestroyMenu(popup);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        if (command) PostMessageW(hwnd_, WM_COMMAND, command, 0);
    }

    void startWork(Mode mode) {
        if (busy_) return;
        const auto before = current_;
        busy_ = true;
        workerError_.clear();
        updateTooltip();
        try {
            worker_ = std::thread([this, mode, before] {
                result_ = before;
                if (mode == Mode::Srgb || detectHdr() == HdrState::Active) {
                    try {
                        result_ = patcher_.set(mode);
                    } catch (const Failure& error) {
                        workerError_ = errorText(error);
                    } catch (...) {
                        workerError_ = L"Could not apply the gamma mode.";
                    }
                }
                PostMessageW(hwnd_, doneMessage, 0, 0);
            });
        } catch (...) {
            busy_ = false;
            automaticBlocked_ = true;
            updateTooltip();
            report(L"Could not start the gamma change.");
        }
    }

    void select(Mode mode) {
        refreshHdr();
        if (!canSelectMode(mode, hdr_, current_.mode, busy_)) return;
        desired_ = mode;
        automaticBlocked_ = false;
        if (mode == current_.mode) return;
        startWork(mode);
    }

    LRESULT onMessage(UINT message, WPARAM wp, LPARAM lp) {
        if (message == taskbarCreated_ && taskbarCreated_) {
            addIcon();
            return 0;
        }
        switch (message) {
        case iconMessage:
            if (LOWORD(lp) == WM_CONTEXTMENU && HIWORD(lp) == trayId) {
                POINT where{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
                menu(where);
            }
            return 0;
        case WM_COMMAND: {
            const auto command = LOWORD(wp);
            if (command >= firstMode && command < firstMode + selectableCount)
                select(static_cast<Mode>(command - firstMode));
            else if (command == exitCommand)
                DestroyWindow(hwnd_);
            return 0;
        }
        case doneMessage:
            if (!worker_.joinable()) return 0;
            worker_.join();
            busy_ = false;
            if (workerError_.empty()) {
                current_ = result_;
                if (current_.mode != Mode::Unknown) error_.clear();
                logMessage(L"DWM actual mode=" + std::to_wstring(index(current_.mode)));
                updateTooltip();
            } else {
                refresh();
                automaticBlocked_ = true;
                updateTooltip();
                report(workerError_);
            }
            return 0;
        case WM_TIMER:
            refreshHdr();
            if (!busy_) {
                if (currentDwm(patcher_.session) != current_.pid) {
                    refresh();
                    automaticBlocked_ = false;
                    updateTooltip();
                }
                // 保留用户选择；HDR 暂时关闭时恢复原生，重新开启后再应用。
                // 校验失败后不循环重启 DWM，须等待状态变化或用户重新选择。
                const Mode target = automaticTarget(desired_, hdr_);
                if (!automaticBlocked_ && hdr_ != HdrState::Unavailable &&
                    index(target) < selectableCount && current_.mode != Mode::Unknown &&
                    current_.mode != target)
                    startWork(target);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            refreshHdr();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY: {
            KillTimer(hwnd_, 1);
            auto info = data();
            Shell_NotifyIconW(NIM_DELETE, &info);
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, message, wp, lp);
        }
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool quiet = false;
    try {
        int count=0;
        auto args=CommandLineToArgvW(GetCommandLineW(),&count);
        if (!args) fail(L"Read command line");
        bool gamma22=false, valid=true;
        for (int i=1;i<count;++i) {
            if (wcscmp(args[i],L"--windows-only")==0) {} // 兼容早期 Windows-only 启动命令。
            else if (wcscmp(args[i],L"--gamma22")==0) gamma22=true;
            else if (wcscmp(args[i],L"--quiet")==0) quiet=true;
            else valid=false;
        }
        LocalFree(args);
        if (!valid) fail(L"Supported options: --gamma22 --quiet", ERROR_INVALID_PARAMETER);
        Handle singleton(CreateMutexW(nullptr, FALSE, L"Local\\Codex.SdrGamma.Tray.1"));
        if (!singleton.value) fail(L"Create tray instance lock");
        if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        Tray tray(instance,quiet,gamma22);
        return tray.run(instance);
    } catch (const Failure& error) {
        logMessage(errorText(error));
        if (!quiet) MessageBoxW(nullptr, errorText(error).c_str(), title, MB_OK | MB_ICONERROR);
    } catch (...) {
        logMessage(L"Could not start the tray application.");
        if (!quiet) MessageBoxW(nullptr, L"Could not start the tray application.", title, MB_OK | MB_ICONERROR);
    }
    return 1;
}




