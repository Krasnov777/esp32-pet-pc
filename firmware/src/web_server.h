// Configuration UI + small REST surface on port 80.
//
// Synchronous WebServer with the page compiled in as a flash constant rather
// than an async server on a filesystem: one fewer thing to upload, and this
// device serves one operator, occasionally.
#pragma once

namespace web {

void begin();
void tick();

}  // namespace web
