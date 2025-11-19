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
    
    // Define status box
    statusBox.x = 40;
    statusBox.y = 200;
    statusBox.w = WIN_WIDTH - 80;
    statusBox.h = 70;
    
    // Define buttons
    int buttonWidth = 220;
    int buttonHeight = 60;
    int buttonY = WIN_HEIGHT - buttonHeight - 40;
    int spacing = (WIN_WIDTH - 2 * buttonWidth) / 3;
    
    settingsButton.x = spacing;
    settingsButton.y = buttonY;
    settingsButton.w = buttonWidth;
    settingsButton.h = buttonHeight;
    
    exitButton.x = spacing * 2 + buttonWidth;
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
        // Draw inner rect to make it hollow
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
    ClearScreen();
    
    // Draw title
    SetFont(titleFont, BLACK);
    const char *title = "Pocketbook Companion";
    int titleWidth = StringWidth(title);
    DrawString((WIN_WIDTH - titleWidth) / 2, 40, title);
    
    // Draw separator line
    DrawLine(40, 90, WIN_WIDTH - 40, 90, BLACK);
    
    // Draw connection status
    int indicatorX = 60;
    int indicatorY = 130;
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
    
    PartialUpdate(0, 0, WIN_WIDTH, WIN_HEIGHT);
}

// Window event handler
int windowHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            initUI();
            break;
            
        case EVT_SHOW:
            drawWindow();
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
    // Center window on screen
    int screenW = ScreenWidth();
    int screenH = ScreenHeight();
    int winX = (screenW - WIN_WIDTH) / 2;
    int winY = (screenH - WIN_HEIGHT) / 2;
    
    // Create windowed application
    CreateWin(winX, winY, WIN_WIDTH, WIN_HEIGHT, "calibre-companion", windowHandler);
    
    return 0;
}
