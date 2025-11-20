#include "inkview.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// APPLICATION STATE
// ============================================================================

struct AppSettings {
    char ip[64];
    char port[16];
    char password[128];
    char readColumn[64];
    char readDateColumn[64];
    char favoriteColumn[64];
    char inputFolder[256];
    bool connectionEnabled;
};

static AppSettings settings = {
    "192.168.1.100",
    "8080",
    "",
    "Last Read",
    "Last Read Date",
    "Favorite",
    "/mnt/ext1/Books",
    false
};

// UI state
static ifont *titleFont = NULL;
static ifont *labelFont = NULL;
static ifont *valueFont = NULL;
static int screenWidth = 0;
static int screenHeight = 0;
static int currentEditField = -1;

// Layout constants
static const int STATUS_BAR_HEIGHT = 48;
static const int TITLE_BAR_HEIGHT = 70;
static const int HOME_ICON_SIZE = 48;
static const int PADDING = 30;
static const int ITEM_HEIGHT = 100;
static const int TOGGLE_SIZE = 60;

enum FieldIndex {
    FIELD_CONNECTION_TOGGLE = 0,
    FIELD_IP,
    FIELD_PORT,
    FIELD_PASSWORD,
    FIELD_READ_COLUMN,
    FIELD_READ_DATE_COLUMN,
    FIELD_FAVORITE_COLUMN,
    FIELD_INPUT_FOLDER,
    FIELD_COUNT
};

struct UIField {
    const char* label;
    char* value;
    int maxLen;
    bool isFolder;
    bool isToggle;
};

static UIField uiFields[FIELD_COUNT];

// ============================================================================
// SETTINGS MANAGEMENT
// ============================================================================

void initUIFields() {
    uiFields[FIELD_CONNECTION_TOGGLE] = {"Connection status", NULL, 0, false, true};
    uiFields[FIELD_IP] = {"IP Address", settings.ip, sizeof(settings.ip), false, false};
    uiFields[FIELD_PORT] = {"Port", settings.port, sizeof(settings.port), false, false};
    uiFields[FIELD_PASSWORD] = {"Password", settings.password, sizeof(settings.password), false, false};
    uiFields[FIELD_READ_COLUMN] = {"Read status column", settings.readColumn, sizeof(settings.readColumn), false, false};
    uiFields[FIELD_READ_DATE_COLUMN] = {"Read date column", settings.readDateColumn, sizeof(settings.readDateColumn), false, false};
    uiFields[FIELD_FAVORITE_COLUMN] = {"Favorite column", settings.favoriteColumn, sizeof(settings.favoriteColumn), false, false};
    uiFields[FIELD_INPUT_FOLDER] = {"Input folder", settings.inputFolder, sizeof(settings.inputFolder), true, false};
}

void loadSettings() {
    FILE *f = iv_fopen("/mnt/ext1/system/config/calibre-companion.cfg", "r");
    if (f) {
        char line[512];
        while (iv_fgets(line, sizeof(line), f)) {
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
                } else if (strcmp(key, "connection_enabled") == 0) {
                    settings.connectionEnabled = (strcmp(value, "1") == 0);
                }
            }
        }
        iv_fclose(f);
    }
}

void saveSettings() {
    iv_buildpath("/mnt/ext1/system/config");
    FILE *f = iv_fopen("/mnt/ext1/system/config/calibre-companion.cfg", "w");
    if (f) {
        fprintf(f, "ip=%s\n", settings.ip);
        fprintf(f, "port=%s\n", settings.port);
        fprintf(f, "password=%s\n", settings.password);
        fprintf(f, "read_column=%s\n", settings.readColumn);
        fprintf(f, "read_date_column=%s\n", settings.readDateColumn);
        fprintf(f, "favorite_column=%s\n", settings.favoriteColumn);
        fprintf(f, "input_folder=%s\n", settings.inputFolder);
        fprintf(f, "connection_enabled=%d\n", settings.connectionEnabled ? 1 : 0);
        iv_fclose(f);
        iv_sync();
    }
}

