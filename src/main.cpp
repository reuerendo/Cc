#include "inkview.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
void showMainMenu();
void showSettingsScreen();

// ============================================================================
// SETTINGS STRUCTURE
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

// ============================================================================
// SETTINGS I/O
// ============================================================================

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

// ============================================================================
// SETTINGS MENU
// ============================================================================

static imenu settingsMenu;

void editField(char* field, int maxLen, const char* title) {
    char buffer[256];
    strncpy(buffer, field, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    if (OpenKeyboard(title, buffer, maxLen - 1, KBD_NORMAL, NULL) != 0) {
        strncpy(field, buffer, maxLen - 1);
        field[maxLen - 1] = '\0';
    }
}

void selectFolderCallback(char *path) {
    if (path && path[0]) {
        strncpy(settings.inputFolder, path, sizeof(settings.inputFolder) - 1);
        settings.inputFolder[sizeof(settings.inputFolder) - 1] = '\0';
    }
}

void settingsMenuHandler(int index) {
    switch (index) {
        case 0: // IP
            editField(settings.ip, sizeof(settings.ip), "IP адрес");
            break;
        case 1: // Port
            editField(settings.port, sizeof(settings.port), "Порт");
            break;
        case 2: // Password
            editField(settings.password, sizeof(settings.password), "Пароль");
            break;
        case 3: // Read Column
            editField(settings.readColumn, sizeof(settings.readColumn), "Read column");
            break;
        case 4: // Read Date Column
            editField(settings.readDateColumn, sizeof(settings.readDateColumn), "Read date column");
            break;
        case 5: // Favorite Column
            editField(settings.favoriteColumn, sizeof(settings.favoriteColumn), "Favorite column");
            break;
        case 6: // Input Folder
            OpenDirectorySelector("Выберите папку", settings.inputFolder, 
                                sizeof(settings.inputFolder), selectFolderCallback);
            break;
        case 7: // Save
            saveSettings();
            showMainMenu();
            return;
        case 8: // Cancel
            showMainMenu();
            return;
    }
    
    // Refresh menu to show updated values
    showSettingsScreen();
}

void showSettingsScreen() {
    ClearScreen();
    
    // Build menu items
    char ipItem[128], portItem[128], passItem[128];
    char readColItem[128], readDateItem[128], favItem[128], folderItem[300];
    
    snprintf(ipItem, sizeof(ipItem), "IP адрес: %s", settings.ip);
    snprintf(portItem, sizeof(portItem), "Порт: %s", settings.port);
    snprintf(passItem, sizeof(passItem), "Пароль: %s", 
             strlen(settings.password) > 0 ? "••••••" : "(не задан)");
    snprintf(readColItem, sizeof(readColItem), "Read column: %s", settings.readColumn);
    snprintf(readDateItem, sizeof(readDateItem), "Read date column: %s", settings.readDateColumn);
    snprintf(favItem, sizeof(favItem), "Favorite column: %s", settings.favoriteColumn);
    snprintf(folderItem, sizeof(folderItem), "Папка: %s", settings.inputFolder);
    
    iminit(&settingsMenu, sizeof(settingsMenu));
    settingsMenu.font = OpenFont("LiberationSans", 28, 1);
    
    imenuex(&settingsMenu, 1, ipItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 2, portItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 3, passItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 4, readColItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 5, readDateItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 6, favItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 7, folderItem, NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 8, "---", NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 9, "Сохранить", NULL, 0, NULL, NULL);
    imenuex(&settingsMenu, 10, "Отмена", NULL, 0, NULL, NULL);
    
    OpenMenu(&settingsMenu, 0, 0, settingsMenuHandler);
}

// ============================================================================
// MAIN MENU
// ============================================================================

static imenu mainMenu;
static bool isConnected = false;

void mainMenuHandler(int index) {
    switch (index) {
        case 0: // Settings
            showSettingsScreen();
            break;
        case 1: // Sync
            Message(ICON_INFORMATION, "Синхронизация", 
                    "Функция синхронизации в разработке", 2000);
            showMainMenu();
            break;
        case 2: // Exit
            CloseApp();
            break;
        default:
            showMainMenu();
            break;
    }
}

void showMainMenu() {
    ClearScreen();
    
    iminit(&mainMenu, sizeof(mainMenu));
    mainMenu.font = OpenFont("LiberationSans", 32, 1);
    
    char statusStr[256];
    snprintf(statusStr, sizeof(statusStr), "Статус: %s", 
             isConnected ? "Подключено" : "Не подключено");
    
    imenuex(&mainMenu, 1, "Pocketbook Companion", NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 2, statusStr, NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 3, "---", NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 4, "Настройки", NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 5, "Синхронизация", NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 6, "---", NULL, 0, NULL, NULL);
    imenuex(&mainMenu, 7, "Выход", NULL, 0, NULL, NULL);
    
    OpenMenu(&mainMenu, 0, 0, mainMenuHandler);
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

static int mainEventHandler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            loadSettings();
            showMainMenu();
            break;
        case EVT_SHOW:
            break;
        case EVT_EXIT:
            break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    InkViewMain(mainEventHandler);
    return 0;
}
