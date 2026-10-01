#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cwchar>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"AnyPS5PortableGui";
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;

enum ControlId {
    IdInput = 100,
    IdBrowseInput,
    IdOutput,
    IdBrowseOutput,
    IdIntel,
    IdWindowsGui,
    IdDiagnostics,
    IdRegistry,
    IdLazyBinding,
    IdSkipSyscall,
    IdSkipModules,
    IdCopyRuntime,
    IdUnusedFilter,
    IdRun,
    IdOpenFolder,
    IdLog,
    IdStatus
};

HWND gWindow = nullptr;
HWND gInput = nullptr;
HWND gOutput = nullptr;
HWND gLog = nullptr;
HWND gRun = nullptr;
HWND gStatus = nullptr;
HWND gUnusedFilter = nullptr;
HWND gCopyRuntime = nullptr;

std::wstring GetText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0)
        GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void SetText(HWND control, const std::wstring& value) {
    SetWindowTextW(control, value.c_str());
}

std::filesystem::path AppDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::wstring QuoteArgument(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos)
        return value;

    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring ToWide(const std::string& text) {
    if (text.empty())
        return {};

    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (length <= 0) {
        codePage = CP_ACP;
        length = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (length <= 0)
        return L"[Unable to decode process output]\r\n";

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), result.data(), length);

    std::wstring normalized;
    normalized.reserve(result.size() + 16);
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (result[i] == L'\n' && (i == 0 || result[i - 1] != L'\r'))
            normalized.push_back(L'\r');
        normalized.push_back(result[i]);
    }
    return normalized;
}

