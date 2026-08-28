// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Direct TCP Push OTA Server on Port 3232
#pragma once

namespace OtaPushServer {

void StartServer(int port = 3232);
void StopServer();

} // namespace OtaPushServer
