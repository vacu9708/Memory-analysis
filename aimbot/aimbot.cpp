#include <windows.h>
#include <dwmapi.h>
#include <cmath>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <TlHelp32.h>
#include <fstream>
#include <sstream>    // For std::stringstream
#include <algorithm>  // For std::sort and std::unique

using namespace std;

// Structure for 3D vectors
struct Vec3 {
    float x, y, z;
};

// Structure for 4x4 matrices
struct Matrix4x4 {
    float m[4][4];
};

struct MyPosition {
    uintptr_t facing_direction_address;
    uintptr_t coordinates_address;
};

// External function declarations
extern int process_position_addresses(MyPosition& myPos, std::vector<uintptr_t>& playersAddresses);
extern DWORD GetProcessID(const std::wstring& processName);

MyPosition myPos;
std::vector<uintptr_t> playersAddresses;
std::mutex playersMutex; // Mutex to protect playersAddresses

// Function to multiply two 4x4 matrices
Matrix4x4 multiplyMatrix(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 result = {};
    for(int row=0; row<4; ++row){
        for(int col=0; col<4; ++col){
            for(int i=0; i<4; ++i){
                result.m[row][col] += a.m[row][i] * b.m[i][col];
            }
        }
    }
    return result;
}

// Function to multiply matrix and vector
Vec3 multiplyMatrixVector(const Matrix4x4& mat, const Vec3& v, float& w_out) {
    Vec3 result;
    float w;
    result.x = mat.m[0][0]*v.x + mat.m[0][1]*v.y + mat.m[0][2]*v.z + mat.m[0][3];
    result.y = mat.m[1][0]*v.x + mat.m[1][1]*v.y + mat.m[1][2]*v.z + mat.m[1][3];
    result.z = mat.m[2][0]*v.x + mat.m[2][1]*v.y + mat.m[2][2]*v.z + mat.m[2][3];
    w        = mat.m[3][0]*v.x + mat.m[3][1]*v.y + mat.m[3][2]*v.z + mat.m[3][3];
    if (w != 0.0f) {
        result.x /= w;
        result.y /= w;
        result.z /= w;
    }
    w_out = w; // Output w for potential use
    return result;
}

// Function to create a view matrix (right-handed look-at)
Matrix4x4 createViewMatrix(const Vec3& eye, const Vec3& target, const Vec3& up) {
    // Calculate the forward vector (zaxis)
    Vec3 zaxis = { eye.x - target.x, eye.y - target.y, eye.z - target.z };
    float zLength = std::sqrt(zaxis.x*zaxis.x + zaxis.y*zaxis.y + zaxis.z*zaxis.z);
    if(zLength == 0.0f) zLength = 1.0f; // Prevent division by zero
    zaxis.x /= zLength; zaxis.y /= zLength; zaxis.z /= zLength;

    // Calculate the right vector (xaxis) = cross(up, zaxis)
    Vec3 xaxis = {
        up.y * zaxis.z - up.z * zaxis.y,
        up.z * zaxis.x - up.x * zaxis.z,
        up.x * zaxis.y - up.y * zaxis.x
    };
    float xLength = std::sqrt(xaxis.x*xaxis.x + xaxis.y*xaxis.y + xaxis.z*xaxis.z);
    if(xLength == 0.0f) xLength = 1.0f; // Prevent division by zero
    xaxis.x /= xLength; xaxis.y /= xLength; xaxis.z /= xLength;

    // Calculate the true up vector (yaxis) = cross(zaxis, xaxis)
    Vec3 yaxis = {
        zaxis.y * xaxis.z - zaxis.z * xaxis.y,
        zaxis.z * xaxis.x - zaxis.x * xaxis.z,
        zaxis.x * xaxis.y - zaxis.y * xaxis.x
    };

    Matrix4x4 view = {};
    view.m[0][0] = xaxis.x; view.m[0][1] = xaxis.y; view.m[0][2] = xaxis.z; view.m[0][3] = - (xaxis.x * eye.x + xaxis.y * eye.y + xaxis.z * eye.z);
    view.m[1][0] = yaxis.x; view.m[1][1] = yaxis.y; view.m[1][2] = yaxis.z; view.m[1][3] = - (yaxis.x * eye.x + yaxis.y * eye.y + yaxis.z * eye.z);
    view.m[2][0] = zaxis.x; view.m[2][1] = zaxis.y; view.m[2][2] = zaxis.z; view.m[2][3] = - (zaxis.x * eye.x + zaxis.y * eye.y + zaxis.z * eye.z);
    view.m[3][3] = 1.0f;
    return view;
}

