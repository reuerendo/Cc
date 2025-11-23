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
#include <regex>

#define LOG_MSG(fmt, ...) { FILE* f = fopen("/mnt/ext1/system/calibre-connect.log", "a"); if(f) { fprintf(f, "[DB] " fmt "\n", ##__VA_ARGS__); fclose(f); } }

static std::string formatIsoTime(time_t timestamp) {
    if (timestamp == 0) return "1970-01-01T00:00:00+00:00";
    char buffer[32];
    struct tm* tm_info = gmtime(&timestamp);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", tm_info);
    return std::string(buffer);
}

static time_t parseIsoTime(const std::string& isoTime) {
    if (isoTime.empty()) return 0;
    int y, m, d, H, M, S;
    if (sscanf(isoTime.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &H, &M, &S) >= 6) {
        struct tm tm = {0};
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = H;
        tm.tm_min = M;
        tm.tm_sec = S;
        return timegm(&tm);
    }
    return 0;
}

BookManager::BookManager() {
    booksDir = "/mnt/ext1"; 
}

BookManager::~BookManager() {
}

bool BookManager::initialize(const std::string& dbPath) {
    return true;
}

std::string BookManager::getBookFilePath(const std::string& lpath) {
    if (lpath.empty()) return "";
    std::string cleanLpath = (lpath[0] == '/') ? lpath.substr(1) : lpath;
    if (!booksDir.empty() && booksDir.back() == '/') {
        return booksDir + cleanLpath;
    } else {
        return booksDir + "/" + cleanLpath;
    }
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

int BookManager::getStorageId(const std::string& filename) {
    if (filename.find("/mnt/ext1") == 0) return 1;
    return 2;
}

std::string BookManager::getFirstLetter(const std::string& str) {
    if (str.empty()) return "";
    unsigned char first = (unsigned char)str[0];
    if (isalnum(first) || ispunct(first)) {
        char upper = toupper(first);
        return std::string(1, upper);
    }
    std::string res = str.substr(0, 2);
    for (char & c : res) c = toupper((unsigned char)c);
    return res;
}

int BookManager::getCurrentProfileId(sqlite3* db) {
    char* profileName = GetCurrentProfile(); 
    if (!profileName) return 1; 

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

bool BookManager::processBookSettings(sqlite3* db, int bookId, const BookMetadata& metadata, int profileId) {
    if (!metadata.isReadDefined && !metadata.isFavoriteDefined) {
        return true; 
    }

    int newCompleted = -1; 
    int newFavorite = -1;
    time_t newCompletedTs = 0;
    
    int oldCompleted = 0;
    int oldFavorite = 0;
    time_t oldCompletedTs = 0;
    bool exists = false;

    sqlite3_stmt* stmt;
    const char* checkSql = "SELECT completed, favorite, completed_ts FROM books_settings WHERE bookid = ? AND profileid = ?";
    
    if (sqlite3_prepare_v2(db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, bookId);
        sqlite3_bind_int(stmt, 2, profileId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
            oldCompleted = sqlite3_column_int(stmt, 0);
            oldFavorite = sqlite3_column_int(stmt, 1);
            oldCompletedTs = (time_t)sqlite3_column_int64(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    if (metadata.isReadDefined) {
        newCompleted = metadata.isRead ? 1 : 0;
    } else {
        newCompleted = oldCompleted;
    }

    if (metadata.isFavoriteDefined) {
        newFavorite = metadata.isFavorite ? 1 : 0;
    } else {
        newFavorite = oldFavorite;
    }

    if (newCompleted == 1) {
        if (!metadata.lastReadDate.empty()) {
            newCompletedTs = parseIsoTime(metadata.lastReadDate);
        } else if (oldCompletedTs > 0) {
            newCompletedTs = oldCompletedTs;
        } else {
            newCompletedTs = time(NULL);
        }
    } else {
        newCompletedTs = 0;
    }

    int cpage = (newCompleted == 1) ? 100 : 0;
    int npage = (newCompleted == 1) ? 100 : 0;

    LOG_MSG("Syncing Book %d: Read=%d, Fav=%d, Date=%ld", bookId, newCompleted, newFavorite, newCompletedTs);

    if (exists) {
        const char* updateSql = 
            "UPDATE books_settings "
            "SET completed = ?, favorite = ?, completed_ts = ?, cpage = ?, npage = ? "
            "WHERE bookid = ? AND profileid = ?";
            
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newCompleted);
            sqlite3_bind_int(stmt, 2, newFavorite);
            sqlite3_bind_int64(stmt, 3, newCompletedTs);
            sqlite3_bind_int(stmt, 4, cpage);
            sqlite3_bind_int(stmt, 5, npage);
            sqlite3_bind_int(stmt, 6, bookId);
            sqlite3_bind_int(stmt, 7, profileId);
            
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        const char* insertSql = 
            "INSERT INTO books_settings (bookid, profileid, completed, favorite, completed_ts, cpage, npage) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)";
            
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookId);
            sqlite3_bind_int(stmt, 2, profileId);
            sqlite3_bind_int(stmt, 3, newCompleted);
            sqlite3_bind_int(stmt, 4, newFavorite);
            sqlite3_bind_int64(stmt, 5, newCompletedTs);
            sqlite3_bind_int(stmt, 6, cpage);
            sqlite3_bind_int(stmt, 7, npage);
            
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return true;
}

bool BookManager::addBook(const BookMetadata& metadata) {
    std::string fullPath = getBookFilePath(metadata.lpath);
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0) return false;

    sqlite3* db = openDB();
    if (!db) return false;

    int bookId = findBookIdByPath(db, metadata.lpath); 
    if (bookId != -1) {
        int profileId = getCurrentProfileId(db);
        processBookSettings(db, bookId, metadata, profileId);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL)", NULL, NULL, NULL);
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
    closeDB(db);
    return true;
}

bool BookManager::updateBookSync(const BookMetadata& metadata) {
    sqlite3* db = openDB();
    if (!db) return false;

    int bookId = findBookIdByPath(db, metadata.lpath);
    if (bookId == -1) {
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
    
    const char* sql = 
        "SELECT f.book_id FROM files f "
        "JOIN folders fo ON f.folder_id = fo.id "
        "WHERE f.filename = ? AND fo.name = ?";
        
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
        const char* insertSql = "INSERT INTO bookshelfs (name, is_deleted, ts, uuid) VALUES (?, 0, ?, NULL)";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, now);
            if (sqlite3_step(stmt) == SQLITE_DONE) shelfId = sqlite3_last_insert_rowid(db);
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

static std::string cleanCollectionName(const std::string& rawName) {
    static const std::regex re(R"((.*)\s+\(.*\)$)"); 
    std::smatch match;
    if (std::regex_match(rawName, match, re)) {
        if (match.size() > 1) {
            return match[1].str();
        }
    }
    return rawName;
}

void BookManager::updateCollections(const std::map<std::string, std::vector<std::string>>& collections) {
    sqlite3* db = openDB();
    if (!db) return;
    
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
    
    for (auto const& entry : collections) {
        std::string rawName = entry.first;
        std::string cleanName = cleanCollectionName(rawName);
        
        LOG_MSG("Syncing Collection: '%s' -> '%s'", rawName.c_str(), cleanName.c_str());
        
        int shelfId = getOrCreateBookshelf(db, cleanName);
        if (shelfId == -1) continue;
        
        for (const std::string& lpath : entry.second) {
            int bookId = findBookIdByPath(db, lpath);
            if (bookId != -1) {
                linkBookToShelf(db, shelfId, bookId);
            } else {
                 LOG_MSG("Book not found for collection: %s", lpath.c_str());
            }
        }
    }
    
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL)", NULL, NULL, NULL);
    sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
    closeDB(db);
}

std::vector<BookMetadata> BookManager::getAllBooks() {
    std::vector<BookMetadata> books;
    sqlite3* db = openDB();
    if (!db) return books;

    int profileId = getCurrentProfileId(db);
    
    std::string sql = 
        "SELECT b.id, b.title, b.author, b.series, b.numinseries, b.size, b.updated, "
        "f.filename, fo.name, bs.completed, bs.favorite, bs.completed_ts, f.modification_time, "
        "bs.cpage, bs.npage "
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
            meta.title = title ? title : "";
            meta.authors = author ? author : "";
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
            
            int completed = sqlite3_column_int(stmt, 9);
            meta.isRead = (completed != 0);
            meta.isReadDefined = true; 
            
            meta.isFavorite = (sqlite3_column_int(stmt, 10) != 0);
            meta.isFavoriteDefined = true;
            
            time_t readTs = (time_t)sqlite3_column_int64(stmt, 11);
            if (meta.isRead && readTs > 0) {
                meta.lastReadDate = formatIsoTime(readTs);
            }
            
            time_t fileModTime = (time_t)sqlite3_column_int64(stmt, 12);
            if (fileModTime > 0) {
                meta.lastModified = formatIsoTime(fileModTime);
            } else {
                time_t updated = (time_t)sqlite3_column_int64(stmt, 6);
                meta.lastModified = formatIsoTime(updated);
            }

            books.push_back(meta);
        }
        sqlite3_finalize(stmt);
    }
    
    closeDB(db);
    return books;
}

bool BookManager::updateBook(const BookMetadata& metadata) {
    return addBook(metadata);
}

bool BookManager::deleteBook(const std::string& lpath) {
    return true;
}

int BookManager::getBookCount() {
    return getAllBooks().size();
}

// Add these implementations to book_manager.cpp

bool BookManager::getBookMetadata(const std::string& lpath, BookMetadata& outMetadata) {
    sqlite3_stmt* stmt;
    
    const char* sql = R"(
        SELECT b.id, f.filename, fo.name, b.title, b.author, b.series, 
               b.numinseries, b.size, b.updated,
               bs.completed, bs.completed_ts, bs.favorite
        FROM books_impl b
        JOIN files f ON b.id = f.book_id
        JOIN folders fo ON f.folder_id = fo.id
        LEFT JOIN books_settings bs ON b.id = bs.bookid AND bs.profileid = ?
        WHERE fo.name || '/' || f.filename = ?
    )";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    int profileId = getCurrentProfileId();
    sqlite3_bind_int(stmt, 1, profileId);
    sqlite3_bind_text(stmt, 2, lpath.c_str(), -1, SQLITE_STATIC);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        outMetadata.lpath = lpath;
        outMetadata.title = getString(stmt, 3);
        outMetadata.authors = getString(stmt, 4);
        outMetadata.series = getString(stmt, 5);
        outMetadata.seriesIndex = sqlite3_column_int(stmt, 6);
        outMetadata.size = sqlite3_column_int64(stmt, 7);
        
        // Format last_modified as ISO timestamp
        time_t updateTime = sqlite3_column_int64(stmt, 8);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S+00:00", gmtime(&updateTime));
        outMetadata.lastModified = timeBuf;
        
        // Read status
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
            outMetadata.isRead = sqlite3_column_int(stmt, 9) == 1;
        }
        
        // Read date
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
            time_t readTime = sqlite3_column_int64(stmt, 10);
            if (readTime > 0) {
                char readBuf[32];
                strftime(readBuf, sizeof(readBuf), "%Y-%m-%dT%H:%M:%S+00:00", gmtime(&readTime));
                outMetadata.lastReadDate = readBuf;
            }
        }
        
        // Favorite status
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL) {
            outMetadata.isFavorite = sqlite3_column_int(stmt, 11) == 1;
        }
        
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}

