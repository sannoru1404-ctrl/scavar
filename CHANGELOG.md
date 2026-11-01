# CHANGELOG

Формат: дата · тип · описание. Изменения баланса (`data/*.csv`) записываются обязательно.

## 2.1.0 — 2026-11-01

- feat(sim): библиотека `Source/ScavarCore/Sim` без зависимостей от Unreal — CSV и данные, ГСЧ, урон и броня (SYS-06), баллистика (SYS-05), выживание, лечение, выносливость (SYS-02/07), предметы и контейнеры с транзакциями и инвариантами (SYS-03), цены и доверие (SYS-09), Central Economy (SYS-04).
- feat(persistence): `Source/ScavarPersistence/Db` — SQLite WAL, схема v1, миграции, идемпотентный журнал сделок, резервные копии (SYS-12).
- test: 73 теста (`Tests/Sim`), ASan+UBSan, `-Werror`; AT-034 — 200 SIGKILL подряд без дублей и потерь; AT-035 — 100 000 операций; AT-015 — 6 пар патрон/броня × 10 000 попаданий в пределах ±2%.
- feat(ue): проект `SCAVAR.uproject`, цели Game/Editor/Server/Client, модули `ScavarCore`/`ScavarPersistence`/`ScavarGameplay`, персонаж, компонент жизненных показателей, GameMode, загрузка данных. **Не компилировалось под UBT.**
- tools: `run_sim_tests.sh`, `build.ps1`, `fetch_sqlite.ps1`; CI `sim-tests`.
- docs: ADR-023…025, RSK-15/16, документ 12, график пилота для режима «один разработчик» (07 §1а).
- fix(spec): износ брони считается от максимума; id предмета — int64.
- data: `items.csv` — 40 предметов слайса (размеры, стаки, цены, параметры CE) с перекрёстной проверкой против weapons/ammo/armor; `Sim/Items` строит каталог инвентаря и типы лута; 76 тестов.
- docs: ADR-026 «Пилот на двоих» (listen-server, урезанный объём), 07 §1б — этапы P0–P4.

## 2.0.0 — 2026-11-01

- docs: полный пакет документации v2.0 (01–11), переработка прежнего дизайна под формулу DayZ + Tarkov.
- data: `poi.csv` (27 POI, канонические координаты), `settlements.csv` (14), `weapons.csv` (16), `ammo.csv` (12), `armor.csv` (5), `asset_licenses.csv` (шаблон).
- tools: `check_docs.py` (ссылки, ID, схемы данных, актуальность таблиц), `gen_tables.py`.
- ci: GitHub Actions `docs-and-data`.
