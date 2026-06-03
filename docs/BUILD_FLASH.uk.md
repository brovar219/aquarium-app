# Збірка, прошивка, моніторинг, OTA — практичний рецепт

Цей файл фіксує точну послідовність команд, яка спрацювала «з нуля» для **ESP32-C6** на цій машині (Windows + PowerShell). Зберігайте поряд із кодом — це швидший спосіб згадати «як саме», ніж знов нишпорити по логах.

## Тестовий стенд

| Параметр | Значення |
|----------|----------|
| Чип | ESP32-C6 (rev v0.2), вбудована флеш 4 МБ |
| MAC | `58:e6:c5:19:1f:70` |
| Підключення до ПК | USB Serial/JTAG (VID:PID `303A:1001`) |
| COM-порт | `COM11` |
| ESP-IDF | `v5.5.4` у `C:\Espressif\frameworks\esp-idf-v5.5.4` |
| Python venv | `C:\Espressif\python_env\idf5.5_py3.11_env` |
| Хост ОС | Windows 10/11, PowerShell |

> Дві USB-«пельки» ESP32-C6 (USB-JTAG + CDC) показуються як `USB JTAG/serial debug unit` і `COM11`. Прошивка/моніторинг ідуть саме через COM-порт інтерфейсу CDC.

## 1. Активація ESP-IDF (PowerShell)

`Initialize-Idf.ps1` від Espressif Tool Installer не завжди підхоплює потрібний Python venv. Найнадійніше — задати змінні явно перед `export.ps1`:

```powershell
$env:IDF_PATH         = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
$env:IDF_TOOLS_PATH   = 'C:\Espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.11_env'
. "$env:IDF_PATH\export.ps1"
idf.py --version   # очікуємо: ESP-IDF v5.5.4
```

Після цього в цій сесії PowerShell є `idf.py`, `cmake`, `ninja`, `riscv32-esp-elf-gcc`.

## 2. Підготовка проєкту

```powershell
cd C:\Users\igor\Documents\projects\aquarium-app
idf.py set-target esp32c6      # перший раз або після зміни цілі
```

`set-target` створить чистий `build/` і `sdkconfig`, який наслідує `sdkconfig.defaults` із проєкту.

### Ключові опції в `sdkconfig.defaults`

- `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y` — дві OTA-партиції (~1.7 МБ кожна, потрібно для OTA через `POST /update`).
- `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` + `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` — без цього `TWO_OTA_LARGE` не вміщається у дефолтні 2 МБ (помилка `partitions tables occupies 3.5MB ...`). У 8 МБ-плат можна збільшити.
- `CONFIG_HTTPD_WS_SUPPORT=y` — WebSocket у вбудованому `esp_http_server`.
- `CONFIG_AQUARIUM_WIFI_SSID` / `CONFIG_AQUARIUM_WIFI_PASSWORD` — SSID/пароль для STA. Ці значення також можна перепризначити через `idf.py menuconfig → Aquarium Wi-Fi`.

Для приватного пароля рекомендується тримати його **поза** репозиторієм: видаліть рядки з паролем із `sdkconfig.defaults`, а в `sdkconfig` (його git ігнорує) виставте `menuconfig`.

## 3. Збірка

```powershell
idf.py build
```

Очікуваний кінець:

```
aquarium-che.bin binary size 0x116b10 bytes. Smallest app partition is 0x1a9000 bytes. 0x924f0 bytes (34%) free.
Project build complete. To flash, run:
 idf.py flash
```

Артефакт OTA — `build\aquarium-che.bin` (саме його заливаємо на `POST /update`).

## 4. Перший залив через USB

Перший раз після зміни розмітки партицій (або з невідомого стану) — обовʼязково сотрети флеш:

```powershell
idf.py -p COM11 erase-flash
idf.py -p COM11 flash
```

Після прошивки плата сама ребутає й готова до Wi-Fi.

## 5. Моніторинг і пошук IP у консолі

Стандартно:

```powershell
idf.py -p COM11 monitor
```

(`Ctrl+]` для виходу.)

Після `wifi_init_sta` в логах буде блок із трьома рядками, які ми спеціально додали в `components/aquarium_app/src/wifi_station.cpp`:

```
I (4721) wifi_sta: З'єднано з AP "Home" (канал 11)
I (5753) wifi_sta: Wi-Fi GOT IP=192.168.89.6 mask=255.255.255.0 gw=192.168.89.1
I (5757) wifi_sta: Веб-панель: http://192.168.89.6/  (WS: ws://192.168.89.6/ws, OTA: POST http://192.168.89.6/update)
I (5768) wifi_sta: Wi-Fi підключено, IP=192.168.89.6
```

### Альтернатива моніторингу: скрипт, що чекає на IP

Якщо хочете тільки витягти IP (наприклад, для CI / автоматизації), без зайвих ESC-кодів `idf.py monitor`:

```powershell
$py = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$code = @'
import serial, time, re, sys
ser = serial.Serial("COM11", 115200, timeout=0.2)
# (опційно) пульс RTS/DTR для рестарту:
ser.setRTS(True); ser.setDTR(False); time.sleep(0.1)
ser.setRTS(False); ser.setDTR(False)
deadline = time.time() + 30; buf = bytearray(); ip = None
while time.time() < deadline:
    chunk = ser.read(4096)
    if not chunk:
        time.sleep(0.05); continue
    buf.extend(chunk); sys.stdout.write(chunk.decode("utf-8","replace")); sys.stdout.flush()
    m = re.search(r"GOT IP=(\d+\.\d+\.\d+\.\d+)", buf.decode("utf-8","replace"))
    if m: ip = m.group(1); break
ser.close(); print(f"\nDEVICE_IP={ip}")
'@
$tmp = [IO.Path]::GetTempFileName() + '.py'
$code | Set-Content -Encoding utf8 $tmp
& $py $tmp
Remove-Item $tmp -Force
```

Скрипт виведе фінальний рядок виду `DEVICE_IP=192.168.89.6`.

## 6. OTA — оновлення «по повітрю»

### Що вже зроблено в коді

- HTTP-маршрут `POST /update` (тіло запиту = сирий бінарник, `Content-Type: application/octet-stream`) — `components/aquarium_app/src/firmware_http_update.cpp`.
- Контроль розміру vs `update_partition->size`, `esp_ota_begin` / `_write` / `_end` / `set_boot_partition`, перезавантаження.
- Після першого старту нового образу — `ota_mark_app_valid_if_needed()` у `aquarium_app::start()` підтверджує його як добрий і скасовує rollback.
- У JSON-стані пристрою (поле `data` у `type: "state"`) додано:
  - `fw_version` — значення з `PROJECT_VER` у кореневому `CMakeLists.txt`.
  - `fw_idf` — версія IDF, з якою зібрано.
  - `ota_partition` — `ota_0` або `ota_1` (видно, з якого слота завантажилися).

### Тест-сценарій

1. Підняти версію в `CMakeLists.txt`:

   ```cmake
   set(PROJECT_VER "1.0.1")
   ```

2. `idf.py build` — отримуємо новий `build\aquarium-che.bin`.

