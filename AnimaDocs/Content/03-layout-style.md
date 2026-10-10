# Layout and styling
> Describe structure and visual hierarchy with properties.

## Flow containers

The `flow` property configures how child nodes are arranged.

```anima
node CardStack {
  flow: column(cross: stretch, route: start, gap: 14);
}

node ActionRow {
  flow: row(cross: center, route: spread, gap: 10, wrap: true);
}
```

Supported flow modes documented by the current runtime are `column`, `row`, `trail`, `hidden`, and `free`. Options include `gap`, `cross`, `route`, and `wrap`.

- `cross`: `start`, `center`, `end`, `stretch`.
- `route`: `start`, `center`, `end`, `spread`, `even`.
- `gap`: numeric spacing between children.

## Styling a glass card

```anima
app GlassCard {
  scene Home {
    node Screen {
      flow: column(cross: center, gap: 20);
      node Panel {
        fill: "rgba(255,255,255,0.10)";
        ink: "#F7FAFF";
        edge: "rgba(255,255,255,0.22)";
        curve: 24;
        halo: "rgba(82,140,255,0.28)";
        veil: 12;
        inset: 24;
        node Label: Heading {
          text: "Glass, composed.";
          scale: 28;
        }
      }
    }
  }
}
```

The example demonstrates the styling vocabulary. Exact visual output depends on the browser runtime's mapping of these property values. A native renderer for the full styling model is still on the migration roadmap.

## Background images

The documented image properties are `bgImage`, `bgSize`, `bgPos`, and `bgRepeat`. Layout and position properties include `inset`, `offset`, `place`, edges (`top`, `left`, `right`, `bottom`), `layer`, `clip`, `pointerEvents`, and `transform`.

## Design tips

- Establish a clear hierarchy with a small number of text sizes.
- Use spacing and alignment before adding decoration.
- Keep motion purposeful and short for interface feedback.
- Validate behavior in the runtime you are actually targeting. Browser styles are not automatically reproduced by the current native room renderer.
