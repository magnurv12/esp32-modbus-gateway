#pragma once

#include <ESPAsyncWebServer.h>

// Lets browser pages served from another origin call the REST API, as
// configured by CORS_ALLOW_ORIGIN (config.h; "" leaves CORS disabled).
//
// Access-Control-Allow-Origin goes on every response through
// DefaultHeaders, which the library applies whenever a response object is
// created -- including the responses modbusTask sends later for paused
// requests, which a middleware running after the handler would miss.
// Preflight requests (OPTIONS on /api/...) are answered directly.
//
// Call before registering other routes.
void enableCors(AsyncWebServer &server);
