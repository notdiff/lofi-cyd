# Lofi Miner

Прошивка для ESP32 Cheap Yellow Display320×240 и Lucky Miner LV07/BM1366.
Четыре экрана: часы по Москве, BTC и обзор майнера, подробные метрики LV07,
график BTC за24часа. Переключение свайпом или кнопкой BOOT.

Курс обновляется через публичный Kraken WebSocket. Майнер опрашивается каждые5с.
Сложность сети, высота блока и комиссия берутся из mempool.space.
На обзоре P10m показывает статистическую вероятность блока за следующие10мин.
Голубой заяц спит на часах, двигается и реагирует на касание на обзоре/BTC.

## Что нужно

- ESP32 CYD с flash4MB и сенсором XPT2046.
- USB-кабель с передачей данных, Wi-Fi2,4GHz и доступный в этой сети LV07.
- Python3.9+ или PlatformIO в VS Code.

## Установка

Из корня репозитория:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

На Windows вместо `source` используй `.venv\Scripts\Activate.ps1`.
Зависимости Python указаны в `requirements.txt`, ESP32 и библиотек — в `platformio.ini`.

## Адрес майнера

В `src/config.h` замени примерный IP на адрес своего майнера:

```cpp
#define LOFI_MINER_HOST "192.168.1.100"
```

Указывай только IP или имя хоста, без `http://` и пути. Пароли Wi-Fi в коде не нужны.

## Прошивка через USB

Посмотреть порты:

```sh
pio device list
```

Основной вариант ST7789:

```sh
pio run -e cyd_st7789
pio run -e cyd_st7789 -t upload --upload-port /dev/cu.usbserial-110
```

Замени порт на свой: `/dev/ttyUSB0` на Linux, `COM3` на Windows и т.п.
Для дисплея ILI9341 используй `-e cyd_ili9341` вместо `-e cyd_st7789`.
Перед прошивкой закрой Serial monitor и подключённые к плате вкладки браузера.

## BIN для браузера и обновления

```sh
pio run -e cyd_st7789 -e cyd_ili9341
python scripts/package_firmware.py
```

В `dist/firmware` появятся:

| Файл | Назначение |
| --- | --- |
| `cyd-st7789.bin`, `cyd-ili9341.bin` | Полная установка с адреса0x0 |
| `cyd-st7789-app.bin`, `cyd-ili9341-app.bin` | Только приложение, адрес0x10000 |
| `manifest.json` | Версия, размеры, адреса и SHA256 |

В [ESPHome Web](https://web.esphome.io/) в Chrome/Edge выбери Connect → Install
и полный BIN своего дисплея. Полный образ затрагивает область NVS, поэтому после
установки может потребоваться повторная настройка Wi-Fi.

Для обновления уже настроенной Lofi Miner v0.4+ с таблицей разделов `min_spiffs`
запиши только приложение, сохранив NVS:

```sh
python -m esptool --chip esp32 --port /dev/cu.usbserial-110 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x10000 dist/firmware/cyd-st7789-app.bin
```

При переходе с другой прошивки сначала выполняется полная установка.
Если PlatformIO использует нестандартный каталог, передай упаковщику
`--core-dir /путь/к/platformio` или задай `PLATFORMIO_CORE_DIR`.

## Wi-Fi

1. Подключись к сети **Lofi-Miner-Setup**, пароль **lofiminer**.
2. Открой **http://192.168.4.1** и введи SSID/пароль своей сети2,4GHz.
3. После подключения точка настройки отключится, начнут загружаться данные.

Wi-Fi сохраняется в NVS платы. Если сеть недоступна30с, настройка открывается снова.
Для смены доступной сети есть команда `w` через Serial. Wi-Fi майнера не меняется.

## Управление

Свайп влево/вправо — следующая/предыдущая страница. Короткий BOOT — следующая,
удержание1,5с — инверсия. Последняя страница и настройки дисплея сохраняются.

На экране LV07:

- **Settings** — Auto/Manual fan0–100% с шагом5, Frequency и Core Voltage.
- **Save** — применить черновик через частичный `PATCH /api/system`.
- **Back** или смена страницы — закрыть несохранённый черновик.
- **Restart** — перезапустить майнер через `POST /api/system/restart` после подтверждения.

Частоты:400/425/450/475/485/500/525/550/575MHz.
Core Voltage:1100/1150/1200/1250/1300mV. Настройки предназначены для BM1366.
Пул, кошелёк и остальные параметры не передаются. После Save автоматического
рестарта нет. При NO REPLY проверь консоль майнера: команда могла дойти;
прошивка не повторяет записи автоматически.

## Serial и диагностика

```sh
pio device monitor --port /dev/cu.usbserial-110 --baud 115200
```

| Команда | Действие |
| --- | --- |
| `1`–`4`, `n` | Страница / следующая |
| `q` | Сеть, HTTP, данные, память и сенсор |
| `w` | Настройка Wi-Fi |
| `l`, `d`, `b` | LIVE / DEMO / только фон |
| `e`, `x` | Открыть/закрыть Settings без записи |
| `a` | Реакция зайца на обзоре/BTC |
| `r`, `i`, `s` | Поворот180°, инверсия, перестановка R/B |

Failed to open port — закрой другое подключение. Failed to connect — удерживай
BOOT, кратко нажми RST и отпусти BOOT после начала записи. WAIT/STALE — проверь
адрес майнера, Wi-Fi и интернет. Неверные цвета — проверь драйвер и команды `i`/`s`.

## Структура

```text
src/                            Логика, экраны, сеть, сенсор и анимация
src/generated/                  Встроенные изображения, шрифты и TLS-корни
include/                        TLS-транспорт WebSocket
scripts/configure_transport.py  Подключение транспорта при сборке
scripts/package_firmware.py     Создание и проверка BIN
platformio.ini                  Драйверы, GPIO и зависимости ESP32
requirements.txt                Инструменты сборки и прошивки
LICENSES.txt                    Лицензии сторонних компонентов
```

Все нужные ассеты уже встроены в код; PNG, генераторы и дополнительные инструменты
для сборки не требуются. `.pio`, `.venv` и `dist` создаются локально и исключены из Git.

Дисплей: HSPI12/13/14, CS15/DC2/BL21. Сенсор: VSPI25/39/32, CS33/IRQ36.
На платах с другим подключением адаптируй `platformio.ini` и `src/touch.cpp`.

Сторонние компоненты: Arduino-ESP32, TFT_eSPI, ArduinoJson, arduinoWebSockets,
XPT2046_Touchscreen, libb64, DejaVu Sans и TLS-корни Mozilla/certifi.
Их уведомления и лицензии сохранены в `LICENSES.txt`.
