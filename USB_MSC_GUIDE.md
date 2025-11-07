# USB Mass Storage Class Integration

## Обзор

Реализация USB MSC (Mass Storage Class) для эмуляции флоппи-диска через TinyUSB.

## Архитектура

```
┌──────────────┐
│   Computer   │
│  (USB Host)  │
└──────┬───────┘
       │ USB Cable
       │
┌──────▼───────────────────────────────────┐
│   Raspberry Pi Pico 2                    │
│                                          │
│  ┌────────────────┐                      │
│  │  TinyUSB MSC   │                      │
│  │  - Callbacks   │                      │
│  │  - Descriptors │                      │
│  └───────┬────────┘                      │
│          │                               │
│  ┌───────▼────────┐                      │
│  │  Floppy Emu    │                      │
│  │  - 320KB Cache │                      │
│  │  - LRU Policy  │                      │
│  └───────┬────────┘                      │
│          │                               │
│  ┌───────▼────────┐                      │
│  │  SD Card Task  │                      │
│  │  - FatFS       │                      │
│  └────────────────┘                      │
│          │                               │
└──────────┼───────────────────────────────┘
           │
     ┌─────▼──────┐
     │  SD Card   │
     │ .IMG Files │
     └────────────┘
```

## TinyUSB Callbacks

### Device Callbacks

#### `tud_mount_cb()`
- Вызывается при подключении USB устройства к хосту
- Устанавливает флаг `usb_mounted = true`

#### `tud_umount_cb()`
- Вызывается при отключении USB устройства
- Устанавливает флаг `usb_mounted = false`

### MSC Callbacks

#### `tud_msc_test_unit_ready_cb(uint8_t lun)`
- **Назначение**: Проверка готовности диска
- **Возврат**: `true` если образ загружен в эмулятор
- **Ошибка**: SCSI_SENSE_NOT_READY (0x3A) если нет образа

#### `tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)`
- **Назначение**: Сообщить размер диска хосту
- **Параметры**:
  - `block_count` = 2880 (секторов в 1.44MB)
  - `block_size` = 512 (bytes)

#### `tud_msc_inquiry_cb(...)`
- **Назначение**: Идентификация устройства (SCSI INQUIRY)
- **Vendor ID**: "RaspPi"
- **Product ID**: "Floppy Emulator"
- **Revision**: "1.0"

#### `tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)`
- **Назначение**: Чтение сектора (SCSI READ10)
- **Поток**:
  1. Проверка границ LBA (< 2880)
  2. Вызов `floppy_read_sector(lba, buffer)`
  3. Чтение через кеш (cache hit/miss)
  4. Возврат `bufsize` или `-1` при ошибке

#### `tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)`
- **Назначение**: Запись сектора (SCSI WRITE10)
- **Поток**:
  1. Проверка границ LBA
  2. Вызов `floppy_write_sector(lba, buffer)`
  3. Запись в кеш с установкой dirty flag
  4. Возврат `bufsize` или `-1` при ошибке

#### `tud_msc_write10_complete_cb(uint8_t lun)`
- **Назначение**: Завершение операции записи
- **Примечание**: Flush выполняется автоматически при замещении блоков

#### `tud_msc_scsi_cb(...)`
- **Назначение**: Обработка дополнительных SCSI команд
- **По умолчанию**: Возврат ошибки ILLEGAL_REQUEST

## USB Descriptors

### Device Descriptor
- **VID**: 0x2E8A (Raspberry Pi)
- **PID**: 0x0003 (Mass Storage Device)
- **Class**: Defined by interface
- **Manufacturer**: "Raspberry Pi"
- **Product**: "USB Floppy Emulator"
- **Serial**: Уникальный ID чипа (16 hex символов)

### Configuration Descriptor
- **Interfaces**: 1 (MSC)
- **Endpoints**:
  - EP1 OUT: Bulk Out (host → device)
  - EP1 IN: Bulk In (device → host)
- **EP Size**: 64 bytes
- **Power**: 100 mA

## Процесс работы

### Загрузка образа

1. **Menu Task** → выбор файла
2. **Floppy Emulator** → загрузка FAT области (33 сектора)
3. **USB Host** → обнаруживает готовность диска
4. **OS** → монтирует как FAT12 floppy
5. **Готово** → можно читать/писать файлы

