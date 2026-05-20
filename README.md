# Проєкт `aquarium-che` (ESP-IDF)

Нативна прошивка для **ESP32-C6** з тим самим призначенням, що й `aquarium-che.yaml` у ESPHome: RGBW-світло з добовим планом, помпа, (опційно) температура, веб-панель.

Повна документація українською: [docs/README.uk.md](docs/README.uk.md).

## Швидкий старт

1. Встановіть [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (рекомендовано 5.2+).
2. `idf.py set-target esp32c6`
3. `idf.py menuconfig` → **Aquarium Wi-Fi** → SSID і пароль.
4. `idf.py build flash monitor`

Відкрийте в браузері `http://<IP-пристрою>/` — завантажується одна HTML-сторінка; усі оновлення та команди йдуть через WebSocket `ws://<IP>/ws`.

**OTA:** після першого переходу на таблицю розділів з двома слотами (`TWO_OTA_LARGE`) зробіть повний `flash` (за потреби `erase-flash`). Далі можна заливати зібраний `build/aquarium-che.bin` через форму на сторінці або `curl -X POST --data-binary @build/aquarium-che.bin http://<IP>/update`.

## Структура

- `components/aquarium_app/` — основний код (модель, HAL, сервіс, HTTP/WS).
- `docs/` — план, архітектура, інструкції (UK).
- `main/app_main.cpp` — точка входу.

## Ліцензія

Код надано як приклад для вашого обладнання; перевірте відповідність пінів вашій платі.
