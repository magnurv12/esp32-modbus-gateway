#pragma once

#include <ESPAsyncWebServer.h>

// Serves the API documentation:
//   GET /                  -> redirect to /docs
//   GET /docs              -> Swagger UI (assets from a CDN, so the
//                             browser needs internet access)
//   GET /api/openapi.yaml  -> the OpenAPI spec, embedded into the
//                             firmware from docs/openapi.yaml
//   GET /ws-test           -> live WebSocket test page (web/ws-test.html),
//                             self-contained, works offline
void registerDocsRoutes(AsyncWebServer &server);
