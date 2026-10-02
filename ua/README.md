# Українська локалізація — гілка `ua`

Ця гілка — форк [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds)
з українськими правками з проєкту [tloz-tmc-ua](https://gitlab.com/alexandrmudryi/tloz-tmc-ua).
Правки зведені до мінімуму й ізольовані, щоб коміти апстріму зливалися без конфліктів.

## Як це влаштовано

Порт не містить ігрових даних — він читає їх із `.gba` за зміщеннями оригінального
USA-рому. Тому майже вся локалізація живе в **ромі**, а не в коді:

| Що | Де | Зміни в коді порту |
|---|---|---|
| Тексти (діалоги, меню гри, предмети) | таблиця рядків у ромі, `0x9B1D90` | не потрібні |
| Шрифт з кирилицею | графіка в ромі | не потрібні |
| Лого «Диво-Ковпак», «НАТИСНИ START» | графіка в ромі | не потрібні |
| Літери «КІНЕЦЬ ГРИ» | графіка в ромі | не потрібні |
| Розташування літер «КІНЕЦЬ ГРИ» | **код** (`DrawGameOverText`) | `port/port_ua.h` + хук у `src/gameOverTask.c` |
| Заставка: лого й підзаголовок виїжджають знизу, без меча, «НАТИСНИ START» вище | **код** (`title.c`, `titleScreenObject.c`, `japaneseSubtitle.c`) | `port/port_ua_title*.inc`, `port/port_ua_japanese_subtitle.inc` + хуки |
| «Лінк» замість імені файлу в меню злиття Дивокаменів | **код** (`kinstoneMenu.c`) | `Port_UA_KinstoneFuserName` у `port/port_ua.h` + хук |
| Зсув заголовка «УЛАМКИ ДИВОКАМЕНІВ» у меню паузи | **код** (`pauseMenu.c`) | `Port_UA_KinstoneHeaderX` у `port/port_ua.h` + хук |
| Фікс перевірки контрольної суми збереження (ARHafer) | код (`save.c`) | уже є в апстрімі порту, нічого не треба |
| Написи самого порту (нижній екран, налаштування, оновлювач) | **код** порту, англійські рядки | `port/port_ua_panel.h` + 4 хуки в `port/port_second_screen_theme.c` |
| Лого «ЛЕГЕНДА ПРО ЗЕЛЬДУ / Диво-Ковпак» на заставці при запуску (до завантаження рому) | **код і romfs** порту (`romfs:/splash.rgb565`) | `port/port_ua_splash.h` + хук у `platform/3ds/source/platform_3ds.c`, `romfs/splash-ua.rgb565` |

Ром для порту збирається в репозиторії tloz-tmc-ua командою `bash make-port.sh` →
`tmc-ua-port.gba`. Його розкладка побайтово збігається з чистим USA-ромом (скрипт
це перевіряє), а за зміщенням `0xFFFFF0` записано маркер `TMC-UA`.

Порт вмикає українські правки коду **лише** коли бачить цей маркер
(`Port_IsUkrainianRom()`), тож англійський USA та європейський роми працюють
у цій збірці точно так само, як в апстрімі.

## Що змінено відносно апстріму

- `port/port_ua.h` — **новий файл**, уся українська логіка: перевірка маркера та
  розкладка «КІНЕЦЬ ГРИ» (позиції 40, 72, 104, 136, 168, 200; слоти 3 і 6 не
  малюються — так само, як у `tloz-tmc-ua/tmc/src/gameOverTask.c`).
- `src/gameOverTask.c` — два невеликі блоки під `#ifdef PC_PORT`, позначені
  коментарем `tloz-tmc-ua` (include і виклик `Port_UA_GameOverLetter`).
- `port/port_ua_title.inc`, `port/port_ua_title_object.inc`,
  `port/port_ua_japanese_subtitle.inc` — **нові файли** із заставкою в стилі
  tloz-tmc-ua (дзеркало `tmc/src/title.c`, `object/titleScreenObject.c`,
  `object/japaneseSubtitle.c` з GitLab). Підключаються `#include` наприкінці
  відповідних файлів у `src/`, бо користуються їхніми static-функціями; у
  кожному з трьох файлів — прототип, 3-рядковий хук на початку головної функції
  та `#include` в кінці, усе з позначкою `tloz-tmc-ua`.
- `src/menu/kinstoneMenu.c`, `src/menu/pauseMenu.c` — по одному рядку з хуком.
- `port/port_ua_panel.h` — **новий файл**: українські написи панелі порту.
  Порт малює свої написи шрифтами з рому (дрібний шрифт повідомлень і великий
  «банерний»), а в українському ромі на місці латиниці стоїть кирилиця — тому
  «BACK» виглядав як «БАВИ». Для українського рому хук перекладає англійський
  рядок за таблицею `kPortUaStrings` (цілий рядок) або `kPortUaWords` (пословно,
  для складених рядків на кшталт `PAGE 2 OF 5`) і кодує результат у байти
  українського шрифту. Латиниця, якої нема в таблицях (версії, changelog з
  GitHub), малюється латинськими гліфами, що лишилися в дрібному шрифті, а у
  великому — транслітерується (там латинських гліфів немає).
  **Переклади — чернетка**, правте таблиці прямо в цьому файлі.
- `port/port_second_screen_theme.c` — п'ять однорядкових вставок з позначкою
  `tloz-tmc-ua`: `PORT_UA_PANEL_TEXT` на вході чотирьох функцій тексту і
  гілка для кодів ≥ 0x80 (банк 2 шрифту) у `GlyphData()`.
- `port/port_ua_marker.h` — **новий файл**: константи маркера `TMC-UA`, спільні для
  `port_ua.h` і `port_ua_splash.h`.
- `port/port_ua_splash.h` — **новий файл**: логотип при запуску. Його порт малює
  ще до завантаження рому, тому `Port_IsUkrainianRom()` тут не працює; натомість
  `Port_UA_SplashPath()` читає маркер у `.gba`-файлах теки
  `sdmc:/3ds/The Minish Cap 3DS/` і, якщо знаходить український ром, віддає
  `romfs:/splash-ua.rgb565` замість `romfs:/splash.rgb565`.
- `platform/3ds/source/platform_3ds.c` — include і виклик `Port_UA_SplashPath()`
  у `Platform3DS_ShowSplash()`, з позначкою `tloz-tmc-ua`.
- `platform/3ds/assets/splash-ua.png` (джерело, прозорий фон) →
  `platform/3ds/romfs/splash-ua.rgb565` (400×240 RGB565 на чорному) скриптом
  `python3 ua/make_splash.py`; після зміни PNG перегенеруйте й закомітьте обидва.
- `platform/3ds/CMakeLists.txt` — один `configure_file`, що копіює
  `splash-ua.rgb565` у romfs (з позначкою `tloz-tmc-ua`).
- `platform/3ds/source/update_manifest.c|h` — `Update_FormatNotesUtf8()`: той самий
  форматер changelog, але з прапорцем `utf8`, який не викидає байти ≥ 0x80 і не
  розрізає UTF-8 при перенесенні рядків; `Update_FormatNotes()` викликає його з
  `false`, тож для апстріму нічого не змінюється.
- `platform/3ds/source/update_ui_3ds.inc` — include `port_ua.h` і виклик
  `Update_FormatNotesUtf8(..., Port_IsUkrainianRom())`: з українським ромом
  кириличний changelog з GitHub малюється українським шрифтом.
- `ua/` — ця документація та перевірка `ua/check.sh`.

Інших змін у файлах збірки немає; `port_ua*.h` — header-only.

## Оновлення з апстріму

```sh
git fetch upstream
git merge upstream/main        # або: git rebase upstream/main
bash ua/check.sh               # хук на місці, компілюється, тест поведінки проходить
```

Конфлікти можливі лише біля хуків: `src/gameOverTask.c` (`DrawGameOverText()`),
`src/title.c` (`HandleTitlescreen()`), `src/object/titleScreenObject.c`,
`src/object/japaneseSubtitle.c`, `src/menu/kinstoneMenu.c`, `src/menu/pauseMenu.c`,
`port/port_second_screen_theme.c` (функції тексту й `GlyphData`),
`platform/3ds/source/update_manifest.c` і `update_ui_3ds.inc` (changelog),
`platform/3ds/source/platform_3ds.c` (`Platform3DS_ShowSplash()`) та
`platform/3ds/CMakeLists.txt` (копіювання romfs).
Тоді досить повернути рядки з позначкою `tloz-tmc-ua` і знову запустити
`ua/check.sh` — він впаде, якщо якийсь хук загубився. Нові англійські написи,
що з'являться в апстрімі, просто додайте до `kPortUaStrings`.

## Збірка та встановлення

Збірка — як в апстрімі (потрібен devkitPro з devkitARM, libctru, citro2d, citro3d):

```sh
./platform/3ds/build.sh        # → build-3ds/game/*.cia / *.3dsx
```

На SD-карті: встановити CIA (або запускати 3DSX) і покласти `tmc-ua-port.gba` у
`sdmc:/3ds/The Minish Cap 3DS/` (ім'я файлу будь-яке). Якщо раніше запускався
англійський ром, кеш ассетів перебудується автоматично (порт порівнює розмір і час
зміни рому); у разі сумнівів можна видалити теку `assets/usa`.

## Відомі обмеження

- Написи порту перекладено чернеткою. Changelog з GitHub з українським ромом
  показує кирилицю; латиниця в ньому транслітерується (у великому шрифті немає
  латинських гліфів), тож нотатки українських релізів варто писати кирилицею.
  Рядок changelog обмежений 42 байтами, а кирилична літера займає 2, тож
  українські рядки виходять удвічі коротші за англійські.
- **Вбудований оновлювач** бере релізи з `EstebanPdN/zelda-tmc-3ds`
  (`platform/3ds/source/update_manifest.h`, `UPDATE_REPOSITORY`). Оновлення
  з нього встановить офіційну збірку: текст лишиться українським (він у ромі), але
  повернеться англійська розкладка «КІНЕЦЬ ГРИ». Якщо українські збірки
  публікуватимуться в окремому репозиторії, варто спрямувати `UPDATE_REPOSITORY`
  туди.
- Заставка перенесена з vanilla-`tmc` на GitLab (`origin/main`, v1.3); графіка
  лого та підзаголовка приходить з `tmc-ua-port.gba`, зібраного з того ж
  `baserom_ukr_newfont.gba`. Збірка на 3DS/в Azahar ще не перевірена — після
  першої збірки порівняйте з `ultimate_auto.gba` в mGBA.
