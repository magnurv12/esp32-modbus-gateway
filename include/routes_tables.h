#pragma once

#include <ESPAsyncWebServer.h>

// Registers the data routes for every Modbus table:
//   GET /api/<table>?start=&count=[&slave=]   block read
//   GET /api/<table>/{address}[?slave=]       one address
//   PUT /api/<table>/{address}[?slave=]       FC 05/06 (holding, coils)
//   PUT /api/<table>[?slave=]                 FC 15/16 (holding, coils)
// <table> is holding, input, coils or discrete. Every request is executed
// on the bus when it arrives -- there's no cache or fixed window.
void registerTableRoutes(AsyncWebServer &server);
