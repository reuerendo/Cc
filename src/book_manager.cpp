#include "book_manager.h"
#include "inkview.h"
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

// Макрос для логирования (можно заменить на ваш механизм)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[BookManager] Error: " fmt "\n", ##__VA_ARGS__)

BookManager::BookManager() {
    booksDir = "/mnt/ext1"; // Корневая папка для путей, так как DB хранит полные пути
}

BookManager::~BookManager() {
    // БД открывается и закрывается по требованию, как в Lua скрипте
}

bool BookManager::initialize(const std::string& dbPath) {
    // Проверяем доступность системной БД
    FILE* f = fopen(SYSTEM_DB_PATH.c_str(), "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

sqlite3* BookManager::openDB() {
    sqlite3* db;
    int rc = sqlite3_open_v2(SYSTEM_DB_PATH.c_str(), &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERR("Failed to open DB: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return nullptr;
    }
    // Аналог: db:exec("PRAGMA busy_timeout = 5000;")
    sqlite3_busy_timeout(db, 5000);
    return db;
}

void BookManager::closeDB(sqlite3* db) {
    if (db) sqlite3_close(db);
}

// Аналог get_storage_id из pb-db.lua
int BookManager::getStorageId(const std::string& filename) {
    if (filename.find("/mnt/ext1") == 0) {
        return 1;
    }
    return 2;
}

// Аналог getFirstLetter
std::string BookManager::getFirstLetter(const std::string& str) {
    if (str.empty()) return "";
    char first = toupper(str[0]);
    if (isalnum(first)) {
        return std::string(1, first);
    }
    if (str.length() > 1) {
        return std::string(1, toupper(str[1]));
    }
    return "";
}

// Аналог getCurrentProfileId
int BookManager::getCurrentProfileId(sqlite3* db) {
    char* profileName = GetCurrentProfile(); // InkView API
    if (!profileName) return 1; // Default profile

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM profiles WHERE name = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 1;

    sqlite3_bind_text(stmt, 1, profileName, -1, SQLITE_STATIC);
    
    int id = 1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (profileName) free(profileName); // InkView обычно требует освобождения
    return id;
}

// Получение или создание ID папки (логика из saveBookToDatabase для folders)
int BookManager::getOrCreateFolder(sqlite3* db, const std::string& folderPath, int storageId) {
    int folderId = -1;
    
    // INSERT INTO folders ... ON CONFLICT DO NOTHING
    const char* insertSql = "INSERT INTO folders (storageid, name) VALUES (?, ?) "
                            "ON CONFLICT(storageid, name) DO NOTHING";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, storageId);
        sqlite3_bind_text(stmt, 2, folderPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // SELECT id FROM folders ...
    const char* selectSql = "SELECT id FROM folders WHERE storageid = ? AND name = ?";
    if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, storageId);
        sqlite3_bind_text(stmt, 2, folderPath.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            folderId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    return folderId;
}

// Реализация addBook / updateBook на основе saveBookToDatabase из pb-db.lua
bool BookManager::addBook(const BookMetadata& metadata) {
    // Получаем полный путь к файлу
    std::string fullPath = getBookFilePath(metadata.lpath);
    
    // Получаем статистику файла (размер, время) - аналог lfs.attributes
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0) {
        LOG_ERR("File not found on disk: %s", fullPath.c_str());
        return false;
    }

    sqlite3* db = openDB();
    if (!db) return false;

    // Разбор пути: папка и имя файла
    std::string folderName, fileName;
    size_t lastSlash = fullPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        folderName = "";
        fileName = fullPath;
    } else {
        folderName = fullPath.substr(0, lastSlash);
        fileName = fullPath.substr(lastSlash + 1);
    }
    
    std::string fileExt = "";
    size_t lastDot = fileName.find_last_of('.');
    if (lastDot != std::string::npos) fileExt = fileName.substr(lastDot + 1);

    int storageId = getStorageId(fullPath); // logic

    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int folderId = getOrCreateFolder(db, folderName, storageId);
    if (folderId == -1) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        closeDB(db);
        return false;
    }

    // Проверяем, существует ли файл
    const char* checkFileSql = "SELECT id, book_id FROM files WHERE filename = ? AND folder_id = ?";
    sqlite3_stmt* stmt;
    int fileId = -1;
    int bookId = -1;
    
    if (sqlite3_prepare_v2(db, checkFileSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, folderId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            fileId = sqlite3_column_int(stmt, 0);
            bookId = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    time_t now = time(NULL);

    if (fileId != -1) {
        // --- UPDATE EXISTING BOOK ---
        
        // Update files table
        const char* updateFileSql = "UPDATE files SET size = ?, modification_time = ? WHERE id = ?";
        if (sqlite3_prepare_v2(db, updateFileSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, (long long)st.st_size);
            sqlite3_bind_int64(stmt, 2, (long long)st.st_mtime);
            sqlite3_bind_int(stmt, 3, fileId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Update books_impl table
        const char* updateBookSql = 
            "UPDATE books_impl SET title=?, first_title_letter=?, author=?, firstauthor=?, "
            "first_author_letter=?, series=?, numinseries=?, size=?, isbn=?, sort_title=?, "
            "updated=?, ts_added=? WHERE id=?";
            
        if (sqlite3_prepare_v2(db, updateBookSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, getFirstLetter(metadata.title).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, metadata.authors.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, metadata.authors.c_str(), -1, SQLITE_TRANSIENT); // Simplified
            sqlite3_bind_text(stmt, 5, getFirstLetter(metadata.authors).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, metadata.series.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, metadata.seriesIndex);
            sqlite3_bind_int64(stmt, 8, metadata.size);
            sqlite3_bind_text(stmt, 9, metadata.isbn.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 11, now);
            sqlite3_bind_int64(stmt, 12, now);
            sqlite3_bind_int(stmt, 13, bookId);
            
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

    } else {
        // --- INSERT NEW BOOK ---
        
        const char* insertBookSql = 
            "INSERT INTO books_impl (title, first_title_letter, author, firstauthor, "
            "first_author_letter, series, numinseries, size, isbn, sort_title, creationtime, "
            "updated, ts_added, hidden) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            
        if (sqlite3_prepare_v2(db, insertBookSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, getFirstLetter(metadata.title).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, metadata.authors.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, metadata.authors.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, getFirstLetter(metadata.authors).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, metadata.series.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, metadata.seriesIndex);
            sqlite3_bind_int64(stmt, 8, metadata.size);
            sqlite3_bind_text(stmt, 9, metadata.isbn.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 11, 0);
            sqlite3_bind_int(stmt, 12, 0);
            sqlite3_bind_int64(stmt, 13, now);
            sqlite3_bind_int(stmt, 14, 0);
            
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                bookId = sqlite3_last_insert_rowid(db);
            }
            sqlite3_finalize(stmt);
        }

        if (bookId != -1) {
            const char* insertFileSql = 
                "INSERT INTO files (storageid, folder_id, book_id, filename, size, modification_time, ext) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)";
                
            if (sqlite3_prepare_v2(db, insertFileSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, storageId);
                sqlite3_bind_int(stmt, 2, folderId);
                sqlite3_bind_int(stmt, 3, bookId);
                sqlite3_bind_text(stmt, 4, fileName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 5, (long long)st.st_size);
                sqlite3_bind_int64(stmt, 6, (long long)st.st_mtime);
                sqlite3_bind_text(stmt, 7, fileExt.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Update settings (Read status/Favorites)
    if (bookId != -1) {
        int profileId = getCurrentProfileId(db);
        processBookSettings(db, bookId, metadata, profileId);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL)", NULL, NULL, NULL);
    
    closeDB(db);
    return true;
}

// Реализация processBookSettings из pb-db.lua
bool BookManager::processBookSettings(sqlite3* db, int bookId, const BookMetadata& metadata, int profileId) {
    if (!metadata.isRead && !metadata.isFavorite) return true;

    int completed = metadata.isRead ? 1 : 0;
    int favorite = metadata.isFavorite ? 1 : 0;
    int cpage = metadata.isRead ? 100 : 0; // Заглушка, как в Lua
    
    // Парсинг времени прочтения из ISO строки, если нужно, опустим для краткости, 
    // используем текущее или 0
    time_t completedTs = metadata.isRead ? time(NULL) : 0;

    // Check if settings exist
    sqlite3_stmt* stmt;
    const char* checkSql = "SELECT bookid FROM books_settings WHERE bookid = ? AND profileid = ?";
    bool exists = false;
    
    if (sqlite3_prepare_v2(db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, bookId);
        sqlite3_bind_int(stmt, 2, profileId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
        }
        sqlite3_finalize(stmt);
    }

    if (exists) {
        const char* updateSql = "UPDATE books_settings SET completed=?, favorite=?, completed_ts=?, cpage=? WHERE bookid=? AND profileid=?";
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, completed);
            sqlite3_bind_int(stmt, 2, favorite);
            sqlite3_bind_int64(stmt, 3, completedTs);
            sqlite3_bind_int(stmt, 4, cpage);
            sqlite3_bind_int(stmt, 5, bookId);
            sqlite3_bind_int(stmt, 6, profileId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        const char* insertSql = "INSERT INTO books_settings (bookid, profileid, completed, favorite, completed_ts, cpage) VALUES (?, ?, ?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_bind_int(stmt, 2, profileId);
            sqlite3_bind_int(stmt, 3, completed);
            sqlite3_bind_int(stmt, 4, favorite);
            sqlite3_bind_int64(stmt, 5, completedTs);
            sqlite3_bind_int(stmt, 6, cpage);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return true;
}

bool BookManager::updateBook(const BookMetadata& metadata) {
    return addBook(metadata); // Логика Insert/Update уже внутри addBook
}

std::string BookManager::getBookFilePath(const std::string& lpath) {
    // booksDir уже содержит /mnt/ext1 (без папки Calibre, если вы ее убрали)
    if (booksDir.back() == '/' && lpath.front() == '/') {
        return booksDir + lpath.substr(1);
    } else if (booksDir.back() != '/' && lpath.front() != '/') {
        return booksDir + "/" + lpath;
    }
    return booksDir + lpath;
}

// Остальные методы (getAllBooks, getBookCount и т.д.) 
// нужно адаптировать для чтения из explorer-3.db по аналогии,
// но для приема книг они менее критичны.
// Для корректной работы кэша их нужно реализовать через SELECT из books_impl.

std::vector<BookMetadata> BookManager::getAllBooks() {
    // TODO: Реализовать SELECT uuid, title... FROM books_impl JOIN files ...
    // для заполнения кэша при подключении.
    // Для простоты пока можно возвращать пустой вектор, тогда Calibre 
    // будет думать, что книг нет, и пытаться их отправить (или обновить, если UUID совпадет).
    return std::vector<BookMetadata>();
}

int BookManager::getBookCount() {
    // Аналогично getAllBooks
    return 0;
}

bool BookManager::hasMetadataChanged(const BookMetadata& metadata) {
    // Всегда true, чтобы обновить данные в системной БД
    return true; 
}

void BookManager::updateMetadataCache(const BookMetadata& metadata) {
    // Пусто, так как мы пишем сразу в системную БД
}

void BookManager::updateCollections(const std::map<std::string, std::vector<std::string>>& collections) {
    // Реализация syncCollectionsIncremental из pb-db.lua потребует работы с таблицами
    // bookshelfs и bookshelfs_books.
    // Это объемная задача, лучше убедиться, что базовая передача работает.
}

bool BookManager::deleteBook(const std::string& lpath) {
    // 1. Удаляем файл с диска
    std::string filePath = getBookFilePath(lpath);
    if (remove(filePath.c_str()) != 0) {
        // Если файла нет, возможно, он уже удален, продолжаем чистку БД
        LOG_ERR("Failed to remove file: %s", filePath.c_str());
    }

    // 2. Чистим базу данных
    sqlite3* db = openDB();
    if (!db) return false;

    // Разбираем путь для поиска в БД
    std::string folderName, fileName;
    size_t lastSlash = filePath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        folderName = "";
        fileName = filePath;
    } else {
        folderName = filePath.substr(0, lastSlash);
        fileName = filePath.substr(lastSlash + 1);
    }
    
    int storageId = getStorageId(filePath);

    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    // Находим ID файла и книги
    const char* findSql = 
        "SELECT f.id, f.book_id FROM files f "
        "JOIN folders fo ON f.folder_id = fo.id "
        "WHERE f.filename = ? AND fo.name = ? AND f.storageid = ?";
        
    sqlite3_stmt* stmt;
    int fileId = -1;
    int bookId = -1;

    if (sqlite3_prepare_v2(db, findSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, folderName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, storageId);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            fileId = sqlite3_column_int(stmt, 0);
            bookId = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    if (fileId != -1) {
        // Удаляем из files
        const char* delFileSql = "DELETE FROM files WHERE id = ?";
        if (sqlite3_prepare_v2(db, delFileSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, fileId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Удаляем из books_settings
        const char* delSettingsSql = "DELETE FROM books_settings WHERE bookid = ?";
        if (sqlite3_prepare_v2(db, delSettingsSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Удаляем из books_impl
        const char* delBookSql = "DELETE FROM books_impl WHERE id = ?";
        if (sqlite3_prepare_v2(db, delBookSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    // Очистка места (аналог VACUUM в pb-db.lua)
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
    
    closeDB(db);
    return true;
}