// ============================================================================
// UI DRAWING
// ============================================================================

void drawTitleBar() {
    int y = STATUS_BAR_HEIGHT;
    
    // Draw separator line
    DrawLine(0, y, screenWidth, y, BLACK);
    
    // Draw home icon area (left side)
    int iconX = PADDING;
    int iconY = y + (TITLE_BAR_HEIGHT - HOME_ICON_SIZE) / 2;
    
    // Simple home icon - house shape
    DrawLine(iconX + HOME_ICON_SIZE/2, iconY + 5, iconX + 5, iconY + HOME_ICON_SIZE/2, BLACK);
    DrawLine(iconX + HOME_ICON_SIZE/2, iconY + 5, iconX + HOME_ICON_SIZE - 5, iconY + HOME_ICON_SIZE/2, BLACK);
    DrawRect(iconX + 10, iconY + HOME_ICON_SIZE/2, iconX + HOME_ICON_SIZE - 10, iconY + HOME_ICON_SIZE - 5, BLACK);
    
    // Draw title (centered)
    SetFont(titleFont, BLACK);
    const char* title = "CALIBRE COMPANION";
    int titleWidth = StringWidth(title);
    DrawString((screenWidth - titleWidth) / 2, y + (TITLE_BAR_HEIGHT - 28) / 2, title);
    
    // Draw bottom separator line
    DrawLine(0, y + TITLE_BAR_HEIGHT, screenWidth, y + TITLE_BAR_HEIGHT, BLACK);
}

void drawToggleButton(int x, int y, bool enabled) {
    // Draw circle
    if (enabled) {
        FillArea(x, y, TOGGLE_SIZE, TOGGLE_SIZE, DGRAY);
    }
    DrawRect(x, y, x + TOGGLE_SIZE, y + TOGGLE_SIZE, BLACK);
    
    // Draw checkmark if enabled
    if (enabled) {
        SetFont(valueFont, WHITE);
        DrawString(x + 15, y + 12, "✓");
    }
}

void drawListItem(int index, int y) {
    UIField *field = &uiFields[index];
    
    // Draw separator line above
    if (index > 0) {
        DrawLine(PADDING, y, screenWidth - PADDING, y, LGRAY);
    }
    
    int contentY = y + (ITEM_HEIGHT - 50) / 2;
    
    // Draw label
    SetFont(labelFont, BLACK);
    DrawString(PADDING, contentY, field->label);
    
    if (field->isToggle) {
        // Draw toggle button on right
        int toggleX = screenWidth - PADDING - TOGGLE_SIZE;
        int toggleY = y + (ITEM_HEIGHT - TOGGLE_SIZE) / 2;
        drawToggleButton(toggleX, toggleY, settings.connectionEnabled);
        
        // Draw status text
        SetFont(valueFont, DGRAY);
        const char* status = settings.connectionEnabled ? "Enabled" : "Disabled";
        int statusWidth = StringWidth(status);
        DrawString(toggleX - statusWidth - 20, contentY + 30, status);
    } else {
        // Draw value text
        SetFont(valueFont, DGRAY);
        if (field->value && field->value[0] != '\0') {
            char displayValue[256];
            if (index == FIELD_PASSWORD && strlen(field->value) > 0) {
                // Mask password
                strcpy(displayValue, "••••••••");
            } else {
                strncpy(displayValue, field->value, sizeof(displayValue) - 1);
                displayValue[sizeof(displayValue) - 1] = '\0';
            }
            
            // Truncate if too long
            int maxWidth = screenWidth - PADDING * 2 - 100;
            while (StringWidth(displayValue) > maxWidth && strlen(displayValue) > 0) {
                displayValue[strlen(displayValue) - 1] = '\0';
            }
            
            DrawString(PADDING, contentY + 30, displayValue);
        } else {
            DrawString(PADDING, contentY + 30, "Not set");
        }
        
        // Draw arrow on right
        int arrowX = screenWidth - PADDING - 20;
        DrawString(arrowX, contentY + 5, "›");
    }
}

