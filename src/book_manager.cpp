#include "book_manager.h"
#include "inkview.h"
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <cctype>

#define LOG_MSG(fmt, ...) { FILE* f = fopen("/mnt/ext1/system/calibre-connect.log", "a"); if(f) { fprintf(f, "[DB] " fmt "\n", ##__VA_ARGS__); fclose(f); } }

// Helper to format Unix timestamp to ISO 8601 (for Calibre)
static std::string formatIsoTime(time_t timestamp) {
    if (timestamp == 0) return "1970-01-01T00:00:00+00:00";
    char buffer[32];
    struct tm* tm_info = gmtime(&timestamp);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", tm_info);
    return std::string(buffer);
}

BookManager::BookManager() {
    booksDir = "/mnt/ext1"; 
}

BookManager::~BookManager() {
}

bool BookManager::initialize(const std::string& dbPath) {
    return true;
}

sqlite3* BookManager::openDB() {
    sqlite3* db;
    int rc = sqlite3_open_v2(SYSTEM_DB_PATH.c_str(), &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        LOG_MSG("Failed to open DB: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return nullptr;
    }
    sqlite3_busy_timeout(db, 5000);
    return db;
}

void BookManager::closeDB(sqlite3* db) {
    if (db) sqlite3_close(db);
}

// Соответствует функции get_storage_id из pb-db.lua
int BookManager::getStorageId(const std::string& filename) {
    if (filename.find("/mnt/ext1") == 0) return 1;
    return 2;
}

std::string BookManager::getFirstLetter(const std::string& str) {
    if (str.empty()) return "";
    
    // Аналог str:sub(1,1):upper() из lua
    unsigned char first = (unsigned char)str[0];
    
    if (isalnum(first) || ispunct(first)) {
        char upper = toupper(first);
        return std::string(1, upper);
    }
    
    // Аналог str:sub(1,2):upper()
    std::string res = str.substr(0, 2);
    for (char & c : res) c = toupper((unsigned char)c);
    return res;
}

// Соответствует getCurrentProfileId из pb-db.lua
int BookManager::getCurrentProfileId(sqlite3* db) {
    char* profileName = GetCurrentProfile(); // InkView API
    if (!profileName) return 1; // Default profile ID

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM profiles WHERE name = ?";
    int id = 1;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, profileName, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    if (profileName) free(profileName);
    return id;
}

int BookManager::getOrCreateFolder(sqlite3* db, const std::string& folderPath, int storageId) {
    int folderId = -1;
    // Логика 1-в-1 как insert_folder_sql и select_folder_sql в lua
    const char* insertSql = "INSERT INTO folders (storageid, name) VALUES (?, ?) "
                            "ON CONFLICT(storageid, name) DO NOTHING";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, storageId);
        sqlite3_bind_text(stmt, 2, folderPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

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

std::string BookManager::getBookFilePath(const std::string& lpath) {
    if (lpath.empty()) return "";
    std::string cleanLpath = (lpath[0] == '/') ? lpath.substr(1) : lpath;
    if (booksDir.back() == '/') return booksDir + cleanLpath;
    else return booksDir + "/" + cleanLpath;
}

// Реализует логику PocketBookDBHandler:processBookSettings из pb-db.lua
bool BookManager::processBookSettings(sqlite3* db, int bookId, const BookMetadata& metadata, int profileId) {
    // Lua строка 24: if not read_lookup_name ... (проверка наличия настроек)
    // В нашем случае, если статус не "прочитано" и не "избранное", мы ничего не делаем,
    // точно так же, как pb-db.lua не входит в блок `if has_read or has_favorite`.
    
    if (!metadata.isRead && !metadata.isFavorite) {
        // Логика Lua: если нет флагов чтения или избранного, таблица books_settings не обновляется.
        return true; 
    }

    // Lua строки 44-47: 
    // local completed = has_read and 1 or 0
    // local favorite = has_favorite and 1 or 0
    // local cpage_value = has_read and 100 or 0
    // local npage_value = has_read and 100 or 0
    
    int completed = metadata.isRead ? 1 : 0;
    int favorite = metadata.isFavorite ? 1 : 0;
    
    // ВНИМАНИЕ: Это поведение в точности соответствует pb-db.lua.
    // Если книга НЕ прочитана (но, например, избрана), страницы сбрасываются в 0.
    int cpage = metadata.isRead ? 100 : 0;
    int npage = metadata.isRead ? 100 : 0;
    
    // Lua строки 35-42: Парсинг даты
    time_t completedTs = 0; // По умолчанию 0, как в Lua (completed_timestamp or 0)
    if (metadata.isRead && !metadata.lastReadDate.empty()) {
        int y, m, d, H, M, S;
        if (sscanf(metadata.lastReadDate.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &H, &M, &S) >= 6) {
            struct tm tm = {0};
            tm.tm_year = y - 1900;
            tm.tm_mon = m - 1;
            tm.tm_mday = d;
            tm.tm_hour = H;
            tm.tm_min = M;
            tm.tm_sec = S;
            completedTs = timegm(&tm); 
        }
    }
    // В отличие от моего предыдущего кода, Lua НЕ ставит текущее время, если дата пустая.
    // Оставляем 0, если парсинг не удался.

    sqlite3_stmt* stmt;
    
    // Lua строка 51: SELECT bookid FROM books_settings ...
    const char* checkSql = "SELECT bookid FROM books_settings WHERE bookid = ? AND profileid = ?";
    bool exists = false;
    
    if (sqlite3_prepare_v2(db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, bookId);
        sqlite3_bind_int(stmt, 2, profileId);
        if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
        sqlite3_finalize(stmt);
    }

    if (exists) {
        // Lua строка 67: UPDATE books_settings ...
        // Полностью повторяет список полей из скрипта
        const char* updateSql = 
            "UPDATE books_settings "
            "SET completed = ?, favorite = ?, completed_ts = ?, cpage = ?, npage = ? "
            "WHERE bookid = ? AND profileid = ?";
            
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, completed);
            sqlite3_bind_int(stmt, 2, favorite);
            sqlite3_bind_int64(stmt, 3, completedTs);
            sqlite3_bind_int(stmt, 4, cpage);
            sqlite3_bind_int(stmt, 5, npage);
            sqlite3_bind_int(stmt, 6, bookId);
            sqlite3_bind_int(stmt, 7, profileId);
            
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOG_MSG("Error updating settings for book %d", bookId);
            }
            sqlite3_finalize(stmt);
        }
    } else {
        // Lua строка 91: INSERT INTO books_settings ...
        const char* insertSql = 
            "INSERT INTO books_settings (bookid, profileid, completed, favorite, completed_ts, cpage, npage) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)";
            
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_bind_int(stmt, 2, profileId);
            sqlite3_bind_int(stmt, 3, completed);
            sqlite3_bind_int(stmt, 4, favorite);
            sqlite3_bind_int64(stmt, 5, completedTs);
            sqlite3_bind_int(stmt, 6, cpage);
            sqlite3_bind_int(stmt, 7, npage);
            
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOG_MSG("Error creating settings for book %d", bookId);
            }
            sqlite3_finalize(stmt);
        }
    }
    return true;
}

