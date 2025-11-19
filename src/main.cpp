#include "inkview.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// SETTINGS MODULE
// ============================================================================

struct AppSettings {
    char ip[64];
    char port[16];
    char password[128];
    char readColumn[64];
    char readDateColumn[64];
    char favoriteColumn[64];
    char inputFolder[256];
};

static AppSettings settings = {
    "192.168.1.100",
    "8080",
    "",
    "Last Read",
    "Last Read Date",
    "Favorite",
    "/mnt/ext1/Books"
};

enum FieldIndex {
    FIELD_IP = 0,
    FIELD_PORT,
    FIELD_PASSWORD,
    FIELD_READ_COLUMN,
    FIELD_READ_DATE_COLUMN,
    FIELD_FAVORITE_COLUMN,
    FIELD_INPUT_FOLDER,
    FIELD_COUNT
};

struct FieldInfo {
    const char* label;
    char* value;
    int maxLen;
    bool isFolder;
};

static FieldInfo fieldInfo[FIELD_COUNT];
static int currentField = -1;

void initFieldInfo() {
    fieldInfo[FIELD_IP] = {"IP адрес:", settings.ip, sizeof(settings.ip), false};
    fieldInfo[FIELD_PORT] = {"Порт:", settings.port, sizeof(settings.port), false};
    fieldInfo[FIELD_PASSWORD] = {"Пароль:", settings.password, sizeof(settings.password), false};
    fieldInfo[FIELD_READ_COLUMN] = {"Read column:", settings.readColumn, sizeof(settings.readColumn), false};
    fieldInfo[FIELD_READ_DATE_COLUMN] = {"Read date column:", settings.readDateColumn, sizeof(settings.readDateColumn), false};
    fieldInfo[FIELD_FAVORITE_COLUMN] = {"Favorite column:", settings.favoriteColumn, sizeof(settings.favoriteColumn), false};
    fieldInfo[FIELD_INPUT_FOLDER] = {"Input folder:", settings.inputFolder, sizeof(settings.inputFolder), true};
}

void loadSettings() {
    FILE *f = fopen("/mnt/ext1/system/config/calibre-companion.cfg", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *key = line;
                char *value = eq + 1;
                
                char *nl = strchr(value, '\n');
                if (nl) *nl = '\0';
                
                if (strcmp(key, "ip") == 0) {
                    strncpy(settings.ip, value, sizeof(settings.ip) - 1);
                } else if (strcmp(key, "port") == 0) {
                    strncpy(settings.port, value, sizeof(settings.port) - 1);
                } else if (strcmp(key, "password") == 0) {
                    strncpy(settings.password, value, sizeof(settings.password) - 1);
                } else if (strcmp(key, "read_column") == 0) {
                    strncpy(settings.readColumn, value, sizeof(settings.readColumn) - 1);
                } else if (strcmp(key, "read_date_column") == 0) {
                    strncpy(settings.readDateColumn, value, sizeof(settings.readDateColumn) - 1);
                } else if (strcmp(key, "favorite_column") == 0) {
                    strncpy(settings.favoriteColumn, value, sizeof(settings.favoriteColumn) - 1);
                } else if (strcmp(key, "input_folder") == 0) {
                    strncpy(settings.inputFolder, value, sizeof(settings.inputFolder) - 1);
                }
            }
        }
        fclose(f);
    }
}

void saveSettings() {
    FILE *f = fopen("/mnt/ext1/system/config/calibre-companion.cfg", "w");
    if (f) {
        fprintf(f, "ip=%s\n", settings.ip);
        fprintf(f, "port=%s\n", settings.port);
        fprintf(f, "password=%s\n", settings.password);
        fprintf(f, "read_column=%s\n", settings.readColumn);
        fprintf(f, "read_date_column=%s\n", settings.readDateColumn);
        fprintf(f, "favorite_column=%s\n", settings.favoriteColumn);
        fprintf(f, "input_folder=%s\n", settings.inputFolder);
        fclose(f);
        
        Message(ICON_INFORMATION, "Успешно", "Настройки сохранены", 1500);
    } else {
        Message(ICON_ERROR, "Ошибка", "Не удалось сохранить настройки", 2000);
    }
}

// Forward declarations
void showSettingsPanel();
void showMainDialog();

// Callback для ввода текста
void keyboardCallback(char *text) {
    if (text && currentField >= 0 && currentField < FIELD_COUNT) {
        strncpy(fieldInfo[currentField].value, text, fieldInfo[currentField].maxLen - 1);
        fieldInfo[currentField].value[fieldInfo[currentField].maxLen - 1] = '\0';
        showSettingsPanel();
    }
}

