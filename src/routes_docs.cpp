#include "routes_docs.h"

// docs/openapi.yaml, linked into flash by `board_build.embed_txtfiles`
// (see platformio.ini). Text embeds are NUL-terminated.
extern const char openapiYamlStart[] asm("_binary_docs_openapi_yaml_start");
// web/ws-test.html, embedded the same way.
extern const char wsTestHtmlStart[] asm("_binary_web_ws_test_html_start");

namespace {

const char kSwaggerHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Modbus Gateway API</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css">
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
  <script>
    SwaggerUIBundle({ url: '/api/openapi.yaml', dom_id: '#swagger-ui', tryItOutEnabled: true });
  </script>
</body>
</html>
)HTML";

} // namespace

void registerDocsRoutes(AsyncWebServer &server) {
  // Anchored regex -- see the comment in routes_holding.cpp.
  server.on("^\\/$", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/docs");
  });

  server.on("^\\/docs$", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", kSwaggerHtml);
  });

  server.on("^\\/ws-test$", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8",
                  reinterpret_cast<const uint8_t *>(wsTestHtmlStart), strlen(wsTestHtmlStart));
  });

  server.on("^\\/api\\/openapi\\.yaml$", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/yaml; charset=utf-8",
                  reinterpret_cast<const uint8_t *>(openapiYamlStart), strlen(openapiYamlStart));
  });
}
