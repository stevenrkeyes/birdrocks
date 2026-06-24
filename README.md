## Build

```bash
python -m platformio run
```

## Flashing

### First time (or after partition / flash problems)

Erase flash, then upload firmware and audio:

```bash
python -m platformio run -t erase
python -m platformio run -t upload
python -m platformio run -t uploadfs
```

### Code changes only

```bash
python -m platformio run -t upload
```

You do **not** need `uploadfs` for everyday firmware edits.

### Audio file changes

When you add, remove, or replace `.wav` files in the project root:

```bash
python -m platformio run -t uploadfs
```

## Serial monitor

```bash
python -m platformio device monitor
```
