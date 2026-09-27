#include "cors.h"

#include "config.h"

void enableCors(AsyncWebServer &server) {
  if (CORS_ALLOW_ORIGIN[0] == '\0') return;

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);

  server.on("^\\/api\\/.*$", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Max-Age", "86400");
    request->send(response);
  });
}