void PostLog(const std::wstring& text) {
    PostMessageW(gWindow, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

bool IsChecked(int id) {
    return SendDlgItemMessageW(gWindow, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void BrowseInput() {
    wchar_t buffer[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gWindow;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrFilter =
        L"PlayStation 5 executable\0*.elf;*.bin;eboot.bin\0"
        L"All files\0*.*\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&dialog))
        return;

    SetText(gInput, buffer);
    const std::filesystem::path input(buffer);
    const auto output = input.parent_path() / (input.stem().wstring() + L".exe");
    SetText(gOutput, output.wstring());
}

void BrowseOutput() {
    wchar_t buffer[32768] = {};
    const auto current = GetText(gOutput);
    if (!current.empty()) {
        std::wcsncpy(buffer, current.c_str(), std::size(buffer) - 1);
        buffer[std::size(buffer) - 1] = L'\0';
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gWindow;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrFilter = L"Windows executable\0*.exe\0All files\0*.*\0\0";
    dialog.lpstrDefExt = L"exe";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetSaveFileNameW(&dialog))
        SetText(gOutput, buffer);
}

bool CopyRuntimeLibraries(const std::filesystem::path& outputExecutable) {
    const auto source = AppDirectory() / L"libs";
    const auto destination = outputExecutable.parent_path() / L"libs";

    if (!std::filesystem::exists(source))
        throw std::runtime_error("Portable runtime folder 'libs' was not found beside AnyPS5.exe");

    const auto sourceAbs = std::filesystem::absolute(source).lexically_normal();
    const auto destinationAbs = std::filesystem::absolute(destination).lexically_normal();
    if (sourceAbs == destinationAbs)
        return true;

    std::filesystem::create_directories(destination);
    std::filesystem::copy(
        source,
        destination,
        std::filesystem::copy_options::recursive |
        std::filesystem::copy_options::overwrite_existing
    );
    return true;
}

struct WorkerContext {
    HANDLE process = nullptr;
    HANDLE readPipe = nullptr;
    std::filesystem::path outputExecutable;
    bool copyRuntime = false;
};

DWORD WINAPI ProcessWorker(void* raw) {
    auto* context = static_cast<WorkerContext*>(raw);
    char buffer[4096];
    DWORD read = 0;

    while (ReadFile(context->readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        PostLog(ToWide(std::string(buffer, buffer + read)));

    WaitForSingleObject(context->process, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(context->process, &exitCode);

    if (exitCode == 0 && context->copyRuntime) {
        try {
            CopyRuntimeLibraries(context->outputExecutable);
            PostLog(L"\r\nRuntime libraries copied to the output folder.\r\n");
        } catch (const std::exception& error) {
            PostLog(L"\r\nWARNING: " + ToWide(error.what()) + L"\r\n");
        }
    }

    CloseHandle(context->readPipe);
    CloseHandle(context->process);
    delete context;

    PostMessageW(gWindow, WM_APP_DONE, exitCode, 0);
    return 0;
}

void StartConversion() {
    const std::wstring input = GetText(gInput);
    const std::wstring output = GetText(gOutput);

    if (input.empty() || !std::filesystem::is_regular_file(input)) {
        MessageBoxW(gWindow, L"Choose a valid PS5 executable first.", L"AnyPS5", MB_OK | MB_ICONWARNING);
        return;
    }
    if (output.empty()) {
        MessageBoxW(gWindow, L"Choose an output .exe file.", L"AnyPS5", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto appDir = AppDirectory();
    const auto relinker = appDir / L"relinker.exe";
    if (!std::filesystem::is_regular_file(relinker)) {
        MessageBoxW(
            gWindow,
            L"relinker.exe was not found beside AnyPS5.exe.\n\nUse the portable release package without moving files out of it.",
            L"AnyPS5",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    std::vector<std::wstring> arguments;
    if (IsChecked(IdIntel))
        arguments.emplace_back(L"--to-intel");
    if (IsChecked(IdWindowsGui))
        arguments.emplace_back(L"--windows-gui");
    if (IsChecked(IdDiagnostics))
        arguments.emplace_back(L"--windows-diagnostics");
    if (IsChecked(IdRegistry))
        arguments.emplace_back(L"--registry");
    if (IsChecked(IdLazyBinding))
        arguments.emplace_back(L"--lazy-binding");
    if (IsChecked(IdSkipSyscall))
        arguments.emplace_back(L"--skip-syscall-check");
    if (IsChecked(IdSkipModules))
        arguments.emplace_back(L"--skip-sce-module");

    const LRESULT filterIndex = SendMessageW(gUnusedFilter, CB_GETCURSEL, 0, 0);
    arguments.emplace_back(L"unused-filter=" + std::to_wstring(filterIndex < 0 ? 0 : filterIndex));
    arguments.push_back(input);
    arguments.push_back(output);

    std::wstring command = QuoteArgument(relinker.wstring());
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += QuoteArgument(argument);
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        MessageBoxW(gWindow, L"Unable to create process output pipe.", L"AnyPS5", MB_OK | MB_ICONERROR);
        return;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = command;
    const BOOL started = CreateProcessW(
        relinker.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        appDir.c_str(),
        &startup,
        &process
    );

    CloseHandle(writePipe);

    if (!started) {
        CloseHandle(readPipe);
        MessageBoxW(gWindow, L"Unable to start relinker.exe.", L"AnyPS5", MB_OK | MB_ICONERROR);
        return;
    }

    CloseHandle(process.hThread);

    SetWindowTextW(gLog, L"");
    SetText(gStatus, L"Converting...");
    EnableWindow(gRun, FALSE);
    PostLog(L"Starting conversion...\r\n\r\n");

    auto* context = new WorkerContext{
        process.hProcess,
        readPipe,
        std::filesystem::path(output),
        SendMessageW(gCopyRuntime, BM_GETCHECK, 0, 0) == BST_CHECKED
    };

    HANDLE worker = CreateThread(nullptr, 0, ProcessWorker, context, 0, nullptr);
    if (worker)
        CloseHandle(worker);
    else {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        CloseHandle(readPipe);
        delete context;
        SetText(gStatus, L"Failed to start worker thread");
        EnableWindow(gRun, TRUE);
    }
}

void OpenOutputFolder() {
    const std::wstring output = GetText(gOutput);
    if (output.empty())
        return;
    const auto folder = std::filesystem::path(output).parent_path();
    if (!folder.empty())
        ShellExecuteW(gWindow, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

HWND AddControl(
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int id
) {
    HWND control = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        gWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr
    );
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        gWindow = window;
        AddControl(L"STATIC", L"Input PS5 executable", 0, 20, 18, 180, 20, 0);
        gInput = AddControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 20, 40, 650, 25, IdInput);
        AddControl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 680, 39, 100, 27, IdBrowseInput);

        AddControl(L"STATIC", L"Output Windows executable", 0, 20, 77, 180, 20, 0);
        gOutput = AddControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 20, 99, 650, 25, IdOutput);
        AddControl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 680, 98, 100, 27, IdBrowseOutput);

        AddControl(L"STATIC", L"Conversion options", 0, 20, 140, 180, 20, 0);
        AddControl(L"BUTTON", L"Intel compatibility (--to-intel)", BS_AUTOCHECKBOX, 20, 165, 250, 22, IdIntel);
        AddControl(L"BUTTON", L"Generate game as GUI app (no console)", BS_AUTOCHECKBOX, 290, 165, 300, 22, IdWindowsGui);
        AddControl(L"BUTTON", L"Windows dependency diagnostics", BS_AUTOCHECKBOX, 20, 192, 250, 22, IdDiagnostics);
        AddControl(L"BUTTON", L"Write call registry JSON", BS_AUTOCHECKBOX, 290, 192, 220, 22, IdRegistry);
        AddControl(L"BUTTON", L"Lazy symbol binding", BS_AUTOCHECKBOX, 20, 219, 250, 22, IdLazyBinding);
        AddControl(L"BUTTON", L"Skip syscall validation", BS_AUTOCHECKBOX, 290, 219, 220, 22, IdSkipSyscall);
        AddControl(L"BUTTON", L"Skip sce_module processing", BS_AUTOCHECKBOX, 20, 246, 250, 22, IdSkipModules);
        gCopyRuntime = AddControl(L"BUTTON", L"Copy portable runtime libraries beside output", BS_AUTOCHECKBOX, 290, 246, 340, 22, IdCopyRuntime);
        SendMessageW(gCopyRuntime, BM_SETCHECK, BST_CHECKED, 0);

        AddControl(L"STATIC", L"Unused NID filter:", 0, 20, 282, 120, 22, 0);
        gUnusedFilter = AddControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 145, 278, 120, 120, IdUnusedFilter);
        SendMessageW(gUnusedFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0 - Off"));
        SendMessageW(gUnusedFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1 - Normal"));
        SendMessageW(gUnusedFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2 - Strict"));
        SendMessageW(gUnusedFilter, CB_SETCURSEL, 0, 0);

        gRun = AddControl(L"BUTTON", L"Convert to Windows", BS_DEFPUSHBUTTON, 20, 320, 180, 34, IdRun);
        AddControl(L"BUTTON", L"Open output folder", BS_PUSHBUTTON, 212, 320, 160, 34, IdOpenFolder);
        gStatus = AddControl(L"STATIC", L"Ready", SS_LEFT, 390, 328, 390, 22, IdStatus);

        AddControl(L"STATIC", L"Log", 0, 20, 370, 100, 20, 0);
        gLog = AddControl(
            L"EDIT",
            L"",
            WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            20,
            392,
            760,
            190,
            IdLog
        );

        AddControl(
            L"STATIC",
            L"AnyPS5 converts compatible PS5 executables to native Windows PE files. Game resources are not included.",
            SS_LEFT,
            20,
            594,
            760,
            35,
            0
        );
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdBrowseInput:
            BrowseInput();
            return 0;
        case IdBrowseOutput:
            BrowseOutput();
            return 0;
        case IdRun:
            StartConversion();
            return 0;
        case IdOpenFolder:
            OpenOutputFolder();
            return 0;
        default:
            break;
        }
        break;

    case WM_APP_LOG: {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        if (text) {
            SendMessageW(gLog, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            SendMessageW(gLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text->c_str()));
            delete text;
        }
        return 0;
    }

    case WM_APP_DONE: {
        const DWORD exitCode = static_cast<DWORD>(wParam);
        if (exitCode == 0) {
            SetText(gStatus, L"Completed successfully");
            PostLog(L"\r\nConversion completed successfully.\r\n");
        } else {
            SetText(gStatus, L"Conversion failed");
            PostLog(L"\r\nConversion failed. See the log above.\r\n");
        }
        EnableWindow(gRun, TRUE);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&windowClass))
        return 1;

    gWindow = CreateWindowExW(
        0,
        kWindowClass,
        L"AnyPS5 - Windows Portable GUI",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        820,
        680,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!gWindow)
        return 1;

    ShowWindow(gWindow, showCommand);
    UpdateWindow(gWindow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