std::map<std::string, std::set<std::string>> BookManager::getCollections() {
    std::map<std::string, std::set<std::string>> collections;
    sqlite3_stmt* stmt;
    
    const char* sql = R"(
        SELECT bs.name, fo.name || '/' || f.filename as lpath
        FROM bookshelfs bs
        JOIN bookshelfs_books bb ON bs.id = bb.bookshelfid
        JOIN books_impl b ON bb.bookid = b.id
        JOIN files f ON b.id = f.book_id
        JOIN folders fo ON f.folder_id = fo.id
        WHERE bs.is_deleted = 0 AND bb.is_deleted = 0
    )";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return collections;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string collName = getString(stmt, 0);
        std::string lpath = getString(stmt, 1);
        collections[collName].insert(lpath);
    }
    
    sqlite3_finalize(stmt);
    return collections;
}

void BookManager::removeFromCollection(const std::string& lpath, const std::string& collectionName) {
    sqlite3_stmt* stmt;
    
    // Find book_id and bookshelf_id
    const char* findSql = R"(
        SELECT b.id, bs.id
        FROM books_impl b
        JOIN files f ON b.id = f.book_id
        JOIN folders fo ON f.folder_id = fo.id
        CROSS JOIN bookshelfs bs
        WHERE fo.name || '/' || f.filename = ?
          AND bs.name = ?
          AND bs.is_deleted = 0
    )";
    
    if (sqlite3_prepare_v2(db, findSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, lpath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, collectionName.c_str(), -1, SQLITE_STATIC);
    
    int bookId = -1;
    int bookshelfId = -1;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        bookId = sqlite3_column_int(stmt, 0);
        bookshelfId = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    
    if (bookId < 0 || bookshelfId < 0) return;
    
    // Mark link as deleted
    const char* updateSql = R"(
        UPDATE bookshelfs_books 
        SET is_deleted = 1, ts = ?
        WHERE bookshelfid = ? AND bookid = ?
    )";
    
    if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    time_t now = time(nullptr);
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int(stmt, 2, bookshelfId);
    sqlite3_bind_int(stmt, 3, bookId);
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void BookManager::addToCollection(const std::string& lpath, const std::string& collectionName) {
    sqlite3_stmt* stmt;
    
    // Find book_id
    const char* findBookSql = R"(
        SELECT b.id
        FROM books_impl b
        JOIN files f ON b.id = f.book_id
        JOIN folders fo ON f.folder_id = fo.id
        WHERE fo.name || '/' || f.filename = ?
    )";
    
    if (sqlite3_prepare_v2(db, findBookSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, lpath.c_str(), -1, SQLITE_STATIC);
    
    int bookId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        bookId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    if (bookId < 0) return;
    
    // Get or create bookshelf
    const char* findShelfSql = "SELECT id FROM bookshelfs WHERE name = ?";
    if (sqlite3_prepare_v2(db, findShelfSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, collectionName.c_str(), -1, SQLITE_STATIC);
    
    int bookshelfId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        bookshelfId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    time_t now = time(nullptr);
    
    if (bookshelfId < 0) {
        // Create new bookshelf
        const char* createSql = "INSERT INTO bookshelfs (name, is_deleted, ts) VALUES (?, 0, ?)";
        if (sqlite3_prepare_v2(db, createSql, -1, &stmt, nullptr) != SQLITE_OK) {
            return;
        }
        
        sqlite3_bind_text(stmt, 1, collectionName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, now);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            bookshelfId = sqlite3_last_insert_rowid(db);
        }
        sqlite3_finalize(stmt);
    } else {
        // Reactivate if deleted
        const char* reactivateSql = "UPDATE bookshelfs SET is_deleted = 0, ts = ? WHERE id = ?";
        if (sqlite3_prepare_v2(db, reactivateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_int(stmt, 2, bookshelfId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    if (bookshelfId < 0) return;
    
    // Check if link exists
    const char* checkLinkSql = "SELECT is_deleted FROM bookshelfs_books WHERE bookshelfid = ? AND bookid = ?";
    if (sqlite3_prepare_v2(db, checkLinkSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_int(stmt, 1, bookshelfId);
    sqlite3_bind_int(stmt, 2, bookId);
    
    bool linkExists = false;
    bool isDeleted = false;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        linkExists = true;
        isDeleted = sqlite3_column_int(stmt, 0) == 1;
    }
    sqlite3_finalize(stmt);
    
    if (linkExists && isDeleted) {
        // Reactivate link
        const char* updateSql = "UPDATE bookshelfs_books SET is_deleted = 0, ts = ? WHERE bookshelfid = ? AND bookid = ?";
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_int(stmt, 2, bookshelfId);
            sqlite3_bind_int(stmt, 3, bookId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (!linkExists) {
        // Create new link
        const char* insertSql = "INSERT INTO bookshelfs_books (bookshelfid, bookid, ts, is_deleted) VALUES (?, ?, ?, 0)";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, bookshelfId);
            sqlite3_bind_int(stmt, 2, bookId);
            sqlite3_bind_int64(stmt, 3, now);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void BookManager::removeCollection(const std::string& collectionName) {
    sqlite3_stmt* stmt;
    
    const char* sql = "UPDATE bookshelfs SET is_deleted = 1, ts = ? WHERE name = ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    
    time_t now = time(nullptr);
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, collectionName.c_str(), -1, SQLITE_STATIC);
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}