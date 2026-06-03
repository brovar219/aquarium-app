# Проєкт `aquarium-che` (ESP-IDF)

Нативна прошивка для **ESP32-C6** з тим самим призначенням, що й `aquarium-che.yaml` у ESPHome: RGBW-світло з добовим планом, помпа, (опційно) температура, веб-панель.

Повна документація українською: [docs/README.uk.md](docs/README.uk.md).
Покроковий «бойовий» рецепт збірки/прошивки/OTA — [docs/BUILD_FLASH.uk.md](docs/BUILD_FLASH.uk.md).

## Швидкий старт

1. Встановіть [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (перевірено на **5.5.4**).
2. У PowerShell активуйте середовище (див. деталі в [docs/BUILD_FLASH.uk.md](docs/BUILD_FLASH.uk.md)):
   ```powershell
   $env:IDF_PATH         = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
   $env:IDF_TOOLS_PATH   = 'C:\Espressif'
   $env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.11_env'
   . "$env:IDF_PATH\export.ps1"
   ```
3. `idf.py set-target esp32c6`
4. `idf.py menuconfig` → **Aquarium Wi-Fi** → SSID і пароль (або тримайте їх у `sdkconfig.defaults` для домашнього використання).
5. `idf.py -p COM11 erase-flash` (перший раз або після зміни розмітки) → `idf.py -p COM11 flash monitor`.

Після підключення до Wi-Fi в консолі зʼявиться рядок:

```
I (5753) wifi_sta: Wi-Fi GOT IP=192.168.X.Y mask=... gw=...
I (5757) wifi_sta: Веб-панель: http://192.168.X.Y/  (WS: ws://192.168.X.Y/ws, OTA: POST http://192.168.X.Y/update)
```

Відкрийте `http://<IP>/` — завантажується одна HTML-сторінка; усі оновлення та команди йдуть через WebSocket `ws://<IP>/ws`.

**OTA:** таблиця розділів — `TWO_OTA_LARGE` на 4 МБ флеш. Заливайте зібраний `build/aquarium-che.bin` через форму на сторінці або:

```bash
curl -X POST -H "Content-Type: application/octet-stream" \
     --data-binary @build/aquarium-che.bin http://<IP>/update
```

Версію образу і поточний OTA-слот видно у `data.fw_version` та `data.ota_partition` у WebSocket-стані.

## Структура

- `components/aquarium_app/` — основний код (модель, HAL, сервіс, HTTP/WS).
- `docs/` — план, архітектура, інструкції (UK).
- `main/app_main.cpp` — точка входу.

## Ліцензія

Код надано як приклад для вашого обладнання; перевірте відповідність пінів вашій платі.