// Function to create a projection matrix (right-handed perspective)
Matrix4x4 createProjectionMatrix(float fov, float aspect, float nearPlane, float farPlane) {
    Matrix4x4 proj = {};
    float f = 1.0f / tanf(fov / 2.0f);
    proj.m[0][0] = f / aspect;
    proj.m[1][1] = f;
    proj.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
    proj.m[2][3] = (2 * farPlane * nearPlane) / (nearPlane - farPlane);
    proj.m[3][2] = -1.0f;
    proj.m[3][3] = 0.0f;
    return proj;
}

// Global variables for screen size
int screenWidth = 1920;
int screenHeight = 1200;

HANDLE hProcess = NULL;

// Define unique identifiers for hotkeys
//#define HOTKEY_REMOVE 1
//#define HOTKEY_TRIGGER_LEFT_CLICK 2
//#define HOTKEY_TRIGGER_RIGHT_CLICK 3 // New: Mouse Wheel Click for auto right-click

#define HOTKEY_REMOVE 1 // Keep only the remove hotkey

// Atomic flags to toggle auto-click features
std::atomic<bool> autoLeftClickEnabled(false);
std::atomic<bool> autoRightClickEnabled(false);
// State flags to prevent repeated click events
std::atomic<bool> leftClickHeld(false);
std::atomic<bool> rightClickHeld(false);
std::atomic<bool> runningKeyMonitor(true); // Flag to control the key monitor thread

// Function to read Vec3 from a given memory address
bool ReadVec3(uintptr_t address, Vec3& outVec) {
    if (hProcess == NULL) {
        // Assuming same process; direct memory access
        Vec3* ptr = reinterpret_cast<Vec3*>(address);
        if (ptr == nullptr) return false;
        outVec = *ptr;
        return true;
    } else {
        // If reading from another process, use ReadProcessMemory
        SIZE_T bytesRead;
        return ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &outVec, sizeof(Vec3), &bytesRead) && bytesRead == sizeof(Vec3);
    }
}

// void MoveMouseRelative(int dx, int dy) {
//     INPUT input = { 0 };
//     input.type = INPUT_MOUSE;
//     input.mi.dwFlags = MOUSEEVENTF_MOVE;
//     input.mi.dx = dx;
//     input.mi.dy = dy;
//     SendInput(1, &input, sizeof(INPUT));
// }

void MoveMouseRelative(double dx, double dy) {
    static double accumulatedX = 0.0;
    static double accumulatedY = 0.0;

    accumulatedX += dx;
    accumulatedY += dy;

    int moveX = static_cast<int>(std::round(accumulatedX));
    int moveY = static_cast<int>(std::round(accumulatedY));

    // Update the accumulated values only by the amount actually moved
    accumulatedX -= moveX;
    accumulatedY -= moveY;

    if (moveX != 0 || moveY != 0) {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = moveX;
        input.mi.dy = moveY;
        SendInput(1, &input, sizeof(INPUT));
    }
}

// Function to perform a left mouse click
void performLeftClickDown() {
    // Prepare the INPUT structure for mouse down
    INPUT inputs[1] = {};

    // Mouse left button down
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    // Send the inputs
    SendInput(1, inputs, sizeof(INPUT));
}
void performLeftClickUp() {
    // Prepare the INPUT structure for mouse up
    INPUT inputs[1] = {};

    // Mouse left button up
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    // Send the inputs
    SendInput(1, inputs, sizeof(INPUT));
}

// Function to perform a right mouse click
void performRightClickDown() {
    // Prepare the INPUT structure for right mouse button down
    INPUT inputs[1] = {};

    // Mouse right button down
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;

    // Send the inputs
    SendInput(1, inputs, sizeof(INPUT));
}

void performRightClickUp() {
    // Prepare the INPUT structure for right mouse button up
    INPUT inputs[1] = {};

    // Mouse right button up
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTUP;

    // Send the inputs
    SendInput(1, inputs, sizeof(INPUT));
}

