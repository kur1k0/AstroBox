# Astro Box Universe String Format v1

Astro Box exports the complete physical state as one QR-friendly ASCII string:

```text
ORBIT1:<BASE44_PAYLOAD>
```

The payload alphabet is `0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$%*+-./:`. It is
a subset of the QR alphanumeric character set and contains no whitespace.
Base44 encodes each pair of bytes as three big-endian radix-44 digits. A final
single byte is encoded as two digits. A decoder rejects non-canonical groups,
invalid characters, or payload lengths whose remainder modulo 3 is 1.

## Binary packet

All multibyte values are little-endian. Floats are IEEE-754 binary32 bit
patterns. The 26-byte header contains:

| Field | Type | Meaning |
| --- | --- | --- |
| `flags` | `u8` | Bit 0 enables collisions; all other bits are reserved. |
| `body_count` | `u8` | Active body count, 0 through 64. |
| `gravity` | `f32` | Finite, non-negative gravitational constant. |
| `softening` | `f32` | Finite, non-negative softening factor. |
| `fixed_dt` | `f32` | Finite, positive integration step. |
| `position_scale` | `f32` | World units per signed 24-bit position unit. |
| `velocity_scale` | `f32` | World units per signed 24-bit velocity unit. |
| `radius_scale` | `f32` | World units per unsigned 16-bit radius unit. |

Each body has a 20-byte fixed record followed by 0 through 15 name bytes:

| Field | Type | Meaning |
| --- | --- | --- |
| `style` | `u8` | Bits 7-6 body type, bits 5-3 palette color, bits 2-0 reserved. |
| `name_length` | `u8` | Number of following name bytes, 0 through 15. |
| `pos_x`, `pos_y` | `s24`, `s24` | Position divided by `position_scale`. |
| `vel_x`, `vel_y` | `s24`, `s24` | Velocity divided by `velocity_scale`. |
| `mass` | `f32` | Finite, positive mass. |
| `radius` | `u16` | Radius divided by `radius_scale`, 1 through 65535. |
| `name` | bytes | Name bytes; Astro Box currently exports printable ASCII names. |

Body types are `0 = star`, `1 = planet`, `2 = asteroid`, and `3 = black
hole`. Palette values are `0 = red`, `1 = orange`, `2 = yellow`, `3 = green`,
`4 = cyan`, `5 = blue`, and `6 = purple`. IDs, accelerations, and trails are
not serialized; a loader assigns IDs and recomputes acceleration after import.

The packet ends with a little-endian CRC-32/ISO-HDLC over all preceding bytes.
It uses reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final
XOR `0xFFFFFFFF`.

## QR capacity guarantee

The largest packet is:

```text
26 + 64 * (20 + 15) + 4 = 2270 bytes
```

Base44 produces 3405 payload characters, or 3412 including `ORBIT1:`. A QR
Code Version 36 at error-correction level L stores this alphanumeric string.
Its 161 modules plus the required four-module quiet zone on every side occupy
169 x 169 pixels, so every valid Astro Box v1 universe fits on the 320 x 170 screen.

Camera position, zoom, selection, pause state, time multiplier, menus, spawn
editor state, trails, and frame timing are deliberately excluded.
