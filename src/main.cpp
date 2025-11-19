#include "inkview.h"
#include <string.h>

// Application state
static bool isConnected = false;
static char statusText[256] = "Ожидание подключения...";
static ifont *titleFont = NULL;
static ifont *textFont = NULL;
static ifont *buttonFont = NULL;

// Window dimensions
static const int WIN_WIDTH = 600;
static const int WIN_HEIGHT = 500;
static int winX = 0;
static int winY = 0;

// UI element areas (relative to window)
static irect settingsButton;
static irect exitButton;
static irect statusBox;

// Forward declarations
static void drawWindow();
static int windowHandler(int type, int par1, int par2);

// Initialize UI elements
void initUI() {
    // Load fonts
    titleFont = OpenFont("LiberationSans-Bold", 28, 1);
    textFont = OpenFont("LiberationSans", 20, 1);
    buttonFont = OpenFont("LiberationSans", 20, 1);
    
    // Calculate window position (centered)
    int screenW = ScreenWidth();
    int screenH = ScreenHeight();
    winX = (screenW - WIN_WIDTH) / 2;
    winY = (screenH - WIN_HEIGHT) / 2;
    
    // Define status box (relative to window)
    statusBox.x = winX + 40;
    statusBox.y = winY + 200;
    statusBox.w = WIN_WIDTH - 80;
    statusBox.h = 70;
    
    // Define buttons (relative to window)
    int buttonWidth = 220;
    int buttonHeight = 60;
    int buttonY = winY + WIN_HEIGHT - buttonHeight - 40;
    int spacing = (WIN_WIDTH - 2 * buttonWidth) / 3;
    
    settingsButton.x = winX + spacing;
    settingsButton.y = buttonY;
    settingsButton.w = buttonWidth;
    settingsButton.h = buttonHeight;
    
    exitButton.x = winX + spacing * 2 + buttonWidth;
    exitButton.y = buttonY;
    exitButton.w = buttonWidth;
    exitButton.h = buttonHeight;
}

// Draw connection indicator
void drawConnectionIndicator(int x, int y, int size, bool connected) {
    if (connected) {
        FillArea(x, y, size, size, BLACK);
    } else {
        DrawRect(x, y, size, size, BLACK);
        FillArea(x + 2, y + 2, size - 4, size - 4, WHITE);
    }
}

// Draw a button
void drawButton(irect *rect, const char *text) {
    DrawRect(rect->x, rect->y, rect->w, rect->h, BLACK);
    DrawRect(rect->x + 1, rect->y + 1, rect->w - 2, rect->h - 2, BLACK);
    
    SetFont(buttonFont, BLACK);
    int textWidth = StringWidth(text);
    int textX = rect->x + (rect->w - textWidth) / 2;
    int textY = rect->y + (rect->h - 20) / 2;
    DrawString(textX, textY, text);
}

// Draw window content
void drawWindow() {
    // Draw window background
    FillArea(winX, winY, WIN_WIDTH, WIN_HEIGHT, WHITE);
    DrawRect(winX, winY, WIN_WIDTH, WIN_HEIGHT, BLACK);
    DrawRect(winX + 1, winY + 1, WIN_WIDTH - 2, WIN_HEIGHT - 2, BLACK);
    
    // Draw title
    SetFont(titleFont, BLACK);
    const char *title = "Pocketbook Companion";
    int titleWidth = StringWidth(title);
    DrawString(winX + (WIN_WIDTH - titleWidth) / 2, winY + 40, title);
    
    // Draw separator line
    DrawLine(winX + 40, winY + 90, winX + WIN_WIDTH - 40, winY + 90, BLACK);
    
    // Draw connection status
    int indicatorX = winX + 60;
    int indicatorY = winY + 130;
    int indicatorSize = 20;
    
    drawConnectionIndicator(indicatorX, indicatorY, indicatorSize, isConnected);
    
    SetFont(textFont, BLACK);
    const char *connText = isConnected ? "Подключено к Calibre" : "Нет подключения";
    DrawString(indicatorX + indicatorSize + 15, indicatorY, connText);
    
    // Draw status box
    DrawRect(statusBox.x, statusBox.y, statusBox.w, statusBox.h, BLACK);
    DrawRect(statusBox.x + 2, statusBox.y + 2, statusBox.w - 4, statusBox.h - 4, BLACK);
    
    SetFont(textFont, BLACK);
    DrawString(statusBox.x + 15, statusBox.y + 25, statusText);
    
    // Draw buttons
    drawButton(&settingsButton, "Настройки");
    drawButton(&exitButton, "Выход");
}

// Window event handler
int windowHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            initUI();
            drawWindow();
            FullUpdate();
            break;
            
        case EVT_SHOW:
            drawWindow();
            FullUpdate();
            break;
            
        case EVT_EXIT:
            if (titleFont) CloseFont(titleFont);
            if (textFont) CloseFont(textFont);
            if (buttonFont) CloseFont(buttonFont);
            break;
            
        case EVT_POINTERUP: {
            int x = par1;
            int y = par2;
            
            // Check Settings button
            if (x >= settingsButton.x && x <= settingsButton.x + settingsButton.w &&
                y >= settingsButton.y && y <= settingsButton.y + settingsButton.h) {
                strncpy(statusText, "Настройки (в разработке)", sizeof(statusText) - 1);
                drawWindow();
                PartialUpdate(winX, winY, WIN_WIDTH, WIN_HEIGHT);
            }
            
            // Check Exit button
            if (x >= exitButton.x && x <= exitButton.x + exitButton.w &&
                y >= exitButton.y && y <= exitButton.y + exitButton.h) {
                CloseApp();
            }
            break;
        }
    }
    
    return 0;
}

// Application entry point
int main(int argc, char *argv[]) {
    // Open as panel (non-fullscreen)
    OpenScreen();
    InkViewMain(windowHandler);
    
    return 0;
}