// Function prototype for the hotkey listener thread
DWORD WINAPI HotkeyListener(LPVOID lpParam) {
    // Register hotkey for removing indices (Ctrl + Shift + R)
    if (!RegisterHotKey(NULL, HOTKEY_REMOVE, MOD_CONTROL | MOD_SHIFT, 'R')) {
        MessageBox(NULL, "Failed to register hotkey (Ctrl + Shift + R).", "Error", MB_ICONERROR);
        return 1;
    }

    // Message loop for the hotkeys
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == HOTKEY_REMOVE) {
                // Print the remaining players
                std::cout << "Remaining players: " << playersAddresses.size() << std::endl;
                // Prompt user to enter indices to remove
                std::cout << "Enter indices to remove (separated by spaces): ";
                std::cout.flush(); // Ensure the prompt is displayed

                std::string inputLine;
                std::getline(std::cin, inputLine);

                // Use stringstream to parse the input line
                std::stringstream ss(inputLine);
                std::vector<int> indicesToRemove;
                int index;
                while (ss >> index) {
                    indicesToRemove.push_back(index);
                }

                if (indicesToRemove.empty()) {
                    std::cout << "No valid indices entered." << std::endl;
                    continue;
                }

                // Remove duplicates and sort indices in descending order
                std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());
                indicesToRemove.erase(std::unique(indicesToRemove.begin(), indicesToRemove.end()), indicesToRemove.end());

                // Lock the mutex before accessing playersAddresses
                {
                    std::lock_guard<std::mutex> lock(playersMutex);

                    for (const int& idxToRemove : indicesToRemove) {
                        if (idxToRemove >= 0 && static_cast<size_t>(idxToRemove) < playersAddresses.size()) {
                            playersAddresses.erase(playersAddresses.begin() + idxToRemove);
                            std::cout << "Removed index " << idxToRemove << std::endl;
                            std::cout << "Remaining players: " << playersAddresses.size() << std::endl;
                        } else {
                            std::cout << "Invalid index: " << idxToRemove << std::endl;
                        }
                    }
                }
            }
        }
    }

    // Unregister all hotkeys
    UnregisterHotKey(NULL, HOTKEY_REMOVE);
    return 0;
}

