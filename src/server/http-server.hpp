#pragma once

#include <string>

// Local web server for the stream overlay. It listens on 127.0.0.1 only, serves the files in
// data/overlay and the live stats as JSON at /api/state.

// Starts (or restarts) the server on `port`. Returns false if the port is in use.
bool startOverlayServer(int port);
void stopOverlayServer();
bool overlayServerRunning();

// Address to put in a Browser Source, e.g. "http://127.0.0.1:48620/".
std::string overlayUrl();
