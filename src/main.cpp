#include "inkview.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// КОНСТАНТЫ
// ============================================================================

#define PANEL_HEIGHT 70
#define MARGIN 20
#define ROW_HEIGHT 80
#define LABEL_WIDTH 280
#define FIELD_HEIGHT 60

// ============================================================================
// СТРУКТУРЫ
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

// Шрифты
static ifont *fontNormal = NULL;
static ifont *fontBold = NULL;
static ifont *fontSmall = NULL;

// Размеры экрана
static int screenWidth = 0;
static int screenHeight = 0;
static int contentY = 0;

// Текущее редактируемое поле
static int selectedField = -1;

// ============================================================================
// УПРАВЛЕНИЕ НАСТРОЙКАМИ
// ============================================================================

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
// CALLBACK-ФУНКЦИИ ДЛЯ ВВОДА
// ============================================================================

void ipCallback(char *text) {
    if (text) {
        strncpy(settings.ip, text, sizeof(settings.ip) - 1);
        settings.ip[sizeof(settings.ip) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void portCallback(char *text) {
    if (text) {
        strncpy(settings.port, text, sizeof(settings.port) - 1);
        settings.port[sizeof(settings.port) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void passwordCallback(char *text) {
    if (text) {
        strncpy(settings.password, text, sizeof(settings.password) - 1);
        settings.password[sizeof(settings.password) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void readColumnCallback(char *text) {
    if (text) {
        strncpy(settings.readColumn, text, sizeof(settings.readColumn) - 1);
        settings.readColumn[sizeof(settings.readColumn) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void readDateColumnCallback(char *text) {
    if (text) {
        strncpy(settings.readDateColumn, text, sizeof(settings.readDateColumn) - 1);
        settings.readDateColumn[sizeof(settings.readDateColumn) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void favoriteColumnCallback(char *text) {
    if (text) {
        strncpy(settings.favoriteColumn, text, sizeof(settings.favoriteColumn) - 1);
        settings.favoriteColumn[sizeof(settings.favoriteColumn) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

void folderCallback(char *path) {
    if (path && path[0] != '\0') {
        strncpy(settings.inputFolder, path, sizeof(settings.inputFolder) - 1);
        settings.inputFolder[sizeof(settings.inputFolder) - 1] = '\0';
        saveSettings();
        FullUpdate();
    }
}

// ============================================================================
// ОТРИСОВКА ИНТЕРФЕЙСА
// ============================================================================

void drawSettingRow(const char *label, const char *value, int y, bool isPassword, int fieldIndex) {
    // Отрисовка метки
    SetFont(fontBold, BLACK);
    DrawString(MARGIN, y, label);
    
    // Отрисовка рамки поля
    int fieldX = MARGIN;
    int fieldY = y + 35;
    int fieldW = screenWidth - 2 * MARGIN;
    
    DrawRect(fieldX, fieldY, fieldW, FIELD_HEIGHT, BLACK);
    FillArea(fieldX + 2, fieldY + 2, fieldW - 4, FIELD_HEIGHT - 4, WHITE);
    
    // Отрисовка значения
    SetFont(fontNormal, BLACK);
    char displayValue[256];
    if (isPassword && strlen(value) > 0) {
        strcpy(displayValue, "••••••••");
    } else if (strlen(value) == 0) {
        SetFont(fontSmall, DGRAY);
        strcpy(displayValue, "Tap to set");
    } else {
        strncpy(displayValue, value, sizeof(displayValue) - 1);
        displayValue[sizeof(displayValue) - 1] = '\0';
    }
    
    DrawTextRect(fieldX + 15, fieldY + 5, fieldW - 30, FIELD_HEIGHT - 10, 
                 displayValue, ALIGN_LEFT | VALIGN_MIDDLE);
    
    // Выделение активного поля
    if (selectedField == fieldIndex) {
        DrawRect(fieldX - 2, fieldY - 2, fieldW + 4, FIELD_HEIGHT + 4, BLACK);
    }
}

void drawMainScreen() {
    ClearScreen();
    
    // Панель сверху
    DrawPanel(NULL, "", "Calibre Companion", 0);
    
    // Начало контента после панели
    contentY = PanelHeight();
    int y = contentY + MARGIN;
    
    // Заголовок раздела подключения
    SetFont(fontBold, BLACK);
    DrawString(MARGIN, y, "Connection");
    y += 45;
    
    // Статус подключения с переключателем
    SetFont(fontNormal, BLACK);
    char statusText[64];
    snprintf(statusText, sizeof(statusText), "Status: %s", 
             settings.connectionEnabled ? "Enabled" : "Disabled");
    DrawString(MARGIN, y, statusText);
    y += ROW_HEIGHT + 10;
    
    // IP адрес
    drawSettingRow("IP Address", settings.ip, y, false, 0);
    y += ROW_HEIGHT + 20;
    
    // Порт
    drawSettingRow("Port", settings.port, y, false, 1);
    y += ROW_HEIGHT + 20;
    
    // Пароль
    drawSettingRow("Password", settings.password, y, true, 2);
    y += ROW_HEIGHT + 20;
    
    // Заголовок раздела настроек Calibre
    SetFont(fontBold, BLACK);
    DrawString(MARGIN, y, "Calibre Settings");
    y += 45;
    
    // Read Status Column
    drawSettingRow("Read Status Column", settings.readColumn, y, false, 3);
    y += ROW_HEIGHT + 20;
    
    // Read Date Column
    drawSettingRow("Read Date Column", settings.readDateColumn, y, false, 4);
    y += ROW_HEIGHT + 20;
    
    // Favorite Column
    drawSettingRow("Favorite Column", settings.favoriteColumn, y, false, 5);
    y += ROW_HEIGHT + 20;
    
    // Input Folder
    drawSettingRow("Input Folder", settings.inputFolder, y, false, 6);
    
    FullUpdate();
}

// ============================================================================
// ОБРАБОТКА НАЖАТИЙ
// ============================================================================

int getFieldAtPoint(int x, int y) {
    if (y < contentY) return -1;
    
    int fieldY = contentY + MARGIN + 45 + ROW_HEIGHT + 10; // После заголовка и статуса
    
    for (int i = 0; i < 7; i++) {
        // Пропускаем заголовок "Calibre Settings" перед полем 3
        if (i == 3) {
            fieldY += 45; // Высота заголовка
        }
        
        int fieldTop = fieldY + 35;
        int fieldBottom = fieldTop + FIELD_HEIGHT;
        
        if (y >= fieldTop && y <= fieldBottom) {
            return i;
        }
        
        fieldY += ROW_HEIGHT + 20;
    }
    
    return -1;
}

void handleFieldTap(int fieldIndex) {
    char buffer[256];
    
    switch (fieldIndex) {
        case 0: // IP Address
            strncpy(buffer, settings.ip, sizeof(buffer) - 1);
            OpenKeyboard("IP Address", buffer, sizeof(buffer) - 1, KBD_NORMAL, ipCallback);
            break;
            
        case 1: // Port
            strncpy(buffer, settings.port, sizeof(buffer) - 1);
            OpenKeyboard("Port", buffer, sizeof(buffer) - 1, KBD_NUMERIC, portCallback);
            break;
            
        case 2: // Password
            strncpy(buffer, settings.password, sizeof(buffer) - 1);
            OpenKeyboard("Password", buffer, sizeof(buffer) - 1, KBD_PASSWORD, passwordCallback);
            break;
            
        case 3: // Read Column
            strncpy(buffer, settings.readColumn, sizeof(buffer) - 1);
            OpenKeyboard("Read Status Column", buffer, sizeof(buffer) - 1, KBD_NORMAL, readColumnCallback);
            break;
            
        case 4: // Read Date Column
            strncpy(buffer, settings.readDateColumn, sizeof(buffer) - 1);
            OpenKeyboard("Read Date Column", buffer, sizeof(buffer) - 1, KBD_NORMAL, readDateColumnCallback);
            break;
            
        case 5: // Favorite Column
            strncpy(buffer, settings.favoriteColumn, sizeof(buffer) - 1);
            OpenKeyboard("Favorite Column", buffer, sizeof(buffer) - 1, KBD_NORMAL, favoriteColumnCallback);
            break;
            
        case 6: // Input Folder
            strncpy(buffer, settings.inputFolder, sizeof(buffer) - 1);
            OpenDirectorySelector("Input Folder", buffer, sizeof(buffer), folderCallback);
            break;
    }
}

void handleConnectionToggle() {
    settings.connectionEnabled = !settings.connectionEnabled;
    saveSettings();
    drawMainScreen();
}

// ============================================================================
// ОБРАБОТЧИК СОБЫТИЙ
// ============================================================================

int mainEventHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            // Получение размеров экрана
            screenWidth = ScreenWidth();
            screenHeight = ScreenHeight();
            
            // Загрузка шрифтов
            fontBold = OpenFont("LiberationSans-Bold", 28, 1);
            fontNormal = OpenFont("LiberationSans", 24, 1);
            fontSmall = OpenFont("LiberationSans", 20, 1);
            
            // Загрузка настроек
            loadSettings();
            
            // Отрисовка интерфейса
            drawMainScreen();
            break;
            
        case EVT_SHOW:
            // Перерисовка при возврате из клавиатуры
            drawMainScreen();
            break;
            
        case EVT_POINTERUP:
            {
                int x = par1;
                int y = par2;
                
                // Проверка нажатия на переключатель подключения
                int statusY = contentY + MARGIN + 45;
                if (y >= statusY && y <= statusY + 40 && x >= MARGIN && x <= screenWidth - MARGIN) {
                    handleConnectionToggle();
                    return 1;
                }
                
                // Проверка нажатия на поля
                int fieldIndex = getFieldAtPoint(x, y);
                if (fieldIndex >= 0) {
                    handleFieldTap(fieldIndex);
                    return 1;
                }
            }
            break;
            
        case EVT_KEYPRESS:
            if (par1 == IV_KEY_BACK || par1 == IV_KEY_PREV) {
                CloseApp();
                return 1;
            }
            break;
            
        case EVT_EXIT:
            // Освобождение ресурсов
            if (fontBold) CloseFont(fontBold);
            if (fontNormal) CloseFont(fontNormal);
            if (fontSmall) CloseFont(fontSmall);
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
