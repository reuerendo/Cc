#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif

// Language codes
typedef enum {
    LANG_ENGLISH = 0,
    LANG_RUSSIAN = 1,
    LANG_UKRAINIAN = 2,
    LANG_SPANISH = 3
} LanguageCode;

// String identifiers
typedef enum {
    STR_APP_TITLE = 0,
    STR_IP_ADDRESS,
    STR_PORT,
    STR_PASSWORD,
    STR_READ_COLUMN,
    STR_READ_DATE_COLUMN,
    STR_FAVORITE_COLUMN,
    STR_ENABLE_LOG,
    STR_CONNECTION_FAILED,
    STR_CONNECTED,
    STR_DISCONNECTED,
    STR_SYNC_COMPLETE,
    STR_BATCH_SYNC_FINISHED,
    STR_BOOK_SINGULAR,
    STR_BOOKS_PLURAL,
    STR_RECEIVING,
    STR_CONNECTED_IDLE,
    STR_CANCEL,
    STR_RETRY,
    STR_FAILED_CONNECT_SERVER,
    STR_CHECK_IP_PORT,
    STR_HANDSHAKE_FAILED,
    STR_WIFI_CONNECT_FAILED,
    STR_TOTAL_RECEIVED,
    STR_OFF,
    STR_ON,
    STR_COUNT
} StringId;

// Initialize i18n system and detect system language
void i18n_init();

// Get translated string by ID
const char* i18n_get(StringId id);

// Manually set language (for testing or user preference)
void i18n_set_language(LanguageCode lang);

// Get current language
LanguageCode i18n_get_language();

#ifdef __cplusplus
}
#endif

#endif // I18N_H