// Function prototype for the key monitor thread
DWORD WINAPI KeyMonitorThread(LPVOID lpParam) {
    bool prevCapsLockPressed = false;
    bool prevMiddleMousePressed = false;

    while (runningKeyMonitor.load()) {
        // Monitor VK_CAPITAL (Caps Lock) for auto left-click
        bool capsPressed = (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0;
        if (capsPressed != prevCapsLockPressed) {
            autoLeftClickEnabled.store(capsPressed);
            std::cout << "Auto-Left-Click Enabled: " << (capsPressed ? "ON" : "OFF") << std::endl;
            prevCapsLockPressed = capsPressed;
            if (!capsPressed && leftClickHeld.load()) {
                performLeftClickUp();
                leftClickHeld.store(false);
            }
        }

        // Monitor VK_MBUTTON (Middle Mouse Button) for auto right-click
        bool middlePressed = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        if (middlePressed != prevMiddleMousePressed) {
            autoRightClickEnabled.store(middlePressed);
            std::cout << "Auto-Right-Click Enabled: " << (middlePressed ? "ON" : "OFF") << std::endl;
            prevMiddleMousePressed = middlePressed;
            if (!middlePressed && rightClickHeld.load()) {
                performRightClickUp();
                rightClickHeld.store(false);
            }
        }
    }

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_ERASEBKGND:
            // Prevent background from being erased to maintain transparency
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Create a compatible memory DC for double buffering
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, screenWidth, screenHeight);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            // Fill the memory DC with transparent color
            HBRUSH backgroundBrush = CreateSolidBrush(RGB(0, 0, 0));
            RECT rect = {0, 0, screenWidth, screenHeight};
            FillRect(memDC, &rect, backgroundBrush);
            DeleteObject(backgroundBrush);

            // Read 'eye' and 'facing_direction' from their respective addresses
            Vec3 up = {0.0f, 1.0f, 0.0f};
            Vec3 eye = {0.0f, 0.0f, 0.0f};
            Vec3 facing_direction = {0.0f, 0.0f, 0.0f};
            
            bool eyeRead = ReadVec3(myPos.coordinates_address, eye);
            if (!eyeRead) {
                // Handle the error appropriately
                EndPaint(hwnd, &ps);
                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                return 0;
            }

            bool facingDirRead = ReadVec3(myPos.facing_direction_address, facing_direction);
            if (!facingDirRead) {
                // Handle the error appropriately
                EndPaint(hwnd, &ps);
                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                return 0;
            }

            // Calculate the target position
            Vec3 target = {
                eye.x + facing_direction.x,
                eye.y + facing_direction.y,
                eye.z + facing_direction.z
            };

            // Set up view and projection matrices
            Matrix4x4 view = createViewMatrix(eye, target, up);

            float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
            float hFOV = 97.5f * (3.1415926535f / 180.0f); // Convert to radians
            float vFOV = 2 * atanf(tanf(hFOV / 2.0f) / aspect); // Compute vertical FOV
            float nearPlane = 1.0f;
            float farPlane = 100.0f;
            static Matrix4x4 projection = createProjectionMatrix(vFOV, aspect, nearPlane, farPlane);

            Matrix4x4 vp = multiplyMatrix(projection, view);

            // Set the drawing color (red) for the pen
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
            HGDIOBJ oldPen = SelectObject(memDC, pen);

            // Select a hollow brush to prevent filling
            HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ oldBrush = SelectObject(memDC, brush);

            // Create a larger, bold font for text rendering
            HFONT hFont = CreateFont(
                -MulDiv(24, GetDeviceCaps(memDC, LOGPIXELSY), 72), // 24-point font
                0, 0, 0,
                FW_BOLD, // Bold font for better visibility
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY,
                VARIABLE_PITCH,
                TEXT("Arial")
            );

            HGDIOBJ oldFont = SelectObject(memDC, hFont);

            // Set text color and background mode
            SetTextColor(memDC, RGB(255, 255, 255)); // White text
            SetBkMode(memDC, TRANSPARENT);

            bool shouldClick = false; // Flag to determine if a click should be performed

            // Lock the mutex before accessing playersAddresses
            {
                std::lock_guard<std::mutex> lock(playersMutex);

                // Iterate through playersAddresses and draw them
                for(size_t idx = 0; idx < playersAddresses.size(); ++idx){
                    uintptr_t addr = playersAddresses[idx];

                    Vec3 playerPos;
                    if(!ReadVec3(addr, playerPos)){
                        // Failed to read player position; skip this player
                        continue;
                    }

                    float w;
                    Vec3 transformed = multiplyMatrixVector(vp, playerPos, w);
                    
                    // Only proceed if w is positive (in front of the camera)
                    if(w <= 0.0f) continue;

                    // Adjust the transformed coordinates to NDC [0,1]
                    transformed.x = (transformed.x + 1.0f) * 0.5f;
                    transformed.y = (transformed.y + 1.0f) * 0.5f;
                    transformed.z = (transformed.z + 1.0f) * 0.5f; // If needed

                    // Convert to screen space
                    int x = static_cast<int>(transformed.x * screenWidth);
                    int y = static_cast<int>((1.0f - transformed.y) * screenHeight); // Y is inverted

                    // Only draw if within screen bounds
                    if(x >=0 && x < screenWidth && y >=0 && y < screenHeight){
                        // Calculate Euclidean distance between eye and player position
                        float distance = sqrtf(
                            (playerPos.x - eye.x) * (playerPos.x - eye.x) +
                            (playerPos.y - eye.y) * (playerPos.y - eye.y) +
                            (playerPos.z - eye.z) * (playerPos.z - eye.z)
                        );

                        // Define a scaling factor based on empirical testing
                        static float scaleFactor = 1100.0f; // Adjust as needed

                        // Calculate rectangle size inversely proportional to distance
                        int rectSize = static_cast<int>(scaleFactor / distance);

                        // Clamp rectangle size to reasonable bounds
                        rectSize = min(max(rectSize, 10), 2000);

                        // Define the rectangle coordinates
                        int left = static_cast<int>(x - (rectSize / 2) * 0.4f);
                        int top = static_cast<int>(y - rectSize / 2 - 200 / distance); // Adjust vertically based on distance
                        int right = static_cast<int>(x + (rectSize / 2) * 0.4f);
                        int bottom = static_cast<int>(y + rectSize / 2 - 450 / distance);

                        // Draw the rectangle outline
                        Rectangle(memDC, left, top, right, bottom);

                        // Prepare the index string
                        char indexStr[16];
                        sprintf_s(indexStr, "%zu", idx);

                        SIZE textSize;
                        GetTextExtentPoint32(memDC, indexStr, strlen(indexStr), &textSize);

                        int textX = x - textSize.cx / 2; // Centered horizontally
                        int textY = y - rectSize / 2 - textSize.cy; // Above the rectangle

                        // Draw the index number
                        TextOut(memDC, textX, textY, indexStr, strlen(indexStr));

                        // Check if the center point is within this rectangle
                        static int centerX = screenWidth / 2;
                        static int centerY = screenHeight / 2;
                        // Auto tracking
                        if(autoLeftClickEnabled.load() || autoRightClickEnabled.load()) {
                            const double move = 0.3;
                            if(centerX > x && centerX - x < 50) {
                                MoveMouseRelative(-move, 0);
                                centerX -= move;
                            }
                            if(centerX < x && x - centerX < 50) {
                                MoveMouseRelative(move, 0);
                                centerX += move;
                            }
                        }
                        if(centerX >= left && centerX <= right && centerY >= top && centerY <= bottom){
                            shouldClick = true;
                        } else
                            shouldClick = false;
                        // Handle auto left-click
                        if(autoLeftClickEnabled.load()) {
                            if (shouldClick && !leftClickHeld.load()) {
                                performLeftClickDown();
                                leftClickHeld.store(true);
                            }
                            if (!shouldClick && leftClickHeld.load()) {
                                performLeftClickUp();
                                leftClickHeld.store(false);
                            }
                        }
                        // Handle auto right-click
                        if(autoRightClickEnabled.load()) {
                            if (shouldClick && !rightClickHeld.load()) {
                                performRightClickDown();
                                rightClickHeld.store(true);
                            }
                            if (!shouldClick && rightClickHeld.load()) {
                                printf("Right click up\n");
                                performRightClickUp();
                                rightClickHeld.store(false);
                            }
                        }
                    }
                }
            } // Mutex is unlocked here

            // Clean up GDI objects
            SelectObject(memDC, oldPen);
            DeleteObject(pen);
            SelectObject(memDC, oldBrush);
            // Do NOT delete the NULL_BRUSH

            // Restore and delete the font
            SelectObject(memDC, oldFont);
            DeleteObject(hFont);

            // Blit the memory DC to the window DC
            BitBlt(hdc, 0, 0, screenWidth, screenHeight, memDC, 0, 0, SRCCOPY);

            // Clean up memory DC
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Function to create a transparent, click-through window
HWND CreateOverlayWindow(HINSTANCE hInstance) {
    const char CLASS_NAME[]  = "OverlayWindowClass";

    WNDCLASS wc = { };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // Get screen dimensions
    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        "Overlay",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL){
        return NULL;
    }

    // Make the window transparent by setting a color key (black)
    // All black pixels will be transparent
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Show the window
    ShowWindow(hwnd, SW_SHOW);

    return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow){
    // Allocate console for user input
    AllocConsole();
    FILE* fpstdin;
    freopen_s(&fpstdin, "CONIN$", "r", stdin);
    FILE* fpstdout;
    freopen_s(&fpstdout, "CONOUT$", "w", stdout);
    FILE* fpstderr;
    freopen_s(&fpstderr, "CONOUT$", "w", stderr);
    ShowWindow(GetConsoleWindow(), SW_SHOW);

    DWORD pid = GetProcessID(L"Overwatch.exe");
    if(pid == 0){
        MessageBox(NULL, "Failed to find target process.", "Error", MB_ICONERROR);
        return 0;
    }

    // Open the target process
    hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if(hProcess == NULL){
        MessageBox(NULL, "Failed to open target process.", "Error", MB_ICONERROR);
        return 0;
    }

    // Initialize positions
    if(process_position_addresses(myPos, playersAddresses) != 0){
        MessageBox(NULL, "Failed to process position addresses.", "Error", MB_ICONERROR);
        CloseHandle(hProcess);
        return 0;
    }

    // Create overlay window
    HWND hwnd = CreateOverlayWindow(hInstance);
    if(hwnd == NULL){
        MessageBox(NULL, "Failed to create overlay window.", "Error", MB_ICONERROR);
        return 0;
    }

    // Create the hotkey listener thread (only for HOTKEY_REMOVE)
    HANDLE hHotkeyThread = CreateThread(NULL, 0, HotkeyListener, NULL, 0, NULL);
    if (hHotkeyThread == NULL) {
        MessageBox(NULL, "Failed to create hotkey listener thread.", "Error", MB_ICONERROR);
        return 0;
    }

    // Create the key monitor thread for auto-click features
    HANDLE hKeyMonitorThread = CreateThread(NULL, 0, KeyMonitorThread, NULL, 0, NULL);
    if (hKeyMonitorThread == NULL) {
        MessageBox(NULL, "Failed to create key monitor thread.", "Error", MB_ICONERROR);
        runningKeyMonitor.store(false);
        TerminateThread(hHotkeyThread, 0);
        CloseHandle(hHotkeyThread);
        return 0;
    }

    // Message loop
    MSG msg = { };
    while(true){
        // Process all pending messages
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){
            if(msg.message == WM_QUIT){
                runningKeyMonitor.store(false);
                CloseHandle(hHotkeyThread);
                CloseHandle(hKeyMonitorThread);
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Check if the window still exists
        if (!IsWindow(hwnd)) {
            runningKeyMonitor.store(false);
            PostQuitMessage(0);
            break;
        }

        // Invalidate the window to trigger WM_PAINT
        InvalidateRect(hwnd, NULL, FALSE);
    }

    // Free the console before exiting
    FreeConsole();

    CloseHandle(hHotkeyThread);
    CloseHandle(hKeyMonitorThread);
    return 0;
}
