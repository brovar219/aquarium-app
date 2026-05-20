#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace aq {

/** Реєструє POST /update — тіло запиту = сирий бінарник прошивки (application/octet-stream). */
esp_err_t register_firmware_update_uri(httpd_handle_t server);

/** Після успішного OTA новий застосунок стартує як «pending verify» — підтверджуємо його. */
void ota_mark_app_valid_if_needed();

}  // namespace aq
