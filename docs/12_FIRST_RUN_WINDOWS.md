# 12. Первый запуск на Windows

**Версия:** 2.0 · **Дата:** 2026-11-01 · **Статус:** DRAFT. Шаги, помеченные «не проверено», ещё не выполнялись на реальной машине (облачная среда Claude не содержит Unreal Engine).

Цель — получить режим B из [11](11_AI_WORKFLOW.md): Claude Code работает прямо на машине, где стоит Unreal, и сам собирает, запускает тесты и читает логи.

## 1. Что нужно от вас

| Что | Зачем | Действие |
|---|---|---|
| Windows 10/11 x64, 16+ ГБ ОЗУ, SSD 300+ ГБ свободно | Сборка и запуск UE 5.8 | Подойдёт ваша VM |
| Unreal Engine **из исходников** (5.8.x) | Цели `SCAVARServer` и `SCAVARClient` собираются только из исходников. Версия из Epic Games Launcher собирает лишь `SCAVAR` (Game) и `SCAVAREditor` | Свяжите аккаунт Epic с GitHub и получите доступ к `EpicGames/UnrealEngine`; клонируйте, выполните `Setup.bat`, `GenerateProjectFiles.bat`, соберите `UnrealEditor` |
| Visual Studio 2022 с нагрузками «Разработка игр на C++» и «Разработка классических приложений на C++» | Компилятор MSVC | Установить |
| Git, Git LFS, Python 3.11+ | Репозиторий и скрипты | Установить |
| Claude Code на этой машине | Прямая работа с проектом | Установить, открыть каталог репозитория |
| Аккаунт Epic с доступом к **Fab** | Покупные ассеты (Fab-first, [05](05_CONTENT_PLAN.md)) | См. §4 |

До появления исходной сборки движка можно работать с лаунчерной версией: все тесты уровня `Sim` и цели Editor/Game доступны, серверные цели — нет.

## 2. Порядок первого запуска

1. Клонировать репозиторий в путь без пробелов, например `C:\Dev\scavar`.
2. Задать переменную окружения `SCAVAR_UE_ROOT` на корень движка (например `D:\UE\UnrealEngine`).
3. Загрузить SQLite: открыть <https://www.sqlite.org/download.html>, взять актуальную версию amalgamation и её SHA3/SHA256, затем:
   ```powershell
   .\tools\fetch_sqlite.ps1 -Year 2025 -Version 3490100 -Sha256 <хеш с сайта>
   ```
4. ПКМ на `SCAVAR.uproject` → *Switch Unreal Engine version…* → выбрать вашу сборку. Это локально меняет `EngineAssociation`; **файл `SCAVAR.uproject` не коммитить**, пока версия не утверждена.
5. Собрать: `.\tools\build.ps1 -Target Editor` (затем `-Target Server`, `-Target Client`). Логи — `Saved\BuildLogs\`.
6. Запустить автотесты:
   ```powershell
   & "$env:SCAVAR_UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\SCAVAR.uproject" -ExecCmds="Automation RunTests Scavar; Quit" -unattended -nullrhi -log
   ```
7. Передать Claude Code первую задачу: «Выполни шаги 5–6, исправь все ошибки компиляции и предупреждения, пока `Scavar.*` тесты не станут зелёными, и пришли отчёт».

Ожидание: **при первой сборке ошибки компиляции вероятны** — код Unreal-слоя писался без доступа к компилятору UE. Его проверка и есть первая задача на Windows. Игровая логика (`Source/ScavarCore/Sim`) уже проверена отдельно: [`tools/run_sim_tests.sh`](../tools/run_sim_tests.sh).

## 3. Что проверено, а что нет

| Слой | Статус |
|---|---|
| `Source/ScavarCore/Sim` — чистая логика | Собирается g++ 13 (C++20, `-Werror`, ASan+UBSan), автотесты зелёные (см. [CHANGELOG](../CHANGELOG.md)) |
| `Tests/Sim/FaultInjection.cpp` — SIGKILL-тест сохранения | Запускается в Linux; на Windows не применим |
| Unreal-обёртки (`*.Build.cs`, `*.Target.cs`, `U*`/`A*` классы) | **Не проверено**: не компилировалось под UBT/MSVC |
| Репликация, PIE, мультиплеер | **Не проверено** |

## 4. Fab: как подключить ассеты

Я не могу скачивать из Fab: покупка и лицензия привязаны к вашему аккаунту.

1. В Fab добавьте нужные ассеты в библиотеку (бесплатные — «Add to My Library»).
2. В Epic Games Launcher → *Library* → *Fab Library* → *Add to Project* → `SCAVAR`.
3. Для каждого ассета заполните строку в [`data/asset_licenses.csv`](../data/asset_licenses.csv): название, автор, ссылка, лицензия, дата, где используется.
4. Новые файлы `.uasset`/`.umap` коммитятся только через Git LFS.
5. Список того, что искать в первую очередь, Claude составит из [05](05_CONTENT_PLAN.md): сельские постройки (деревянные дома, амбары, зерносклад), заборы, растительность хвойного леса, советская техника и интерьеры, оружие и руки FPS, персонаж с модульной одеждой, звуки оружия и окружения.
