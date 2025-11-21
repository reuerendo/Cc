#ifndef BOOK_MANAGER_H
#define BOOK_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <sqlite3.h>

// Book metadata structure (без изменений)
struct BookMetadata {
    std::string uuid;
    std::string title;
    std::string authors;
    std::string lpath;           
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
    std::string isbn; // Добавим ISBN, он используется в pb-db.lua
    
    // Sync fields
    bool isRead;
    std::string lastReadDate;
    bool isFavorite;
    
    BookMetadata() : seriesIndex(0), size(0), thumbnailHeight(0), 
                     thumbnailWidth(0), isRead(false), isFavorite(false) {}
};

class BookManager {
public:
    BookManager();
    ~BookManager();
    
    bool initialize(const std::string& ignored_path); // Путь теперь зашит внутри
    
    // Основные методы
    bool addBook(const BookMetadata& metadata);
    bool updateBook(const BookMetadata& metadata); // То же, что и addBook
    bool deleteBook(const std::string& lpath);
    
    // Синхронизация коллекций
    void updateCollections(const std::map<std::string, std::vector<std::string>>& collections);
    
    // Вспомогательные (для совместимости с текущим кодом protocol)
    std::string getBookFilePath(const std::string& lpath);
    std::vector<BookMetadata> getAllBooks(); 
    int getBookCount();
    bool hasMetadataChanged(const BookMetadata& metadata);
    void updateMetadataCache(const BookMetadata& metadata);

private:
    const std::string SYSTEM_DB_PATH = "/mnt/ext1/system/explorer-3/explorer-3.db";
    std::string booksDir;
    
    // Внутренние методы работы с БД
    sqlite3* openDB();
    void closeDB(sqlite3* db);
    
    int getStorageId(const std::string& filename);
    int getCurrentProfileId(sqlite3* db);
    std::string getFirstLetter(const std::string& str);
    std::string formatAuthorName(const std::string& author);
    
    // Логика из pb-db.lua
    bool processBookSettings(sqlite3* db, int book_id, const BookMetadata& metadata, int profile_id);
    int getOrCreateFolder(sqlite3* db, const std::string& folderPath, int storageId);
};

#endif // BOOK_MANAGER_H