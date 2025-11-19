#include "inkview.h"
#include <string.h>

// Application state
static bool isConnected = false;
static char statusText[256] = "Не подключено";
static ifont *mainFont = NULL;
static ifont *buttonFont = NULL;

// Screen dimensions
static int screenWidth = 0;
static int screenHeight = 0;

// Button areas
static irect settingsButton;
static irect exitButton;

// Forward declarations
static void drawMainScreen();
static void handlePointer(int type, int x, int y);
static int mainHandler(int type, int par1, int par2);

// Initialize the application
void initApp() {
    screenWidth = ScreenWidth();
    screenHeight = ScreenHeight();
    
    // Load fonts
    mainFont = OpenFont("LiberationSans", 24, 1);
    buttonFont = OpenFont("LiberationSans", 20, 1);
    
    // Define button positions (bottom of screen)
    int buttonWidth = 250;
    int buttonHeight = 60;
    int buttonY = screenHeight - buttonHeight - 40;
    int spacing = 50;
    
    // Settings button (left)
    settingsButton.x = spacing;
    settingsButton.y = buttonY;
    settingsButton.w = buttonWidth;
    settingsButton.h = buttonHeight;
    
    // Exit button (right)
    exitButton.x = screenWidth - buttonWidth - spacing;
    exitButton.y = buttonY;
    exitButton.w = buttonWidth;
    exitButton.h = buttonHeight;
}

// Draw the main screen
void drawMainScreen() {
    ClearScreen();
    
    // Draw title
    SetFont(mainFont, BLACK);
    DrawString(50, 50, "Calibre Companion");
    
    // Draw connection status indicator
    int indicatorX = 50;
    int indicatorY = 120;
    int indicatorSize = 30;
    
    // Draw indicator circle
    if (isConnected) {
        FillArea(indicatorX, indicatorY, indicatorSize, indicatorSize, BLACK);
        DrawString(indicatorX + indicatorSize + 20, indicatorY + 5, "Подключено");
    } else {
        DrawRect(indicatorX, indicatorY, indicatorSize, indicatorSize, BLACK);
        DrawString(indicatorX + indicatorSize + 20, indicatorY + 5, "Отключено");
    }
    
    // Draw status text
    DrawString(50, 200, "Статус:");
    DrawString(50, 240, statusText);
    
    // Draw buttons
    // Settings button
    DrawRect(settingsButton.x, settingsButton.y, settingsButton.w, settingsButton.h, BLACK);
    SetFont(buttonFont, BLACK);
    int textWidth = StringWidth("Настройки");
    DrawString(settingsButton.x + (settingsButton.w - textWidth) / 2, 
               settingsButton.y + 20, "Настройки");
    
    // Exit button
    DrawRect(exitButton.x, exitButton.y, exitButton.w, exitButton.h, BLACK);
    textWidth = StringWidth("Выход");
    DrawString(exitButton.x + (exitButton.w - textWidth) / 2, 
               exitButton.y + 20, "Выход");
    
    FullUpdate();
}

// Handle pointer events (touch)
void handlePointer(int type, int x, int y) {
    if (type == EVT_POINTERUP) {
        // Check if Settings button was clicked
        if (x >= settingsButton.x && x <= settingsButton.x + settingsButton.w &&
            y >= settingsButton.y && y <= settingsButton.y + settingsButton.h) {
            
            strncpy(statusText, "Настройки (в разработке)", sizeof(statusText) - 1);
            drawMainScreen();
        }
        
        // Check if Exit button was clicked
        if (x >= exitButton.x && x <= exitButton.x + exitButton.w &&
            y >= exitButton.y && y <= exitButton.y + exitButton.h) {
            
            CloseApp();
        }
    }
}

// Main event handler
int mainHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            initApp();
            drawMainScreen();
            break;
            
        case EVT_EXIT:
            // Cleanup
            if (mainFont) CloseFont(mainFont);
            if (buttonFont) CloseFont(buttonFont);
            break;
            
        case EVT_SHOW:
            drawMainScreen();
            break;
            
        case EVT_POINTERDOWN:
        case EVT_POINTERMOVE:
        case EVT_POINTERUP:
            handlePointer(type, par1, par2);
            break;
            
        case EVT_KEYPRESS:
            // Handle hardware buttons if needed
            if (par1 == KEY_BACK || par1 == KEY_HOME) {
                CloseApp();
            }
            break;
    }
    
    return 0;
}

// Application entry point
int main(int argc, char *argv[]) {
    InkViewMain(mainHandler);
    return 0;
}
