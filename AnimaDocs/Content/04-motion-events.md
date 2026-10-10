# Motion and events
> Use named motions and bind them to interface signals.

## Built-in motions

The included browser runtime registers five motion names:

`Reveal`, `FadeIn`, `FadeUp`, `Glow`, `Pulse`.

```anima
node Welcome: Heading {
  text: "A living interface";
  motion: Reveal;
}

node PrimaryAction: Button {
  text: "Continue";
  motion: Pulse;
  on hover: Glow;
}
```

## Compose motion

The motion expression syntax supports these documented combinations:

- `A | B` — run sequentially.
- `A & B` — run in parallel.
- `A * 3` — repeat three times.
- `~A` — reverse a simple named motion's keyframes.
- `A.delay(400)` — add a delay in milliseconds.
- `A.repeat(3)` — repeat through a modifier.

```anima
node Entrance: Heading {
  text: "First light";
  motion: FadeUp | Reveal.delay(160);
}
```

Motion composition can communicate sequencing and emphasis. Test a composed expression against the bundled runtime because unsupported motion names or modifier forms may not be evaluated as expected.

## Signals

```anima
node SoundButton: Button {
  text: "Play chime";
  on click: chime(660);
  on hover: Glow;
  on focus: Reveal;
  on press: Pulse;
}
```

- `on press` maps to DOM `pointerdown`.
- `on click` maps to DOM `click`.
- `on hover` maps to DOM `pointerenter`.
- `on focus` maps to DOM `focus`.

These mappings are part of the browser runtime and use DOM event behavior. Native C++ event dispatch is being implemented separately.

## Current boundaries

Additional names such as `Bloom`, `Ripple`, `Line`, or `Twinkle`, free-form timelines, and a general animation graph are not implemented by the included browser runtime. The native renderer currently has its own scene loop and does not yet evaluate these motion expressions.
