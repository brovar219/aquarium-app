# Home Assistant + MQTT

## Чому «не підключається»

Прошивка **не** використовує стандартні топіки ESPHome. Раніше HA міг підключитися до брокера, але **сутності світла не з’являлися**, бо не було MQTT Discovery.

Після оновлення прошивки (з `mqtt_ha_bridge`) при підключенні до брокера пристрій публікує:

- `homeassistant/light/aquarium_che_<MAC>/config`
- `homeassistant/switch/aquarium_che_<MAC>_pump/config`

У HA: **Налаштування → Пристрої та служби → MQTT → Налаштувати → Увімкнути Discovery** (за замовчуванням увімкнено).

## Налаштування на пристрої

1. Відкрийте `http://<IP>/` (IP у логах `wifi_sta: GOT IP=...`).
2. Секція **MQTT**: увімкніть, вкажіть **IP брокера** (той самий, що в HA — часто IP Raspberry/сервера, не `homeassistant.local`, якщо ESP не бачить mDNS).
3. `topic_prefix` — наприклад `aquarium` (тоді HA-топіки: `aquarium/light/set`, `aquarium/light/state`).
4. Збережіть MQTT → у логах UART має бути `mqtt_hub: MQTT connected` і `mqtt_ha: HA discovery`.

Перевірка в Mosquitto:

```bash
mosquitto_sub -h <broker> -t 'aquarium/#' -v
mosquitto_sub -h <broker> -t 'homeassistant/light/#' -v
```

## Керування з HA

| Сутність (після discovery) | Топік команд |
|---------------------------|--------------|
| Aquarium Light | `{prefix}/light/set` — `ON` / `OFF` або `{"state":"ON","brightness":200}` |
| Aquarium Pump | `{prefix}/pump/set` — `ON` / `OFF` |

Розклад, програма, години — через **веб-панель** або JSON на `{prefix}/cmd`:

```json
{"type":"cmd","name":"set_settings","data":{"operation_mode":"manual","light_program":"plants_pro","hour_start":8,"hour_end":20}}
```

Відповідь: `{prefix}/reply`.

## Світло не горить

1. **Режим «Авто 24 год»** без SNTP до 90 с — світло вимкнене; після 90 с — денний профіль (14:00). Перевірте Wi‑Fi та доступ до `pool.ntp.org`.
2. У **ночі** за розкладом яскравість може бути 0 — увімкніть **Ручний** або сцену «Показ гостям» у веб-панелі / HA Light ON.
3. Переконайтеся, що прошивка зібрана для вашої плати (піни RGBW: GPIO 3,4,5,6).

## Приклад automation (якщо discovery вимкнено)

```yaml
mqtt:
  light:
    - name: "Aquarium"
      schema: json
      command_topic: "aquarium/light/set"
      state_topic: "aquarium/light/state"
      brightness: true
      brightness_scale: 255
```
