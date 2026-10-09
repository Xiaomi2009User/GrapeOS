# Відновлення проєкту після зміни ОС

У репозиторії збережено вихідники, графічні ресурси, шрифти, тести,
документацію та готову збірку у `downloads/grapeos-desktop.zip`.
Папки `build`, `.runtime` та особисті дані емулятора `data/*.img`
до репозиторію не входять.

## Забрати з GitHub

Натисни **Code → Download ZIP** і розпакуй архів. Або скопіюй URL
репозиторію з кнопки Code та виконай `git clone <URL>`.

Для запуску на ноутбуці розпакуй `downloads/grapeos-desktop.zip`.
У ньому є готовий USB-образ, EFI-файл та інструкція. Якщо флешка GrapeOS
вже підготовлена, заміни лише `EFI/BOOT/BOOTX64.EFI` файлом із цього ZIP:
документи, фото та налаштування у папці `GRAPE` залишаться на місці.
Потрібна UEFI x86-64 із вимкненим Secure Boot.

## Зібрати в Arch Linux

```sh
sudo pacman -S --needed base-devel python gnu-efi qemu-system-x86 qemu-ui-gtk edk2-ovmf
cd grapeos
make desktop
./run.sh --desktop
```

У інших Linux потрібні GCC, Binutils, Make, Python 3, GNU-EFI та,
для емулятора, QEMU й OVMF. Назви пакетів залежать від дистрибутива.
У Windows можна використати готовий образ, а збірку робити в Linux.

## Власні дані

Перед зміною основної ОС скопіюй папку `GRAPE` зі своєї флешки окремо:
там документи, фото і службові файли профілю. Увімкни показ прихованих
файлів, щоб також скопіювати `.profile-a`, `.profile-b` та `.desktop-theme`,
якщо вони існують. На GitHub особисті дані не завантажуються.

Дані класичного емулятора — `data/grapeos-data.img`; дані UEFI-емулятора —
його поточний `build/grapeos-desktop.img`. За потреби скопіюй їх окремо:
перезбирання UEFI-образу або `make clean` стирає його вміст.