// Реализует saveBookToDatabase из pb-db.lua
bool BookManager::addBook(const BookMetadata& metadata) {
    std::string fullPath = getBookFilePath(metadata.lpath);
    
    // Lua использует lfs.attributes, мы используем stat
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0) {
        LOG_MSG("Error: Failed to get file attributes for %s", fullPath.c_str());
        return false;
    }

    sqlite3* db = openDB();
    if (!db) return false;

    // Парсинг путей как в Lua
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

    int storageId = getStorageId(fullPath);
    // Lua использует os.time()
    time_t now = time(NULL);

    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    // Работа с папками (полный аналог lua)
    int folderId = getOrCreateFolder(db, folderName, storageId);
    if (folderId == -1) {
        LOG_MSG("Error: Failed to get folder ID");
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        closeDB(db);
        return false;
    }

    // Проверка существования файла (Lua: check_file_sql)
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

    std::string sortAuthor = metadata.authorSort.empty() ? metadata.authors : metadata.authorSort;
    std::string firstAuthorLetter = getFirstLetter(sortAuthor);
    std::string firstTitleLetter = getFirstLetter(metadata.title);

    if (fileId != -1) {
        // Lua: File exists, update it
        // Lua строка 212: UPDATE files ...
        const char* updateFileSql = "UPDATE files SET size = ?, modification_time = ? WHERE id = ?";
        if (sqlite3_prepare_v2(db, updateFileSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, (long long)st.st_size);
            sqlite3_bind_int64(stmt, 2, (long long)st.st_mtime);
            sqlite3_bind_int(stmt, 3, fileId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Lua строка 230: UPDATE books_impl ...
        const char* updateBookSql = 
            "UPDATE books_impl SET title=?, first_title_letter=?, author=?, firstauthor=?, "
            "first_author_letter=?, series=?, numinseries=?, size=?, isbn=?, sort_title=?, "
            "updated=?, ts_added=? WHERE id=?";
            
        if (sqlite3_prepare_v2(db, updateBookSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, firstTitleLetter.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, metadata.authors.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, sortAuthor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, firstAuthorLetter.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, metadata.series.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, metadata.seriesIndex);
            sqlite3_bind_int64(stmt, 8, metadata.size);
            sqlite3_bind_text(stmt, 9, metadata.isbn.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 11, now);
            sqlite3_bind_int64(stmt, 12, now); // Lua также обновляет ts_added при апдейте
            sqlite3_bind_int(stmt, 13, bookId);
            
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        // Lua: File doesn't exist
        // Lua строка 272: INSERT INTO books_impl ...
        const char* insertBookSql = 
            "INSERT INTO books_impl (title, first_title_letter, author, firstauthor, "
            "first_author_letter, series, numinseries, size, isbn, sort_title, creationtime, "
            "updated, ts_added, hidden) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            
        if (sqlite3_prepare_v2(db, insertBookSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, firstTitleLetter.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, metadata.authors.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, sortAuthor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, firstAuthorLetter.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, metadata.series.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, metadata.seriesIndex);
            sqlite3_bind_int64(stmt, 8, metadata.size);
            sqlite3_bind_text(stmt, 9, metadata.isbn.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 10, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 11, 0);   // creationtime 0
            sqlite3_bind_int(stmt, 12, 0);   // updated 0
            sqlite3_bind_int64(stmt, 13, now); // ts_added current
            sqlite3_bind_int(stmt, 14, 0);   // hidden 0
            
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                bookId = sqlite3_last_insert_rowid(db);
            }
            sqlite3_finalize(stmt);
        }

        if (bookId != -1) {
            // Lua строка 305: INSERT INTO files ...
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

    if (bookId != -1) {
        int profileId = getCurrentProfileId(db);
        processBookSettings(db, bookId, metadata, profileId);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL)", NULL, NULL, NULL);
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL); // Добавлено, как в Lua
    closeDB(db);
    return true;
}

// Этот метод остается, но теперь он просто обертка
bool BookManager::updateBookSync(const BookMetadata& metadata) {
    sqlite3* db = openDB();
    if (!db) return false;

    int bookId = findBookIdByPath(db, metadata.lpath);
    
    if (bookId == -1) {
        LOG_MSG("Sync: Book not found in DB: %s", metadata.lpath.c_str());
        closeDB(db);
        return false;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int profileId = getCurrentProfileId(db);
    bool res = processBookSettings(db, bookId, metadata, profileId);

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL)", NULL, NULL, NULL);
    
    closeDB(db);
    return res;
}

bool BookManager::updateBook(const BookMetadata& metadata) {
    return addBook(metadata);
}

bool BookManager::deleteBook(const std::string& lpath) {
    std::string filePath = getBookFilePath(lpath);
    LOG_MSG("Deleting book: %s", filePath.c_str());
    
    remove(filePath.c_str());

    sqlite3* db = openDB();
    if (!db) return false;

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
        const char* sql1 = "DELETE FROM files WHERE id = ?";
        if (sqlite3_prepare_v2(db, sql1, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, fileId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        const char* sql2 = "DELETE FROM books_settings WHERE bookid = ?";
        if (sqlite3_prepare_v2(db, sql2, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        const char* sql3 = "DELETE FROM books_impl WHERE id = ?";
        if (sqlite3_prepare_v2(db, sql3, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
    
    closeDB(db);
    return true;
}

std::vector<BookMetadata> BookManager::getAllBooks() {
    std::vector<BookMetadata> books;
    sqlite3* db = openDB();
    if (!db) return books;

    int profileId = getCurrentProfileId(db);
    
    std::string sql = 
        "SELECT b.id, b.title, b.author, b.series, b.numinseries, b.size, b.updated, "
        "f.filename, fo.name, bs.completed, bs.favorite, bs.completed_ts "
        "FROM books_impl b "
        "JOIN files f ON b.id = f.book_id "
        "JOIN folders fo ON f.folder_id = fo.id "
        "LEFT JOIN books_settings bs ON b.id = bs.bookid AND bs.profileid = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, profileId);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BookMetadata meta;
            meta.dbBookId = sqlite3_column_int(stmt, 0);
            
            const char* title = (const char*)sqlite3_column_text(stmt, 1);
            const char* author = (const char*)sqlite3_column_text(stmt, 2);
            const char* series = (const char*)sqlite3_column_text(stmt, 3);
            
            meta.title = title ? title : "";
            meta.authors = author ? author : "";
            meta.series = series ? series : "";
            meta.seriesIndex = sqlite3_column_int(stmt, 4);
            meta.size = sqlite3_column_int64(stmt, 5);
            
            const char* filename = (const char*)sqlite3_column_text(stmt, 7);
            const char* folder = (const char*)sqlite3_column_text(stmt, 8);
            
            std::string fullFolder = folder ? folder : "";
            std::string fName = filename ? filename : "";
            std::string fullPath = fullFolder + "/" + fName;
            
            if (fullPath.find(booksDir) == 0) {
                meta.lpath = fullPath.substr(booksDir.length());
                if (!meta.lpath.empty() && meta.lpath[0] == '/') {
                    meta.lpath = meta.lpath.substr(1);
                }
            } else {
                meta.lpath = fName;
            }
            
            meta.isRead = (sqlite3_column_int(stmt, 9) != 0);
            meta.isFavorite = (sqlite3_column_int(stmt, 10) != 0);
            
            time_t readTs = (time_t)sqlite3_column_int64(stmt, 11);
            if (meta.isRead && readTs > 0) {
                meta.lastReadDate = formatIsoTime(readTs);
            }
            
            time_t updated = (time_t)sqlite3_column_int64(stmt, 6);
            meta.lastModified = formatIsoTime(updated);

            meta.uuid = ""; 

            books.push_back(meta);
        }
        sqlite3_finalize(stmt);
    }
    
    closeDB(db);
    return books;
}

int BookManager::getBookCount() {
    return getAllBooks().size();
}

int BookManager::findBookIdByPath(sqlite3* db, const std::string& lpath) {
    std::string fullPath = getBookFilePath(lpath);
    std::string folderName, fileName;
    
    size_t lastSlash = fullPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        folderName = "";
        fileName = fullPath;
    } else {
        folderName = fullPath.substr(0, lastSlash);
        fileName = fullPath.substr(lastSlash + 1);
    }
    
    const char* sql = "SELECT f.book_id FROM files f JOIN folders fo ON f.folder_id = fo.id WHERE f.filename = ? AND fo.name = ?";
    sqlite3_stmt* stmt;
    int bookId = -1;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, folderName.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            bookId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return bookId;
}

int BookManager::getOrCreateBookshelf(sqlite3* db, const std::string& name) {
    int shelfId = -1;
    time_t now = time(NULL);
    
    const char* findSql = "SELECT id FROM bookshelfs WHERE name = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, findSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            shelfId = sqlite3_column_int(stmt, 0);
            const char* restoreSql = "UPDATE bookshelfs SET is_deleted = 0, ts = ? WHERE id = ?";
            sqlite3_stmt* stmt2;
            if (sqlite3_prepare_v2(db, restoreSql, -1, &stmt2, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt2, 1, now);
                sqlite3_bind_int(stmt2, 2, shelfId);
                sqlite3_step(stmt2);
                sqlite3_finalize(stmt2);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    if (shelfId == -1) {
        const char* insertSql = "INSERT INTO bookshelfs (name, is_deleted, ts) VALUES (?, 0, ?)";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, now);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                shelfId = sqlite3_last_insert_rowid(db);
            }
            sqlite3_finalize(stmt);
        }
    }
    return shelfId;
}

void BookManager::linkBookToShelf(sqlite3* db, int shelfId, int bookId) {
    time_t now = time(NULL);
    
    const char* checkSql = "SELECT 1 FROM bookshelfs_books WHERE bookshelfid = ? AND bookid = ?";
    bool exists = false;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, shelfId);
        sqlite3_bind_int(stmt, 2, bookId);
        if (sqlite3_step(stmt) == SQLITE_ROW) exists = true;
        sqlite3_finalize(stmt);
    }
    
    if (exists) {
        const char* updateSql = "UPDATE bookshelfs_books SET is_deleted = 0, ts = ? WHERE bookshelfid = ? AND bookid = ?";
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_int(stmt, 2, shelfId);
            sqlite3_bind_int(stmt, 3, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        const char* insertSql = "INSERT INTO bookshelfs_books (bookshelfid, bookid, ts, is_deleted) VALUES (?, ?, ?, 0)";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, shelfId);
            sqlite3_bind_int(stmt, 2, bookId);
            sqlite3_bind_int64(stmt, 3, now);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void BookManager::updateCollections(const std::map<std::string, std::vector<std::string>>& collections) {
    sqlite3* db = openDB();
    if (!db) return;
    
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (auto const& entry : collections) {
        const std::string& colName = entry.first;
        const std::vector<std::string>& lpaths = entry.second;
        
        LOG_MSG("Processing collection: %s with %d books", colName.c_str(), (int)lpaths.size());
        
        int shelfId = getOrCreateBookshelf(db, colName);
        if (shelfId == -1) continue;
        
        for (const std::string& lpath : lpaths) {
            int bookId = findBookIdByPath(db, lpath);
            if (bookId != -1) {
                linkBookToShelf(db, shelfId, bookId);
            } else {
                LOG_MSG("Book not found for collection: %s", lpath.c_str());
            }
        }
    }
    
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    closeDB(db);
}