### Чтение файла (Read Operation)

```
Computer                    Pico                     SD Card
   │                         │                         │
   ├─READ(LBA=100)──────────►│                         │
   │                         ├─Cache Lookup            │
   │                         │  [MISS]                 │
   │                         ├─READ sectors 96-103────►│
   │                         │◄────8 sectors───────────┤
   │                         ├─Store in cache          │
   │◄────Sector 100──────────┤                         │
   │                         │                         │
   ├─READ(LBA=101)──────────►│                         │
   │                         ├─Cache Lookup            │
   │                         │  [HIT!]                 │
   │◄────Sector 101──────────┤                         │
```

### Запись файла (Write Operation)

```
Computer                    Pico                     SD Card
   │                         │                         │
   ├─WRITE(LBA=200)─────────►│                         │
   │                         ├─Cache Lookup/Load       │
   │                         ├─Update cache            │
   │                         ├─Mark DIRTY              │
   │◄────OK───────────────────┤                         │
   │                         │                         │
   │  [Later: cache eviction]│                         │
   │                         ├─Flush dirty block──────►│
   │                         │◄────OK──────────────────┤
```

## Производительность

### Скорость чтения
- **Cache Hit**: ~500 KB/s (RAM speed)
- **Cache Miss**: ~100 KB/s (SD card + 8x read-ahead)
- **FAT область**: Всегда cache hit

### Скорость записи
- **Initial write**: ~500 KB/s (write to cache)
- **Flush**: ~100 KB/s (write to SD card)
- **Lazy write-back**: Минимизирует SD операции

### Эффективность кеша
- **FAT hits**: ~100% (постоянно в памяти)
- **Sequential reads**: ~87.5% (7 из 8 секторов)
- **Random reads**: Зависит от рабочего набора

## Тестирование

### Проверка на Windows
```
1. Подключить Pico 2 по USB
2. Загрузить .IMG файл через меню
3. Windows обнаружит "Removable Disk"
4. Формат: FAT12, 1.44 MB
5. Можно копировать файлы
```

### Проверка на Linux
```bash
# Посмотреть устройство
lsusb | grep "Raspberry Pi"

# Информация о диске
sudo fdisk -l /dev/sdX

# Монтировать
sudo mount /dev/sdX /mnt/floppy

# Проверить
ls -l /mnt/floppy
```

## Отладка

### Логи USB
- `[USB] Device mounted` - Хост подключен
- `[USB] Capacity request` - Хост запрашивает размер
- `[USB] Read error at LBA X` - Ошибка чтения
- `[USB] Write error at LBA X` - Ошибка записи

### Проверка готовности
```c
// В коде
if (!floppy_is_ready()) {
    printf("Emulator not ready!\n");
}
```

### Статистика кеша
```c
const floppy_info_t* info = floppy_get_info();
printf("Cache hits: %lu\n", info->cache_hits);
printf("Cache misses: %lu\n", info->cache_misses);
printf("Hit rate: %.1f%%\n", 
       100.0 * info->cache_hits / (info->cache_hits + info->cache_misses));
```

## Известные ограничения

1. **Только чтение/запись**: Hot-swap SD карт не поддерживается
2. **Одно устройство**: Эмулируется только один диск
3. **FAT12 only**: Поддерживается только стандартный флоппи формат
4. **Write-back delays**: Запись на SD может быть отложена
5. **Cache size**: 320KB ограничение памяти

## Безопасность данных

### При извлечении образа
- Все dirty blocks сбрасываются на SD
- Кеш очищается
- USB устройство сообщает "medium not present"

### При отключении USB
- Dirty blocks НЕ сбрасываются автоматически
- Нужно извлечь через меню перед отключением
- Или использовать "Safely Remove Hardware" в Windows

## Рекомендации

1. **Всегда извлекайте** через меню перед отключением USB
2. **Не вынимайте SD карту** во время работы
3. **Используйте качественные** SD карты (Class 10+)
4. **Мониторьте** cache hit rate для оптимизации
5. **Резервное копирование** .IMG файлов перед записью
