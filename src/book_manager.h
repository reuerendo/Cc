#ifndef BOOK_MANAGER_H
#define BOOK_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <sqlite3.h>
#include <ctime>

struct BookMetadata {
    std::string uuid; // Calibre UUID
    std::string title;
    std::string authors;
    std::string lpath; // Относительный путь (Author/Title.epub)          
    std::string series;
    int seriesIndex;
    std::string publisher;
    std::string pubdate;
    std::string lastModified;
    std::string tags;
    std::string comments;
    long long size;
    std::string thumbnail;
    int thumbnailHeight;
    int thumbnailWidth;
    std::string isbn;
    
    // Sync fields
    bool isRead;
    std::string lastReadDate;
    bool isFavorite;
    
    // DB internal IDs (для оптимизации)
    int dbBookId; 
    
    BookMetadata() : seriesIndex(0), size(0), thumbnailHeight(0), 
                     thumbnailWidth(0), isRead(false), isFavorite(false), dbBookId(-1) {}
};

class BookManager {
public:
    BookManager();
    ~BookManager();
    
    bool initialize(const std::string& ignored_path);
    
    // Основные методы (CRUD)
    bool addBook(const BookMetadata& metadata);
    bool updateBook(const BookMetadata& metadata);
    bool deleteBook(const std::string& lpath);
    
    // Синхронизация коллекций (полок)
    void updateCollections(const std::map<std::string, std::vector<std::string>>& collections);
    
    // Методы чтения
    std::vector<BookMetadata> getAllBooks(); 
    int getBookCount();
    
    // Утилиты
    std::string getBookFilePath(const std::string& lpath);

private:
    const std::string SYSTEM_DB_PATH = "/mnt/ext1/system/explorer-3/explorer-3.db";
    std::string booksDir;
    
    // Внутренние методы работы с БД
    sqlite3* openDB();
    void closeDB(sqlite3* db);
    
    // Вспомогательные методы (портированы из pb-db.lua)
    int getStorageId(const std::string& filename);
    int getCurrentProfileId(sqlite3* db);
    std::string getFirstLetter(const std::string& str);
    
    int getOrCreateFolder(sqlite3* db, const std::string& folderPath, int storageId);
    bool processBookSettings(sqlite3* db, int book_id, const BookMetadata& metadata, int profile_id);
    
    // Для коллекций
    int getOrCreateBookshelf(sqlite3* db, const std::string& name);
    int findBookIdByPath(sqlite3* db, const std::string& lpath);
    void linkBookToShelf(sqlite3* db, int shelfId, int bookId);
};

#endif // BOOK_MANAGER_H