// Callback для выбора папки
void folderCallback(char *path) {
    if (path && path[0] != '\0') {
        strncpy(settings.inputFolder, path, sizeof(settings.inputFolder) - 1);
        settings.inputFolder[sizeof(settings.inputFolder) - 1] = '\0';
        showSettingsPanel();
    }
}

// ============================================================================
// SETTINGS PANEL
// ============================================================================

void settingsPanelHandler(int button) {
    if (button == 1) {
        // Save button
        saveSettings();
    }
    // Dialog closes automatically, return to main dialog
    showMainDialog();
}

void showSettingsPanel() {
    char buffer[2048];
    int offset = 0;
    
    // Build settings text
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "IP адрес: %s\n\n", settings.ip);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "Порт: %s\n\n", settings.port);
    
    // Mask password
    if (strlen(settings.password) > 0) {
        char masked[128];
        int len = strlen(settings.password);
        for (int j = 0; j < len && j < 127; j++) {
            masked[j] = '*';
        }
        masked[len] = '\0';
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "Пароль: %s\n\n", masked);
    } else {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "Пароль: \n\n");
    }
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "Read column: %s\n\n", settings.readColumn);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "Read date column: %s\n\n", settings.readDateColumn);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "Favorite column: %s\n\n", settings.favoriteColumn);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                      "Input folder: %s", settings.inputFolder);
    
    Dialog(ICON_INFORMATION, "Настройки", buffer, "OK", "Отмена", settingsPanelHandler);
}

// ============================================================================
// MAIN DIALOG
// ============================================================================

static ifont *titleFont = NULL;
static ifont *textFont = NULL;
static int panelX, panelY, panelW, panelH;

void drawMainPanel() {
    int screenW = ScreenWidth();
    int screenH = ScreenHeight();
    
    // Calculate panel dimensions (60% of screen width, auto height)
    panelW = (screenW * 60) / 100;
    panelH = screenH / 3;
    panelX = (screenW - panelW) / 2;
    panelY = (screenH - panelH) / 2;
    
    // Clear screen
    ClearScreen();
    
    // Draw panel background
    SetColor(WHITE);
    FillArea(panelX, panelY, panelW, panelH, WHITE);
    
    // Draw panel border
    SetColor(BLACK);
    DrawRect(panelX, panelY, panelW, panelH, BLACK);
    
    // Calculate positions
    int titleY = panelY + 60;
    int iconY = panelY + 20;
    int iconSize = 48;
    int settingsIconX = panelX + panelW - iconSize - 70;
    int closeIconX = panelX + panelW - iconSize - 20;
    
    // Draw title
    if (!titleFont) {
        titleFont = OpenFont("LiberationSans-Bold", 32, 1);
    }
    if (titleFont) {
        SetFont(titleFont, BLACK);
        int titleW = StringWidth("Pocketbook Companion");
        DrawString((screenW - titleW) / 2, titleY, "Pocketbook Companion");
    }
    
    // Draw settings icon (gear symbol using text)
    if (!textFont) {
        textFont = OpenFont("LiberationSans", iconSize, 1);
    }
    if (textFont) {
        SetFont(textFont, BLACK);
        DrawString(settingsIconX, iconY + iconSize, "⚙");
        
        // Draw close icon (X symbol)
        DrawString(closeIconX, iconY + iconSize, "✕");
    }
    
    // Add status text
    const char* statusText = "Статус: Не подключено";
    if (textFont) {
        SetFont(textFont, DGRAY);
        int statusW = StringWidth(statusText);
        DrawString((screenW - statusW) / 2, titleY + 80, statusText);
    }
    
    FullUpdate();
}

int mainPanelHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            drawMainPanel();
            break;
            
        case EVT_SHOW:
            drawMainPanel();
            break;
            
        case EVT_POINTERUP: {
            // Check if settings icon was clicked
            int iconSize = 48;
            int settingsIconX = panelX + panelW - iconSize - 70;
            int closeIconX = panelX + panelW - iconSize - 20;
            int iconY = panelY + 20;
            
            if (par1 >= settingsIconX && par1 <= settingsIconX + iconSize &&
                par2 >= iconY && par2 <= iconY + iconSize) {
                // Settings icon clicked
                showSettingsPanel();
                return 1;
            }
            
            if (par1 >= closeIconX && par1 <= closeIconX + iconSize &&
                par2 >= iconY && par2 <= iconY + iconSize) {
                // Close icon clicked
                CloseApp();
                return 1;
            }
            break;
        }
        
        case EVT_KEYPRESS:
            if (par1 == KEY_BACK || par1 == KEY_HOME) {
                CloseApp();
                return 1;
            }
            break;
    }
    
    return 0;
}

void showMainDialog() {
    SetEventHandler(mainPanelHandler);
    drawMainPanel();
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

int main(int argc, char *argv[]) {
    loadSettings();
    initFieldInfo();
    InkViewMain(mainPanelHandler);
    return 0;
}
