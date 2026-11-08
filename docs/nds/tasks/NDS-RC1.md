# NDS-RC1: доставка Dungeons of Kremlin 1.0-RC1

8 ноября 2026 года. Отдельное поручение пользователя: завершить текущую
работу, закоммитить всё и опубликовать GitHub prerelease DS-порта1.0-RC1.
Это доставка принятой пользователем сборки, без автоматического закрытия
оставшихся критериев N23 или начала N24.

## Вход и границы

- ROM resident53c-ru: `artifacts/nds/dok.nds`,10184192 байта,
  SHA-256 `f2634c7697db63ed2df53a2956a8632c4f6707061c6e14f227645bbea8f90703`.
- Пользователь подтвердил заметное ускорение и остановил дальнейшую разработку.
- В коммит включаются все текущие tracked/untracked изменения репозитория;
  originals/research/build/готовые бинарники остаются вне Git по правилам проекта.
- Публичное имя ROM — `Dungeons of Kremlin.nds`, архив —
  `Dungeons.of.Kremlin.DS.1.0-RC1.zip`. Новая игровая сборка не создаётся.
- Release title: `Dungeons of Kremlin — Nintendo DS 1.0-RC1`;
  tag `nds-1.0-rc1`, prerelease true, latest false.
- Пользовательское описание — [1.0-RC1](../releases/1.0-RC1.md), только
  возможности и управление. Внутренние проверки/ограничения — в этом
  паспорте и [resident53](../spec/resident53-20261108.md).

## Подготовленный результат

`package_rc1.py` проверяет SHA принятой ROM, отсутствие audit/frontend fixtures,
встроенные NitroFS payload и записывает ZIP с ROM, README.md и SHA256SUMS.txt.
Архив повторно читается: CRC, список файлов и все байты проверяются.
Размер ZIP3309481 байт; SHA-256
`726a3914aaeb9f00be8091b13a03f99029c95f36079016a632c7899bc282a71d`.
Компактная запись — `analysis/nds-release-1.0-rc1-20261108.json`.

Команды подготовки и доставки:

```powershell
py -3.14 -B -X utf8 ports/nds/tools/package_rc1.py
py -3.14 -B -X utf8 tools/reports/summarize_reports.py --check
git diff --check
git add -A
git commit -m "nds: prepare Dungeons of Kremlin 1.0-RC1 prerelease"
git tag -a nds-1.0-rc1 -m "Dungeons of Kremlin - Nintendo DS 1.0-RC1"
git push origin main refs/tags/nds-1.0-rc1
gh release create nds-1.0-rc1 "artifacts/nds/releases/nds-1.0-rc1/Dungeons.of.Kremlin.DS.1.0-RC1.zip" --repo RetrogamingNaBalkone/DoK-port --verify-tag --prerelease --latest=false --title "Dungeons of Kremlin — Nintendo DS 1.0-RC1" --notes-file docs/nds/releases/1.0-RC1.md
```

GitHub connector не предоставляет release/asset upload; используется GitHub CLI.
Сеть, keyring и запись .git требуют запуска вне рабочей песочницы.
Удалённый main перед подготовкой совпал с HEAD; новых игровых проверок
для переименования файла/архива не требуется. Runtime-проверки принятой
ROM приведены в resident53; физических DS/DraStic замеров эта доставка
не добавляет. N23 не объявляется полностью закрытой, N24 не начата.

## Публикация

Подготовка завершена; публикация и проверка удалённого ZIP выполняются
в рамках этого поручения. Запись будет дополнена фактическими commit/tag,
URL и результатом сверки загруженного архива.

Ожидаемый URL:
https://github.com/RetrogamingNaBalkone/DoK-port/releases/tag/nds-1.0-rc1

После доставки остановиться: новой игровой задачи нет. GBA/SDL2 не менялись.
