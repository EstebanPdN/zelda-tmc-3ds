# Normal Wide streaming corrections

The 3DS normal Wide view renders up to 266×160 pixels and scales them onto
400×240. It is separate from the Wide + Pixel Perfect Full View experiment.

## Adjacent rooms

The native tilemap previously shifted a 240-pixel viewport while the camera
and the reveal columns used the wider view. After the room loader replaced
its map arrays, the reveal also lost access to the outgoing room. This could
split scenery at x=240 and leave a horizontal camera correction after the
transition finished.

Before an eligible normal Wide scroll, `Port_Widescreen_BeginScroll` retains
a bounded 36×24 tile window for each outgoing map layer. The native tilemap
and its reveal then sample the same incoming/outgoing world coordinates.
The camera reaches the destination viewport edge over the existing 60
horizontal or 40 vertical ticks. Player carry, room selection, collision,
and transition duration retain their existing behavior.

This path covers adjacent rooms that both support normal Wide. Fixed-width
scenes, special map formats, repeating fake rooms, and Full View keep their
existing paths. No allocation or file I/O occurs during frame rendering.
Normal reveal columns also follow the actual background screen-shake offset.

## Horizontal Minish paths

The two leaf planes now provide their own reveal tiles using each plane's
parallax position. Their source is the flattened 128-column plane rather than
the ground map or the wrapped 32-column VRAM window.

Parallax clamps before calculating both the tile page and the fine scroll,
so approaching the source edge cannot freeze a tilemap while its offset
continues to wrap. Each DMA read is bounded to its own 8 KiB plane; the first
plane cannot spill into the second.

## Regression checks

Build and run each target separately:

```sh
xmake build -y widescreen_stream_test
./build/pc/widescreen_stream_test
xmake build -y horizontal_minish_path_test
./build/pc/horizontal_minish_path_test
```

The production streaming test checks every visible pixel's tile selection
through all four transition directions at widths 256 and 266, both layers,
monotonic camera movement, exact endpoints, screen shake, and 883 parallax
positions. The DMA regression covers both planes' valid pages and rejected
out-of-range requests. CPU renderer and PICA200 model regressions remain
separate from these producer checks.

Native USA/EU scene captures demonstrate the corrected village crossing and
horizontal paths. A 161-frame comparison preserves the horizontal path's
central 240 pixels; another preserves the fixed-width vertical path in full.
These checks do not replace Old/New Nintendo 3DS gameplay testing or establish
a sustained frame-rate guarantee.
