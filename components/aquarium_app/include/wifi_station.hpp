#pragma once

namespace aq {

void wifi_init_sta();
/** Чекає підключення STA (блокуюче). */
void wifi_wait_connected();

}  // namespace aq