void drawUI() {
    ClearScreen();
    
    // Draw title bar with home button
    drawTitleBar();
    
    // Draw list items
    int startY = STATUS_BAR_HEIGHT + TITLE_BAR_HEIGHT + 20;
    for (int i = 0; i < FIELD_COUNT; i++) {
        drawListItem(i, startY + i * ITEM_HEIGHT);
    }
    
    FullUpdate();
}

// ============================================================================
// INPUT CALLBACKS
// ============================================================================

void keyboardCallback(char *text) {
    if (text && currentEditField >= 0 && currentEditField < FIELD_COUNT) {
        UIField *field = &uiFields[currentEditField];
        if (field->value) {
            strncpy(field->value, text, field->maxLen - 1);
            field->value[field->maxLen - 1] = '\0';
            saveSettings();
        }
    }
    currentEditField = -1;
    drawUI();
}

void folderCallback(char *path) {
    if (path && path[0] != '\0' && currentEditField == FIELD_INPUT_FOLDER) {
        strncpy(settings.inputFolder, path, sizeof(settings.inputFolder) - 1);
        settings.inputFolder[sizeof(settings.inputFolder) - 1] = '\0';
        saveSettings();
    }
    currentEditField = -1;
    drawUI();
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

int getItemIndexAtY(int y) {
    int startY = STATUS_BAR_HEIGHT + TITLE_BAR_HEIGHT + 20;
    
    for (int i = 0; i < FIELD_COUNT; i++) {
        int itemY = startY + i * ITEM_HEIGHT;
        if (y >= itemY && y < itemY + ITEM_HEIGHT) {
            return i;
        }
    }
    
    return -1;
}

bool isHomeIconClicked(int x, int y) {
    int iconY = STATUS_BAR_HEIGHT;
    return (x >= 0 && x <= HOME_ICON_SIZE + PADDING * 2 &&
            y >= iconY && y <= iconY + TITLE_BAR_HEIGHT);
}

void handleItemClick(int index) {
    UIField *field = &uiFields[index];
    
    if (field->isToggle) {
        // Toggle connection
        settings.connectionEnabled = !settings.connectionEnabled;
        saveSettings();
        drawUI();
    } else if (field->isFolder) {
        // Open folder selector
        currentEditField = index;
        char buffer[256];
        strncpy(buffer, field->value, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        OpenDirectorySelector(field->label, buffer, sizeof(buffer), folderCallback);
    } else {
        // Open keyboard
        currentEditField = index;
        char buffer[256];
        if (field->value) {
            strncpy(buffer, field->value, sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        
        int keyboardType = (index == FIELD_PORT) ? KBD_NUMERIC : KBD_NORMAL;
        OpenKeyboard(field->label, buffer, field->maxLen - 1, keyboardType, keyboardCallback);
    }
}

int mainEventHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            screenWidth = ScreenWidth();
            screenHeight = ScreenHeight();
            
            titleFont = OpenFont("LiberationSans-Bold", 28, 1);
            labelFont = OpenFont("LiberationSans", 26, 1);
            valueFont = OpenFont("LiberationSans", 24, 1);
            
            loadSettings();
            initUIFields();
            drawUI();
            break;
            
        case EVT_SHOW:
            drawUI();
            break;
            
        case EVT_POINTERUP:
            // Check home icon
            if (isHomeIconClicked(par1, par2)) {
                CloseApp();
                return 1;
            }
            
            // Check list items
            int itemIndex = getItemIndexAtY(par2);
            if (itemIndex >= 0) {
                handleItemClick(itemIndex);
                return 1;
            }
            break;
            
        case EVT_KEYPRESS:
            if (par1 == IV_KEY_BACK || par1 == IV_KEY_PREV) {
                CloseApp();
                return 1;
            }
            break;
            
        case EVT_EXIT:
            if (titleFont) CloseFont(titleFont);
            if (labelFont) CloseFont(labelFont);
            if (valueFont) CloseFont(valueFont);
            break;
    }
    
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    InkViewMain(mainEventHandler);
    return 0;
}