3. З **будь-якого пристрою у тій самій мережі**, що й ESP32 (телефон, ноут, інша ВМ; ВАЖЛИВО — не з-за NAT), залити бін:

   ```powershell
   curl.exe -X POST `
     -H "Content-Type: application/octet-stream" `
     --data-binary "@C:\Users\igor\Documents\projects\aquarium-app\build\aquarium-che.bin" `
     http://192.168.89.6/update
   ```

   Linux/macOS варіант:

   ```bash
   curl -X POST -H "Content-Type: application/octet-stream" \
        --data-binary @build/aquarium-che.bin \
        http://192.168.89.6/update
   ```

   Очікувана відповідь HTTP: `OK rebooting`. Через ~0.5 с плата ребутне і піде з нового OTA-слоту.

4. Перевірити в моніторі:
   - `ota_http: Нова прошивка підтверджена (rollback скасовано)` — означає, що другий запуск успішний, тепер плата залишиться на новому образі.
   - У WS-стані прийде `"fw_version":"1.0.1"` і `"ota_partition"` тепер інший (`ota_1`, якщо раніше було `ota_0`, і навпаки).

### Перевірка стану з командного рядка

Швидко перевірити, що віддає WS, можна WebSocket-клієнтом `wscat` (npm) або просто HTTP:

- `GET http://<IP>/` — статична сторінка інтерфейсу.
- `ws://<IP>/ws` → надішліть `{"type":"cmd","name":"get_state"}` і отримайте поточний JSON; саме там видно `fw_version`, `fw_idf`, `ota_partition`.

### Безпека

`POST /update` не вимагає автентифікації. **Будь-хто на тій самій L2-мережі може перешити пристрій.** Для домашньої ізольованої IoT-Wi-Fi це прийнятно; за межами — додайте мінімум `Authorization: Bearer …` або обмежте файрволом.

## 7. Типові помилки та фікси

| Симптом | Причина | Що зробили |
|---------|---------|------------|
| `partitions tables occupies 3.5MB of flash ... does not fit in configured flash size 2MB` | дефолтна флеш-сайз 2 МБ, а `TWO_OTA_LARGE` потребує більше | Додали `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` / `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` у `sdkconfig.defaults`. |
| `'cJSON' was not declared in this scope` у `api_dispatch.cpp` | відсутній include `cJSON.h` | Додано `#include "cJSON.h"`. |
| `'esp_sntp_sync_status_t' does not name a type; did you mean 'sntp_sync_status_t'?` | у v5.5+ enum називається без префікса `esp_` | Замінили тип на `sntp_sync_status_t`. |
| `'void* memset(void*, int, size_t)' clearing an object of non-trivial type 'MqttNvsConfig'` | C++ `-Werror=class-memaccess` для типу з NSDMI | Замінили на `cfg = MqttNvsConfig{}` / `s_cfg = MqttNvsConfig{}`. |
| `the address of 'esp_partition_t::label' will never be NULL` | `label` — `char[16]`, не вказівник | Перевіряємо тепер `run->label[0] != '\0'`. |
| `MqttClientHub::s_svc_ is private` у `ws_handler` | приватне поле, до якого звертається вільна функція | Додали публічний `static DeviceService* service()`; вживаємо його. |
| `undefined reference to MqttClientHub::rebuild_topics()` | у заголовку оголошено як член, у .cpp було як вільну функцію | Перенесли реалізацію в `MqttClientHub::rebuild_topics()`. |
| `expected '}' at end of input` (`api_dispatch.cpp`) | не закрита `namespace aq` | Додали `}  // namespace aq` у кінці файлу. |
| `ledc: requested frequency 12000 and duty resolution 13 can not be achieved` → `abort()` у `RgbwLedc::RgbwLedc()` | LEDC на ESP32-C6 не дає 12 кГц × 8192 кроки (>80 МГц PLL_DIV) | Знизили `freq_hz` до **5000** у `rgbw_ledc.cpp`. |

## 8. Корисні one-liners

- Перебудувати тільки прикладні файли (без bootloader/IDF), коли змінили лише `components/aquarium_app/`:

  ```powershell
  idf.py app
  ```

- Прошити **тільки** прикладну партицію (швидше, без перезапису bootloader і таблиці):

  ```powershell
  idf.py -p COM11 app-flash
  ```

- Дивитись консоль без `idf.py monitor` (наприклад, з іншого терміналу):

  ```powershell
  python -m serial.tools.miniterm COM11 115200
  ```

- Подивитися, у який слот зараз завантажились — найшвидше через WS:

  ```json
  {"type":"cmd","name":"get_state"}
  ```

  у відповіді `data.ota_partition` = `ota_0`/`ota_1`, `data.fw_version` = з `PROJECT_VER`.
