// Оновлення прошивки по WiFi. Прошивка пишеться в запасний слот; лампа перемикається на неї після перезавантаження.
//
// POST /api/update з файлом прошивки приймається у двох випадках:
//   офіційна збірка — заголовок X-Signature з підписом ECDSA P-256 (base64) від SHA-256 файлу. Лампа перевіряє його
//                     вшитим відкритим ключем, тому пароль не потрібен: чужу прошивку так не залити.
//                     Так працює кнопка «Встановити» на сторінці лампи; підписує збірки GitHub Actions.
//   свій файл       — заголовок X-OTA-Password з паролем оновлення. Пароль задається зі сторінки лампи;
//                     поки його немає, цей шлях вимкнений. Тим самим паролем працює PlatformIO: pio run -e ota -t upload
#include <ArduinoOTA.h>
#include <Update.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "app.h"
#include "assets.h"

const uint8_t PASSWORD_MIN = 6;

static String password;        // зберігається у флеші; забутий пароль скидає лише стирання по USB (pio run -t erase)
static String uploadError;
static bool   signedUpload = false;          // цей файл прийшов із підписом, а не з паролем
static uint8_t signature[80];                // ECDSA P-256 у DER — до 72 байтів
static size_t  signatureLen = 0;
static mbedtls_sha256_context sha;

static bool signatureValid(const uint8_t* hash) {
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  int failed = mbedtls_pk_parse_public_key(&key, (const unsigned char*)UPDATE_PUBLIC_KEY, sizeof UPDATE_PUBLIC_KEY);
  if (!failed) failed = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, 32, signature, signatureLen);
  mbedtls_pk_free(&key);
  return !failed;
}
static bool   otaStarted = false;

bool otaReady() { return password.length() > 0; }

static void reply(int code, const char* error) {
  JsonDocument doc;
  if (error) doc["error"] = error; else doc["ok"] = true;
  sendJson(code, doc);
}

static void startArduinoOta() {
  if (portal || !otaReady()) return;
  ArduinoOTA.setPassword(password.c_str());
  if (otaStarted) return;
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setMdnsEnabled(false);          // mDNS уже піднято в net.cpp
  ArduinoOTA.onStart([] { lightSolid(0x00B4FF); });
  ArduinoOTA.begin();
  otaStarted = true;
}

// {"old": поточний пароль, "new": новий}. Перший пароль задається без old; порожній new вимикає оновлення.
static void handlePassword() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  const char* next = in["new"] | "";
  if (otaReady() && password != (in["old"] | "")) return reply(403, "wrong password");
  if (*next && strlen(next) < PASSWORD_MIN) return reply(400, "password too short");
  password = next;
  if (otaReady()) prefs.putString("otapass", password); else prefs.remove("otapass");
  startArduinoOta();
  reply(200, nullptr);
}

static void handleUploadChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadError = "";
    String encoded = server.header("X-Signature");
    signedUpload = encoded.length() > 0;
    if (signedUpload) {
      if (mbedtls_base64_decode(signature, sizeof signature, &signatureLen, (const unsigned char*)encoded.c_str(), encoded.length()))
        uploadError = "bad signature";
    } else if (!otaReady()) uploadError = "set update password first";
    else if (server.header("X-OTA-Password") != password) uploadError = "wrong password";
    if (uploadError.isEmpty() && !Update.begin(UPDATE_SIZE_UNKNOWN)) uploadError = Update.errorString();
    if (!uploadError.isEmpty()) return;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    lightSolid(0x00B4FF);
  } else if (!uploadError.isEmpty()) {
    return;                                   // решту файлу ігноруємо, помилку віддасть handleUploadDone
  } else if (up.status == UPLOAD_FILE_WRITE) {
    mbedtls_sha256_update_ret(&sha, up.buf, up.currentSize);
    if (Update.write(up.buf, up.currentSize) != up.currentSize) uploadError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    uint8_t hash[32];
    mbedtls_sha256_finish_ret(&sha, hash);
    mbedtls_sha256_free(&sha);
    if (signedUpload && !signatureValid(hash)) {   // записане в запасний слот лишається неактивним
      Update.abort();
      uploadError = "bad signature";
    } else if (!Update.end(true)) uploadError = Update.errorString();   // перевіряє цілісність і робить слот завантажувальним
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    uploadError = "upload aborted";
  }
}

static void handleUploadDone() {
  if (!uploadError.isEmpty())
    return reply(uploadError == "wrong password" || uploadError == "bad signature" ? 403 : 400, uploadError.c_str());
  reply(200, nullptr);
  delay(500);
  ESP.restart();
}

void otaBegin() {
  if (prefs.isKey("otapass")) password = prefs.getString("otapass");
  static const char* headers[] = {"X-OTA-Password", "X-Signature"};
  server.collectHeaders(headers, 2);
  server.on("/api/ota/password", HTTP_POST, handlePassword);
  server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  startArduinoOta();
}

void pollOta() {
  if (otaStarted && otaReady()) ArduinoOTA.handle();
}
