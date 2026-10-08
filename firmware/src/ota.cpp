// Оновлення прошивки по WiFi. Два шляхи, обидва лише з паролем оновлення:
//   сторінка лампи — POST /api/update з файлом firmware.bin і заголовком X-OTA-Password
//   PlatformIO     — pio run -e ota -t upload (протокол espota)
// Прошивка пишеться в запасний слот; лампа перемикається на неї після перезавантаження.
// Пароль задається зі сторінки лампи. Поки його немає, оновлення вимкнене.
#include <ArduinoOTA.h>
#include <Update.h>
#include "app.h"

const uint8_t PASSWORD_MIN = 6;

static String password;        // зберігається у флеші; забутий пароль скидає лише стирання по USB (pio run -t erase)
static String uploadError;
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
    if (!otaReady()) uploadError = "set update password first";
    else if (server.header("X-OTA-Password") != password) uploadError = "wrong password";
    else if (!Update.begin(UPDATE_SIZE_UNKNOWN)) uploadError = Update.errorString();
    else lightSolid(0x00B4FF);
  } else if (!uploadError.isEmpty()) {
    return;                                   // решту файлу ігноруємо, помилку віддасть handleUploadDone
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) uploadError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) uploadError = Update.errorString();   // перевіряє цілісність і робить слот завантажувальним
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    uploadError = "upload aborted";
  }
}

static void handleUploadDone() {
  if (!uploadError.isEmpty()) return reply(uploadError == "wrong password" ? 403 : 400, uploadError.c_str());
  reply(200, nullptr);
  delay(500);
  ESP.restart();
}

void otaBegin() {
  if (prefs.isKey("otapass")) password = prefs.getString("otapass");
  static const char* headers[] = {"X-OTA-Password"};
  server.collectHeaders(headers, 1);
  server.on("/api/ota/password", HTTP_POST, handlePassword);
  server.on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  startArduinoOta();
}

void pollOta() {
  if (otaStarted && otaReady()) ArduinoOTA.handle();
}
